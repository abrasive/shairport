#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"

#include "metadata_hub.h"
#include "mpris-service.h"

void mpris_metadata_watcher(struct metadata_bundle *argc, void *userdata) {
  debug(1, "MPRIS metadata watcher called");
  char response[100];
  switch (argc->repeat_status) {
  case RS_NONE:
    strcpy(response, "None");
    break;
  case RS_SINGLE:
    strcpy(response, "Track");
    break;
  case RS_ALL:
    strcpy(response, "Playlist");
    break;
  }

  // debug(1,"Set loop status to \"%s\"",response);
  media_player2_player_set_loop_status(mprisPlayerPlayerSkeleton, response);

  GVariantBuilder *dict_builder, *aa;
  GVariant *trackname, *albumname, *trackid, *tracklength, *artUrl;

  // Build the Track ID from the 16-byte item_composite_id in hex prefixed by
  // /org/gnome/ShairportSync

  char st[33];
  char *pt = st;
  int it;
  for (it = 0; it < 16; it++) {
    sprintf(pt, "%02X", argc->item_composite_id[it]);
    pt += 2;
  }
  *pt = 0;
  // debug(1, "Item composite ID set to 0x%s.", st);

  char artURIstring[1024];
  char trackidstring[1024];
  sprintf(trackidstring, "/org/gnome/ShairportSync/%s", st);

  trackid = g_variant_new("o", trackidstring);

  // Make up the track name and album name
  trackname = g_variant_new("s", argc->track_name);
  albumname = g_variant_new("s", argc->album_name);

  // Make up the track length in microseconds as an int64

  uint64_t track_length_in_microseconds = argc->songtime_in_milliseconds;

  track_length_in_microseconds *= 1000; // to microseconds in 64-bit precision

  // Make up the track name and album name
  tracklength = g_variant_new("x", track_length_in_microseconds);

  /* Build the artists array */
  // debug(1,"Build artists");
  aa = g_variant_builder_new(G_VARIANT_TYPE("as"));
  g_variant_builder_add(aa, "s", argc->artist_name);
  GVariant *artists = g_variant_builder_end(aa);
  g_variant_builder_unref(aa);

  /* Build the genre array */
  // debug(1,"Build genre");
  aa = g_variant_builder_new(G_VARIANT_TYPE("as"));
  g_variant_builder_add(aa, "s", argc->genre);
  GVariant *genres = g_variant_builder_end(aa);
  g_variant_builder_unref(aa);

  /* Build the metadata array */
  // debug(1,"Build metadata");
  dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
  // Make up the artwork URI if we have one
  if (argc->cover_art_pathname)
    sprintf(artURIstring, "file://%s", argc->cover_art_pathname);
  else
    artURIstring[0] = 0;
  // sprintf(artURIstring,"");
  artUrl = g_variant_new("s", artURIstring);
  g_variant_builder_add(dict_builder, "{sv}", "mpris:artUrl", artUrl);
  g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
  g_variant_builder_add(dict_builder, "{sv}", "mpris:length", tracklength);

  g_variant_builder_add(dict_builder, "{sv}", "xesam:title", trackname);
  g_variant_builder_add(dict_builder, "{sv}", "xesam:album", albumname);
  g_variant_builder_add(dict_builder, "{sv}", "xesam:artist", artists);
  g_variant_builder_add(dict_builder, "{sv}", "xesam:genre", genres);
  GVariant *dict = g_variant_builder_end(dict_builder);
  g_variant_builder_unref(dict_builder);

  media_player2_player_set_metadata(mprisPlayerPlayerSkeleton, dict);
}

static gboolean on_handle_next(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               gpointer user_data) {
  send_simple_dacp_command("nextitem");
  media_player2_player_complete_next(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_previous(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                                   gpointer user_data) {
  send_simple_dacp_command("previtem");
  media_player2_player_complete_previous(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_stop(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               gpointer user_data) {
  send_simple_dacp_command("stop");
  media_player2_player_complete_stop(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_pause(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                                gpointer user_data) {
  send_simple_dacp_command("pause");
  media_player2_player_complete_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play_pause(MediaPlayer2Player *skeleton,
                                     GDBusMethodInvocation *invocation, gpointer user_data) {
  send_simple_dacp_command("playpause");
  media_player2_player_complete_play_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               gpointer user_data) {
  send_simple_dacp_command("play");
  media_player2_player_complete_play(skeleton, invocation);
  return TRUE;
}

static void on_mpris_name_acquired(GDBusConnection *connection, const gchar *name,
                                   gpointer user_data) {

  const char *empty_string_array[] = {NULL};

  debug(1, "MPRIS well-known interface name \"%s\" acquired for %s.", name, config.appName);
  mprisPlayerSkeleton = media_player2_skeleton_new();
  mprisPlayerPlayerSkeleton = media_player2_player_skeleton_new();

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);

  media_player2_set_desktop_entry(mprisPlayerSkeleton, "shairport-sync");
  media_player2_set_identity(mprisPlayerSkeleton, "Shairport Sync");
  media_player2_set_can_quit(mprisPlayerSkeleton, FALSE);
  media_player2_set_can_raise(mprisPlayerSkeleton, FALSE);
  media_player2_set_has_track_list(mprisPlayerSkeleton, FALSE);
  media_player2_set_supported_uri_schemes(mprisPlayerSkeleton, empty_string_array);
  media_player2_set_supported_mime_types(mprisPlayerSkeleton, empty_string_array);

  media_player2_player_set_playback_status(mprisPlayerPlayerSkeleton, "Stopped");
  media_player2_player_set_loop_status(mprisPlayerPlayerSkeleton, "None");
  media_player2_player_set_volume(mprisPlayerPlayerSkeleton, 0.5);
  media_player2_player_set_minimum_rate(mprisPlayerPlayerSkeleton, 1.0);
  media_player2_player_set_maximum_rate(mprisPlayerPlayerSkeleton, 1.0);
  media_player2_player_set_can_go_next(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_go_previous(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_play(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_pause(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_seek(mprisPlayerPlayerSkeleton, FALSE);
  media_player2_player_set_can_control(mprisPlayerPlayerSkeleton, TRUE);

  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-play", G_CALLBACK(on_handle_play), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-pause", G_CALLBACK(on_handle_pause), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-play-pause", G_CALLBACK(on_handle_play_pause),
                   NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-stop", G_CALLBACK(on_handle_stop), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-next", G_CALLBACK(on_handle_next), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-previous", G_CALLBACK(on_handle_previous),
                   NULL);

  add_metadata_watcher(mpris_metadata_watcher, NULL);

  debug(1, "Shairport Sync D-BUS service started on interface \"%s\".", name);

  debug(1, "MPRIS service started on interface \"%s\".", name);
}

static void on_mpris_name_lost_again(GDBusConnection *connection, const gchar *name,
                                     gpointer user_data) {
  warn("Could not acquire an MPRIS interface.");
}

static void on_mpris_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  debug(1, "Could not acquire well-known interface name \"%s\" -- will try adding the process "
           "number to the end of it.",
        name);
  pid_t pid = getpid();
  char interface_name[256] = "";
  sprintf(interface_name, "org.mpris.MediaPlayer2.ShairportSync.i%d", pid);
  GBusType mpris_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.mpris_service_bus_type == DBT_session)
    mpris_bus_type = G_BUS_TYPE_SESSION;
  if (mpris_bus_type == G_BUS_TYPE_SYSTEM)
    debug(1, "Looking for well-known interface name \"%s\" on the system bus.", interface_name);
  else
    debug(1, "Looking for well-known interface name \"%s\" on the session bus.", interface_name);
  g_bus_own_name(mpris_bus_type, interface_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                 on_mpris_name_acquired, on_mpris_name_lost_again, NULL, NULL);
}

int start_mpris_service() {
  mprisPlayerSkeleton = NULL;
  mprisPlayerPlayerSkeleton = NULL;
  GBusType mpris_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.mpris_service_bus_type == DBT_session)
    mpris_bus_type = G_BUS_TYPE_SESSION;
  debug(1, "Looking for well-known name \"org.mpris.MediaPlayer2.ShairportSync\".");
  g_bus_own_name(mpris_bus_type, "org.mpris.MediaPlayer2.ShairportSync",
                 G_BUS_NAME_OWNER_FLAGS_NONE, NULL, on_mpris_name_acquired, on_mpris_name_lost,
                 NULL, NULL);
  return 0; // this is just to quieten a compiler warning
}
