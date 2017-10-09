/*
 * Audio driver handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
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

#include "audio.h"
#include "common.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_SNDIO
extern audio_output audio_sndio;
#endif
#ifdef CONFIG_AO
extern audio_output audio_ao;
#endif
#ifdef CONFIG_SOUNDIO
extern audio_output audio_soundio;
#endif
#ifdef CONFIG_PA
extern audio_output audio_pa;
#endif
#ifdef CONFIG_ALSA
extern audio_output audio_alsa;
#endif
#ifdef CONFIG_DUMMY
extern audio_output audio_dummy;
#endif
#ifdef CONFIG_PIPE
extern audio_output audio_pipe;
#endif
#ifdef CONFIG_STDOUT
extern audio_output audio_stdout;
#endif

static audio_output *outputs[] = {
#ifdef CONFIG_ALSA
    &audio_alsa,
#endif
#ifdef CONFIG_SNDIO
    &audio_sndio,
#endif
#ifdef CONFIG_PA
    &audio_pa,
#endif
#ifdef CONFIG_AO
    &audio_ao,
#endif
#ifdef CONFIG_SOUNDIO
    &audio_soundio,
#endif
#ifdef CONFIG_PIPE
    &audio_pipe,
#endif
#ifdef CONFIG_STDOUT
    &audio_stdout,
#endif
#ifdef CONFIG_DUMMY
    &audio_dummy,
#endif
    NULL};

audio_output *audio_get_output(char *name) {
  audio_output **out;

  // default to the first
  if (!name)
    return outputs[0];

  for (out = outputs; *out; out++)
    if (!strcasecmp(name, (*out)->name))
      return *out;

  return NULL;
}

void audio_ls_outputs(void) {
  audio_output **out;

  printf("Available audio outputs:\n");
  for (out = outputs; *out; out++)
    printf("    %s%s\n", (*out)->name, out == outputs ? " (default)" : "");

  for (out = outputs; *out; out++) {
    printf("\n");
    printf("Options for output %s:\n", (*out)->name);
    (*out)->help();
  }
}

void parse_general_audio_options(void) {
  /* this must be called after the output device has been initialised, so that the default values
   * are set before any options are chosen */
  int value;
  double dvalue;
  if (config.cfg != NULL) {

    /* Get the desired buffer size setting (deprecated). */
    if (config_lookup_int(config.cfg, "general.audio_backend_buffer_desired_length", &value)) {
      if ((value < 0) || (value > 66150)) {
        inform("The setting general.audio_backend_buffer_desired_length is deprecated. "
               "Use alsa.audio_backend_buffer_desired_length_in_seconds instead.");
        die("Invalid audio_backend_buffer_desired_length value: \"%d\". It "
            "should be between 0 and "
            "66150, default is %d",
            value, (int)(config.audio_backend_buffer_desired_length * 44100));
      } else {
        inform("The setting general.audio_backend_buffer_desired_length is deprecated. "
               "Use general.audio_backend_buffer_desired_length_in_seconds instead.");
        config.audio_backend_buffer_desired_length = 1.0 * value / 44100;
      }
    }

    /* Get the desired buffer size setting in seconds. */
    if (config_lookup_float(config.cfg, "general.audio_backend_buffer_desired_length_in_seconds",
                            &dvalue)) {
      if ((dvalue < 0) || (dvalue > 1.5)) {
        die("Invalid audio_backend_buffer_desired_length_in_seconds value: \"%f\". It "
            "should be between 0 and "
            "1.5, default is %.3f seconds",
            dvalue, config.audio_backend_buffer_desired_length);
      } else {
        config.audio_backend_buffer_desired_length = dvalue;
      }
    }

    /* Get the latency offset (deprecated). */
    if (config_lookup_int(config.cfg, "general.audio_backend_latency_offset", &value)) {
      if ((value < -66150) || (value > 66150)) {
        inform("The setting general.audio_backend_latency_offset is deprecated. "
               "Use general.audio_backend_latency_offset_in_seconds instead.");
        die("Invalid  audio_backend_latency_offset value: \"%d\". It "
            "should be between -66150 and +66150, default is 0",
            value);
      } else {
        inform("The setting general.audio_backend_latency_offset is deprecated. "
               "Use general.audio_backend_latency_offset_in_seconds instead.");
        config.audio_backend_latency_offset = 1.0 * value / 44100;
      }
    }

    /* Get the latency offset in seconds. */
    if (config_lookup_float(config.cfg, "general.audio_backend_latency_offset_in_seconds",
                            &dvalue)) {
      if ((dvalue < -1.0) || (dvalue > 1.5)) {
        die("Invalid audio_backend_latency_offset_in_seconds \"%f\". It "
            "should be between -1.0 and +1.5, default is 0 seconds",
            dvalue);
      } else {
        config.audio_backend_latency_offset = dvalue;
      }
    }

    /* Get the desired length of the silent lead-in. */
    if (config_lookup_float(config.cfg, "general.audio_backend_silent_lead_in_time", &dvalue)) {
      if ((dvalue < 0.0) || (dvalue > 4)) {
        die("Invalid audio_backend_silent_lead_in_time \"%f\". It "
            "must be between 0.0 and 4.0 seconds. Omit setting to use the default value",
            dvalue);
      } else {
        config.audio_backend_silent_lead_in_time = dvalue;
      }
    }
  }
}
