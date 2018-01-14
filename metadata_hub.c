/*
 * Metadata hub and access methods.
 * Basically, if you need to store metadata
 * (e.g. for use with the dbus interfaces),
 * then you need a metadata hub,
 * where everything is stored
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017
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

#include <stdlib.h>
#include <string.h>

#include "dacp.h"
#include "metadata_hub.h"

void metadata_hub_init(void) {
  debug(1, "Metadata bundle initialisation.");
  memset(&metadata_store, 0, sizeof(metadata_store));
}

void add_metadata_watcher(metadata_watcher fn, void *userdata) {
  int i;
  for (i = 0; i < number_of_watchers; i++) {
    if (metadata_store.watchers[i] == NULL) {
      metadata_store.watchers[i] = fn;
      metadata_store.watchers_data[i] = userdata;
      debug(1, "Added a metadata watcher into slot %d", i);
      break;
    }
  }
}

void run_metadata_watchers(void) {
  int i;
  for (i = 0; i < number_of_watchers; i++) {
    if (metadata_store.watchers[i]) {
      metadata_store.watchers[i](&metadata_store, metadata_store.watchers_data[i]);
    }
  }
}

void metadata_hub_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length) {
  // metadata coming in from the audio source or from Shairport Sync itself passes through here
  // this has more information about tags, which might be relevant:
  // https://code.google.com/p/ytrack/wiki/DMAP
  char *payload;
  if (length < 2048)
    payload = strndup(data, length);
  else
    payload = NULL;
  switch (code) {
  case 'asal':
    debug(1, "MH Album Name: \"%s\".", payload);
    break;
  case 'asar':
    debug(1, "MH Artist: \"%s\".", payload);
    break;
  case 'ascm':
    debug(1, "MH Comment: \"%s\".", payload);
    break;
  case 'asgn':
    debug(1, "MH Genre: \"%s\".", payload);
    break;
  case 'minm':
    debug(1, "MH Title: \"%s\".", payload);
    break;
  case 'ascp':
    debug(1, "MH Composer: \"%s\".", payload);
    break;
  case 'asdt':
    debug(1, "MH File kind: \"%s\".", payload);
    break;
  case 'assn':
    debug(1, "MH Sort as: \"%s\".", payload);
    break;
  case 'PICT':
    debug(1, "MH Picture received, length %u bytes.", length);
    break;
  case 'clip':
    debug(1, "MH Client's IP: \"%s\".", payload);
    break;
  default:
    if (type == 'ssnc') {
      char typestring[5];
      *(uint32_t *)typestring = htonl(type);
      typestring[4] = 0;
      char codestring[5];
      *(uint32_t *)codestring = htonl(code);
      codestring[4] = 0;
      debug(1, "MH \"%s\" \"%s\": \"%s\".", typestring, codestring, payload);
    }
  }
  if (payload)
    free(payload);
}
