#ifndef _PLAYER_H
#define _PLAYER_H

#include "audio.h"

typedef struct {
    uint8_t aesiv[16], aeskey[16];
    int32_t fmtp[12];
} stream_cfg;

typedef uint16_t seq_t;

// wrapped number between two seq_t.
int32_t seq_diff(seq_t a, seq_t b);

int player_play(stream_cfg *cfg);
void player_stop(void);

void player_volume(double f);
void player_flush(uint32_t timestamp);
void player_resync(void);

void player_put_packet(seq_t seqno,uint32_t timestamp, uint8_t *data, int len);

#endif //_PLAYER_H
