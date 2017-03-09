#ifndef _PLAYER_H
#define _PLAYER_H

#include "config.h"

#ifdef HAVE_LIBMBEDTLS
#include <mbedtls/aes.h>
#include <mbedtls/havege.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/aes.h>
#endif

#include "audio.h"

typedef uint16_t seq_t;

typedef struct audio_buffer_entry { // decoded audio packets
  int ready;
  int64_t timestamp;
  seq_t sequence_number;
  signed short *data;
  int length; // the length of the decoded data
} abuf_t;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
#define BUFFER_FRAMES 512

typedef struct {
  int encrypted;
  uint8_t aesiv[16], aeskey[16];
  int32_t fmtp[12];
} stream_cfg;

typedef struct {
  int fd;
  int authorized; // set if a password is required and has been supplied
  stream_cfg stream;
  SOCKADDR remote, local;
  int stop;
  int running;
  pthread_t thread;
  pthread_t player_thread;

  abuf_t audio_buffer[BUFFER_FRAMES];
  int max_frames_per_packet,input_num_channels,input_bit_depth,input_rate;
  uint32_t please_stop;
  uint64_t packet_count;
#ifdef HAVE_LIBMBEDTLS
  mbedtls_aes_context dctx;
#endif

#ifdef HAVE_LIBPOLARSSL
  aes_context dctx;
#endif

#ifdef HAVE_LIBSSL
  AES_KEY aes;
#endif
} rtsp_conn_info;

// wrapped number between two seq_t.
int32_t seq_diff(seq_t a, seq_t b);

int player_play(pthread_t *thread, rtsp_conn_info* conn);
void player_stop(pthread_t *thread, rtsp_conn_info* conn);

void player_volume(double f);
void player_flush(int64_t timestamp);
void player_put_packet(seq_t seqno, int64_t timestamp, uint8_t *data, int len, rtsp_conn_info* conn);

int64_t monotonic_timestamp(uint32_t timestamp); // add an epoch to the timestamp. The monotonic
                                                 // timestamp guaranteed to start between 2^32 2^33
                                                 // frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

uint64_t monotonic_seqno(uint16_t seq_no); // add an epoch to the seq_no. Uses the accompanying
                                           // timstamp to detemine the correct epoch

#endif //_PLAYER_H
