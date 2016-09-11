#ifndef _PLAYER_H
#define _PLAYER_H

#include "audio.h"

typedef struct {
  int encrypted;
  uint8_t aesiv[16], aeskey[16];
  int32_t fmtp[12];
} stream_cfg;

typedef uint16_t seq_t;

// wrapped number between two seq_t.
int32_t seq_diff(seq_t a, seq_t b);

int player_play(stream_cfg *cfg, pthread_t *thread);
void player_stop(pthread_t *thread);

void player_volume(double f);
void player_flush(int64_t timestamp);

void player_put_packet(seq_t seqno, int64_t timestamp, uint8_t *data, int len);

int64_t monotonic_timestamp(uint32_t timestamp); // add an epoch to the timestamp. The monotonic
                                                 // timestamp guaranteed to start between 2^32 2^33
                                                 // frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

uint64_t monotonic_seqno(uint16_t seq_no); // add an epoch to the seq_no. Uses the accompanying
                                           // timstamp to detemine the correct epoch

#endif //_PLAYER_H
