#pragma once
#include "common.h"
#include "config.h"
#include <pthread.h>

enum play_status_type {
  PS_PLAYING = 0,
  PS_PAUSED,
  PS_STOPPED,
} play_status_type;

enum shuffle_status_type {
  SS_OFF = 0,
  SS_ON,
} shuffle_status_type;

enum repeat_status_type {
  RS_NONE = 0,
  RS_SINGLE,
  RS_ALL,
} repeat_status_type;

typedef struct metadata_bundle {
  int changed;                          // normally 0, nonzero if a field has been changed
  int playerstatusupdates_are_received; // false if it's "traditional" metadata

  enum play_status_type play_status;
  int play_status_changed;

  enum shuffle_status_type shuffle_status;
  int shuffle_status_changed;

  enum repeat_status_type repeat_status;
  int repeat_status_changed;

  char *track_name; // a malloced string -- if non-zero, free it before replacing it
  int track_name_changed;

  char *artist_name; // a malloced string -- if non-zero, free it before replacing it
  int artist_name_changed;

  char *album_name; // a malloced string -- if non-zero, free it before replacing it
  int album_name_changed;

  char *genre; // a malloced string -- if non-zero, free it before replacing it
  int genre_changed;

  uint32_t item_id; // seems to be a track ID -- see itemid in DACP.c
  int item_id_changed;

  unsigned char
      item_composite_id[16]; // seems to be nowplaying 4 ids: dbid, plid, playlistItem, itemid

} metadata_bundle;

struct metadata_bundle metadata;

void metadata_bundle_init(void);
