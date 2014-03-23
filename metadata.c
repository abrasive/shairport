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

#include "common.h"
#include "metadata.h"


metadata * metadata_init(void) {
    metadata *meta = malloc(sizeof(metadata));
    memset(meta, 0, sizeof(metadata));
    return meta;
}

void metadata_free(metadata *meta) {
    int i;
    if (meta->artist)
        free(meta->artist);
    if (meta->title)
        free(meta->title);    
    if (meta->album)
        free(meta->album);       
    if (meta->comment)
        free(meta->comment);
    if (meta->genre)
        free(meta->genre);
    free(meta);
}

FILE* metadata_open(const char* mode) {
  FILE* fh = NULL;
  if (config.cover_dir) {
    const char fn[] = "now_playing.txt";
    size_t pl = strlen(config.cover_dir) + 1 + strlen(fn);
    
    char* path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s", config.cover_dir, fn);
    
    fh = fopen(path, mode);
    free(path);
  }
  return fh;
}

void metadata_write(metadata* meta, const char* dir) {
  FILE* fh = metadata_open("w");
  if (fh) {
    fprintf(fh, "%s\n", meta->artist);
    fprintf(fh, "%s\n", meta->title);
    fprintf(fh, "%s\n", meta->album);
    fprintf(fh, "%s\n", meta->genre);
    fprintf(fh, "%s\n", (meta->comment == NULL) ? "" : meta->comment);
    fclose(fh);
  }
}
