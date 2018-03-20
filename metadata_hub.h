#pragma once
#include "common.h"
#include "config.h"
#include <pthread.h>

#define number_of_watchers 2

enum play_status_type {
  PS_STOPPED = 0,
  PS_PAUSED,
  PS_PLAYING,
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

struct metadata_bundle;

typedef void (*metadata_watcher)(struct metadata_bundle *argc, void *userdata);

typedef struct metadata_bundle {

  int dacp_server_active; // true if there's a reachable DACP server (assumed to be the Airplay
                          // client) ; false otherwise

  int changed;                          // normally 0, nonzero if a field has been changed
  int playerstatusupdates_are_received; // false if it's "traditional" metadata

  int player_thread_active; // true if there is a player threrad; false otherwise

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

  char *comment; // a malloced string -- if non-zero, free it before replacing it
  int comment_changed;

  char *composer; // a malloced string -- if non-zero, free it before replacing it
  int composer_changed;

  char *file_kind; // a malloced string -- if non-zero, free it before replacing it
  int file_kind_changed;

  char *sort_as; // a malloced string -- if non-zero, free it before replacing it
  int sort_as_changed;

  char *client_ip; // IP number used by the audio source (i.e. the "client"), which is also the DACP
                   // server
  int client_ip_changed;

  char *server_ip; // IP number used by Shairport Sync

  uint32_t item_id; // seems to be a track ID -- see itemid in DACP.c
  int item_id_changed;

  uint32_t songtime_in_milliseconds;

  unsigned char
      item_composite_id[16]; // seems to be nowplaying 4 ids: dbid, plid, playlistItem, itemid

  char *cover_art_pathname; // if non-zero, it will have been assigned with malloc.

  //

  enum play_status_type
      player_state; // this is the state of the actual player itself, which can be a bit noisy.

  int speaker_volume; // this is the actual speaker volume, allowing for the main volume and the
                      // speaker volume control
  // int previous_speaker_volume; // this is needed to prevent a loop

  int airplay_volume;

  metadata_watcher watchers[number_of_watchers]; // functions to call if the metadata is changed.
  void *watchers_data[number_of_watchers];       // their individual data

} metadata_bundle;

struct metadata_bundle metadata_store;

void add_metadata_watcher(metadata_watcher fn, void *userdata);

void metadata_hub_init(void);
void metadata_hub_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length);
void metadata_hub_reset_track_metadata(void);
void metadata_hub_release_track_artwork(void);

// these functions lock and unlock the read-write mutex on the metadata hub and run the watchers
// afterwards
void metadata_hub_modify_prolog(void);
void metadata_hub_modify_epilog(int modified); // set to true if modifications occured, 0 otherwise

// these are for safe reading
void metadata_hub_read_prolog(void);
void metadata_hub_read_epilog(void);
