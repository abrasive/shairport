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


typedef uint16_t seq_t;

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
