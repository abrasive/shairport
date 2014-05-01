/*
 * libalsa output driver. This file is part of Shairport.
 * Copyright (c) Muffinman, Skaman 2013
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

#define ALSA_PCM_NEW_HW_PARAMS_API

#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <alsa/asoundlib.h>
#include "common.h"
#include "audio.h"

static void help(void);
static int init(int argc, char **argv);
static void deinit(void);
static void start(int sample_rate);
static void play(short buf[], int samples);
static void stop(void);
static void flush(void);
static uint32_t delay(void);
static void volume(double vol);
static int has_mute=0;
static int has_db_vol=0;

audio_output audio_alsa = {
    .name = "alsa",
    .help = &help,
    .init = &init,
    .deinit = &deinit,
    .start = &start,
    .stop = &stop,
    .flush = &flush,
    .delay = &delay,
    .play = &play,
    .volume = NULL
};

static snd_pcm_t *alsa_handle = NULL;
static snd_pcm_hw_params_t *alsa_params = NULL;

static snd_mixer_t *alsa_mix_handle = NULL;
static snd_mixer_elem_t *alsa_mix_elem = NULL;
static snd_mixer_selem_id_t *alsa_mix_sid = NULL;
static long alsa_mix_minv, alsa_mix_range;

static char *alsa_out_dev = "default";
static char *alsa_mix_dev = NULL;
static char *alsa_mix_ctrl = "Master";
static int alsa_mix_index = 0;

static int play_number;
static int64_t accumulated_delay,accumulated_da_delay;

static void help(void) {
    printf("    -d output-device    set the output device [default*|...]\n"
           "    -t mixer-type       set the mixer type [software*|hardware]\n"
           "    -m mixer-device     set the mixer device ['output-device'*|...]\n"
           "    -c mixer-control    set the mixer control [Master*|...]\n"
           "    -i mixer-index      set the mixer index [0*|...]\n"
           "    *) default option\n"
          );
}

static int init(int argc, char **argv) {
    int hardware_mixer = 0;

    optind = 1; // optind=0 is equivalent to optind=1 plus special behaviour
    argv--;     // so we shift the arguments to satisfy getopt()
    argc++;
    // some platforms apparently require optreset = 1; - which?
    int opt;
    while ((opt = getopt(argc, argv, "d:t:m:c:i:")) > 0) {
        switch (opt) {
            case 'd':
                alsa_out_dev = optarg;
                break;
            case 't':
                if (strcmp(optarg, "hardware") == 0)
                    hardware_mixer = 1;
                break;
            case 'm':
                alsa_mix_dev = optarg;
                break;
            case 'c':
                alsa_mix_ctrl = optarg;
                break;
            case 'i':
                alsa_mix_index = strtol(optarg, NULL, 10);
                break;
            default:
                help();
                die("Invalid audio option -%c specified", opt);
        }
    }

    if (optind < argc)
        die("Invalid audio argument: %s", argv[optind]);

    if (!hardware_mixer)
        return 0;

    if (alsa_mix_dev == NULL)
        alsa_mix_dev = alsa_out_dev;
    

    int ret = 0;
    long alsa_mix_maxv;

    snd_mixer_selem_id_alloca(&alsa_mix_sid);
    snd_mixer_selem_id_set_index(alsa_mix_sid, alsa_mix_index);
    snd_mixer_selem_id_set_name(alsa_mix_sid, alsa_mix_ctrl);

    if ((snd_mixer_open(&alsa_mix_handle, 0)) < 0)
        die ("Failed to open mixer");
    if ((snd_mixer_attach(alsa_mix_handle, alsa_mix_dev)) < 0)
        die ("Failed to attach mixer");
    if ((snd_mixer_selem_register(alsa_mix_handle, NULL, NULL)) < 0)
        die ("Failed to register mixer element");

    ret = snd_mixer_load(alsa_mix_handle);
    if (ret < 0)
        die ("Failed to load mixer element");
    alsa_mix_elem = snd_mixer_find_selem(alsa_mix_handle, alsa_mix_sid);
    if (!alsa_mix_elem)
        die ("Failed to find mixer element");
    if (snd_mixer_selem_get_playback_dB_range (alsa_mix_elem, &alsa_mix_minv, &alsa_mix_maxv)==0) {
       has_db_vol=1;
       debug(1,"Hardware mixer has dB volume from %ld to %ld.\n",alsa_mix_minv,alsa_mix_maxv);
       audio_alsa.volume = &volume;
       alsa_mix_range = alsa_mix_maxv - alsa_mix_minv;
    } else {
       debug(1,"Hardware mixer does not have dB volume -- linear only??\n");
    }
    
    if (snd_mixer_selem_has_playback_switch(alsa_mix_elem)) {
        has_mute=1;
        debug(1,"Has mute ability.\n");
    }


    return 0;
}

static void deinit(void) {
    stop();
    if (alsa_mix_handle) {
        snd_mixer_close(alsa_mix_handle);
    }
}

static void start(int sample_rate) {
  if (sample_rate != 44100)
    die("Unexpected sample rate!");
  unsigned int mysamplerate=sample_rate;

  int ret, dir = 0;
  snd_pcm_uframes_t frames = 441*10;
  snd_pcm_uframes_t buffer_size = frames*4;
  ret = snd_pcm_open(&alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
  if (ret < 0)
    die("Alsa initialization failed: unable to open pcm device: %s\n", snd_strerror(ret));

  snd_pcm_hw_params_alloca(&alsa_params);
  snd_pcm_hw_params_any(alsa_handle, alsa_params);
  snd_pcm_hw_params_set_access(alsa_handle, alsa_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(alsa_handle, alsa_params, SND_PCM_FORMAT_S16);
  snd_pcm_hw_params_set_channels(alsa_handle, alsa_params, 2);
  snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params, (unsigned int *)&mysamplerate, &dir);
  // snd_pcm_hw_params_set_period_size_near(alsa_handle, alsa_params, &frames, &dir);
  // snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params, &buffer_size);
  ret = snd_pcm_hw_params(alsa_handle, alsa_params);
  if (ret < 0)
    die("unable to set hw parameters: %s\n", snd_strerror(ret));
  if (mysamplerate!=sample_rate) {
    die("Can't set the D/A converter to %d -- set to %d instead./n",sample_rate,mysamplerate);
  }
}

static uint32_t delay() {
  snd_pcm_sframes_t current_delay = 0;
  int derr;
  if ((snd_pcm_state(alsa_handle)==SND_PCM_STATE_PREPARED) || (snd_pcm_state(alsa_handle)==SND_PCM_STATE_RUNNING)) {
    if (derr = snd_pcm_delay(alsa_handle,&current_delay)) {
      if (derr < 0) {
        derr = snd_pcm_recover(alsa_handle, derr, 0);
        debug(1,"Error in delay(): %s\n", snd_strerror(derr));
      }
      current_delay=-1;
    }
  } else {
    debug(1,"Error -- ALSA device not in correct state for delay.\n");
    if (derr = snd_pcm_prepare(alsa_handle)) {
      derr = snd_pcm_recover(alsa_handle, derr, 0);
      debug(1,"Error preparing after delay error: %s\n", snd_strerror(derr));
    }

    current_delay = -1;
  }
  return current_delay;
}

static void play(short buf[], int samples) {
  snd_pcm_sframes_t current_delay = 0;
  int err;
  if ((snd_pcm_state(alsa_handle)==SND_PCM_STATE_PREPARED) || (snd_pcm_state(alsa_handle)==SND_PCM_STATE_RUNNING)) {
    err = snd_pcm_writei(alsa_handle, (char*)buf, samples);
    if (err < 0) {
      err = snd_pcm_recover(alsa_handle, err, 0);
      debug(1,"Error writing in play(): %s\n", snd_strerror(err));
    }
  } else {
    debug(1,"Error -- ALSA device not in correct state for play.\n");
    if (err = snd_pcm_prepare(alsa_handle)) {
      err = snd_pcm_recover(alsa_handle, err, 0);
      debug(1,"Error preparing after play error: %s\n", snd_strerror(err));
    }
  }
}

static void flush(void) {
  debug(1,"ALSA flush called.\n");
    int derr;
    if (alsa_handle) {
//        debug(1,"Dropping frames for flush...\n");
        if (derr = snd_pcm_drop(alsa_handle))
          debug(1,"Error dropping frames: %s\n", snd_strerror(derr));
//        debug(1,"Dropped frames ok. State is %d.\n",snd_pcm_state(alsa_handle));
        if (derr = snd_pcm_prepare(alsa_handle))
          debug(1,"Error preparing after flush: %s\n", snd_strerror(derr));
//        debug(1,"Frames successfully dropped.\n");
    }
}

static void stop(void) {
  debug(1,"ALSA stop called.\n");
    if (alsa_handle) {
        snd_pcm_drain(alsa_handle);
        snd_pcm_close(alsa_handle);
        alsa_handle = NULL;
    }
}

static void volume(double vol) {
  if (has_db_vol) {
    // guess that if the range is 60db or greater, that it's amplitude rather than power, do multiply by 2, i.e. 200
    uint32_t multiplier = 200;
    if (alsa_mix_range<6000)
      multiplier=alsa_mix_range/30;
    // otherwise multiply by <range in db>/30
    if(snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, vol*multiplier+alsa_mix_range+alsa_mix_minv, -1) != 0)
      die ("Failed to set playback dB volume");
  }
  if (has_mute)
    snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, (vol!=-144.0));
}
