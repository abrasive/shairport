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
  switch (code) {
  case 'asal':
    if ((metadata_store.album_name == NULL) ||
        (strncmp(metadata_store.album_name, data, length) != 0)) {
      if (metadata_store.album_name)
        free(metadata_store.album_name);
      metadata_store.album_name = strndup(data, length);
      // debug(1, "MH Album name set to: \"%s\"", metadata_store.album_name);
      metadata_store.album_name_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'asar':
    if ((metadata_store.artist_name == NULL) ||
        (strncmp(metadata_store.artist_name, data, length) != 0)) {
      if (metadata_store.artist_name)
        free(metadata_store.artist_name);
      metadata_store.artist_name = strndup(data, length);
      // debug(1, "MH Artist name set to: \"%s\"", metadata_store.artist_name);
      metadata_store.artist_name_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'ascm':
    if ((metadata_store.comment == NULL) ||
        (strncmp(metadata_store.comment, data, length) != 0)) {
      if (metadata_store.comment)
        free(metadata_store.comment);
      metadata_store.comment = strndup(data, length);
      // debug(1, "MH Comment set to: \"%s\"", metadata_store.comment);
      metadata_store.comment_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'asgn':
    if ((metadata_store.genre == NULL) ||
        (strncmp(metadata_store.genre, data, length) != 0)) {
      if (metadata_store.genre)
        free(metadata_store.genre);
      metadata_store.genre = strndup(data, length);
      // debug(1, "MH Genre set to: \"%s\"", metadata_store.genre);
      metadata_store.genre_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'minm':
    if ((metadata_store.track_name == NULL) ||
        (strncmp(metadata_store.track_name, data, length) != 0)) {
      if (metadata_store.track_name)
        free(metadata_store.track_name);
      metadata_store.track_name = strndup(data, length);
      // debug(1, "MH Track name set to: \"%s\"", metadata_store.track_name);
      metadata_store.track_name_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'ascp':
    if ((metadata_store.composer == NULL) ||
        (strncmp(metadata_store.composer, data, length) != 0)) {
      if (metadata_store.composer)
        free(metadata_store.composer);
      metadata_store.composer = strndup(data, length);
      // debug(1, "MH Composer set to: \"%s\"", metadata_store.composer);
      metadata_store.composer_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'asdt':
    if ((metadata_store.file_kind == NULL) ||
        (strncmp(metadata_store.file_kind, data, length) != 0)) {
      if (metadata_store.file_kind)
        free(metadata_store.file_kind);
      metadata_store.file_kind = strndup(data, length);
      // debug(1, "MH File Kind set to: \"%s\"", metadata_store.file_kind);
      metadata_store.file_kind_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'asaa':
    if ((metadata_store.file_kind == NULL) ||
        (strncmp(metadata_store.file_kind, data, length) != 0)) {
      if (metadata_store.file_kind)
        free(metadata_store.file_kind);
      metadata_store.file_kind = strndup(data, length);
      // debug(1, "MH File Kind set to: \"%s\"", metadata_store.file_kind);
      metadata_store.file_kind_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'assn':
    if ((metadata_store.sort_as == NULL) ||
        (strncmp(metadata_store.sort_as, data, length) != 0)) {
      if (metadata_store.sort_as)
        free(metadata_store.sort_as);
      metadata_store.sort_as = strndup(data, length);
      // debug(1, "MH Sort As set to: \"%s\"", metadata_store.sort_as);
      metadata_store.sort_as_changed = 1;
      metadata_store.changed = 1;
    }
    break;
  case 'PICT':
    debug(1, "MH Picture received, length %u bytes.", length);
    break;
  case 'clip':
    if ((metadata_store.client_ip == NULL) ||
        (strncmp(metadata_store.client_ip, data, length) != 0)) {
      if (metadata_store.client_ip)
        free(metadata_store.client_ip);
      metadata_store.client_ip = strndup(data, length);
      // debug(1, "MH Client IP set to: \"%s\"", metadata_store.client_ip);
      metadata_store.client_ip_changed = 1;
      metadata_store.changed = 1;
    }
    break;
      
  default:
    if (type == 'ssnc')
    {
      char typestring[5];
      *(uint32_t *)typestring = htonl(type);
      typestring[4] = 0;
      char codestring[5];
      *(uint32_t *)codestring = htonl(code);
      codestring[4] = 0;
      char *payload;
      if (length < 2048)
        payload = strndup(data, length);
      else
        payload = NULL;
      debug(1, "MH \"%s\" \"%s\" (%d bytes): \"%s\".", typestring, codestring, length, payload);
      if (payload)
        free(payload);
    }
  }
}
