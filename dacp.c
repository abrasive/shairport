/*
 * DACP remote methods. This file is part of Shairport.
 * Copyright (c) 2014 Josh Butts <josh@joshbutts.com>
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

#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/md5.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "common.h"
#include "dacp.h"

dacpdata player_dacp;
static int fd = -1;
static int dirty = 0;

void dacp_set(char** field, const char* value) {
    if (*field) {
        if (!strcmp(*field, value))
            return;
        free(*field);
    }
    *field = strdup(value);
    dirty = 1;
}

void dacp_open(void) {
    if (!config.dacp_dir)
        return;

    const char fn[] = "dacp";
    size_t pl = strlen(config.dacp_dir) + 1 + strlen(fn);

    char* path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s", config.dacp_dir, fn);

    if (mkfifo(path, 0644) && errno != EEXIST)
        die("Could not create dacp FIFO %s", path);

    fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0)
        debug(1, "Could not open dacp FIFO %s. Will try again later.", path);

    free(path);
}

static void dacp_close(void) {
    close(fd);
    fd = -1;
}

static void print_one(const char *name, const char *value) {
    write_unchecked(fd, name, strlen(name));
    write_unchecked(fd, "=", 1);
    if (value)
        write_unchecked(fd, value, strlen(value));
    write_unchecked(fd, "\n", 1);
}

#define write_one(name) \
   print_one(#name, player_dacp.name)

void dacp_write(void) {
    int ret;

    // readers may go away and come back
    if (fd < 0)
        dacp_open();
    if (fd < 0)
        return;

    if (!dirty)
        return;

    dirty = 0;
    
    write_one(dacp_id);
	write_one(active_remote);

    ret = write(fd, "\n", 1);
    if (ret < 1)    // no reader
        dacp_close();
}