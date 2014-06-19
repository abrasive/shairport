/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
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


// interthread variables
static double volume = 1.0;
static int fix_volume = 0x10000;
static pthread_mutex_t vol_mutex = PTHREAD_MUTEX_INITIALIZER;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
#define BUFFER_FRAMES  512
#define MAX_PACKET      2048
static int sane_buffer_size;

//player states
#define BUFFERING 0
#define SYNCING   1
#define PLAYING   2
int state;

//buffer states
#define SIGNALLOSS 0
#define UNSYNC 1
#define INSYNC 2

typedef struct audio_buffer_entry {   // decoded audio packets
    int ready;
    sync_cfg sync;
    signed short *data;
} abuf_t;
static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

// mutex-protected variables
static seq_t ab_read, ab_write;
static int ab_buffering = 1, ab_synced = 0;
static pthread_mutex_t ab_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ab_resync(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        audio_buffer[i].ready = 0;
    ab_synced = 0;
    ab_buffering = 1;
}

// reset the audio frames in the range to NOT ready
static void ab_reset(seq_t from, seq_t to) {
    abuf_t *abuf = 0;

    while (seq_diff(from, to)) {
        if (seq_diff(from, to) >= BUFFER_FRAMES) {
           from =  from + BUFFER_FRAMES;
        } else {
           abuf = audio_buffer + BUFIDX(from);
           abuf->ready = 0;
           from++;
        }
    }
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

static long us_to_frames(long long us) {
    return us * sampling_rate / 1000000;
}

static inline long long get_sync_time(long long ntp_tsp) {
    long long sync_time_est;
    sync_time_est = (ntp_tsp + config.delay) - (tstp_us() + get_ntp_offset() + config.output->get_delay());
    return sync_time_est;
}

void player_put_packet(seq_t seqno, sync_cfg sync_tag, uint8_t *data, int len) {
    abuf_t *abuf = 0;
    int16_t buf_fill;

    pthread_mutex_lock(&ab_mutex);
    if (ab_synced == SIGNALLOSS) {
        debug(2, "picking up first seqno %04X\n", seqno);
        ab_write = seqno-1;
        ab_read = seqno;
        ab_synced = UNSYNC;
    }
    debug(3, "packet: ab_write %04X, ab_read %04X, seqno %04X\n", ab_write, ab_read, seqno);
    if (seq_diff(ab_write, seqno) == 1) {                  // expected packet
        abuf = audio_buffer + BUFIDX(seqno);
        ab_write = seqno;
    } else if (seq_order(ab_write, seqno)) {    // newer than expected
        rtp_request_resend(ab_write+1, seqno-1);
        abuf = audio_buffer + BUFIDX(seqno);
        ab_write = seqno;
    } else if (seq_order(ab_read - 1, seqno)) {     // late but not yet played
        abuf = audio_buffer + BUFIDX(seqno);
    } else {    // too late.
        debug(1, "late packet %04X (%04X:%04X)\n", seqno, ab_read, ab_write);
    }
    buf_fill = seq_diff(ab_read, ab_write);
    pthread_mutex_unlock(&ab_mutex);

    if (abuf) {
        alac_decode(abuf->data, data, len);
        abuf->sync.rtp_tsp = sync_tag.rtp_tsp;
        // sync packets with extension bit seem to be one audio packet off:
        // if the extension bit was set, pull back the ntp time by one packet's time
        if (sync_tag.sync_mode == E_NTPSYNC) {
            abuf->sync.ntp_tsp = sync_tag.ntp_tsp - (long long)frame_size * 1000000LL / (long long)sampling_rate;
            abuf->sync.sync_mode = NTPSYNC;
        } else {
            abuf->sync.ntp_tsp = sync_tag.ntp_tsp;
            abuf->sync.sync_mode = sync_tag.sync_mode;
        }
        abuf->ready = 1;
    }

    pthread_mutex_lock(&ab_mutex);
    if (ab_synced == UNSYNC && (sync_tag.sync_mode == NTPSYNC)) {
       // only stop buffering when the new frame is a timestamp with good sync
       long long sync_time = get_sync_time(sync_tag.ntp_tsp);
       if (sync_time > (config.delay/8)) {
          debug(1, "found good sync (%04X:%04X) sync: %lld\n", ab_read, ab_write, sync_time);
          ab_synced = INSYNC;
       }
       ab_reset(ab_read, seqno);
       ab_read = seqno;
    }
    if (ab_synced == INSYNC && ab_buffering && buf_fill >= sane_buffer_size) {
        debug(1, "buffering over. starting play\n");
        ab_buffering = 0;
    }
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
static short *buffer_get_frame(sync_cfg *sync_tag) {
    int16_t buf_fill;
    seq_t read, next;
    abuf_t *abuf = 0;
    int i;

    sync_tag->sync_mode = NOSYNC;

    pthread_mutex_lock(&ab_mutex);
    if (ab_buffering) {
        pthread_mutex_unlock(&ab_mutex);
        return 0;
    }

    buf_fill = seq_diff(ab_read, ab_write);
    if (buf_fill < 1) {
        if (buf_fill < 1)
            warn("underrun.");
        ab_buffering = 1;
        ab_synced = SIGNALLOSS;
        state = BUFFERING;
        pthread_mutex_unlock(&ab_mutex);
        return 0;
    }
    if (buf_fill >= BUFFER_FRAMES) {   // overrunning! uh-oh. restart at a sane distance
        warn("overrun.");
        ab_read = ab_write - sane_buffer_size;
    }
    read = ab_read;
    ab_read++;

    // check if t+16, t+32, t+64, t+128, ... (sane_buffer_size / 2)
    // packets have arrived... last-chance resend
    if (!ab_buffering) {
        for (i = 16; i < (sane_buffer_size / 2); i = (i * 2)) {
            next = ab_read + i;
            abuf = audio_buffer + BUFIDX(next);
            if (!abuf->ready) {
                rtp_request_resend(next, next);
            }
        }
    }

    abuf_t *curframe = audio_buffer + BUFIDX(read);
    if (!curframe->ready) {
        debug(1, "missing frame %04X.", read);
        memset(curframe->data, 0, FRAME_BYTES(frame_size));
    }

    curframe->ready = 0;
    sync_tag->rtp_tsp = curframe->sync.rtp_tsp;
    sync_tag->ntp_tsp = curframe->sync.ntp_tsp;
    sync_tag->sync_mode = curframe->sync.sync_mode;
    pthread_mutex_unlock(&ab_mutex);

    return curframe->data;
}

static int stuff_buffer(double playback_rate, short *inptr, short *outptr) {
    int i;
    int stuffsamp = frame_size;
    int stuff = 0;
    double p_stuff;

    p_stuff = 1.0 - pow(1.0 - fabs(playback_rate-1.0), frame_size);

    if (rand() < p_stuff * RAND_MAX) {
        stuff = playback_rate > 1.0 ? -1 : 1;
        stuffsamp = rand() % (frame_size - 1);
    }

    pthread_mutex_lock(&vol_mutex);
    for (i=0; i<stuffsamp; i++) {   // the whole frame, if no stuffing
        *outptr++ = dithered_vol(*inptr++);
        *outptr++ = dithered_vol(*inptr++);
    };
    if (stuff) {
        if (stuff==1) {
            debug(3, "+++++++++\n");
            // interpolate one sample
            *outptr++ = dithered_vol(((long)inptr[-2] + (long)inptr[0]) >> 1);
            *outptr++ = dithered_vol(((long)inptr[-1] + (long)inptr[1]) >> 1);
        } else if (stuff==-1) {
            debug(3, "---------\n");
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

//constant first-order filter
#define ALPHA 0.945
#define LOSS 850000.0

static double bf_playback_rate = 1.0;

static void *player_thread_func(void *arg) {
    int play_samples = frame_size;
    sync_cfg sync_tag;
    long long sync_time;
    double sync_time_diff = 0.0;
    long sync_frames = 0;
    state = BUFFERING;

    signed short *inbuf, *outbuf, *resbuf, *silence;
    outbuf = resbuf = malloc(OUTFRAME_BYTES(frame_size));
    inbuf = silence = malloc(OUTFRAME_BYTES(frame_size));
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
    debug(1,"Player STATE: %d\n", state);
    while (!please_stop) {
        switch (state) {
        case BUFFERING: {
            inbuf = buffer_get_frame(&sync_tag);
            // as long as the buffer keeps returning NULL, we assume it is still filling up
            if (inbuf) {
                if (sync_tag.sync_mode != NOSYNC) {
                    // figure out how much silence to insert before playback starts
                    sync_frames = us_to_frames(get_sync_time(sync_tag.ntp_tsp));
                } else {
                    // what if first packet(s) is lost?
                    warn("Ouch! first packet has no sync...\n");
                    sync_frames = us_to_frames(config.delay) - config.output->get_delay();
                }
                if (sync_frames < 0)
                    sync_frames = 0;
                debug(1, "Fill with %ld frames and %ld samples\n", sync_frames / frame_size , sync_frames % frame_size);
                state = SYNCING;
                debug(1,"Changing player STATE: %d\n", state);
            }
            outbuf = silence;
            play_samples = frame_size;
            break;
        }
        case SYNCING: {
            if (sync_frames > 0) {
                if (((sync_frames < frame_size * 50) && (sync_frames >= frame_size * 49)) && \
                        (sync_tag.sync_mode != NOSYNC)) {
                    debug(3,"sync_frames adjusting: %d->", sync_frames);
                    // figure out how much silence to insert before playback starts
                    sync_frames = us_to_frames(get_sync_time(sync_tag.ntp_tsp));
                    if (sync_frames < 0)
                        sync_frames = 0;
                    debug(3,"%d\n", sync_frames);
                }
                play_samples = (sync_frames >= frame_size ? frame_size : sync_frames);
                outbuf = silence;
                sync_frames -= play_samples;

                debug(3,"Samples to go before playback start: %d\n", sync_frames);
            } else {
                outbuf = resbuf;
                play_samples = stuff_buffer(bf_playback_rate, inbuf, outbuf);
                state = PLAYING;
                debug(1,"Changing player STATE: %d\n", state);
            }
            break;
        }
        case PLAYING: {
            inbuf = buffer_get_frame(&sync_tag);
            if (!inbuf)
                inbuf = silence;
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
            if (sync_tag.sync_mode == NTPSYNC) {
                //check if we're still in sync.
                sync_time = get_sync_time(sync_tag.ntp_tsp);
                sync_time_diff = (ALPHA * sync_time_diff) + (1.0- ALPHA) * (double)sync_time;
                bf_playback_rate = 1.0 - (sync_time_diff / LOSS);
                debug(2, "Playback rate %f, sync_time %lld\n", bf_playback_rate, sync_time);
            }
            play_samples = stuff_buffer(bf_playback_rate, inbuf, outbuf);
            break;
        }
        default:
            break;
        }
        config.output->play(outbuf, play_samples);
    }

    free(resbuf);
    free(silence);
    return 0;
}

// takes the volume as specified by the airplay protocol
void player_volume(double f) {
    double linear_volume = pow(10.0, 0.05*f);

    if (config.output->volume) {
        config.output->volume(linear_volume);
    } else {
        pthread_mutex_lock(&vol_mutex);
        volume = linear_volume;
        fix_volume = 65536.0 * volume;
        pthread_mutex_unlock(&vol_mutex);
    }
}

unsigned long player_flush(int seqno, unsigned long rtp_tsp) {
    unsigned long result = 0;
    pthread_mutex_lock(&ab_mutex);
    abuf_t *curframe = audio_buffer + BUFIDX(ab_read);
    if (curframe->ready) {
        result = curframe->sync.rtp_tsp;
    }

    ab_resync();
    ab_write = seqno-1;
    ab_read = seqno;
    // a negative seqno mean the client did not supply one, so we will
    // treat the first audio packet that comes along, as the first in the audio stream
    ab_synced = (seqno < 0 ? SIGNALLOSS : UNSYNC);
    pthread_mutex_unlock(&ab_mutex);
    state = BUFFERING;
    return result;
}

int player_play(stream_cfg *stream) {
    AES_set_decrypt_key(stream->aeskey, 128, &aes);
    aesiv = stream->aesiv;
    init_decoder(stream->fmtp);
    // must be after decoder init
    init_buffer();
#ifdef FANCY_RESAMPLING
    init_src();
#endif

    sane_buffer_size = ((config.delay / 1000) * sampling_rate * 2) / (frame_size * 1000 * 3);
    sane_buffer_size = (sane_buffer_size >= 10 ? sane_buffer_size : 10);
    if (sane_buffer_size > BUFFER_FRAMES)
        die("buffer starting fill %d > buffer size %d", sane_buffer_size, BUFFER_FRAMES);
    debug(1, "buffer size set to %d\n", sane_buffer_size);

    please_stop = 0;
    command_start();
    config.output->start(sampling_rate);
    // generic outputs cannot report the delay, so we estimate the buffer depth
    // at startup and hope for the best
    if (!config.output->get_delay) {
        config.output->get_delay = audio_get_delay;
        audio_estimate_delay(config.output);
    }
    pthread_create(&player_thread, NULL, player_thread_func, NULL);

    return 0;
}

void player_stop(void) {
    please_stop = 1;
    pthread_join(player_thread, NULL);
    config.output->stop();
    command_stop();
    free_buffer();
    free_decoder();
#ifdef FANCY_RESAMPLING
    free_src();
#endif
}
