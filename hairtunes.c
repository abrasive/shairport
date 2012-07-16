/*
 * HairTunes - RAOP packet handler and slave-clocked replay engine
 * Copyright (c) James Laird 2011
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
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <openssl/aes.h>
#include <math.h>
#include <sys/stat.h>

#include "hairtunes.h"
#include <sys/signal.h>
#include <fcntl.h>
#include <ao/ao.h>

#ifdef FANCY_RESAMPLING
#include <samplerate.h>
#endif

#include <assert.h>
static int debug = 0;

#include "alac.h"

// and how full it needs to be to begin (must be <BUFFER_FRAMES)
#define START_FILL    282

#define MAX_PACKET      2048

typedef unsigned short seq_t;

// global options (constant after init)
static unsigned char aeskey[16], aesiv[16];
static AES_KEY aes;
static char *rtphost = 0;
static int dataport = 0, controlport = 0, timingport = 0;
static int fmtp[32];
static int sampling_rate;
static int frame_size;

static int buffer_start_fill;

static char *libao_driver = NULL;
static char *libao_devicename = NULL;
static char *libao_deviceid = NULL; // ao_options expects "char*"

// FIFO name and file handle
static char *pipename = NULL;
static int pipe_handle = -1;

#define FRAME_BYTES (4*frame_size)
// maximal resampling shift - conservative
#define OUTFRAME_BYTES (4*(frame_size+3))


static alac_file *decoder_info;

#ifdef FANCY_RESAMPLING
static int fancy_resampling = 1;
static SRC_STATE *src;
#endif

static int  init_rtp(void);
static void init_buffer(void);
static int  init_output(void);
static void rtp_request_resend(seq_t first, seq_t last);
static void ab_resync(void);

// interthread variables
// stdin->decoder
static double volume = 1.0;
static int fix_volume = 0x10000;
static pthread_mutex_t vol_mutex = PTHREAD_MUTEX_INITIALIZER;



typedef struct audio_buffer_entry {   // decoded audio packets
    int ready;
    signed short *data;
} abuf_t;
static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

// mutex-protected variables
static seq_t ab_read, ab_write;
static int ab_buffering = 1, ab_synced = 0;
static pthread_mutex_t ab_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ab_buffer_ready = PTHREAD_COND_INITIALIZER;

static void die(char *why) {
    fprintf(stderr, "FATAL: %s\n", why);
    exit(1);
}

#ifdef HAIRTUNES_STANDALONE
static int hex2bin(unsigned char *buf, char *hex) {
    int i, j;
    if (strlen(hex) != 0x20)
        return 1;
    for (i=0; i<0x10; i++) {
        if (!sscanf(hex, "%2X", &j))
           return 1;
        hex += 2;
        *buf++ = j;
    }
    return 0;
}
#endif

static int init_decoder(void) {
    alac_file *alac;

    frame_size = fmtp[1]; // stereo samples
    sampling_rate = fmtp[11];

    int sample_size = fmtp[3];
    if (sample_size != 16)
        die("only 16-bit samples supported!");

    alac = create_alac(sample_size, 2);
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
    allocate_buffers(alac);
    return 0;
}

int hairtunes_init(char *pAeskey, char *pAesiv, char *fmtpstr, int pCtrlPort, int pTimingPort,
         int pDataPort, char *pRtpHost, char*pPipeName, char *pLibaoDriver, char *pLibaoDeviceName, char *pLibaoDeviceId,
         int bufStartFill)
{
    if(pAeskey != NULL)    
        memcpy(aeskey, pAeskey, sizeof(aeskey));
    if(pAesiv != NULL)
        memcpy(aesiv, pAesiv, sizeof(aesiv));
    if(pRtpHost != NULL)
        rtphost = pRtpHost;
    if(pPipeName != NULL)
        pipename = pPipeName;
    if(pLibaoDriver != NULL)
        libao_driver = pLibaoDriver;
    if(pLibaoDeviceName != NULL)
        libao_devicename = pLibaoDeviceName;
    if(pLibaoDeviceId != NULL)
        libao_deviceid = pLibaoDeviceId;
    
    controlport = pCtrlPort;
    timingport = pTimingPort;
    dataport = pDataPort;
    if(bufStartFill < 0)
        bufStartFill = START_FILL;
    buffer_start_fill = bufStartFill;

    AES_set_decrypt_key(aeskey, 128, &aes);

    memset(fmtp, 0, sizeof(fmtp));
    int i = 0;
    char *arg;
    while ( (arg = strsep(&fmtpstr, " \t")) )
        fmtp[i++] = atoi(arg);

    init_decoder();
    init_buffer();
    init_rtp();      // open a UDP listen port and start a listener; decode into ring buffer
    fflush(stdout);
    init_output();              // resample and output from ring buffer

    char line[128];
    int in_line = 0;
    int n;
    double f;
    while (fgets(line + in_line, sizeof(line) - in_line, stdin)) {
        n = strlen(line);
        if (line[n-1] != '\n') {
            in_line = strlen(line) - 1;
            if (n == sizeof(line)-1)
                in_line = 0;
            continue;
        }
        if (sscanf(line, "vol: %lf\n", &f)) {
            assert(f<=0);
            if (debug)
                fprintf(stderr, "VOL: %lf\n", f);
            pthread_mutex_lock(&vol_mutex);
            volume = pow(10.0,0.05*f);
            fix_volume = 65536.0 * volume;
            pthread_mutex_unlock(&vol_mutex);
            continue;
        }
        if (!strcmp(line, "exit\n")) {
            exit(0);
        }
        if (!strcmp(line, "flush\n")) {
            pthread_mutex_lock(&ab_mutex);
            ab_resync();
            pthread_mutex_unlock(&ab_mutex);
            if (debug)
                fprintf(stderr, "FLUSH\n");
        }
    }
    fprintf(stderr, "bye!\n");
    fflush(stderr);

    return EXIT_SUCCESS;
}

#ifdef HAIRTUNES_STANDALONE
int main(int argc, char **argv) {
    char *hexaeskey = 0, *hexaesiv = 0;
    char *fmtpstr = 0;
    char *arg;
    assert(RAND_MAX >= 0x10000);    // XXX move this to compile time
    while ( (arg = *++argv) ) {
        if (!strcasecmp(arg, "iv")) {
            hexaesiv = *++argv;
            argc--;
        } else
        if (!strcasecmp(arg, "key")) {
            hexaeskey = *++argv;
            argc--;
        } else
        if (!strcasecmp(arg, "fmtp")) {
            fmtpstr = *++argv;
        } else
        if (!strcasecmp(arg, "cport")) {
            controlport = atoi(*++argv);
        } else
        if (!strcasecmp(arg, "tport")) {
            timingport = atoi(*++argv);
        } else
        if (!strcasecmp(arg, "dport")) {
            dataport = atoi(*++argv);
        } else
        if (!strcasecmp(arg, "host")) {
            rtphost = *++argv;
        } else
        if (!strcasecmp(arg, "pipe")) {
            if (libao_driver || libao_devicename || libao_deviceid ) {
                die("Option 'pipe' may not be combined with 'ao_driver', 'ao_devicename' or 'ao_deviceid'");
            }

            pipename = *++argv;
        } else
        if (!strcasecmp(arg, "ao_driver")) {
            if (pipename) {
                die("Option 'ao_driver' may not be combined with 'pipe'");
            }

            libao_driver = *++argv;
        } else
        if (!strcasecmp(arg, "ao_devicename")) {
            if (pipename || libao_deviceid ) {
                die("Option 'ao_devicename' may not be combined with 'pipe' or 'ao_deviceid'");
            }

            libao_devicename = *++argv;
        } else
        if (!strcasecmp(arg, "ao_deviceid")) {
            if (pipename || libao_devicename) {
                die("Option 'ao_deviceid' may not be combined with 'pipe' or 'ao_devicename'");
            }

            libao_deviceid = *++argv;
        }
#ifdef FANCY_RESAMPLING
        else
        if (!strcasecmp(arg, "resamp")) {
            fancy_resampling = atoi(*++argv);
        }
#endif
    }

    if (!hexaeskey || !hexaesiv)
        die("Must supply AES key and IV!");

    if (hex2bin(aesiv, hexaesiv))
        die("can't understand IV");
    if (hex2bin(aeskey, hexaeskey))
        die("can't understand key");
    return hairtunes_init(NULL, NULL, fmtpstr, controlport, timingport, dataport,
                    NULL, NULL, NULL, NULL, NULL, START_FILL);
}
#endif

static void init_buffer(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        audio_buffer[i].data = malloc(OUTFRAME_BYTES);
    ab_resync();
}

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

static void alac_decode(short *dest, char *buf, int len) {
    unsigned char packet[MAX_PACKET];
    assert(len<=MAX_PACKET);

    unsigned char iv[16];
    int aeslen = len & ~0xf;
    memcpy(iv, aesiv, sizeof(iv));
    AES_cbc_encrypt((unsigned char*)buf, packet, aeslen, &aes, iv, AES_DECRYPT);
    memcpy(packet+aeslen, buf+aeslen, len-aeslen);

    int outsize;

    decode_frame(decoder_info, packet, dest, &outsize);

    assert(outsize == FRAME_BYTES);
}

static void buffer_put_packet(seq_t seqno, char *data, int len) {
    abuf_t *abuf = 0;
    short buf_fill;

    pthread_mutex_lock(&ab_mutex);
    if (!ab_synced) {
        ab_write = seqno;
        ab_read = seqno-1;
        ab_synced = 1;
    }
    if (seqno == ab_write+1) {                  // expected packet
        abuf = audio_buffer + BUFIDX(seqno);
        ab_write = seqno;
    } else if (seq_order(ab_write, seqno)) {    // newer than expected
        rtp_request_resend(ab_write+1, seqno-1);
        abuf = audio_buffer + BUFIDX(seqno);
        ab_write = seqno;
    } else if (seq_order(ab_read, seqno)) {     // late but not yet played
        abuf = audio_buffer + BUFIDX(seqno);
    } else {    // too late.
        fprintf(stderr, "\nlate packet %04X (%04X:%04X)\n", seqno, ab_read, ab_write);
    }
    buf_fill = ab_write - ab_read;
    pthread_mutex_unlock(&ab_mutex);

    if (abuf) {
        alac_decode(abuf->data, data, len);
        abuf->ready = 1;
    }

    pthread_mutex_lock(&ab_mutex);
    if (ab_buffering && buf_fill >= buffer_start_fill) {
        ab_buffering = 0;
        pthread_cond_signal(&ab_buffer_ready);
    }
    pthread_mutex_unlock(&ab_mutex);
}

static int rtp_sockets[2];  // data, control
#ifdef AF_INET6
static struct sockaddr_in6 rtp_client;
#else
static struct sockaddr_in rtp_client;
#endif

static void *rtp_thread_func(void *arg) {
    socklen_t si_len = sizeof(rtp_client);
    char packet[MAX_PACKET];
    char *pktp;
    seq_t seqno;
    ssize_t plen;
    int sock = rtp_sockets[0], csock = rtp_sockets[1];
    int readsock;
    char type;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    FD_SET(csock, &fds);

    while (select(csock>sock ? csock+1 : sock+1, &fds, 0, 0, 0)!=-1) {
        if (FD_ISSET(sock, &fds)) {
            readsock = sock;
        } else {
            readsock = csock;
        }
        FD_SET(sock, &fds);
        FD_SET(csock, &fds);

        plen = recvfrom(readsock, packet, sizeof(packet), 0, (struct sockaddr*)&rtp_client, &si_len);
        if (plen < 0)
            continue;
        assert(plen<=MAX_PACKET);

        type = packet[1] & ~0x80;
        if (type == 0x60 || type == 0x56) {   // audio data / resend
            pktp = packet;
            if (type==0x56) {
                pktp += 4;
                plen -= 4;
            }
            seqno = ntohs(*(unsigned short *)(pktp+2));

            // adjust pointer and length
            pktp += 12;
            plen -= 12;

            // check if packet contains enough content to be reasonable
            if (plen >= 16) {
                buffer_put_packet(seqno, pktp, plen);
            } else {
                // resync?
                if (type == 0x56 && seqno == 0) {
                    fprintf(stderr, "Suspected resync request packet received. Initiating resync.\n");
                    pthread_mutex_lock(&ab_mutex);
                    ab_resync();
                    pthread_mutex_unlock(&ab_mutex);
                }
            }
        }
    }

    return 0;
}

static void rtp_request_resend(seq_t first, seq_t last) {
    if (seq_order(last, first))
        return;

    fprintf(stderr, "requesting resend on %d packets (port %d)\n", last-first+1, controlport);

    char req[8];    // *not* a standard RTCP NACK
    req[0] = 0x80;
    req[1] = 0x55|0x80;  // Apple 'resend'
    *(unsigned short *)(req+2) = htons(1);  // our seqnum
    *(unsigned short *)(req+4) = htons(first);  // missed seqnum
    *(unsigned short *)(req+6) = htons(last-first+1);  // count

#ifdef AF_INET6
    rtp_client.sin6_port = htons(controlport);
#else
    rtp_client.sin_port = htons(controlport);
#endif
    sendto(rtp_sockets[1], req, sizeof(req), 0, (struct sockaddr *)&rtp_client, sizeof(rtp_client));
}


static int init_rtp(void) {
    struct sockaddr_in si;
    int type = AF_INET;
    struct sockaddr* si_p = (struct sockaddr*)&si;
    socklen_t si_len = sizeof(si);
    unsigned short *sin_port = &si.sin_port;
    memset(&si, 0, sizeof(si));
#ifdef AF_INET6
    struct sockaddr_in6 si6;
    type = AF_INET6;
    si_p = (struct sockaddr*)&si6;
    si_len = sizeof(si6);
    sin_port = &si6.sin6_port;
    memset(&si6, 0, sizeof(si6));
#endif

    si.sin_family = AF_INET;
#ifdef SIN_LEN
    si.sin_len = sizeof(si);
#endif
    si.sin_addr.s_addr = htonl(INADDR_ANY);
#ifdef AF_INET6
    si6.sin6_family = AF_INET6;
    #ifdef SIN6_LEN
        si6.sin6_len = sizeof(si);
    #endif
    si6.sin6_addr = in6addr_any;
    si6.sin6_flowinfo = 0;
#endif

    int sock = -1, csock = -1;    // data and control (we treat the streams the same here)
    unsigned short port = 6000;
    while(1) {
        if(sock < 0)
            sock = socket(type, SOCK_DGRAM, IPPROTO_UDP);
#ifdef AF_INET6
        if(sock==-1 && type == AF_INET6) {
            // try fallback to IPv4
            type = AF_INET;
            si_p = (struct sockaddr*)&si;
            si_len = sizeof(si);
            sin_port = &si.sin_port;
            continue;
        }
#endif
        if (sock==-1)
            die("Can't create data socket!");

        if(csock < 0)
            csock = socket(type, SOCK_DGRAM, IPPROTO_UDP);
        if (csock==-1)
            die("Can't create control socket!");

        *sin_port = htons(port);
        int bind1 = bind(sock, si_p, si_len);
        *sin_port = htons(port + 1);
        int bind2 = bind(csock, si_p, si_len);

        if(bind1 != -1 && bind2 != -1) break;
        if(bind1 != -1) { close(sock); sock = -1; }
        if(bind2 != -1) { close(csock); csock = -1; }

        port += 3;
    }

    printf("port: %d\n", port); // let our handler know where we end up listening
    printf("cport: %d\n", port+1);

    pthread_t rtp_thread;
    rtp_sockets[0] = sock;
    rtp_sockets[1] = csock;
    pthread_create(&rtp_thread, NULL, rtp_thread_func, (void *)rtp_sockets);

    return port;
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

typedef struct {
    double hist[2];
    double a[2];
    double b[3];
} biquad_t;

static void biquad_init(biquad_t *bq, double a[], double b[]) {
    bq->hist[0] = bq->hist[1] = 0.0;
    memcpy(bq->a, a, 2*sizeof(double));
    memcpy(bq->b, b, 3*sizeof(double));
}

static void biquad_lpf(biquad_t *bq, double freq, double Q) {
    double w0 = 2.0 * M_PI * freq * frame_size / (double)sampling_rate;
    double alpha = sin(w0)/(2.0*Q);

    double a_0 = 1.0 + alpha;
    double b[3], a[2];
    b[0] = (1.0-cos(w0))/(2.0*a_0);
    b[1] = (1.0-cos(w0))/a_0;
    b[2] = b[0];
    a[0] = -2.0*cos(w0)/a_0;
    a[1] = (1-alpha)/a_0;

    biquad_init(bq, a, b);
}

static double biquad_filt(biquad_t *bq, double in) {
    double w = in - bq->a[0]*bq->hist[0] - bq->a[1]*bq->hist[1];
    double out = bq->b[1]*bq->hist[0] + bq->b[2]*bq->hist[1] + bq->b[0]*w;
    bq->hist[1] = bq->hist[0];
    bq->hist[0] = w;

    return out;
}

static double bf_playback_rate = 1.0;

static double bf_est_drift = 0.0;   // local clock is slower by
static biquad_t bf_drift_lpf;
static double bf_est_err = 0.0, bf_last_err;
static biquad_t bf_err_lpf, bf_err_deriv_lpf;
static double desired_fill;
static int fill_count;

static void bf_est_reset(short fill) {
    biquad_lpf(&bf_drift_lpf, 1.0/180.0, 0.3);
    biquad_lpf(&bf_err_lpf, 1.0/10.0, 0.25);
    biquad_lpf(&bf_err_deriv_lpf, 1.0/2.0, 0.2);
    fill_count = 0;
    bf_playback_rate = 1.0;
    bf_est_err = bf_last_err = 0;
    desired_fill = fill_count = 0;
}

static void bf_est_update(short fill) {
    if (fill_count < 1000) {
        desired_fill += (double)fill/1000.0;
        fill_count++;
        return;
    }

#define CONTROL_A   (1e-4)
#define CONTROL_B   (1e-1)

    double buf_delta = fill - desired_fill;
    bf_est_err = biquad_filt(&bf_err_lpf, buf_delta);
    double err_deriv = biquad_filt(&bf_err_deriv_lpf, bf_est_err - bf_last_err);
    double adj_error = CONTROL_A * bf_est_err;

    bf_est_drift = biquad_filt(&bf_drift_lpf, CONTROL_B*(adj_error + err_deriv) + bf_est_drift);

    if (debug)
        fprintf(stderr, "bf %d err %f drift %f desiring %f ed %f estd %f\n",
                fill, bf_est_err, bf_est_drift, desired_fill, err_deriv, err_deriv + adj_error);
    bf_playback_rate = 1.0 + adj_error + bf_est_drift;

    bf_last_err = bf_est_err;
}

// get the next frame, when available. return 0 if underrun/stream reset.
static short *buffer_get_frame(void) {
    short buf_fill;
    seq_t read;
    abuf_t *abuf = 0;
    unsigned short next;
    int i;

    pthread_mutex_lock(&ab_mutex);

    buf_fill = ab_write - ab_read;
    if (buf_fill < 1 || !ab_synced || ab_buffering) {    // init or underrun. stop and wait
        if (ab_synced)
            fprintf(stderr, "\nunderrun.\n");

        ab_buffering = 1;
        pthread_cond_wait(&ab_buffer_ready, &ab_mutex);
        ab_read++;
        buf_fill = ab_write - ab_read;
        bf_est_reset(buf_fill);
        pthread_mutex_unlock(&ab_mutex);

        return 0;
    }
    if (buf_fill >= BUFFER_FRAMES) {   // overrunning! uh-oh. restart at a sane distance
        fprintf(stderr, "\noverrun.\n");
        ab_read = ab_write - buffer_start_fill;
    }
    read = ab_read;
    ab_read++;
    buf_fill = ab_write - ab_read;
    bf_est_update(buf_fill);

    // check if t+16, t+32, t+64, t+128, ... (START_FILL / 2)
    // packets have arrived... last-chance resend
    if (!ab_buffering) {
        for (i = 16; i < (START_FILL / 2); i = (i * 2)) {
            next = ab_read + i;
            abuf = audio_buffer + BUFIDX(next);
            if (!abuf->ready) {
                rtp_request_resend(next, next);
            }
        }
    }

    abuf_t *curframe = audio_buffer + BUFIDX(read);
    if (!curframe->ready) {
        fprintf(stderr, "\nmissing frame.\n");
        memset(curframe->data, 0, FRAME_BYTES);
    }
    curframe->ready = 0;
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
            if (debug)
                fprintf(stderr, "+++++++++\n");
            // interpolate one sample
            *outptr++ = dithered_vol(((long)inptr[-2] + (long)inptr[0]) >> 1);
            *outptr++ = dithered_vol(((long)inptr[-1] + (long)inptr[1]) >> 1);
        } else if (stuff==-1) {
            if (debug)
                fprintf(stderr, "---------\n");
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

static void *audio_thread_func(void *arg) {
    ao_device* dev = arg;
    int play_samples;

    signed short buf_fill __attribute__((unused));
    signed short *inbuf, *outbuf, *silence;
    outbuf = malloc(OUTFRAME_BYTES);
    silence = malloc(OUTFRAME_BYTES);
    int i;

    for (i=0; i<OUTFRAME_BYTES/2; i++) {
        silence[i] = 0;
    }

#ifdef FANCY_RESAMPLING
    float *frame, *outframe;
    SRC_DATA srcdat;
    if (fancy_resampling) {
        frame = malloc(frame_size*2*sizeof(float));
        outframe = malloc(2*frame_size*2*sizeof(float));

        srcdat.data_in = frame;
        srcdat.data_out = outframe;
        srcdat.input_frames = FRAME_BYTES;
        srcdat.output_frames = 2*FRAME_BYTES;
        srcdat.src_ratio = 1.0;
        srcdat.end_of_input = 0;
    }
#endif

    while (1) {
       if (ab_buffering) {
           inbuf = silence;
       } else {
            do {
                inbuf = buffer_get_frame();
            } while (!inbuf);
       }

#ifdef FANCY_RESAMPLING
        if (fancy_resampling) {
            int i;
            pthread_mutex_lock(&vol_mutex);
            for (i=0; i<2*FRAME_BYTES; i++) {
                frame[i] = (float)inbuf[i] / 32768.0;
                frame[i] *= volume;
            }
            pthread_mutex_unlock(&vol_mutex);
            srcdat.src_ratio = bf_playback_rate;
            src_process(src, &srcdat);
            assert(srcdat.input_frames_used == FRAME_BYTES);
            src_float_to_short_array(outframe, outbuf, FRAME_BYTES*2);
            play_samples = srcdat.output_frames_gen;
        } else
#endif

            play_samples = stuff_buffer(bf_playback_rate, inbuf, outbuf);

        if (pipename) {
            if (pipe_handle == -1) {
                // attempt to open pipe - block if there are no readers
                pipe_handle = open(pipename, O_WRONLY);
            }

            // only write if pipe open (there's a reader)
            if (pipe_handle != -1) {
                 if (write(pipe_handle, outbuf, play_samples*4) == -1) {
                    // write failed - do anything here?
                    // SIGPIPE is handled elsewhere...
                 }
            }
        } else {
            ao_play(dev, (char *)outbuf, play_samples*4);
        }
    }

    return 0;
}

#define NUM_CHANNELS 2

static void handle_broken_fifo() {
    close(pipe_handle);
    pipe_handle = -1;
}

static void init_pipe(const char* pipe) {
    // make the FIFO and catch the broken pipe signal
    mknod(pipe, S_IFIFO | 0644, 0);
    signal(SIGPIPE, handle_broken_fifo);
}

static void* init_ao(void) {
    ao_initialize();

    int driver;
    if (libao_driver) {
        // if a libao driver is specified on the command line, use that
        driver = ao_driver_id(libao_driver);
        if (driver == -1) {
            die("Could not find requested ao driver");
        }
    } else {
        // otherwise choose the default
        driver = ao_default_driver_id();
    }

    ao_sample_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.bits = 16;
    fmt.rate = sampling_rate;
    fmt.channels = NUM_CHANNELS;
    fmt.byte_format = AO_FMT_NATIVE;

    ao_option *ao_opts = NULL;
    if(libao_deviceid) {
        ao_append_option(&ao_opts, "id", libao_deviceid);
    } else if(libao_devicename){
        ao_append_option(&ao_opts, "dev", libao_devicename);
        // Old libao versions (for example, 0.8.8) only support
        // "dsp" instead of "dev".
        ao_append_option(&ao_opts, "dsp", libao_devicename);
    }

    ao_device *dev = ao_open_live(driver, &fmt, ao_opts);
    if (dev == NULL) {
        die("Could not open ao device");
    }

    return dev;
}

static int init_output(void) {
    void* arg = 0;

    if (pipename) {
        init_pipe(pipename);
    } else {
        arg = init_ao();
    }

#ifdef FANCY_RESAMPLING
    int err;
    if (fancy_resampling)
        src = src_new(SRC_SINC_MEDIUM_QUALITY, 2, &err);
    else
        src = 0;
#endif

    pthread_t audio_thread;
    pthread_create(&audio_thread, NULL, audio_thread_func, arg);

    return 0;
}

