#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <ao/ao.h>
#include "common.h"
#include "audio.h"

ao_device *dev = NULL;

static int init(int argc, char **argv) {
    printf("ao: init\n");
    ao_initialize();
    ao_sample_format fmt;
    memset(&fmt, 0, sizeof(fmt));

    fmt.bits = 16;
    fmt.rate = 44100;
    fmt.channels = 2;
    fmt.byte_format = AO_FMT_NATIVE;

    int driver = ao_default_driver_id();

    dev = ao_open_live(driver, &fmt, NULL);

    return dev ? 0 : 1;
}

static void deinit(void) {
    if (dev)
        ao_close(dev);
}

static void start(int sample_rate) {
    printf("ao: start\n");
    if (sample_rate != 44100)
        die("unexpceted sample rate!\n");
}

static void play(short buf[], int samples) {
    ao_play(dev, (char*)buf, samples*4);
}

static void stop(void) {
}

audio_ops audio_ao = {
    .init = &init,
    .deinit = &deinit,
    .start = &start,
    .stop = &stop,
    .play = &play,
    .volume = NULL
};
