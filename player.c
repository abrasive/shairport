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
#include <math.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#include "config.h"

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/aes.h>
#endif

#ifdef HAVE_LIBSOXR
#include <soxr.h>
#endif

#include "common.h"
#include "player.h"
#include "rtp.h"
#include "rtsp.h"

#include "alac.h"

// parameters from the source
static unsigned char *aesiv;
#ifdef HAVE_LIBSSL
static AES_KEY aes;
#endif
static int sampling_rate, frame_size;

#define FRAME_BYTES(frame_size) (4*frame_size)
// maximal resampling shift - conservative
#define OUTFRAME_BYTES(frame_size) (4*(frame_size+3))

#ifdef  HAVE_LIBPOLARSSL
static aes_context dctx;
#endif

static pthread_t player_thread;
static int please_stop;

static alac_file *decoder_info;

// debug variables
static int late_packet_message_sent;
static uint64_t packet_count = 0;
static int32_t last_seqno_read;



// interthread variables
static double volume = 1.0;
static int fix_volume = 0x10000;
static pthread_mutex_t vol_mutex = PTHREAD_MUTEX_INITIALIZER;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
#define BUFFER_FRAMES  512
#define MAX_PACKET      2048

// DAC buffer occupancy stuff
#define DAC_BUFFER_QUEUE_DESIRED_LENGTH 6615
#define DAC_BUFFER_QUEUE_MINIMUM_LENGTH 5000

typedef struct audio_buffer_entry {   // decoded audio packets
  int ready;
  uint32_t timestamp;
  seq_t sequence_number;
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
static uint64_t time_of_last_audio_packet;
static int shutdown_requested;

// mutexes and condition variables
static pthread_mutex_t ab_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t flush_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t flowcontrol;

static int64_t first_packet_time_to_play; // nanoseconds

// stats
static uint64_t missing_packets,late_packets,too_late_packets,resend_requests; 

static void ab_resync(void) {
  int i;
  for (i=0; i<BUFFER_FRAMES; i++) {
    audio_buffer[i].ready = 0;
    audio_buffer[i].sequence_number = 0;
  }
  ab_synced = 0;
  last_seqno_read = -1;
  ab_buffering = 1;
}

// the sequence number is a 16-bit unsigned number which wraps pretty often
// to work out if one seqno is 'after' another therefore depends whether wrap has occurred
// this function works out the actual ordinate of the seqno, i.e. the distance up from 
// the zeroth element, at ab_read, taking due account of wrap.

static inline seq_t SUCCESSOR(seq_t x) {
  uint32_t p = x & 0xffff;
  p+=1;
  p = p & 0xffff;
  return p;
}

static inline seq_t PREDECESSOR(seq_t x) {
  uint32_t p = (x & 0xffff)+0x10000;
  p-=1;
  p = p & 0xffff;
  return p;
}

// anything with ORDINATE in it must be proctected by the ab_mutex
static inline int32_t ORDINATE(seq_t x) {
  int32_t p = x & 0xffff;
  int32_t q = ab_read & 0x0ffff;
  int32_t t = (p+0x10000-q) & 0xffff;
  // we definitely will get a positive number in t at this point, but it might be a
  // positive alias of a negative number, i.e. x might actually be "before" ab_read
  // So, if the result is greater than 32767, we will assume its an
  // alias and subtract 65536 from it
  if (t >= 32767) {
    // debug(1,"OOB: %u, ab_r: %u, ab_w: %u",x,ab_read,ab_write);
    t -= 65536;
  }
  return t;
}

// wrapped number between two seq_t.
int32_t seq_diff(seq_t a, seq_t b) {
    int32_t diff = ORDINATE(b) - ORDINATE(a);
    return diff;
}

// the sequence numbers will wrap pretty often.
// this returns true if the second arg is after the first
static inline int seq_order(seq_t a, seq_t b) {
    int32_t d = ORDINATE(b) - ORDINATE(a);
    return d > 0;
}

static inline seq_t seq_sum(seq_t a, seq_t b) {
  uint32_t p = a & 0xffff;
  uint32_t q = b & 0x0ffff;
  uint32_t r = (a+b) & 0xffff;
  return r;
}


// now for 32-bit wrapping in timestamps

// this returns true if the second arg is strictly after the first
// on the assumption that the gap between them is never greater than (2^31)-1
// Represent a and b in 64 bits
static inline int seq32_order(uint32_t a, uint32_t b) {
	if (a==b)
		return 0; 
	int64_t A = a & 0xffffffff;
	int64_t B = b & 0xffffffff;
	int64_t C = B-A;
	// if bit 31 is set, it means either b is before (i.e. less than) a or
	// b is (2^31)-1 ahead of b.
	
	// If we assume the gap between b and a should never reach 2 billion, then
	// bit 31 == 0 means b is strictly after a
  return (C & 0x80000000) == 0;
}



static void alac_decode(short *dest, uint8_t *buf, int len) {
  unsigned char packet[MAX_PACKET];
  unsigned char packetp[MAX_PACKET];
  assert(len<=MAX_PACKET);

  unsigned char iv[16];
  int aeslen = len & ~0xf;
  memcpy(iv, aesiv, sizeof(iv));
   
#ifdef HAVE_LIBPOLARSSL
  aes_crypt_cbc(&dctx,AES_DECRYPT,aeslen,iv,buf, packet);
#endif
#ifdef HAVE_LIBSSL
  AES_cbc_encrypt(buf, packet, aeslen, &aes, iv, AES_DECRYPT);
#endif

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
  
  pthread_mutex_lock(&ab_mutex);
  
	if ((flush_rtp_timestamp!=0x7fffffff) && ((timestamp==flush_rtp_timestamp) || seq32_order(timestamp,flush_rtp_timestamp))) {
		debug(1,"Dropping flushed packet in player_put_packet, seqno %u, timestamp %u, flushing to timestamp: %u.",seqno,timestamp,flush_rtp_timestamp);
	} else {
		if ((flush_rtp_timestamp!=0x7fffffff) && (!seq32_order(timestamp,flush_rtp_timestamp))) // if we have gone past the flush boundary time
			flush_rtp_timestamp=0x7fffffff;

		abuf_t *abuf = 0;

		if (!ab_synced) {
			debug(2, "syncing to seqno %u.", seqno);
			ab_write = seqno;
			ab_read = seqno;
			ab_synced = 1;
		}
		if (ab_write == seqno) {       // expected packet
			abuf = audio_buffer + BUFIDX(seqno);
			ab_write = SUCCESSOR(seqno);
		} else if (seq_order(ab_write, seqno)) {    // newer than expected
			//if (ORDINATE(seqno)>(BUFFER_FRAMES*7)/8)
			// debug(1,"An interval of %u frames has opened, with ab_read: %u, ab_write: %u and seqno: %u.",seq_diff(ab_read,seqno),ab_read,ab_write,seqno);
			int32_t gap = seq_diff(ab_write,PREDECESSOR(seqno))+1;
			if (gap<=0)
				debug(1,"Unexpected gap size: %d.",gap);
			int i;
			for (i=0;i<gap;i++) {
				abuf = audio_buffer + BUFIDX(seq_sum(ab_write,i));
				abuf->ready = 0; // to be sure, to be sure
				abuf->timestamp = 0;
				abuf->sequence_number = 0;
			}
			// debug(1,"N %d s %u.",seq_diff(ab_write,PREDECESSOR(seqno))+1,ab_write);
			abuf = audio_buffer + BUFIDX(seqno);
			rtp_request_resend(ab_write,gap);
			resend_requests++;
			ab_write = SUCCESSOR(seqno);
		} else if (seq_order(ab_read, seqno)) {     // late but not yet played
			late_packets++;
			abuf = audio_buffer + BUFIDX(seqno);
		} else {                                    // too late.
			too_late_packets++;
			/*
			if (!late_packet_message_sent) {
				debug(1, "too-late packet received: %u; ab_read: %u; ab_write: %u.", seqno, ab_read, ab_write);
				late_packet_message_sent=1;
			}
			*/
		}
		// pthread_mutex_unlock(&ab_mutex);

		if (abuf) {
			alac_decode(abuf->data, data, len);
			abuf->ready = 1;
			abuf->timestamp = timestamp;
			abuf->sequence_number = seqno;
		}
	
		// pthread_mutex_lock(&ab_mutex);
	
		time_of_last_audio_packet = get_absolute_time_in_fp();
  }
  int rc = pthread_cond_signal(&flowcontrol);
  if (rc)
    debug(1,"Error signalling flowcontrol.");
  pthread_mutex_unlock(&ab_mutex);
}


static inline short lcg_rand(void) {
  static unsigned long lcg_prev = 12345;
  lcg_prev = lcg_prev * 69069 + 3;
  return lcg_prev & 0xffff;
}

static inline short dithered_vol(short sample) {
  short rand_a, rand_b;
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
  // struct timespec tn;
  abuf_t *abuf = 0;
  int i;
  abuf_t *curframe;
  
  pthread_mutex_lock(&ab_mutex);
  int wait;
  int32_t dac_delay = 0;
  do {
    // get the time
    local_time_now = get_absolute_time_in_fp();    

    // if config.timeout (default 120) seconds have elapsed since the last audio packet was received, then we should stop.
    // config.timeout of zero means don't check..., but iTunes may be confused by a long gap followed by a resumption...
    if ((time_of_last_audio_packet!=0) && (shutdown_requested==0) && (config.timeout!=0)) {
      if (local_time_now-time_of_last_audio_packet>=config.timeout<<32) {
        debug(1,"As Yeats almost said, \"Too long a silence / can make a stone of the heart\"");
        rtsp_request_shutdown_stream();
        shutdown_requested=1;
      }
    }
  
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
    uint32_t flush_limit = 0;
    if (ab_synced) {
    	do {
    		curframe = audio_buffer + BUFIDX(ab_read);
    		if (curframe->ready) {
    		
					if (curframe->sequence_number!=ab_read) {
						// some kind of sync problem has occurred.
						if (BUFIDX(curframe->sequence_number)==BUFIDX(ab_read)) {
							// it looks like some kind of aliasing has happened
							if (seq_order(ab_read,curframe->sequence_number)) {
								ab_read=curframe->sequence_number;
								debug(1,"Aliasing of buffer index -- reset.");
							}
						} else {
							debug(1,"Inconsistent sequence numbers detected");
						}
					}
    		    		
					if ((flush_rtp_timestamp!=0x7fffffff) && ((curframe->timestamp==flush_rtp_timestamp) || seq32_order(curframe->timestamp,flush_rtp_timestamp))) {
						debug(1,"Dropping flushed packet seqno %u, timestamp %u",curframe->sequence_number,curframe->timestamp);
						curframe->ready=0;
						flush_limit++;
						ab_read==SUCCESSOR(ab_read);
					}
					if ((flush_rtp_timestamp!=0x7fffffff) && (!seq32_order(curframe->timestamp,flush_rtp_timestamp))) // if we have gone past the flush boundary time
						flush_rtp_timestamp=0x7fffffff;
				}
    	} while ((flush_rtp_timestamp!=0x7fffffff) && (flush_limit<=8820) && (curframe->ready==0));
    	
    	if (flush_limit==8820) {
    		debug(1,"Flush hit the 8820 frame limit!");
    		flush_limit=0;
    	}
    	    
      dac_delay = 0;
      curframe = audio_buffer + BUFIDX(ab_read);
      if (config.output->delay) {
        dac_delay = config.output->delay();
        if (dac_delay==-1) {
          debug(1,"Error getting dac_delay at start of loop.");
          dac_delay=0;
        }
      }

      if (curframe->ready) {
/*
				// This is broken -- it causes infinite loops. It's in the wrong place, and anyway isn't used much.
        if ((flush_rtp_timestamp) && (flush_rtp_timestamp>=curframe->timestamp)) {
          debug(1,"Dropping flushed packet seqno %u, timestamp %u",curframe->sequence_number,curframe->timestamp);
          curframe->ready=0;
          if (flush_rtp_timestamp==curframe->timestamp) // if we are finished...
          	flush_rtp_timestamp=0;
          ab_read==SUCCESSOR(ab_read);
        } else
*/        
         if (ab_buffering) { // if we are getting packets but not yet forwarding them to the player
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
          
              int64_t delta = ((int64_t)first_packet_timestamp-(int64_t)reference_timestamp);

              first_packet_time_to_play = reference_timestamp_time+((delta+(int64_t)config.latency)<<32)/44100; // using the latency requested...
              if (local_time_now>=first_packet_time_to_play) {
                debug(1,"First packet is late! It should have played before now. Flushing 0.1 seconds");
                player_flush(first_packet_timestamp+4410);
              }
            }
          }      

          if (first_packet_time_to_play!=0) {

            uint32_t filler_size = frame_size;
            uint32_t max_dac_delay = DAC_BUFFER_QUEUE_DESIRED_LENGTH;
            // if (dac_delay==0) // i.e. if this is the first fill
              filler_size = 4410; // 0.1 second -- the maximum we'll add to the DAC

            if (local_time_now>=first_packet_time_to_play) {
              // we've gone past the time...
              // debug(1,"Run past the exact start time by %llu frames, with time now of %llx, fpttp of %llx and dac_delay of %d and %d packets; flush.",(((tn-first_packet_time_to_play)*44100)>>32)+dac_delay,tn,first_packet_time_to_play,dac_delay,seq_diff(ab_read, ab_write));
            
              if (config.output->flush)
                config.output->flush();
              ab_resync();
              first_packet_timestamp = 0;
              first_packet_time_to_play = 0;
            } else {
              uint64_t gross_frame_gap = ((first_packet_time_to_play-local_time_now)*44100)>>32;
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
                if (fs>(max_dac_delay-dac_delay))
                  fs=max_dac_delay-dac_delay;
                if ((exact_frame_gap<=fs) || (exact_frame_gap<=frame_size*2)) {
                  fs=exact_frame_gap;
                  // debug(1,"Exact frame gap is %llu; play %d frames of silence. Dac_delay is %d, with %d packets, ab_read is %04x, ab_write is %04x.",exact_frame_gap,fs,dac_delay,seq_diff(ab_read, ab_write),ab_read,ab_write);
                  ab_buffering = 0;
                }
                signed short *silence;
                silence = malloc(FRAME_BYTES(fs));
                memset(silence, 0, FRAME_BYTES(fs));
                // debug(1,"Exact frame gap is %llu; play %d frames of silence. Dac_delay is %d, with %d packets.",exact_frame_gap,fs,dac_delay,seq_diff(ab_read, ab_write));
                config.output->play(silence, fs);
                free(silence);
              }
            }
          }
        }
      }
    }
    wait = (ab_buffering || (dac_delay>=DAC_BUFFER_QUEUE_DESIRED_LENGTH) || (!ab_synced)) && (!please_stop);
//    wait = (ab_buffering ||  (seq_diff(ab_read, ab_write) < (config.latency-22000)/(352)) || (!ab_synced)) && (!please_stop);
    if (wait) {
      uint64_t time_to_wait_for_wakeup_fp = ((uint64_t)1<<32)/44100; // this is time period of one frame
      time_to_wait_for_wakeup_fp *= 4*352; // four full 352-frame packets
      time_to_wait_for_wakeup_fp /= 3;  //four thirds of a packet time 
      
#ifdef COMPILE_FOR_LINUX
      uint64_t time_of_wakeup_fp = local_time_now+time_to_wait_for_wakeup_fp;
      uint64_t sec = time_of_wakeup_fp>>32;
      uint64_t nsec = ((time_of_wakeup_fp&0xffffffff)*1000000000)>>32;
      
      struct timespec time_of_wakeup;
      time_of_wakeup.tv_sec = sec;
      time_of_wakeup.tv_nsec = nsec;
      
      pthread_cond_timedwait(&flowcontrol,&ab_mutex,&time_of_wakeup);      
      // int rc = pthread_cond_timedwait(&flowcontrol,&ab_mutex,&time_of_wakeup);
      // if (rc!=0)
      //  debug(1,"pthread_cond_timedwait returned error code %d.",rc);
#endif
#ifdef COMPILE_FOR_OSX
      uint64_t sec = time_to_wait_for_wakeup_fp>>32;;
      uint64_t nsec = ((time_to_wait_for_wakeup_fp&0xffffffff)*1000000000)>>32;
      struct timespec time_to_wait;
      time_to_wait.tv_sec = sec;
      time_to_wait.tv_nsec = nsec;
      pthread_cond_timedwait_relative_np(&flowcontrol,&ab_mutex,&time_to_wait);     
#endif
    }    
  } while (wait);

  if (please_stop) {
    pthread_mutex_unlock(&ab_mutex);
    return 0;
  }

  seq_t read = ab_read;

  // check if t+8, t+16, t+32, t+64, t+128, ... (buffer_start_fill / 2)
  // packets have arrived... last-chance resend
  
  
  if (!ab_buffering) {
    for (i = 8; i < (seq_diff(ab_read,ab_write) / 2); i = (i * 2)) {
      seq_t next = seq_sum(ab_read,i);
      abuf = audio_buffer + BUFIDX(next);
      if (!abuf->ready) {
        rtp_request_resend(next, 1);
        // debug(1,"Resend %u.",next);
        resend_requests++;
      }
    }
  }
  
  if (!curframe->ready) {
    // debug(1, "    %d. Supplying a silent frame.", read);
    missing_packets++;
    memset(curframe->data, 0, FRAME_BYTES(frame_size));
    curframe->timestamp=0;    
  }
  curframe->ready = 0;
  ab_read=SUCCESSOR(ab_read);
  pthread_mutex_unlock(&ab_mutex);
  return curframe;
}

static inline short shortmean(short a, short b) {
  long al = (long)a;
  long bl = (long)b;
  long longmean = (al+bl)/2;
  short r = (short)longmean;
  if (r!=longmean)
    debug(1,"Error calculating average of two shorts");
  return r;
}

// stuff: 1 means add 1; 0 means do nothing; -1 means remove 1
static int stuff_buffer_basic(short *inptr, short *outptr, int stuff) {
    if ((stuff>1) || (stuff<-1)) {
      debug(1,"Stuff argument to stuff_buffer must be from -1 to +1.");
      return frame_size;
    }
    int i;
    int stuffsamp = frame_size;
    if (stuff)
//      stuffsamp = rand() % (frame_size - 1);
      stuffsamp = (rand() % (frame_size-2))+1; // ensure there's always a sample before and after the item

    pthread_mutex_lock(&vol_mutex);
    for (i=0; i<stuffsamp; i++) {   // the whole frame, if no stuffing
        *outptr++ = dithered_vol(*inptr++);
        *outptr++ = dithered_vol(*inptr++);
    };
    if (stuff) {
        if (stuff==1) {
            debug(3, "+++++++++");
            // interpolate one sample
            //*outptr++ = dithered_vol(((long)inptr[-2] + (long)inptr[0]) >> 1);
            //*outptr++ = dithered_vol(((long)inptr[-1] + (long)inptr[1]) >> 1);
            *outptr++ = dithered_vol(shortmean(inptr[-2],inptr[0]));
            *outptr++ = dithered_vol(shortmean(inptr[-1],inptr[1]));
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

#ifdef HAVE_LIBSOXR
// stuff: 1 means add 1; 0 means do nothing; -1 means remove 1
static int stuff_buffer_soxr(short *inptr, short *outptr, int stuff) {
  if ((stuff>1) || (stuff<-1)) {
    debug(1,"Stuff argument to sox_stuff_buffer must be from -1 to +1.");
    return frame_size;
  }
  int i;
  short *ip,*op;
  ip=inptr;
  op=outptr;
  
  if (stuff) {
    // debug(1,"Stuff %d.",stuff);
    soxr_io_spec_t io_spec;
    io_spec.itype = SOXR_INT16_I;
    io_spec.otype = SOXR_INT16_I;
    io_spec.scale = 1.0; // this seems to crash if not = 1.0
    io_spec.e = NULL;
    io_spec.flags = 0;

    size_t odone;

    soxr_error_t error = soxr_oneshot(frame_size, frame_size + stuff, 2, /* Rates and # of chans. */
      inptr, frame_size, NULL, /* Input. */
      outptr, frame_size + stuff, &odone, /* Output. */
      &io_spec, /* Input, output and transfer spec. */
      NULL, NULL); /* Default configuration.*/

    if (error)
      die("soxr error: %s\n", "error: %s\n", soxr_strerror(error));

    if (odone > frame_size + 1)
      die("odone = %d!\n", odone);

    const int gpm = 5;
    
    // keep the first (dpm) samples, to mitigate the Gibbs phenomenon
    for (i=0;i<gpm;i++) {
      *op++ = *ip++;
      *op++ = *ip++;
    }

    // keep the last (dpm) samples, to mitigate the Gibbs phenomenon
    op=outptr+(frame_size+stuff-gpm)*sizeof(short);
    ip=inptr+(frame_size-gpm)*sizeof(short);
    for (i=0;i<gpm;i++) {
      *op++ = *ip++;
      *op++ = *ip++;
    }

    // finally, adjust the volume, if necessary
    if (volume!=1.0) {
      // pthread_mutex_lock(&vol_mutex);
      op=outptr;
      for (i=0; i<frame_size+stuff; i++) {
        *op = dithered_vol(*op);
        op++;
        *op = dithered_vol(*op);
        op++;
      };
      // pthread_mutex_unlock(&vol_mutex);
    }
    
  } else { // the whole frame, if no stuffing
  
    // pthread_mutex_lock(&vol_mutex);
    for (i=0; i<frame_size; i++) {   
      *op++ = dithered_vol(*ip++);
      *op++ = dithered_vol(*ip++);
    };
    // pthread_mutex_unlock(&vol_mutex);
  }
  return frame_size + stuff;
}
#endif

typedef struct stats { // statistics for running averages
  int64_t sync_error,correction,drift;
} stats_t;

static void *player_thread_func(void *arg) {
//this is about half a minute
#define trend_interval 3758
  stats_t statistics[trend_interval];
  int number_of_statistics,oldest_statistic,newest_statistic;
  int64_t tsum_of_sync_errors,tsum_of_corrections,tsum_of_insertions_and_deletions,tsum_of_drifts;
  int64_t previous_sync_error,previous_correction;
  int64_t minimum_dac_queue_size = 1000000;
  int32_t minimum_buffer_occupancy = BUFFER_FRAMES;
  int32_t maximum_buffer_occupancy = 0;
  
  int play_samples;
  int64_t current_delay;
  int play_number = 0;
  time_of_last_audio_packet = 0;
  shutdown_requested = 0;
  number_of_statistics = oldest_statistic = newest_statistic = 0;
  tsum_of_sync_errors = tsum_of_corrections = tsum_of_insertions_and_deletions = tsum_of_drifts = 0;

  const int print_interval = trend_interval; // don't ask...
  // I think it's useful to keep this prime to prevent it from falling into a pattern with some other process.
  
  char  rnstate[256];
  initstate(time(NULL),rnstate,256);
  
  signed short *inbuf, *outbuf, *silence;
  outbuf = malloc(OUTFRAME_BYTES(frame_size));
  silence = malloc(OUTFRAME_BYTES(frame_size));
  memset(silence, 0, OUTFRAME_BYTES(frame_size));

  late_packet_message_sent=0;
  missing_packets=late_packets=too_late_packets=resend_requests=0;
  flush_rtp_timestamp=0x7fffffff; // it seems this number has a special significance -- it seems to be used as a null operand, so we'll use it like that too
  int sync_error_out_of_bounds = 0; // number of times in a row that there's been a serious sync error
  while (!please_stop) {
    abuf_t *inframe = buffer_get_frame();
    if (inframe) {
      inbuf = inframe->data;
      if (inbuf) {
        play_number++;
        // if it's a supplied silent frame, let us know...
        if (inframe->timestamp==0) {
          // debug(1,"Player has a supplied silent frame.");
          last_seqno_read = (SUCCESSOR(last_seqno_read)&0xffff); //manage the packet out of sequence minder
          config.output->play(inbuf, frame_size);
        } else {
          // We have a frame of data. We need to see if we want to add or remove a frame from it to keep in sync.
          // So we calculate the timing error for the first frame in the DAC.
          // If it's ahead of time, we add one audio frame to this frame to delay a subsequent frame
          // If it's late, we remove an audio frame from this frame to bring a subsequent frame forward in time
          
          uint32_t reference_timestamp;
          uint64_t reference_timestamp_time;
          get_reference_timestamp_stuff(&reference_timestamp,&reference_timestamp_time);

          int64_t rt,nt;
          rt = reference_timestamp;
          nt = inframe->timestamp;
          
          uint64_t local_time_now = get_absolute_time_in_fp();
          //struct timespec tn;
          //clock_gettime(CLOCK_MONOTONIC,&tn);
          //uint64_t local_time_now=((uint64_t)tn.tv_sec<<32)+((uint64_t)tn.tv_nsec<<32)/1000000000;

          int64_t td_in_frames;
          int64_t td = local_time_now-reference_timestamp_time;
          // debug(1,"td is %lld.",td);          
          if (td>=0) {
            td_in_frames = (td*44100)>>32;
          } else {
            td_in_frames = -((-td*44100)>>32);
          }

          if (config.output->delay) {
            current_delay = config.output->delay();
            if (current_delay==-1) {
              debug(1,"Delay error when checking running latency.");
              current_delay=0;
            }
          } else {
            current_delay = 0;
          }
          if (current_delay<minimum_dac_queue_size)
            minimum_dac_queue_size=current_delay;
          
          uint32_t bo = seq_diff(ab_read,ab_write);
          
          if (bo<minimum_buffer_occupancy)
            minimum_buffer_occupancy=bo;

          if (bo>maximum_buffer_occupancy)
            maximum_buffer_occupancy=bo;

          // this is the actual delay, including the latency we actually want, which will fluctuate a good bit about a potentially rising or falling trend.
          int64_t delay = td_in_frames+rt-(nt-current_delay);
          
          // This is the timing error for the next audio frame in the DAC.
          int64_t sync_error = delay-config.latency;
          
          int amount_to_stuff = 0;
          // require a certain error before bothering to fix it...
          if (sync_error>88) {
            amount_to_stuff = -1;
          }
          if (sync_error<-88) {
            amount_to_stuff = 1;
          }
          
          // only allow stuffing if there is enough time to do it -- check DAC buffer...
          if (current_delay<DAC_BUFFER_QUEUE_MINIMUM_LENGTH) {
            // debug(1,"DAC buffer too short to allow stuffing.");
            amount_to_stuff=0;
          }

          // try to keep the corrections definitely below 1 in 1000 audio frames
          if (amount_to_stuff) {
            uint32_t x = random()%1000;
            if (x>352)
              amount_to_stuff=0;
          }
            
          if ((amount_to_stuff==0) && (fix_volume==0x10000)) {
            // if no stuffing needed and no volume adjustment, then
            // don't send to stuff_buffer_* and don't copy to outbuf; just send directly to the output device...
            config.output->play(inbuf, frame_size);
          } else {
#ifdef HAVE_LIBSOXR
            switch (config.packet_stuffing) {
              case ST_basic:
//                if (amount_to_stuff) debug(1,"Basic stuff...");
                  play_samples = stuff_buffer_basic(inbuf, outbuf,amount_to_stuff);
                break;
              case ST_soxr:
//                if (amount_to_stuff) debug(1,"Soxr stuff...");
                  play_samples = stuff_buffer_soxr(inbuf, outbuf,amount_to_stuff);
                break;
            }     
#else
//          if (amount_to_stuff) debug(1,"Standard stuff...");
            play_samples = stuff_buffer_basic(inbuf, outbuf,amount_to_stuff);
#endif

      /*
      {
        int co;
        int is_silent=1;
        short *p = outbuf;
        for (co=0;co<play_samples;co++) {
          if (*p!=0)
            is_silent=0;
          p++;
        }
        if (is_silent)
          debug(1,"Silence!");
      }
      */

             config.output->play(outbuf, play_samples);
          }
          
          // check for loss of sync
          // timestamp of zero means an inserted silent frame in place of a missing frame
          if ((inframe->timestamp!=0) && (!please_stop) && (config.resyncthreshold!=0) && (abs(sync_error)>config.resyncthreshold)) {
            sync_error_out_of_bounds++;
            // debug(1,"Sync error out of bounds: Error: %lld; previous error: %lld; DAC: %lld; timestamp: %llx, time now %llx",sync_error,previous_sync_error,current_delay,inframe->timestamp,local_time_now);    
             if (sync_error_out_of_bounds>3) {
              debug(1,"Lost sync with source for %d consecutive packets -- flushing and resyncing. Error: %lld.",sync_error_out_of_bounds,sync_error);
              sync_error_out_of_bounds = 0;
              player_flush(nt);
             }
          } else {
            sync_error_out_of_bounds = 0;
          }
              
          // debug(1,"Sync error %lld frames. Amount to stuff %d." ,sync_error,amount_to_stuff);
          
          // new stats calculation. We want a running average of sync error, drift, adjustment, number of additions+subtractions
          
          if (number_of_statistics==trend_interval) {
            // here we remove the oldest statistical data and take it from the summaries as well
            tsum_of_sync_errors-=statistics[oldest_statistic].sync_error;
            tsum_of_drifts -= statistics[oldest_statistic].drift;
            if (statistics[oldest_statistic].correction>0)
              tsum_of_insertions_and_deletions-=statistics[oldest_statistic].correction;
            else
              tsum_of_insertions_and_deletions+=statistics[oldest_statistic].correction;
            tsum_of_corrections-=statistics[oldest_statistic].correction;
            oldest_statistic=(oldest_statistic+1)%trend_interval;
            number_of_statistics--; 
          }
          
          statistics[newest_statistic].sync_error = sync_error;
          statistics[newest_statistic].correction = amount_to_stuff;

          if (number_of_statistics==0)
            statistics[newest_statistic].drift = 0;
          else
            statistics[newest_statistic].drift = sync_error-previous_sync_error-previous_correction;

          previous_sync_error = sync_error;
          previous_correction = amount_to_stuff;

          tsum_of_sync_errors += sync_error;
          tsum_of_drifts += statistics[newest_statistic].drift;
          if (amount_to_stuff>0)
            tsum_of_insertions_and_deletions+=amount_to_stuff;
          else
            tsum_of_insertions_and_deletions-=amount_to_stuff;
          tsum_of_corrections+=amount_to_stuff;
          
          newest_statistic=(newest_statistic+1)%trend_interval;
          number_of_statistics++;
          
          // check sequencing
          if (last_seqno_read==-1)
              last_seqno_read=inframe->sequence_number;
            else {
              last_seqno_read = (SUCCESSOR(last_seqno_read) & 0xffff);           
              if (inframe->sequence_number!=last_seqno_read)
                debug(1,"Player: packets out of sequence: expected: %d, got %d.",last_seqno_read,inframe->sequence_number);
                last_seqno_read=inframe->sequence_number; // reset warning...
          }
        }
        if (play_number%print_interval==0) {
          // we can now calculate running averages for sync error (frames), corrections (ppm), insertions plus deletions (ppm), drift (ppm)
          double moving_average_sync_error = (1.0*tsum_of_sync_errors)/number_of_statistics;
          double moving_average_correction = (1.0*tsum_of_corrections)/number_of_statistics;
          double moving_average_insertions_plus_deletions = (1.0*tsum_of_insertions_and_deletions)/number_of_statistics;
          double moving_average_drift = (1.0*tsum_of_drifts)/number_of_statistics;
          // if ((play_number/print_interval)%20==0)
            debug(1,"Sync error: %.1f (frames); net correction: %.1f (ppm); corrections: %.1f (ppm); missing packets %llu; late packets %llu; too late packets %llu; resend requests %llu; min DAC queue size %lli, min and max buffer occupancy %u and %u.", moving_average_sync_error, moving_average_correction*1000000/352, moving_average_insertions_plus_deletions*1000000/352,missing_packets,late_packets,too_late_packets,resend_requests,minimum_dac_queue_size,minimum_buffer_occupancy,maximum_buffer_occupancy);
          minimum_dac_queue_size=1000000; // hack reset
          maximum_buffer_occupancy = 0; // can't be less than this
          minimum_buffer_occupancy = BUFFER_FRAMES; // can't be more than this
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
// Thus, we ask our vol2attn function for an appropriate dB between -48.1 and 0 dB and translate it back to a number.
 
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
	// debug(1,"Flush requested up to %u. It seems as if 2147483647 is special.",timestamp);
  pthread_mutex_lock(&flush_mutex);
  flush_requested=1;
  //if (timestamp!=0x7fffffff)
  flush_rtp_timestamp=timestamp; // flush all packets up to (and including?) this
  pthread_mutex_unlock(&flush_mutex);
}

int player_play(stream_cfg *stream) {
  packet_count = 0;
  if (config.buffer_start_fill > BUFFER_FRAMES)
    die("specified buffer starting fill %d > buffer size %d",
      config.buffer_start_fill, BUFFER_FRAMES);

#ifdef HAVE_LIBPOLARSSL
  memset(&dctx,0,sizeof(aes_context));
  aes_setkey_dec(&dctx, stream->aeskey, 128);
#endif

#ifdef HAVE_LIBSSL
  AES_set_decrypt_key(stream->aeskey, 128, &aes);
#endif

  aesiv = stream->aesiv;
  init_decoder(stream->fmtp);
  // must be after decoder init
  init_buffer();
  please_stop = 0;
  command_start();  
  // set the flowcontrol condition variable to wait on a monotonic clock
#ifdef COMPILE_FOR_LINUX
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock( &attr, CLOCK_MONOTONIC); // can't do this in OS X, and don't need it.
  int rc = pthread_cond_init(&flowcontrol,&attr);
#endif
#ifdef COMPILE_FOR_OSX
  int rc = pthread_cond_init(&flowcontrol,NULL);  
#endif
  if (rc)
    debug(1,"Error initialising condition variable.");
  config.output->start(sampling_rate);
  pthread_create(&player_thread, NULL, player_thread_func, NULL);

  return 0;
}

void player_stop(void) {
  please_stop = 1;
  pthread_cond_signal(&flowcontrol); // tell it to give up
  pthread_join(player_thread, NULL);
  config.output->stop();
  command_stop();
  free_buffer();
  free_decoder();
  int rc = pthread_cond_destroy(&flowcontrol);
  if (rc)
    debug(1,"Error destroying condition variable.");
}
