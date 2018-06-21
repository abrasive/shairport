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
struct track_metadata_bundle *track_metadata; // used for a temporary track metadata store

void release_char_string(char **str) {
  if (*str) {
    free(*str);
    *str = NULL;
  }
}

void metadata_hub_release_track_metadata(struct track_metadata_bundle *track_metadata) {
  // debug(1,"release track metadata");
  if (track_metadata) {
    release_char_string(&track_metadata->track_name);
    release_char_string(&track_metadata->artist_name);
    release_char_string(&track_metadata->album_artist_name);
    release_char_string(&track_metadata->album_name);
    release_char_string(&track_metadata->genre);
    release_char_string(&track_metadata->comment);
    release_char_string(&track_metadata->composer);
    release_char_string(&track_metadata->file_kind);
    release_char_string(&track_metadata->song_description);
    release_char_string(&track_metadata->song_album_artist);
    release_char_string(&track_metadata->sort_name);
    release_char_string(&track_metadata->sort_artist);
    release_char_string(&track_metadata->sort_album);
    release_char_string(&track_metadata->sort_composer);
    free((char *)track_metadata);
  } else {
    debug(3, "Asked to release non-existent track metadata");
  }
}

void metadata_hub_init(void) {
  // debug(1, "Metadata bundle initialisation.");
  memset(&metadata_store, 0, sizeof(metadata_store));
  track_metadata = NULL;
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
  if (pthread_rwlock_trywrlock(&metadata_hub_re_lock) != 0) {
    debug(2, "Metadata_hub write lock is already taken -- must wait.");
    pthread_rwlock_wrlock(&metadata_hub_re_lock);
    debug(2, "Okay -- acquired the metadata_hub write lock.");
  }
}

void metadata_hub_release_track_artwork(void) {
  // debug(1,"release track artwork");
  release_char_string(&metadata_store.cover_art_pathname);
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

  // Here, we check to see if the dacp_server is transitioning between active and inactive
  // If it's going off, we will release track metadata and image stuff
  // If it's already off, we do nothing
  // If it's transitioning to on, we will record it for use later.

  int m = 0;
  int tm = modified;

  if ((metadata_store.dacp_server_active == 0) &&
      (metadata_store.dacp_server_has_been_active != 0)) {
    debug(1, "dacp_scanner going inactive -- release track metadata and artwork");
    if (metadata_store.track_metadata) {
      m = 1;
      metadata_hub_release_track_metadata(metadata_store.track_metadata);
      metadata_store.track_metadata = NULL;
    }
    if (metadata_store.cover_art_pathname) {
      m = 1;
      metadata_hub_release_track_artwork();
    }
    if (m)
      debug(2, "Release track metadata after dacp server goes inactive.");
    tm += m;
  }
  metadata_store.dacp_server_has_been_active =
      metadata_store.dacp_server_active; // set the scanner_has_been_active now.
  pthread_rwlock_unlock(&metadata_hub_re_lock);
  if (tm) {
    run_metadata_watchers();
  }
}

void metadata_hub_read_prolog(void) {
  // always run this before reading an entry or a sequence of entries in the metadata_hub
  // debug(1, "locking metadata hub for reading");
  if (pthread_rwlock_tryrdlock(&metadata_hub_re_lock) != 0) {
    debug(1, "Metadata_hub read lock is already taken -- must wait.");
    pthread_rwlock_rdlock(&metadata_hub_re_lock);
    debug(1, "Okay -- acquired the metadata_hub read lock.");
  }
}

void metadata_hub_read_epilog(void) {
  // always run this after reading an entry or a sequence of entries in the metadata_hub
  // debug(1, "unlocking metadata hub for reading");
  pthread_rwlock_unlock(&metadata_hub_re_lock);
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
  mbedtls_md5_update(&tctx, (const unsigned char *)buf, len);
  mbedtls_md5_finish(&tctx, img_md5);
#endif

#ifdef HAVE_LIBPOLARSSL
  md5_context tctx;
  md5_starts(&tctx);
  md5_update(&tctx, (const unsigned char *)buf, len);
  md5_finish(&tctx, img_md5);
#endif

  char img_md5_str[33];
  memset(img_md5_str, 0, sizeof(img_md5_str));
  char *ext;
  char png[] = "png";
  char jpg[] = "jpg";
  int i;
  for (i = 0; i < 16; i++)
    snprintf(&img_md5_str[i * 2], 3, "%02x", (uint8_t)img_md5[i]);
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
      if (track_metadata) {
        track_metadata->item_id = ntohl(*(uint32_t *)data);
        track_metadata->item_id_received = 1;
        debug(2, "MH Item ID set to: \"%u\"", track_metadata->item_id);
      } else {
        debug(1, "No track metadata memory allocated when item id received!");
      }
      break;
    case 'asal':
      if (track_metadata) {
        track_metadata->album_name = strndup(data, length);
        debug(2, "MH Album name set to: \"%s\"", track_metadata->album_name);
      } else {
        debug(1, "No track metadata memory allocated when album name received!");
      }
      break;
    case 'asar':
      if (track_metadata) {
        track_metadata->artist_name = strndup(data, length);
        debug(2, "MH Artist name set to: \"%s\"", track_metadata->artist_name);
      } else {
        debug(1, "No track metadata memory allocated when artist name received!");
      }
      break;
    case 'assl':
      if (track_metadata) {
        track_metadata->album_artist_name = strndup(data, length);
        debug(2, "MH Album Artist name set to: \"%s\"", track_metadata->album_artist_name);
      } else {
        debug(1, "No track metadata memory allocated when album artist name received!");
      }
      break;
    case 'ascm':
      if (track_metadata) {
        track_metadata->comment = strndup(data, length);
        debug(2, "MH Comment set to: \"%s\"", track_metadata->comment);
      } else {
        debug(1, "No track metadata memory allocated when comment received!");
      }
      break;
    case 'asgn':
      if (track_metadata) {
        track_metadata->genre = strndup(data, length);
        debug(2, "MH Genre set to: \"%s\"", track_metadata->genre);
      } else {
        debug(1, "No track metadata memory allocated when genre received!");
      }
      break;
    case 'minm':
      if (track_metadata) {
        track_metadata->track_name = strndup(data, length);
        debug(2, "MH Track name set to: \"%s\"", track_metadata->track_name);
      } else {
        debug(1, "No track metadata memory allocated when track name received!");
      }
      break;
    case 'ascp':
      if (track_metadata) {
        track_metadata->composer = strndup(data, length);
        debug(2, "MH Composer set to: \"%s\"", track_metadata->composer);
      } else {
        debug(1, "No track metadata memory allocated when track name received!");
      }
      break;
    case 'asdt':
      if (track_metadata) {
        track_metadata->song_description = strndup(data, length);
        debug(2, "MH Song Description set to: \"%s\"", track_metadata->song_description);
      } else {
        debug(1, "No track metadata memory allocated when song description received!");
      }
      break;
    case 'asaa':
      if (track_metadata) {
        track_metadata->song_album_artist = strndup(data, length);
        debug(2, "MH Song Album Artist set to: \"%s\"", track_metadata->song_album_artist);
      } else {
        debug(1, "No track metadata memory allocated when song artist received!");
      }
      break;
    case 'assn':
      if (track_metadata) {
        track_metadata->sort_name = strndup(data, length);
        debug(2, "MH Sort Name set to: \"%s\"", track_metadata->sort_name);
      } else {
        debug(1, "No track metadata memory allocated when sort name description received!");
      }
      break;
    case 'assa':
      if (track_metadata) {
        track_metadata->sort_artist = strndup(data, length);
        debug(2, "MH Sort Artist set to: \"%s\"", track_metadata->sort_artist);
      } else {
        debug(1, "No track metadata memory allocated when sort artist description received!");
      }
      break;
    case 'assu':
      if (track_metadata) {
        track_metadata->sort_album = strndup(data, length);
        debug(2, "MH Sort Album set to: \"%s\"", track_metadata->sort_album);
      } else {
        debug(1, "No track metadata memory allocated when sort album description received!");
      }
      break;
    case 'assc':
      if (track_metadata) {
        track_metadata->sort_composer = strndup(data, length);
        debug(2, "MH Sort Composer set to: \"%s\"", track_metadata->sort_composer);
      } else {
        debug(1, "No track metadata memory allocated when sort composer description received!");
      }
      break;

    default:
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
            debug(1, "MH \"%s\" \"%s\" (%d bytes): \"%s\".", typestring, codestring, length,
         payload);
            if (payload)
              free(payload);
          }
      */
      break;
    }
  } else if (type == 'ssnc') {
    switch (code) {

    // ignore the following
    case 'pcst':
    case 'pcen':
      break;

    case 'mdst':
      debug(2, "MH Metadata stream processing start.");
      if (track_metadata) {
        debug(1, "This track metadata bundle still seems to exist -- releasing it");
        metadata_hub_release_track_metadata(track_metadata);
      }
      track_metadata = (struct track_metadata_bundle *)malloc(sizeof(struct track_metadata_bundle));
      if (track_metadata == NULL)
        die("Could not allocate memory for track metadata.");
      memset(track_metadata, 0, sizeof(struct track_metadata_bundle)); // now we have a valid track
                                                                       // metadata space, but the
                                                                       // metadata itself
      // might turin out to be invalid. Specifically, YouTube on iOS can generate a sequence of
      // track metadata that is invalid.
      // it is distinguished by having an item_id of zero.
      break;
    case 'mden':
      if (track_metadata) {
        if ((track_metadata->item_id_received == 0) || (track_metadata->item_id != 0)) {
          // i.e. it's only invalid if it has definitely been given an item_id of zero
          metadata_hub_modify_prolog();
          metadata_hub_release_track_metadata(metadata_store.track_metadata);
          metadata_store.track_metadata = track_metadata;
          track_metadata = NULL;
          metadata_hub_modify_epilog(1);
        } else {
          debug(1, "The track information received is invalid -- dropping it");
          metadata_hub_release_track_metadata(track_metadata);
          track_metadata = NULL;
        }
      }
      debug(2, "MH Metadata stream processing end.");
      break;
    case 'PICT':
      if (length > 16) {
        metadata_hub_modify_prolog();
        debug(2, "MH Picture received, length %u bytes.", length);
        release_char_string(&metadata_store.cover_art_pathname);
        metadata_store.cover_art_pathname = metadata_write_image_file(data, length);
        metadata_hub_modify_epilog(1);
      }
      break;
    /*
    case 'clip':
      if ((metadata_store.client_ip == NULL) ||
          (strncmp(metadata_store.client_ip, data, length) != 0)) {
        metadata_hub_modify_prolog();
        if (metadata_store.client_ip)
          free(metadata_store.client_ip);
        metadata_store.client_ip = strndup(data, length);
        debug(1, "MH Client IP set to: \"%s\"", metadata_store.client_ip);
        metadata_store.client_ip_changed = 1;
        metadata_store.changed = 1;
        metadata_hub_modify_epilog(1);
      }
      break;
    */
    case 'prgr':
      if ((metadata_store.progress_string == NULL) ||
          (strncmp(metadata_store.progress_string, data, length) != 0)) {
        metadata_hub_modify_prolog();
        release_char_string(&metadata_store.progress_string);
        metadata_store.progress_string = strndup(data, length);
        debug(1, "MH Progress String set to: \"%s\"", metadata_store.progress_string);
        metadata_hub_modify_epilog(1);
      }
      break;
    case 'svip':
      if ((metadata_store.server_ip == NULL) ||
          (strncmp(metadata_store.server_ip, data, length) != 0)) {
        metadata_hub_modify_prolog();
        release_char_string(&metadata_store.server_ip);
        metadata_store.server_ip = strndup(data, length);
        // debug(1, "MH Server IP set to: \"%s\"", metadata_store.server_ip);
        metadata_hub_modify_epilog(1);
      }
      break;
    // these could tell us about play / pause etc. but will only occur if metadata is enabled, so
    // we'll just ignore them
    case 'pbeg': {
      metadata_hub_modify_prolog();
      int changed = (metadata_store.player_state != PS_PLAYING);
      metadata_store.player_state = PS_PLAYING;
      metadata_store.player_thread_active = 1;
      metadata_hub_modify_epilog(changed);
    } break;
    case 'pend': {
      metadata_hub_modify_prolog();
      metadata_store.player_thread_active = 0;
      metadata_store.player_state = PS_STOPPED;
      metadata_hub_modify_epilog(1);
    } break;
    case 'pfls': {
      metadata_hub_modify_prolog();
      int changed = (metadata_store.player_state != PS_PAUSED);
      metadata_store.player_state = PS_PAUSED;
      metadata_hub_modify_epilog(changed);
    } break;
    case 'pffr': // this is sent when the first frame has been received
    case 'prsm': {
      metadata_hub_modify_prolog();
      int changed = (metadata_store.player_state != PS_PLAYING);
      metadata_store.player_state = PS_PLAYING;
      metadata_hub_modify_epilog(changed);
    } break;
    case 'pvol': {
      // Note: it's assumed that the config.airplay volume has already been correctly set.
      int modified = 0;
      int32_t actual_volume;
      int gv = dacp_get_volume(&actual_volume);
      metadata_hub_modify_prolog();
      if ((gv == 200) && (metadata_store.speaker_volume != actual_volume)) {
        metadata_store.speaker_volume = actual_volume;
        modified = 1;
      }
      if (metadata_store.airplay_volume != config.airplay_volume) {
        metadata_store.airplay_volume = config.airplay_volume;
        modified = 1;
      }
      metadata_hub_modify_epilog(modified); // change
    } break;

    default: {
      char typestring[5];
      uint32_t tm = htonl(type);
      memcpy(typestring, &tm, sizeof(uint32_t));
      typestring[4] = 0;
      char codestring[5];
      uint32_t cm = htonl(code);
      memcpy(codestring, &cm, sizeof(uint32_t));
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
