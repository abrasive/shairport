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

#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "config.h"
#include "common.h"
#include "player.h"
#include "rtp.h"

#ifdef CONFIG_SNDIO
extern audio_output audio_sndio;
#endif
#ifdef CONFIG_AO
extern audio_output audio_ao;
#endif
#ifdef CONFIG_PULSE
extern audio_output audio_pulse;
#endif
#ifdef CONFIG_ALSA
extern audio_output audio_alsa;
#endif
extern audio_output audio_dummy, audio_pipe;

static audio_output *outputs[] = {
#ifdef CONFIG_SNDIO
    &audio_sndio,
#endif
#ifdef CONFIG_ALSA
    &audio_alsa,
#endif
#ifdef CONFIG_PULSE
    &audio_pulse,
#endif
#ifdef CONFIG_AO
    &audio_ao,
#endif
    &audio_dummy,
    &audio_pipe,
    NULL
};

long long audio_delay;

audio_output *audio_get_output(char *name) {
    audio_output **out;

    // default to the first
    if (!name)
        return outputs[0];

    for (out=outputs; *out; out++)
        if (!strcasecmp(name, (*out)->name))
            return *out;

    return NULL;
}

void audio_ls_outputs(void) {
    audio_output **out;

    printf("Available audio outputs:\n");
    for (out=outputs; *out; out++)
        printf("    %s%s\n", (*out)->name, out==outputs ? " (default)" : "");

    for (out=outputs; *out; out++) {
        printf("\n");
        printf("Options for output %s:\n", (*out)->name);
        (*out)->help();
    }
}

//gets called for generic outputs
long long audio_get_delay(void) {
    return audio_delay;
}

void audio_estimate_delay(audio_output *output) {
    signed short *silence;
    int frame_size = 200;
    long long base_time, cur_time, last_time, frame_time, frame_time_limit;

    silence = malloc(4 * frame_size);
    memset(silence, 0, 4 * frame_size);
    frame_time = (frame_size * 1000000) / 44100;
    frame_time_limit = frame_time * 2 / 3;

    base_time = tstp_us();
    last_time = base_time;
    int loop = 0;
    while (loop < 1000) {
        output->play(silence, frame_size);
        cur_time = tstp_us();
        if ((cur_time - last_time) > frame_time_limit)
            break;
        last_time = cur_time;
        loop++;
    }
    debug(3, "totaltime %lld, last loop time %lld, loop %d\n", last_time-base_time,cur_time - last_time, loop);
    audio_delay = (loop * frame_time) - (last_time - base_time);
    debug(2,"Generic output delay %lld\n", audio_delay);
    free(silence);
}
