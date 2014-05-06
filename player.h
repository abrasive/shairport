#ifndef _PLAYER_H
#define _PLAYER_H

#include <limits.h>
#include "audio.h"
#include "metadata.h"

#define NOSYNC 0
#define NTPSYNC 1
#define RTPSYNC 2
#define E_NTPSYNC 3

typedef struct {
    long long ntp_tsp;
    unsigned long rtp_tsp;
    int sync_mode;
} sync_cfg;

typedef struct {
    uint8_t aesiv[16], aeskey[16];
    int32_t fmtp[12];
} stream_cfg;

typedef uint16_t seq_t;

// wrapped number between two seq_t.
static inline uint16_t seq_diff(seq_t a, seq_t b) {
    int16_t diff = b - a;
    return diff;
}

int player_play(stream_cfg *cfg);
void player_stop(void);

void player_volume(double f);
void player_metadata();
void player_cover_image(char *buf, int len, char *ext);
void player_cover_clear();
unsigned long player_flush(int seqno, unsigned long rtp_tsp);
void player_resync(void);

void player_put_packet(seq_t seqno, sync_cfg sync_tag, uint8_t *data, int len);

#endif //_PLAYER_H
