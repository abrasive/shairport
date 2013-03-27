#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include "audio.h"

int Fs;
long long starttime, samples_played;

static int dummy_init(int argc, char **argv) {
    return 0;
}

static void dummy_deinit(void) {
}

static void dummy_start(int sample_rate) {
    Fs = sample_rate;
    starttime = 0;
    samples_played = 0;
    printf("dummy audio output started at Fs=%d Hz\n", sample_rate);
}

static void dummy_play(short buf[], int samples) {
    struct timeval tv;

    // this is all a bit expensive but it's long-term stable.
    gettimeofday(&tv, NULL);

    long long nowtime = tv.tv_usec + 1e6*tv.tv_sec; 

    if (!starttime)
        starttime = nowtime;
    
    samples_played += samples;

    long long finishtime = starttime + samples_played * 1e6 / Fs;

    usleep(finishtime - nowtime);
}

static void dummy_stop(void) {
    printf("dummy audio stopped\n");
}

audio_ops audio_dummy = {
    .init = &dummy_init,
    .deinit = &dummy_deinit,
    .start = &dummy_start,
    .stop = &dummy_stop,
    .play = &dummy_play,
    .volume = NULL
};
