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
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <limits.h>
#include <dlfcn.h>
#include "audio.h"
#include "config.h"
#include "common.h"

#define MAX_AUDIO_OUTPUTS 8

extern audio_output audio_dummy, audio_pipe;

#ifdef CONFIG_DYNAMIC_PLUGINS
static audio_output *outputs[MAX_AUDIO_OUTPUTS+1] = {
#else
static audio_output *outputs[] = {
#endif
    &audio_dummy,
    &audio_pipe,
    NULL
};

static audio_output **next_output = outputs;

void audio_load_plugins(const char *path) {
    size_t dir_length;
    char *filename;
    DIR *d;
    struct dirent *dir;
    struct stat st;
    int ret;
    char *ext;
    void *dl_handle;
    audio_output* (*dl_get_audio)(void);
    audio_output* output;

    while (*next_output != NULL)
        next_output ++;


    dir_length = strlen(path);
    filename = malloc(dir_length + NAME_MAX + 2);
    strcpy(filename, path);
    filename[dir_length] = '/';
    filename[dir_length+1] = 0;

    d = opendir(path);
    if(d == NULL) {
        warn("Could not open plugins directory : %s", path);
        free(filename);
        return;
    }

    while ((dir = readdir(d)) != NULL) {
      if (next_output == outputs + MAX_AUDIO_OUTPUTS) {
          warn("Maximum number of output drivers reached");
          break;
      }

      strcpy(filename + dir_length + 1, dir->d_name);
      ret = stat(filename, &st);

      if (ret < 0) {
          warn("Could not stat file : %s", filename);
          continue;
      }
      
      // Skip non regular.
      if(!S_ISREG(st.st_mode))
          continue;

      // Skip non .so file
      ext = strrchr(dir->d_name, '.');
      if (ext == NULL || strcmp(ext, PLUGIN_EXT) != 0)
          continue;

      dl_handle = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
      if (dl_handle == NULL) {
          warn("Could not dlopen plugin %s : %s", filename, dlerror());
          continue;
      }

      dl_get_audio = dlsym(dl_handle, "plugin_get_audio"); 
      if (dl_get_audio == NULL) {
          warn("Plugin %s entry not found : %s", filename, dlerror());
          continue;
      }

      output = dl_get_audio();
      if (output == NULL) {
          warn("Loading plugin %s failed", filename);
      }

      printf("%s output driver loaded\n", output->name);
      
      *next_output = output;
      next_output ++;
      *next_output = NULL;
    }

    free(filename);
    closedir(d);
}

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
