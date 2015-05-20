/*
 * pipe output driver. This file is part of Shairport.
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
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include "common.h"
#include "audio.h"

static int fd = -1;

char *pipename = NULL;

static void start(int sample_rate) {
  debug(1,"Pipename to start is \"%s\"",pipename);
    fd = open(pipename, O_WRONLY | O_NONBLOCK);
    /*
    if (fd < 0) {
        perror("open");
        die("could not open specified pipe for writing");
    }
    */
}

static void play(short buf[], int samples) {
    int ignore = write(fd, buf, samples*4);
}

static void stop(void) {
    close(fd);
}

static int init(int argc, char **argv, config_t *cfgp) {

  if (cfgp!=NULL) {
       /* Get the Output Pipename. */
      const char *str;
      if(config_lookup_string(cfgp, "pipe.name", &str)) {
        pipename = (char*)str;
      }
  }


    if ((pipename==NULL) && (argc != 1))
        die("bad or missing argument(s) to pipe");

    if (argc==1)
      pipename = strdup(argv[0]);
      
    // here, create the pipe
    if (mkfifo(pipename, 0644) && errno != EEXIST)
      die("Could not create metadata FIFO %s", pipename);

    
    debug(1,"Pipename is \"%s\"",pipename);

    // test open pipe so we error on startup if it's going to fail
    start(44100);
    stop();

    return 0;
}

static void deinit(void) {
    if (fd > 0)
        close(fd);
    if (pipename)
        free(pipename);
}

static void help(void) {
    printf("    pipe takes 1 argument: the name of the FIFO to write to.\n");
}

audio_output audio_pipe = {
    .name = "pipe",
    .help = &help,
    .init = &init,
    .deinit = &deinit,
    .start = &start,
    .stop = &stop,
    .flush = NULL,
    .delay = NULL,
    .play = &play,
    .volume = NULL,
    .parameters = NULL
};
