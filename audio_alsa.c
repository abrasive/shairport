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
#include <math.h>
#include <pthread.h>
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
static void linear_volume(double vol);
static void parameters(audio_parameters *info);
static void mute(int do_mute);
static double set_volume;

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
    .mute = NULL,      // to be set later on...
    .volume = NULL,    // to be set later on...
    .parameters = NULL // to be set later on...
};

static pthread_mutex_t alsa_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int desired_sample_rate;

static snd_pcm_t *alsa_handle = NULL;
static snd_pcm_hw_params_t *alsa_params = NULL;

static snd_mixer_t *alsa_mix_handle = NULL;
static snd_mixer_elem_t *alsa_mix_elem = NULL;
static snd_mixer_selem_id_t *alsa_mix_sid = NULL;
static long alsa_mix_minv, alsa_mix_maxv;
static long alsa_mix_mindb, alsa_mix_maxdb;

static char *alsa_out_dev = "default";
static char *alsa_mix_dev = NULL;
static char *alsa_mix_ctrl = "Master";
static int alsa_mix_index = 0;
static int hardware_mixer = 0;


static int play_number;
static int64_t accumulated_delay, accumulated_da_delay;

static void help(void) {
  printf("    -d output-device    set the output device [default*|...]\n"
         "    -m mixer-device     set the mixer device ['output-device'*|...]\n"
         "    -c mixer-control    set the mixer control [Master*|...]\n"
         "    -i mixer-index      set the mixer index [0*|...]\n"
         "    *) default option\n");
}

int open_mixer() {
  if (hardware_mixer) {
    debug(2, "Open Mixer");
    int ret = 0;
    snd_mixer_selem_id_alloca(&alsa_mix_sid);
    snd_mixer_selem_id_set_index(alsa_mix_sid, alsa_mix_index);
    snd_mixer_selem_id_set_name(alsa_mix_sid, alsa_mix_ctrl);

    if ((snd_mixer_open(&alsa_mix_handle, 0)) < 0)
      die("Failed to open mixer");
    debug(3, "Mixer device name is \"%s\".", alsa_mix_dev);
    if ((snd_mixer_attach(alsa_mix_handle, alsa_mix_dev)) < 0)
      die("Failed to attach mixer");
    if ((snd_mixer_selem_register(alsa_mix_handle, NULL, NULL)) < 0)
      die("Failed to register mixer element");

    ret = snd_mixer_load(alsa_mix_handle);
    if (ret < 0)
      die("Failed to load mixer element");
    debug(3, "Mixer Control name is \"%s\".", alsa_mix_ctrl);
    alsa_mix_elem = snd_mixer_find_selem(alsa_mix_handle, alsa_mix_sid);
    if (!alsa_mix_elem)
      die("Failed to find mixer element");
  }
}

static int init(int argc, char **argv) {
  pthread_mutex_lock(&alsa_mutex);
  debug(2,"audio_alsa init called.");
  const char *str;
  int value;

  config.audio_backend_latency_offset = 0; // this is the default for ALSA
  config.audio_backend_buffer_desired_length =
      6615; // default for alsa with a software mixer

  // get settings from settings file first, allow them to be overridden by
  // command line options

  if (config.cfg != NULL) {
    /* Get the desired buffer size setting. */
    if (config_lookup_int(config.cfg,
                          "alsa.audio_backend_buffer_desired_length", &value)) {
      if ((value < 0) || (value > 66150))
        die("Invalid alsa audio backend buffer desired length \"%d\". It "
            "should be between 0 and "
            "66150, default is 6615",
            value);
      else {
        config.audio_backend_buffer_desired_length = value;
      }
    }

    /* Get the latency offset. */
    if (config_lookup_int(config.cfg, "alsa.audio_backend_latency_offset",
                          &value)) {
      if ((value < -66150) || (value > 66150))
        die("Invalid alsa audio backend buffer latency offset \"%d\". It "
            "should be between -66150 and +66150, default is 0",
            value);
      else
        config.audio_backend_latency_offset = value;
    }

    /* Get the Output Device Name. */
    if (config_lookup_string(config.cfg, "alsa.output_device", &str)) {
      alsa_out_dev = (char *)str;
    }

    /* Get the Mixer Type setting. */

    if (config_lookup_string(config.cfg, "alsa.mixer_type", &str)) {
      inform("The alsa mixer_type setting is deprecated and has been ignored. "
             "FYI, using the \"mixer_control_name\" setting automatically "
             "chooses a hardware mixer.");
    }

    /* Get the Mixer Device Name. */
    if (config_lookup_string(config.cfg, "alsa.mixer_device", &str)) {
      alsa_mix_dev = (char *)str;
    }

    /* Get the Mixer Control Name. */
    if (config_lookup_string(config.cfg, "alsa.mixer_control_name", &str)) {
      alsa_mix_ctrl = (char *)str;
      hardware_mixer = 1;
    }
  }

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
      inform("The alsa backend -t option is deprecated and has been ignored. "
             "FYI, using the -c option automatically chooses a hardware "
             "mixer.");
      break;

    case 'm':
      alsa_mix_dev = optarg;
      break;
    case 'c':
      alsa_mix_ctrl = optarg;
      hardware_mixer = 1;
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

  debug(1, "Output device name is \"%s\".", alsa_out_dev);

  if (hardware_mixer) {
  
    if (alsa_mix_dev == NULL)
      alsa_mix_dev = alsa_out_dev;

    // Open mixer

    open_mixer();

    if (snd_mixer_selem_get_playback_volume_range(alsa_mix_elem, &alsa_mix_minv,
                                                  &alsa_mix_maxv) < 0)
      debug(1, "Can't read mixer's [linear] min and max volumes.");
    else {
      if (snd_mixer_selem_get_playback_dB_range(alsa_mix_elem, &alsa_mix_mindb,
                                                &alsa_mix_maxdb) == 0) {

        audio_alsa.volume =
            &volume; // insert the volume function now we know it can do dB stuff
        audio_alsa.parameters = &parameters; // likewise the parameters stuff
        if (alsa_mix_mindb == SND_CTL_TLV_DB_GAIN_MUTE) {
          // Raspberry Pi does this
          debug(1, "Lowest dB value is a mute.");
          if (snd_mixer_selem_ask_playback_vol_dB(
                  alsa_mix_elem, alsa_mix_minv + 1, &alsa_mix_mindb) == 0)
            debug(1, "Can't get dB value corresponding to a \"volume\" of 1.");
        }
        debug(1, "Hardware mixer has dB volume from %f to %f.",
              (1.0 * alsa_mix_mindb) / 100.0, (1.0 * alsa_mix_maxdb) / 100.0);
      } else {
        // use the linear scale and do the db conversion ourselves
        debug(1, "note: the hardware mixer specified -- \"%s\" -- does not have "
                 "a dB volume scale, so it can't be used.",
              alsa_mix_ctrl);
        /*
        debug(1, "Min and max volumes are %d and
        %d.",alsa_mix_minv,alsa_mix_maxv);
        alsa_mix_maxdb = 0;
        if ((alsa_mix_maxv!=0) && (alsa_mix_minv!=0))
          alsa_mix_mindb =
        -20*100*(log10(alsa_mix_maxv*1.0)-log10(alsa_mix_minv*1.0));
        else if (alsa_mix_maxv!=0)
          alsa_mix_mindb = -20*100*log10(alsa_mix_maxv*1.0);
        audio_alsa.volume = &linear_volume; // insert the linear volume function
        audio_alsa.parameters = &parameters; // likewise the parameters stuff
        debug(1,"Max and min dB calculated are %d and
        %d.",alsa_mix_maxdb,alsa_mix_mindb);
        */
      }
    }
    if (snd_mixer_selem_has_playback_switch(alsa_mix_elem)) {
      audio_alsa.mute =
          &mute; // insert the mute function now we know it can do muting stuff
      debug(1, "Has mute ability.");
    }

    snd_mixer_close(alsa_mix_handle);
  }
  alsa_mix_handle = NULL;
  pthread_mutex_unlock(&alsa_mutex);
  return 0;
}

static void deinit(void) {
  debug(2,"audio_alsa deinit called.");
  stop();
  if (hardware_mixer && alsa_mix_handle) {
    snd_mixer_close(alsa_mix_handle);
  }
}

int open_alsa_device(void) {
  
  const snd_pcm_uframes_t minimal_buffer_headroom =
      352 * 2; // we accept this much headroom in the hardware buffer, but we'll
               // accept less
  const snd_pcm_uframes_t requested_buffer_headroom =
      minimal_buffer_headroom + 2048; // we ask for this much headroom in the
                                      // hardware buffer, but we'll accept less
  int ret, dir = 0;
  unsigned int my_sample_rate = desired_sample_rate;
  // snd_pcm_uframes_t frames = 441 * 10;
  snd_pcm_uframes_t buffer_size, actual_buffer_length;

  ret = snd_pcm_open(&alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
  if (ret < 0)
    return (ret);
  // die("Alsa initialization failed: unable to open pcm device: %s.",
  // snd_strerror(ret));

  snd_pcm_hw_params_alloca(&alsa_params);

  ret = snd_pcm_hw_params_any(alsa_handle, alsa_params);
  if (ret < 0) {
    die("audio_alsa: Broken configuration for device \"%s\": no configurations "
        "available",
        alsa_out_dev);
  }

  ret = snd_pcm_hw_params_set_access(alsa_handle, alsa_params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED);
  if (ret < 0) {
    die("audio_alsa: Access type not available for device \"%s\": %s",
        alsa_out_dev, snd_strerror(ret));
  }

  ret = snd_pcm_hw_params_set_format(alsa_handle, alsa_params,
                                     SND_PCM_FORMAT_S16);
  if (ret < 0) {
    die("audio_alsa: Sample format not available for device \"%s\": %s",
        alsa_out_dev, snd_strerror(ret));
  }

  ret = snd_pcm_hw_params_set_channels(alsa_handle, alsa_params, 2);
  if (ret < 0) {
    die("audio_alsa: Channels count (2) not available for device \"%s\": %s",
        alsa_out_dev, snd_strerror(ret));
  }

  ret = snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params,
                                        &my_sample_rate, &dir);
  if (ret < 0) {
    die("audio_alsa: Rate %iHz not available for playback: %s",
        desired_sample_rate, snd_strerror(ret));
  }

  // snd_pcm_hw_params_set_period_size_near(alsa_handle, alsa_params, &frames,
  // &dir);
  // snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params,
  // &buffer_size);

  ret = snd_pcm_hw_params(alsa_handle, alsa_params);
  if (ret < 0) {
    die("audio_alsa: Unable to set hw parameters for device \"%s\": %s.",
        alsa_out_dev, snd_strerror(ret));
  }

  if (my_sample_rate != desired_sample_rate) {
    die("Can't set the D/A converter to %d.", desired_sample_rate);
  }

  ret = snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_length);
  if (ret < 0) {
    die("audio_alsa: Unable to get hw buffer length for device \"%s\": %s.",
        alsa_out_dev, snd_strerror(ret));
  }

  if (actual_buffer_length <
      config.audio_backend_buffer_desired_length + minimal_buffer_headroom) {
    // the dac buffer is too small, so let's try to set it
    buffer_size =
        config.audio_backend_buffer_desired_length + requested_buffer_headroom;
    ret = snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params,
                                                 &buffer_size);
    if (ret < 0)
      die("audio_alsa: Unable to set hw buffer size to %lu for device \"%s\": "
          "%s.",
          config.audio_backend_buffer_desired_length +
              requested_buffer_headroom,
          alsa_out_dev, snd_strerror(ret));
    if (config.audio_backend_buffer_desired_length + minimal_buffer_headroom >
        buffer_size) {
      die("audio_alsa: Can't set hw buffer size to %lu or more for device "
          "\"%s\". Requested size: %lu, granted size: %lu.",
          config.audio_backend_buffer_desired_length + minimal_buffer_headroom,
          alsa_out_dev, config.audio_backend_buffer_desired_length +
                            requested_buffer_headroom,
          buffer_size);
    }
  }

  return (0);
}

static void start(int sample_rate) {
  debug(2,"audio_alsa start called.");
  if (sample_rate != 44100)
    die("Unexpected sample rate %d -- only 44,100 supported!", sample_rate);
  desired_sample_rate = sample_rate; // must be a variable
}

static uint32_t delay() {
  debug(3,"audio_alsa delay called.");
  if (alsa_handle == NULL) {
    return 0;
  } else {
    pthread_mutex_lock(&alsa_mutex);
    snd_pcm_sframes_t current_avail, current_delay = 0;
    int derr, ignore;
    if (snd_pcm_state(alsa_handle) == SND_PCM_STATE_RUNNING) {
      derr = snd_pcm_avail_delay(alsa_handle, &current_avail, &current_delay);
      // current_avail not used
      if (derr != 0) {
        ignore = snd_pcm_recover(alsa_handle, derr, 0);
        debug(1, "Error %d in delay(): %s. Delay reported is %d frames.", derr,
              snd_strerror(derr), current_delay);
        current_delay = -1;
      }
    } else if (snd_pcm_state(alsa_handle) == SND_PCM_STATE_PREPARED) {
      current_delay = 0;
    } else {
      if (snd_pcm_state(alsa_handle) == SND_PCM_STATE_XRUN)
        current_delay = 0;
      else {
        current_delay = -1;
        debug(1, "Error -- ALSA delay(): bad state: %d.",
              snd_pcm_state(alsa_handle));
      }
      if ((derr = snd_pcm_prepare(alsa_handle))) {
        ignore = snd_pcm_recover(alsa_handle, derr, 0);
        debug(1, "Error preparing after delay error: %s.", snd_strerror(derr));
        current_delay = -1;
      }
    }
    pthread_mutex_unlock(&alsa_mutex);
    return current_delay;
  }
}

static void play(short buf[], int samples) {
  debug(3,"audio_alsa play called.");
  int ret = 0;
  if (alsa_handle == NULL) {
    pthread_mutex_lock(&alsa_mutex);
    ret = open_alsa_device();
    open_mixer();
    pthread_mutex_unlock(&alsa_mutex);
    if ((ret == 0) && (audio_alsa.volume))
      audio_alsa.volume(set_volume);
  }
  if (ret == 0) {
    pthread_mutex_lock(&alsa_mutex);
    snd_pcm_sframes_t current_delay = 0;
    int err, ignore;
    if ((snd_pcm_state(alsa_handle) == SND_PCM_STATE_PREPARED) ||
        (snd_pcm_state(alsa_handle) == SND_PCM_STATE_RUNNING)) {
      err = snd_pcm_writei(alsa_handle, (char *)buf, samples);
      if (err < 0) {
        ignore = snd_pcm_recover(alsa_handle, err, 0);
        debug(1, "Error %d writing %d samples in play() %s.", err, samples,
              snd_strerror(err));
      }
    } else {
      debug(1, "Error -- ALSA device in incorrect state (%d) for play.",
            snd_pcm_state(alsa_handle));
      if ((err = snd_pcm_prepare(alsa_handle))) {
        ignore = snd_pcm_recover(alsa_handle, err, 0);
        debug(1, "Error preparing after play error: %s.", snd_strerror(err));
      }
    }
    pthread_mutex_unlock(&alsa_mutex);
  }
}

static void flush(void) {
  debug(2,"audio_alsa flush called.");
  pthread_mutex_lock(&alsa_mutex);
  int derr;
  if (hardware_mixer && alsa_mix_handle) {
    snd_mixer_close(alsa_mix_handle);
    alsa_mix_handle = NULL;
  }
  if (alsa_handle) {
    // debug(1,"Dropping frames for flush...");
    if ((derr = snd_pcm_drop(alsa_handle)))
      debug(1, "Error dropping frames: %s.", snd_strerror(derr));
    // debug(1,"Dropped frames ok. State is %d.",snd_pcm_state(alsa_handle));
    if ((derr = snd_pcm_prepare(alsa_handle)))
      debug(1, "Error preparing after flush: %s.", snd_strerror(derr));
    // debug(1,"Frames successfully dropped.");
    /*
    if (snd_pcm_state(alsa_handle)==SND_PCM_STATE_PREPARED)
      debug(1,"Flush returns to SND_PCM_STATE_PREPARED state.");
    if (snd_pcm_state(alsa_handle)==SND_PCM_STATE_RUNNING)
      debug(1,"Flush returns to SND_PCM_STATE_RUNNING state.");
    */
    if (!((snd_pcm_state(alsa_handle) == SND_PCM_STATE_PREPARED) ||
          (snd_pcm_state(alsa_handle) == SND_PCM_STATE_RUNNING)))
      debug(1, "Flush returning unexpected state -- %d.",
            snd_pcm_state(alsa_handle));

    // flush also closes the device
    snd_pcm_close(alsa_handle);
    alsa_handle = NULL;
  }
  pthread_mutex_unlock(&alsa_mutex);
}

static void stop(void) {
  debug(2,"audio_alsa stop called.");
  // when we want to stop, we want the alsa device
  // to be closed immediately -- we may even be killing the thread, so we
  // don't wish to wait
  // so we should flush first
  flush(); // flush will also close the device
           // close_alsa_device();
}

static void parameters(audio_parameters *info) {
  info->minimum_volume_dB = alsa_mix_mindb;
  info->maximum_volume_dB = alsa_mix_maxdb;
}

static void volume(double vol) {
  pthread_mutex_lock(&alsa_mutex);
	debug(2, "Setting volume db to %f.", vol);
  set_volume = vol;
  if (hardware_mixer && alsa_mix_handle) {
    if (snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, vol, 0) != 0) {
      debug(1, "Can't set playback volume accurately to %f dB.", vol);
      if (snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, vol, -1) != 0)
        if (snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, vol, 1) != 0)
          die("Failed to set playback dB volume");
    }
  }
  pthread_mutex_unlock(&alsa_mutex);
}

static void linear_volume(double vol) {
  debug(2, "Setting linear volume to %f.", vol);
  set_volume = vol;
  if (hardware_mixer && alsa_mix_handle) {
    double linear_volume = pow(10, vol);
    // debug(1,"Linear volume is %f.",linear_volume);
    long int_vol =
        alsa_mix_minv + (alsa_mix_maxv - alsa_mix_minv) * linear_volume;
    // debug(1,"Setting volume to %ld, for volume input of %f.",int_vol,vol);
    if (alsa_mix_handle) {
      if (snd_mixer_selem_set_playback_volume_all(alsa_mix_elem, int_vol) != 0)
        die("Failed to set playback volume");
    }
  }
}

static void mute(int do_mute) {
  pthread_mutex_lock(&alsa_mutex);
  debug(2,"audio_alsa mute called.");
  if (hardware_mixer && alsa_mix_handle) {
    if (do_mute) {
      // debug(1,"Mute");
      snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, 0);
    } else {
      // debug(1,"Unmute");
      snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, 1);
    }
  }
  pthread_mutex_unlock(&alsa_mutex);
}
