#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/signal.h>
#include <ao/ao.h>
#include "common.h"

#define NUM_CHANNELS 2

static char *libao_driver = NULL;
static char *libao_devicename = NULL;
static char *libao_deviceid = NULL; // ao_options expects "char*"

void audio_set_driver(char* driver) {
    libao_driver = driver;
}

void audio_set_device_name(char* device_name) {
    libao_devicename = device_name;
}

void audio_set_device_id(char* device_id) {
    libao_deviceid = device_id;
}

char* audio_get_driver(void)
{
    return libao_driver;
}

char* audio_get_device_name(void)
{
    return libao_devicename;
}

char* audio_get_device_id(void)
{
    return libao_deviceid;
}

void audio_play(char* outbuf, int samples, void* priv_data)
{
    ao_device* dev = priv_data;
    ao_play(dev, outbuf, samples*4);
}
void* audio_init(int sampling_rate)
{
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

void audio_deinit(void)
{
    // deinitialization not required with libao?
}
