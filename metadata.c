/*
 * Metadate structure and utility methods. This file is part of Shairport.
 * Copyright (c) Benjamin Maus 2013
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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"

#ifdef HAVE_LIBSSL
#include <openssl/md5.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/md5.h>
#endif

#include "common.h"
#include "metadata.h"

metadata player_meta;
static int fd = -1;
static int dirty = 0;

void metadata_set(char** field, const char* value) {
    if (*field) {
        if (!strcmp(*field, value))
            return;
        free(*field);
    }
    *field = strdup(value);
    dirty = 1;
}

void metadata_open(void) {
    if (!config.meta_dir)
        return;

    const char fn[] = "shairport_sync_metadata_pipe";
    size_t pl = strlen(config.meta_dir) + 1 + strlen(fn);

    char* path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s", config.meta_dir, fn);

    if (mkfifo(path, 0644) && errno != EEXIST)
        die("Could not create metadata FIFO %s", path);

    fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0)
        debug(1, "Could not open metadata FIFO %s. Will try again later.", path);

    free(path);
}

static void metadata_close(void) {
    close(fd);
    fd = -1;
}

static void print_one(const char *name, const char *value) {
  int ignore;
    ignore = write(fd, name, strlen(name));
    ignore = write(fd, "=", 1);
    if (value)
        ignore = write(fd, value, strlen(value));
    ignore = write(fd, "\n", 1);
}

#define write_one(name) \
    print_one(#name, player_meta.name)

void metadata_write(void) {
    int ret;

    // readers may go away and come back
    if (fd < 0)
        metadata_open();
    if (fd < 0)
        return;

    if (!dirty)
        return;

    dirty = 0;

    write_one(artist);
    write_one(title);
    write_one(album);
    write_one(artwork);
    write_one(genre);
    write_one(comment);

    ret = write(fd, "\n", 1);
    if (ret < 1)    // no reader
        metadata_close();
}

void metadata_cover_image(const char *buf, int len, const char *ext) {
    if (!config.meta_dir)
        return;

    if (buf) {
        debug(2, "Cover Art set\n");
    } else {
        debug(2, "Cover Art cleared\n");
        return;
    }

    uint8_t img_md5[16];
    
    
#ifdef HAVE_LIBSSL
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, buf, len);
    MD5_Final(img_md5, &ctx);
#endif

    
#ifdef HAVE_LIBPOLARSSL
    md5_context tctx;
    md5_starts(&tctx);
    md5_update(&tctx, buf, len);
    md5_finish(&tctx, img_md5);
#endif

    char img_md5_str[33];
    int i;
    for (i = 0; i < 16; i++)
        sprintf(&img_md5_str[i*2], "%02x", (uint8_t)img_md5[i]);

    char *dir = config.meta_dir;
    char *prefix = "cover-";

    size_t pl = strlen(dir) + 1 + strlen(prefix) + strlen(img_md5_str) + 1 + strlen(ext);

    char *path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s%s.%s", dir, prefix, img_md5_str, ext);

    int cover_fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP| S_IROTH | S_IWOTH);

    if (cover_fd < 0) {
        warn("Could not open file %s for writing cover art", path);
        return;
    }

    if (write(cover_fd, buf, len) < len) {
        warn("writing %s failed\n", path);
        free(path);
        return;
    }
    close(cover_fd);

    debug(2, "Cover Art file is \"%s\".\n", path);
    metadata_set(&player_meta.artwork, path+strlen(dir)+1);

    free(path);
}


// Metadata is not used by shairport-sync.
// Instead we send all metadata to a fifo pipe, so that other apps can listen to the pipe and use the metadata.

// We use two 4-character codes to identify each piece of data and we send the data itself in base64 form.

// The first 4-character code, called the "type", is either:
//    'core' for all the regular metadadata coming from iTunes, etc., or 
//    'ssnc' (for 'shairport-sync') for all metadata coming from Shairport Sync itself, such as start/end delimiters, etc.

// For 'core' metadata, the second 4-character code is the 4-character metadata code coming from iTunes etc.
// For 'ssnc' metadata, the second 4-character code is used to distinguish the messages.

// Cover art is not tagged in the same way as other metadata, it seems, so is sent as an 'ssnc' type metadata message with the code 'PICT'
// The three kinds of 'ssnc' metadata at present are 'strt', 'stop' and 'PICT' for metadata package start, metadata package stop and cover art, respectively.

// Metadata is sent in two disctinct parts:
//    (1) a line with type, code and length information surrounded by XML-type tags and
//    (2) the data itself, if any, in base64 form, surrounded by XML-style data tags.

void metadata_process(uint32_t type,uint32_t code,char *data,uint32_t length) {
  debug(2,"Process metadata with type %x, code %x and length %u.",type,code,length);
  int ret;
  // readers may go away and come back
  if (fd < 0)
    metadata_open();
  if (fd < 0)
    return;
  char thestring[1024];
  snprintf(thestring,1024,"<type>%x</type><code>%x</code><length>%u</length>\n",type,code,length);
  ret = write(fd, thestring, strlen(thestring));
  if (ret < 1)    // no reader
    metadata_close();
  if (length>0) {
    snprintf(thestring,1024,"<data encoding=\"base64\">\n");
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 1)    // no reader
      metadata_close();
      
    char *b64 = base64_enc(data,length);
    ret = write(fd,b64,strlen(b64));
    free(b64);
    
    if (ret < 1)    // no reader
      metadata_close();
    snprintf(thestring,1024,"\n</data>\n");
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 1)    // no reader
      metadata_close();
  }
}
