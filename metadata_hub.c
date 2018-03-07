/*
 * Metadata hub and access methods.
 * Basically, if you need to store metadata
 * (e.g. for use with the dbus interfaces),
 * then you need a metadata hub,
 * where everything is stored
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2018
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

#include "common.h"
#include "dacp.h"
#include "metadata_hub.h"

#ifdef HAVE_LIBMBEDTLS
#include <mbedtls/md5.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/md5.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/md5.h>
#endif

pthread_rwlock_t metadata_hub_re_lock = PTHREAD_RWLOCK_INITIALIZER;

void release_char_string(char **str) {
  if (*str) {
    free(*str);
    *str=NULL;
  }
}

void metadata_hub_init(void) {
  // debug(1, "Metadata bundle initialisation.");
  memset(&metadata_store, 0, sizeof(metadata_store));
}

void add_metadata_watcher(metadata_watcher fn, void *userdata) {
  int i;
  for (i = 0; i < number_of_watchers; i++) {
    if (metadata_store.watchers[i] == NULL) {
      metadata_store.watchers[i] = fn;
      metadata_store.watchers_data[i] = userdata;
      // debug(1, "Added a metadata watcher into slot %d", i);
      break;
    }
  }
}

void metadata_hub_modify_prolog(void) {
  // always run this before changing an entry or a sequence of entries in the metadata_hub
  // debug(1, "locking metadata hub for writing");
  pthread_rwlock_wrlock(&metadata_hub_re_lock);
}

void metadata_hub_release_track_artwork(void) {
  // debug(1,"release track artwork");
  release_char_string(&metadata_store.cover_art_pathname);
}

void metadata_hub_reset_track_metadata(void) {
  //debug(1,"release track metadata");
  release_char_string(&metadata_store.track_name);
  release_char_string(&metadata_store.artist_name);
  release_char_string(&metadata_store.album_name);
  release_char_string(&metadata_store.genre);
  release_char_string(&metadata_store.comment);
  release_char_string(&metadata_store.composer);
  release_char_string(&metadata_store.file_kind);
  release_char_string(&metadata_store.sort_as);
  metadata_store.item_id = 0;
  metadata_store.songtime_in_milliseconds = 0;
}

void run_metadata_watchers(void) {
  int i;
  // debug(1, "locking metadata hub for reading");
  pthread_rwlock_rdlock(&metadata_hub_re_lock);
  for (i = 0; i < number_of_watchers; i++) {
    if (metadata_store.watchers[i]) {
      metadata_store.watchers[i](&metadata_store, metadata_store.watchers_data[i]);
    }
  }
  // debug(1, "unlocking metadata hub for reading");
  pthread_rwlock_unlock(&metadata_hub_re_lock);
}

void metadata_hub_modify_epilog(int modified) {
  // always run this after changing an entry or a sequence of entries in the metadata_hub
  // debug(1, "unlocking metadata hub for writing");
  pthread_rwlock_unlock(&metadata_hub_re_lock);
  if (modified) {
    run_metadata_watchers();
  }
}

char *metadata_write_image_file(const char *buf, int len) {

  // warning -- this removes all files from the directory apart from this one, if it exists
  // it will return a path to the image file allocated with malloc.
  // free it if you don't need it.

  char *path = NULL; // this will be what is returned

  uint8_t img_md5[16];
// uint8_t ap_md5[16];

#ifdef HAVE_LIBSSL
  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, buf, len);
  MD5_Final(img_md5, &ctx);
#endif

#ifdef HAVE_LIBMBEDTLS
  mbedtls_md5_context tctx;
  mbedtls_md5_starts(&tctx);
  mbedtls_md5_update(&tctx, buf, len);
  mbedtls_md5_finish(&tctx, img_md5);
#endif

#ifdef HAVE_LIBPOLARSSL
  md5_context tctx;
  md5_starts(&tctx);
  md5_update(&tctx, buf, len);
  md5_finish(&tctx, img_md5);
#endif

  char img_md5_str[33];
  memset(img_md5_str, 0, sizeof(img_md5_str));
  char *ext;
  char png[] = "png";
  char jpg[] = "jpg";
  int i;
  for (i = 0; i < 16; i++)
    sprintf(&img_md5_str[i * 2], "%02x", (uint8_t)img_md5[i]);
  // see if the file is a jpeg or a png
  if (strncmp(buf, "\xFF\xD8\xFF", 3) == 0)
    ext = jpg;
  else if (strncmp(buf, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) == 0)
    ext = png;
  else {
    debug(1, "Unidentified image type of cover art -- jpg extension used.");
    ext = jpg;
  }
  mode_t oldumask = umask(000);
  int result = mkpath(config.cover_art_cache_dir, 0777);
  umask(oldumask);
  if ((result == 0) || (result == -EEXIST)) {
    // see if the file exists by opening it.
    // if it exists, we're done
    char *prefix = "cover-";

    size_t pl = strlen(config.cover_art_cache_dir) + 1 + strlen(prefix) + strlen(img_md5_str) + 1 +
                strlen(ext);

    path = malloc(pl + 1);
    snprintf(path, pl + 1, "%s/%s%s.%s", config.cover_art_cache_dir, prefix, img_md5_str, ext);
    int cover_fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRGRP | S_IROTH);
    if (cover_fd > 0) {
      // write the contents
      if (write(cover_fd, buf, len) < len) {
        warn("Writing cover art file \"%s\" failed!", path);
        free(path);
        path = NULL;
      }
      close(cover_fd);

      // now delete all other files, if there are any
      DIR *d;
      struct dirent *dir;
      d = opendir(config.cover_art_cache_dir);
      if (d) {
        int fnl = strlen(prefix) + strlen(img_md5_str) + 1 + strlen(ext) + 1;

        char *full_filename = malloc(fnl);
        if (full_filename == NULL)
          die("Can't allocate memory at metadata_write_image_file.");
        memset(full_filename, 0, fnl);
        snprintf(full_filename, fnl, "%s%s.%s", prefix, img_md5_str, ext);
        int dir_fd = open(config.cover_art_cache_dir, O_DIRECTORY);
        if (dir_fd > 0) {
          while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
              if (strcmp(full_filename, dir->d_name) != 0) {
                if (unlinkat(dir_fd, dir->d_name, 0) != 0) {
                  debug(1, "Error %d deleting cover art file \"%s\".", errno, dir->d_name);
                }
              }
            }
          }
        } else {
          debug(1, "Can't open the directory for deletion.");
        }
        free(full_filename);
        closedir(d);
      }
    } else {
      //      if (errno == EEXIST)
      //        debug(1, "Cover art file \"%s\" already exists!", path);
      //      else {
      if (errno != EEXIST) {
        warn("Could not open file \"%s\" for writing cover art", path);
        free(path);
        path = NULL;
      }
    }
  } else {
    debug(1, "Couldn't access or create the cover art cache directory \"%s\".",
          config.cover_art_cache_dir);
  }
  return path;
}

void metadata_hub_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length) {
  // metadata coming in from the audio source or from Shairport Sync itself passes through here
  // this has more information about tags, which might be relevant:
  // https://code.google.com/p/ytrack/wiki/DMAP

  // all the following items of metadata are contained in one metadata packet
  // they are preseded by an 'ssnc' 'mdst' item and followed by an 'ssnc 'mden' item.

  if (type == 'core') {
    switch (code) {
    case 'mper':
      metadata_store.item_id = ntohl(*(uint32_t*)data);
      debug(2, "MH Item ID set to: \"%u\"", metadata_store.item_id);
      break;
    case 'asal':
      if ((metadata_store.album_name == NULL) ||
          (strncmp(metadata_store.album_name, data, length) != 0)) {
        if (metadata_store.album_name)
          free(metadata_store.album_name);
        metadata_store.album_name = strndup(data, length);
        debug(2, "MH Album name set to: \"%s\"", metadata_store.album_name);
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
        debug(2, "MH Artist name set to: \"%s\"", metadata_store.artist_name);
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
        debug(2, "MH Comment set to: \"%s\"", metadata_store.comment);
        metadata_store.comment_changed = 1;
        metadata_store.changed = 1;
      }
      break;
    case 'asgn':
      if ((metadata_store.genre == NULL) || (strncmp(metadata_store.genre, data, length) != 0)) {
        if (metadata_store.genre)
          free(metadata_store.genre);
        metadata_store.genre = strndup(data, length);
        debug(2, "MH Genre set to: \"%s\"", metadata_store.genre);
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
        debug(2, "MH Track name set to: \"%s\"", metadata_store.track_name);
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
        debug(2, "MH Composer set to: \"%s\"", metadata_store.composer);
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
        debug(2, "MH File Kind set to: \"%s\"", metadata_store.file_kind);
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
        debug(2, "MH File Kind set to: \"%s\"", metadata_store.file_kind);
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
        debug(2, "MH Sort As set to: \"%s\"", metadata_store.sort_as);
        metadata_store.sort_as_changed = 1;
        metadata_store.changed = 1;
      }
      break;

      //   default:
      /*
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
      */
    }
  } else if (type == 'ssnc') {
    switch (code) {

    // ignore the following
    case 'pcst':
    case 'pcen':
      break;

    case 'mdst':
      debug(2, "MH Metadata stream processing start.");
      metadata_hub_modify_prolog();
      metadata_hub_reset_track_metadata();
      metadata_hub_release_track_artwork();
      break;
    case 'mden':
      metadata_hub_modify_epilog(1);
      debug(2, "MH Metadata stream processing end.");
      break;
    case 'PICT':
      if (length > 16) {
        metadata_hub_modify_prolog();
        debug(2, "MH Picture received, length %u bytes.", length);
        if (metadata_store.cover_art_pathname)
          free(metadata_store.cover_art_pathname);
        metadata_store.cover_art_pathname = metadata_write_image_file(data, length);
        metadata_hub_modify_epilog(1);
      }
      break;
    case 'clip':
      if ((metadata_store.client_ip == NULL) ||
          (strncmp(metadata_store.client_ip, data, length) != 0)) {
        metadata_hub_modify_prolog();
        if (metadata_store.client_ip)
          free(metadata_store.client_ip);
        metadata_store.client_ip = strndup(data, length);
        // debug(1, "MH Client IP set to: \"%s\"", metadata_store.client_ip);
        metadata_store.client_ip_changed = 1;
        metadata_store.changed = 1;
        metadata_hub_modify_epilog(1);
      }
      break;
    case 'svip':
      if ((metadata_store.server_ip == NULL) ||
          (strncmp(metadata_store.server_ip, data, length) != 0)) {
        metadata_hub_modify_prolog();
        if (metadata_store.server_ip)
          free(metadata_store.server_ip);
        metadata_store.server_ip = strndup(data, length);
        // debug(1, "MH Server IP set to: \"%s\"", metadata_store.server_ip);
        metadata_hub_modify_epilog(1);
      }
      break;
    case 'pbeg':
    case 'pend':
    case 'pfls':
    case 'prsm':
      break;
    
    default: {
      char typestring[5];
      uint32_t tm = htonl(type);
      memcpy(typestring,&tm,sizeof(uint32_t));
      typestring[4] = 0;
      char codestring[5];
      uint32_t cm = htonl(code);
      memcpy(codestring,&cm,sizeof(uint32_t));
      codestring[4] = 0;
      char *payload;
      if (length < 2048)
        payload = strndup(data, length);
      else
        payload = NULL;
      // debug(1, "MH \"%s\" \"%s\" (%d bytes): \"%s\".", typestring, codestring, length, payload);
      if (payload)
        free(payload);
    }
    }
  }
}
