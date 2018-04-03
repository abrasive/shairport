#pragma once
#include "common.h"
#include "config.h"
#include <pthread.h>

#define number_of_watchers 2

enum play_status_type {
  PS_NOT_AVAILABLE = 0,
  PS_STOPPED,
  PS_PAUSED,
  PS_PLAYING,
} play_status_type;

enum shuffle_status_type {
  SS_NOT_AVAILABLE = 0,
  SS_OFF,
  SS_ON,
} shuffle_status_type;

enum repeat_status_type {
  RS_NOT_AVAILABLE = 0,
  RS_OFF,
  RS_ONE,
  RS_ALL,
} repeat_status_type;

typedef struct track_metadata_bundle {
  uint32_t item_id;     // seems to be a track ID -- see itemid in DACP.c
  int item_id_received; // important for deciding if the track information should be ignored.
  unsigned char
      item_composite_id[16]; // seems to be nowplaying 4 ids: dbid, plid, playlistItem, itemid
  char *track_name;          // a malloced string -- if non-zero, free it before replacing it
  char *artist_name;         // a malloced string -- if non-zero, free it before replacing it
  char *album_name;          // a malloced string -- if non-zero, free it before replacing it
  char *genre;               // a malloced string -- if non-zero, free it before replacing it
  char *comment;             // a malloced string -- if non-zero, free it before replacing it
  char *composer;            // a malloced string -- if non-zero, free it before replacing it
  char *file_kind;           // a malloced string -- if non-zero, free it before replacing it
  char *song_description;    // a malloced string -- if non-zero, free it before replacing it
  char *song_album_artist;   // a malloced string -- if non-zero, free it before replacing it
  char *sort_as;             // a malloced string -- if non-zero, free it before replacing it
  uint32_t songtime_in_milliseconds;
} track_metadata_bundle;

struct metadata_bundle;

typedef void (*metadata_watcher)(struct metadata_bundle *argc, void *userdata);

typedef struct metadata_bundle {

  char *client_ip; // IP number used by the audio source (i.e. the "client"), which is also the DACP
                   // server
  char *server_ip; // IP number used by Shairport Sync
  int player_thread_active; // true if a play thread is running
  int dacp_server_active; // true if there's a reachable DACP server (assumed to be the Airplay
                          // client) ; false otherwise
  int advanced_dacp_server_active; // true if there's a reachable DACP server with iTunes
                                   // capabilitiues
                                   // ; false otherwise
  enum play_status_type play_status;
  enum shuffle_status_type shuffle_status;
  enum repeat_status_type repeat_status;

  struct track_metadata_bundle *track_metadata;

  char *cover_art_pathname; // if non-zero, it will have been assigned with malloc.

  enum play_status_type
      player_state; // this is the state of the actual player itself, which can be a bit noisy.

  int speaker_volume; // this is the actual speaker volume, allowing for the main volume and the
                      // speaker volume control
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
