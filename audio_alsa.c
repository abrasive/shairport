#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>
#include "common.h"

#define NUM_CHANNELS 2

static snd_pcm_t *alsa_handle = NULL;
static snd_pcm_hw_params_t *alsa_params = NULL;

void audio_set_driver(char* driver) {
    fprintf(stderr, "audio_set_driver: not supported with alsa");
}

void audio_set_device_name(char* device_name) {
    fprintf(stderr, "audio_set_device_name: not supported with alsa");
}

void audio_set_device_id(char* device_id) {
    fprintf(stderr, "audio_set_device_id: not supported with alsa");
}

char* audio_get_driver(void)
{
    return NULL;
}

char* audio_get_device_name(void)
{
    return NULL;
}

char* audio_get_device_id(void)
{
    return NULL;
}

void audio_play(char* outbuf, int samples, void* priv_data)
{
    int err = snd_pcm_writei(alsa_handle, outbuf, samples);
    if (err < 0)
        err = snd_pcm_recover(alsa_handle, err, 0);
    if (err < 0)
        fprintf(stderr, "snd_pcm_writei failed: %s\n", snd_strerror(err));
}

void* audio_init(int sampling_rate)
{
    int rc, dir = 0;
    snd_pcm_uframes_t frames = 32;
    rc = snd_pcm_open(&alsa_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        fprintf(stderr, "unable to open pcm device: %s\n", snd_strerror(rc));
        die("alsa initialization failed");
    }
    snd_pcm_hw_params_alloca(&alsa_params);
    snd_pcm_hw_params_any(alsa_handle, alsa_params);
    snd_pcm_hw_params_set_access(alsa_handle, alsa_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(alsa_handle, alsa_params, SND_PCM_FORMAT_S16);
    snd_pcm_hw_params_set_channels(alsa_handle, alsa_params, NUM_CHANNELS);
    snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params, (unsigned int *)&sampling_rate, &dir);
    snd_pcm_hw_params_set_period_size_near(alsa_handle, alsa_params, &frames, &dir);
    rc = snd_pcm_hw_params(alsa_handle, alsa_params);
    if (rc < 0) {
        fprintf(stderr, "unable to set hw parameters: %s\n", snd_strerror(rc));
        die("alsa initialization failed");
    }
    return NULL;
}

void audio_deinit(void)
{
    if (alsa_handle) {
        snd_pcm_drain(alsa_handle);
        snd_pcm_close(alsa_handle);
    }
}
