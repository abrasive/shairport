/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <openssl/aes.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#include "common.h"
#include "player.h"
#include "rtp.h"

#ifdef FANCY_RESAMPLING
#include <samplerate.h>
#endif

#include "alac.h"

// parameters from the source
static unsigned char *aesiv;
static AES_KEY aes;
static int sampling_rate, frame_size;

#define FRAME_BYTES(frame_size) (4*frame_size)
// maximal resampling shift - conservative
#define OUTFRAME_BYTES(frame_size) (4*(frame_size+3))

static pthread_t player_thread;
static int please_stop;

static alac_file *decoder_info;

#ifdef FANCY_RESAMPLING
static int fancy_resampling = 1;
static SRC_STATE *src;
#endif


// debug variables
static int late_packet_message_sent;
static uint64_t packet_count = 0;


// interthread variables
static double volume = 1.0;
static int fix_volume = 0x10000;
static pthread_mutex_t vol_mutex = PTHREAD_MUTEX_INITIALIZER;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
#define BUFFER_FRAMES  512
#define MAX_PACKET      2048

typedef struct audio_buffer_entry {   // decoded audio packets
    int ready;
    uint32_t timestamp;
    signed short *data;
} abuf_t;
static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

// mutex-protected variables
static seq_t ab_read, ab_write;
static int ab_buffering = 1, ab_synced = 0;
static uint32_t first_packet_timestamp = 0;
static int flush_requested = 0;
static uint32_t flush_rtp_timestamp;

// mutexes and condition variables
static pthread_mutex_t ab_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t flush_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t flowcontrol;

static int64_t first_packet_time_to_play; // nanoseconds

// stats
static uint64_t missing_packets,late_packets,too_late_packets,resend_requests; 

static void bf_est_reset(short fill);

static void ab_resync(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        audio_buffer[i].ready = 0;
    ab_synced = 0;
    ab_buffering = 1;
}

// the sequence numbers will wrap pretty often.
// this returns true if the second arg is after the first
static inline int seq_order(seq_t a, seq_t b) {
    signed short d = b - a;
    return d > 0;
}

static void alac_decode(short *dest, uint8_t *buf, int len) {
    unsigned char packet[MAX_PACKET];
    assert(len<=MAX_PACKET);

    unsigned char iv[16];
    int aeslen = len & ~0xf;
    memcpy(iv, aesiv, sizeof(iv));
    AES_cbc_encrypt(buf, packet, aeslen, &aes, iv, AES_DECRYPT);
    memcpy(packet+aeslen, buf+aeslen, len-aeslen);

    int outsize;

    alac_decode_frame(decoder_info, packet, dest, &outsize);

    assert(outsize == FRAME_BYTES(frame_size));
}


static int init_decoder(int32_t fmtp[12]) {
    alac_file *alac;

    frame_size = fmtp[1]; // stereo samples
    sampling_rate = fmtp[11];

    int sample_size = fmtp[3];
    if (sample_size != 16)
        die("only 16-bit samples supported!");

    alac = alac_create(sample_size, 2);
    if (!alac)
        return 1;
    decoder_info = alac;

    alac->setinfo_max_samples_per_frame = frame_size;
    alac->setinfo_7a =      fmtp[2];
    alac->setinfo_sample_size = sample_size;
    alac->setinfo_rice_historymult = fmtp[4];
    alac->setinfo_rice_initialhistory = fmtp[5];
    alac->setinfo_rice_kmodifier = fmtp[6];
    alac->setinfo_7f =      fmtp[7];
    alac->setinfo_80 =      fmtp[8];
    alac->setinfo_82 =      fmtp[9];
    alac->setinfo_86 =      fmtp[10];
    alac->setinfo_8a_rate = fmtp[11];
    alac_allocate_buffers(alac);
    return 0;
}

static void free_decoder(void) {
    alac_free(decoder_info);
}

#ifdef FANCY_RESAMPLING
static int init_src(void) {
    int err;
    if (fancy_resampling)
        src = src_new(SRC_SINC_MEDIUM_QUALITY, 2, &err);
    else
        src = NULL;

    return err;
}
static void free_src(void) {
    src_delete(src);
    src = NULL;
}
#endif

static void init_buffer(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        audio_buffer[i].data = malloc(OUTFRAME_BYTES(frame_size));
    ab_resync();
}

static void free_buffer(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        free(audio_buffer[i].data);
}

void player_put_packet(seq_t seqno,uint32_t timestamp, uint8_t *data, int len) {
  packet_count++;
  //if (packet_count<10)
  //  debug(1,"Packet %llu received.",packet_count);
  abuf_t *abuf = 0;
  int16_t buf_fill;

  pthread_mutex_lock(&ab_mutex);
  if (!ab_synced) {
      debug(2, "syncing to first seqno %04X.", seqno);
      ab_write = seqno-1;
      ab_read = seqno;
      ab_synced = 1;
  }
  if (seq_diff(ab_write, seqno) == 1) {                  // expected packet
      abuf = audio_buffer + BUFIDX(seqno);
      ab_write = seqno;
  } else if (seq_order(ab_write, seqno)) {    // newer than expected
      rtp_request_resend(ab_write+1, seqno-1);
      resend_requests++;
      abuf = audio_buffer + BUFIDX(seqno);
      ab_write = seqno;
  } else if (seq_order(ab_read, seqno)) {     // late but not yet played
      late_packets++;
      abuf = audio_buffer + BUFIDX(seqno);
  }  else {    // too late.
    too_late_packets++;
    if (!late_packet_message_sent) {
      debug(1, "late packet %04X (%04X:%04X).", seqno, ab_read, ab_write);
      late_packet_message_sent=1;
    }
  }
  buf_fill = seq_diff(ab_read, ab_write);
//    pthread_mutex_unlock(&ab_mutex);

  if (abuf) {
      alac_decode(abuf->data, data, len);
      abuf->ready = 1;
      abuf->timestamp = timestamp;
  }
  

//    pthread_mutex_lock(&ab_mutex);
  
  int rc = pthread_cond_signal(&flowcontrol);
  if (rc)
  	debug(1,"Error signalling flowcontrol.");

  pthread_mutex_unlock(&ab_mutex);
}


static short lcg_rand(void) {
	static unsigned long lcg_prev = 12345;
	lcg_prev = lcg_prev * 69069 + 3;
	return lcg_prev & 0xffff;
}

static inline short dithered_vol(short sample) {
  static short rand_a, rand_b;
  long out;

  out = (long)sample * fix_volume;
  if (fix_volume < 0x10000) {
      rand_b = rand_a;
      rand_a = lcg_rand();
      out += rand_a;
      out -= rand_b;
  }
  return out>>16;
}

// get the next frame, when available. return 0 if underrun/stream reset.
static abuf_t *buffer_get_frame(void) {
  int16_t buf_fill;
  uint64_t local_time_now;
  seq_t read, next;
  abuf_t *abuf = 0;
  int i;
  abuf_t *curframe;
  pthread_mutex_lock(&ab_mutex);
  int wait;
  int32_t dac_delay = 0;
  do {
    pthread_mutex_lock(&flush_mutex);
    if (flush_requested==1) {
     if (config.output->flush)
      config.output->flush();
     ab_resync();
     first_packet_timestamp = 0;
     first_packet_time_to_play = 0;
      flush_requested=0;
    }
    pthread_mutex_unlock(&flush_mutex);

    curframe = audio_buffer + BUFIDX(ab_read);
    if (config.output->delay) {
      dac_delay = config.output->delay();
      if (dac_delay==-1) {
        debug(1,"Error getting dac_delay at start of loop.");
        dac_delay=0;
      }
    }
        
    if (curframe->ready) {
      if ((flush_rtp_timestamp) && (flush_rtp_timestamp>=curframe->timestamp)) {
        // debug(1,"Dropping flushed packet %u.",curframe->timestamp);
        curframe->ready=0;
        ab_read++;
      } else if (ab_buffering) { // if we are getting packets but not yet forwarding them to the player
        if (first_packet_timestamp==0) { // if this is the very first packet
         // debug(1,"First frame seen, time %u, with %d frames...",curframe->timestamp,seq_diff(ab_read, ab_write));
         uint32_t reference_timestamp;
          uint64_t reference_timestamp_time;
          get_reference_timestamp_stuff(&reference_timestamp,&reference_timestamp_time);
          if (reference_timestamp) { // if we have a reference time
            // debug(1,"First frame seen with timestamp...");
            first_packet_timestamp=curframe->timestamp; // we will keep buffering until we are supposed to start playing this
 
            // here, see if we should start playing. We need to know when to allow the packets to be sent to the player
            // we will get a fix every second or so, which will be stored as a pair consisting of
            // the time when the packet with a particular timestamp should be played.
            // it might not be the timestamp of our first packet, however, so we might have to do some calculations.
          
            struct timespec tn;
            clock_gettime(CLOCK_MONOTONIC,&tn);
            local_time_now=((uint64_t)tn.tv_sec<<32)+((uint64_t)tn.tv_nsec<<32)/1000000000;
            
            int64_t delta = ((int64_t)first_packet_timestamp-(int64_t)reference_timestamp);

            first_packet_time_to_play = reference_timestamp_time+((delta+(int64_t)config.latency)<<32)/44100; // using the latency requested...
            if (local_time_now>=first_packet_time_to_play) {
              debug(1,"First packet is late! It should have played before now. Flushing...");
              player_flush(first_packet_timestamp);
            }
          }
        }      

        if (first_packet_time_to_play!=0) {
         uint32_t filler_size = frame_size;

          if (config.output->delay) {
            dac_delay = config.output->delay();
            if (dac_delay==-1) {
              debug(1,"Delay error when setting a filler size.");
              dac_delay=0;
            }
            if (dac_delay==0)
              filler_size = 11025; // 0.25 second
          }
          struct timespec time_now;
          clock_gettime(CLOCK_MONOTONIC,&time_now);
          uint64_t tn = ((uint64_t)time_now.tv_sec<<32)+((uint64_t)time_now.tv_nsec<<32)/1000000000;
          if (tn>=first_packet_time_to_play) {
            // we've gone past the time...
            // debug(1,"Run past the exact start time by %llu frames, with time now of %llx, fpttp of %llx and dac_delay of %d and %d packets; flush.",(((tn-first_packet_time_to_play)*44100)>>32)+dac_delay,tn,first_packet_time_to_play,dac_delay,seq_diff(ab_read, ab_write));
            
            if (config.output->flush)
              config.output->flush();
            ab_resync();
            first_packet_timestamp = 0;
            first_packet_time_to_play = 0;
          } else {
            uint64_t gross_frame_gap = ((first_packet_time_to_play-tn)*44100)>>32;
            int64_t exact_frame_gap = gross_frame_gap-dac_delay;
            if (exact_frame_gap<=0) {
              // we've gone past the time...
              // debug(1,"Run a bit past the exact start time by %lld frames, with time now of %llx, fpttp of %llx and dac_delay of %d and %d packets; flush.",-exact_frame_gap,tn,first_packet_time_to_play,dac_delay,seq_diff(ab_read, ab_write));
              if (config.output->flush)
                config.output->flush();
              ab_resync();
              first_packet_timestamp = 0;
              first_packet_time_to_play = 0;
            } else {
              uint32_t fs=filler_size;
              if ((exact_frame_gap<=filler_size) || (exact_frame_gap<=frame_size*2)) {
                fs=exact_frame_gap;
                ab_buffering = 0;
              }
              signed short *silence;
              silence = malloc(FRAME_BYTES(fs));
              memset(silence, 0, FRAME_BYTES(fs));
              //debug(1,"Exact frame gap is %llu; play %d frames of silence. Dac_delay is %d, with %d packets.",exact_frame_gap,fs,dac_delay,seq_diff(ab_read, ab_write));
              config.output->play(silence, fs);
              free(silence);
            }
          }
        }
      }
    }
    // allow to DAC to have a 0.15 seconds -- seems necessary for VMWare Fusion and a WiFi connection.
    wait = (ab_buffering || (dac_delay>=6615) || (!ab_synced)) && (!please_stop);
//    wait = (ab_buffering ||  (seq_diff(ab_read, ab_write) < (config.latency-22000)/(352)) || (!ab_synced)) && (!please_stop);
    if (wait) {
      struct timespec to;
      clock_gettime(CLOCK_MONOTONIC, &to);
  // Watch out: should be 1^9/44100, but this causes integer overflow
      uint64_t calc = (uint64_t)(4*352*1000000)/(44*3); //four thirds of a packet time
      uint32_t calcs = calc&0xFFFFFFFF;
      to.tv_nsec+=calcs;
      if (to.tv_nsec>1000000000) {
        to.tv_nsec-=1000000000;
        to.tv_sec+=1;
      }
      pthread_cond_timedwait(&flowcontrol,&ab_mutex,&to); 
    }    
  } while (wait);

  if (!please_stop) {
//    buf_fill = seq_diff(ab_read, ab_write);
//    if (buf_fill < 1 || !ab_synced) {
//      if (buf_fill < 1)
	if (dac_delay==0) {// we just got underrun
      debug(1,"Underrun!");
      ab_resync(); // starting over
    }
  }
  
  if (please_stop) {
    pthread_mutex_unlock(&ab_mutex);
    debug(1,"Exiting from buffer_get_frame.");
    return 0;
  }

/*
  if (buf_fill >= BUFFER_FRAMES) {   // overrunning! uh-oh. restart at a sane distance
    warn("overrun.");
    ab_read = ab_write - config.buffer_start_fill;
  }
*/
  read = ab_read;
  ab_read++;


  // check if t+16, t+32, t+64, t+128, ... (buffer_start_fill / 2)
  // packets have arrived... last-chance resend

  
  if (!ab_buffering) {
    for (i = 16; i < (seq_diff(ab_read,ab_write) / 2); i = (i * 2)) {
      next = ab_read + i;
      abuf = audio_buffer + BUFIDX(next);
      if (!abuf->ready) {
        rtp_request_resend(next, next);
        resend_requests++;
      }
    }
  }
  
  
  if (!curframe->ready) {
    // debug(1, "missing frame %04X. Supplying a silent frame.", read);
    missing_packets++;
    memset(curframe->data, 0, FRAME_BYTES(frame_size));
    curframe->timestamp=0;    
  }
  curframe->ready = 0;
  pthread_mutex_unlock(&ab_mutex);
  return curframe;
}

static int stuff_buffer(short *inptr, short *outptr, int32_t stuff) {
    int i;
    int stuffsamp = frame_size;
    if (stuff)
      stuffsamp = rand() % (frame_size - 1);

    pthread_mutex_lock(&vol_mutex);
    for (i=0; i<stuffsamp; i++) {   // the whole frame, if no stuffing
        *outptr++ = dithered_vol(*inptr++);
        *outptr++ = dithered_vol(*inptr++);
    };
    if (stuff) {
        if (stuff==1) {
            debug(3, "+++++++++");
            // interpolate one sample
            *outptr++ = dithered_vol(((long)inptr[-2] + (long)inptr[0]) >> 1);
            *outptr++ = dithered_vol(((long)inptr[-1] + (long)inptr[1]) >> 1);
        } else if (stuff==-1) {
            debug(3, "---------");
            inptr++;
            inptr++;
        }
        for (i=stuffsamp; i<frame_size + stuff; i++) {
            *outptr++ = dithered_vol(*inptr++);
            *outptr++ = dithered_vol(*inptr++);
        }
    }
    pthread_mutex_unlock(&vol_mutex);

    return frame_size + stuff;
}

static void *player_thread_func(void *arg) {
#define averaging_interval 71
#define trend_interval 44100
    int64_t corrections[trend_interval];
    int64_t sum_of_corrections, sum_of_insertions_and_deletions;
    double moving_average_correction, moving_average_insertions_and_deletions;
    int oldest_correction, newest_correction, number_of_corrections;
    
    int64_t delays[averaging_interval];
    int64_t sum_of_delays,moving_average_delay;
    int oldest_delay,newest_delay,number_of_delays;
    int64_t additions,deletions =  0;
    additions = deletions =  0;
    int64_t frames = 0;
    int play_samples;
    int64_t initial_latency = 0;
    int64_t previous_latency,current_latency,change_in_latency;
    int64_t current_delay;
    int play_number = 0;
    uint64_t accumulated_buffers_in_use = 0;
    int64_t accumulated_delay = 0;
    int64_t accumulated_da_delay = 0;

    const int print_interval = 71;
    // I think it's useful to keep this prime to prevent it from falling into a pattern with some other process.
    
    struct drand48_data randBuffer;
    srand48_r(time(NULL), &randBuffer);

    
    signed short *inbuf, *outbuf, *silence;
    outbuf = malloc(OUTFRAME_BYTES(frame_size));
    silence = malloc(OUTFRAME_BYTES(frame_size));
    memset(silence, 0, OUTFRAME_BYTES(frame_size));

#ifdef FANCY_RESAMPLING
    float *frame, *outframe;
    SRC_DATA srcdat;
    if (fancy_resampling) {
        frame = malloc(frame_size*2*sizeof(float));
        outframe = malloc(2*frame_size*2*sizeof(float));

        srcdat.data_in = frame;
        srcdat.data_out = outframe;
        srcdat.input_frames = FRAME_BYTES(frame_size);
        srcdat.output_frames = 2*FRAME_BYTES(frame_size);
        srcdat.src_ratio = 1.0;
        srcdat.end_of_input = 0;
    }
#endif
  change_in_latency = 0;
  late_packet_message_sent=0;
  oldest_delay=newest_delay=number_of_delays=0;
  sum_of_delays=0;
  oldest_correction=newest_correction=number_of_corrections=0;
  sum_of_corrections=0;
  sum_of_insertions_and_deletions=0;
  missing_packets=late_packets=too_late_packets=resend_requests=0;
  flush_rtp_timestamp=0;
  while (!please_stop) {
    abuf_t *inframe = buffer_get_frame();
    if (inframe) {
      inbuf = inframe->data;
      if (inbuf) {
        #ifdef FANCY_RESAMPLING
        if (fancy_resampling) {
          int i;
          pthread_mutex_lock(&vol_mutex);
          for (i=0; i<2*FRAME_BYTES(frame_size); i++) {
            frame[i] = (float)inbuf[i] / 32768.0;
            frame[i] *= volume;
          }
          pthread_mutex_unlock(&vol_mutex);
          srcdat.src_ratio = bf_playback_rate;
          src_process(src, &srcdat);
          assert(srcdat.input_frames_used == FRAME_BYTES(frame_size));
          src_float_to_short_array(outframe, outbuf, FRAME_BYTES(frame_size)*2);
          play_samples = srcdat.output_frames_gen;
          } else
        #endif
        
        // we need to see whether the player is a bit slow or a bit fast, so we track the overall latency
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC,&tn);
        uint64_t local_time_now=((uint64_t)tn.tv_sec<<32)+((uint64_t)tn.tv_nsec<<32)/1000000000;
        double x;
        drand48_r(&randBuffer, &x);
        play_number++;
        int32_t abs_change_in_latency = change_in_latency;
        
        if (abs_change_in_latency<1)
          abs_change_in_latency=-abs_change_in_latency;
        
        int32_t amount_to_stuff=0;
        
        if (x<=(double)1.0*abs_change_in_latency/print_interval) {
          amount_to_stuff= -abs_change_in_latency/change_in_latency;
        }
        
        // prevent dithering by +/- 1...
        if (abs_change_in_latency<20)
          amount_to_stuff=0;
          
        if (number_of_corrections==trend_interval) { // the array of corrections is full
          sum_of_corrections-=corrections[oldest_correction];
          int64_t oldest_correction_value = corrections[oldest_correction];
          // remove the correction from the tally of insertions and deletions
          if (oldest_correction_value>=0)
            sum_of_insertions_and_deletions-=oldest_correction_value;
          else
            sum_of_insertions_and_deletions+=oldest_correction_value;     
                  
          oldest_correction=(oldest_correction+1)%trend_interval;
          number_of_corrections--;
        }
        
        corrections[newest_correction]=amount_to_stuff;
        sum_of_corrections+=amount_to_stuff;
        // add the correction to the tally of insertions and deletions
        if (amount_to_stuff>=0)
          sum_of_insertions_and_deletions+=amount_to_stuff;
        else
          sum_of_insertions_and_deletions-=amount_to_stuff;           
        newest_correction=(newest_correction+1)%trend_interval;
        number_of_corrections++;
        
        moving_average_correction = (1.0*sum_of_corrections)/number_of_corrections;
        moving_average_insertions_and_deletions = (1.0*sum_of_insertions_and_deletions)/number_of_corrections;


        
        play_samples = stuff_buffer(inbuf, outbuf,amount_to_stuff);
        if (amount_to_stuff > 0)
          additions += amount_to_stuff;
        else
          deletions -= amount_to_stuff;
        frames += play_samples;
        
        if (config.output->delay) {
          current_delay = config.output->delay();
          if (current_delay==-1) {
            debug(1,"Delay error when checking running latency.");
            current_delay=0;
          }
        }

        config.output->play(outbuf, play_samples);
        // now see if we can calculate how early or late the first frame would be
        uint32_t reference_timestamp;
        uint64_t reference_timestamp_time;

        get_reference_timestamp_stuff(&reference_timestamp,&reference_timestamp_time);

        int64_t rt,nt;
        rt = reference_timestamp;
        nt = inframe->timestamp;
        
        int64_t td_in_frames;
        int64_t td = local_time_now-reference_timestamp_time;
        // debug(1,"td is %lld.",td);          
        if (td>=0) {
          td_in_frames = (td*44100)>>32;
        } else {
          td_in_frames = -((-td*44100)>>32);
        }
        // this is the actual delay, which will fluctuate a good bit about a potentially rising or falling trend.
        int64_t delay = td_in_frames+rt-(nt-current_delay);
        
        int64_t sync_error = delay-config.latency;

        // timestamp of zero means an inserted silent frame in place of a missing frame
	      if ((inframe->timestamp!=0) && (! please_stop) && (abs(sync_error)>441*3)) { // 30 ms 
	        debug(1,"Lost sync with source -- flushing and resyncing. Error of %lld frames.",sync_error);
	        player_flush(nt);
	      }
        
//        if ((play_number<(10)) || (play_number%500==0))
//        if ((play_number<(10)))
//          debug(1,"Latency error for packet %d: %d frames.",play_number,delay-config.latency);
        
        if (number_of_delays==averaging_interval) { // the array of delays is full
          sum_of_delays-=delays[oldest_delay];
          oldest_delay=(oldest_delay+1)%averaging_interval;
          number_of_delays--;
        }
        
        delays[newest_delay]=delay;
        sum_of_delays+=delay;
        newest_delay=(newest_delay+1)%averaging_interval;
        number_of_delays++;
        
        moving_average_delay = sum_of_delays/number_of_delays; // this doesn't seem to be right
        
        change_in_latency=moving_average_delay-config.latency;
        
        accumulated_da_delay+=current_delay;
        accumulated_delay+=delay;
        
        accumulated_buffers_in_use += seq_diff(ab_read, ab_write);
        if (play_number%print_interval==0) {
          current_latency=accumulated_delay/print_interval;                    
          if ((play_number/print_interval)%100==0)
            { // only print every hundredth one, in verbose mode
              //debug(1,"Valid frames: %lld; overall frames added/subtracted %lld; frames added + frames deleted %lld; average D/A delay, average latency (frames): %llu, %llu; average buffers in use: %llu, moving average delay (number of delays): %llu (%lu).",
              //  frames-(additions-deletions), additions-deletions, additions+deletions, accumulated_da_delay/print_interval,current_latency,accumulated_buffers_in_use/print_interval,moving_average_delay,number_of_delays);

            //debug(1,"Frames %lld, correction %lld, mods %lld, dac_buffer %llu, latency %llu, missing_packets %llu, late_packets %llu, too_late_packets %llu resend_requests %llu.",
              //frames-(additions-deletions), additions-deletions, additions+deletions,accumulated_da_delay/print_interval,moving_average_delay,missing_packets,late_packets,too_late_packets,resend_requests);
                
            debug(1,"Divergence: %.1f (ppm); corrections: %.1f (ppm); missing packets %llu; late packets %llu; too late packets %llu; resend requests %llu.", moving_average_correction*1000000/352, moving_average_insertions_and_deletions*1000000/352,missing_packets,late_packets,too_late_packets,resend_requests);
          }
          if (previous_latency==0)
            previous_latency=current_latency;
          else {
            previous_latency=current_latency;
          }
          accumulated_delay=0;
          accumulated_da_delay=0;
          accumulated_buffers_in_use=0;
        }
      }
    }
  }
  return 0;
}


// takes the volume as specified by the airplay protocol
void player_volume(double f) {

// The volume ranges -144.0 (mute) or -30 -- 0. See http://git.zx2c4.com/Airtunes2/about/#setting-volume
// By examination, the -30 -- 0 range is linear on the slider; i.e. the slider is calibrated in 30 equal increments
// So, we will pass this on without any weighting if we have a hardware mixer, as we expect the mixer to be calibrated in dB.

// Here, we ask for an attenuation we will apply in software. The dB range of a value from 1 to 65536 is about 48.1 dB (log10 of 65536 is 4.8164).
// Thus, we ask out vol2attn function for an appropriate dB between -48.1 and 0 dB and translate it back to a number.
 
  double linear_volume = pow(10,vol2attn(f,0,-4810)/1000);
    
  if(f == -144.0)
    linear_volume = 0.0;
  
  if (config.output->volume) {
      config.output->volume(f);
      linear_volume=1.0; // no attenuation needed
  } 
  pthread_mutex_lock(&vol_mutex);
  volume = linear_volume;
  fix_volume = 65536.0 * volume;
  pthread_mutex_unlock(&vol_mutex);
}

void player_flush(uint32_t timestamp) {
  pthread_mutex_lock(&flush_mutex);
  flush_requested=1;
  flush_rtp_timestamp=timestamp; // flush all packets up to (and including?) this
  pthread_mutex_unlock(&flush_mutex);
}

/*
    if (config.output->flush)
      config.output->flush();
    pthread_mutex_lock(&ab_mutex);
    ab_resync();
    pthread_mutex_unlock(&ab_mutex);
*/

int player_play(stream_cfg *stream) {
	debug(1,"player_play called...");
	packet_count = 0;
    if (config.buffer_start_fill > BUFFER_FRAMES)
        die("specified buffer starting fill %d > buffer size %d",
            config.buffer_start_fill, BUFFER_FRAMES);

    AES_set_decrypt_key(stream->aeskey, 128, &aes);
    aesiv = stream->aesiv;
    init_decoder(stream->fmtp);
    // must be after decoder init
    init_buffer();
#ifdef FANCY_RESAMPLING
    init_src();
#endif
    please_stop = 0;
    if (config.cmd_start && !fork()) {
        if (system(config.cmd_start))
            warn("exec of external start command failed");
        exit(0);
    }
    
    // set the flowcontrol condition variable to wait on a monotonic clock
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock( &attr, CLOCK_MONOTONIC);
    int rc = pthread_cond_init(&flowcontrol,&attr);
    if (rc)
    	debug(1,"Error initialising condition variable.");
    config.output->start(sampling_rate);
    pthread_create(&player_thread, NULL, player_thread_func, NULL);

    return 0;
}

void player_stop(void) {
	debug(1,"player_stop called...");
    please_stop = 1;
    pthread_cond_signal(&flowcontrol); // tell it to give up
    pthread_join(player_thread, NULL);
    config.output->stop();
    if (config.cmd_stop && !fork()) {
        if (system(config.cmd_stop))
            warn("exec of external stop command failed");
        exit(0);
    }
    free_buffer();
    free_decoder();
#ifdef FANCY_RESAMPLING
    free_src();
#endif
    int rc = pthread_cond_destroy(&flowcontrol);
    if (rc)
    	debug(1,"Error destroying condition variable.");
	debug(1,"player_stop finished...");
}
