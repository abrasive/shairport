#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"
#include "metadata_hub.h"

#include "dbus-basic-remote-control.h"
#include "dbus-basic-remote-control-interface.h"

ShairportSyncBasicRemoteControl *shairportSyncBasicRemoteControlSkeleton;


void dbus_basic_remote_control_metadata_watcher(struct metadata_bundle *argc, __attribute__((unused)) void *userdata) {
  // debug(1, "DBUS basic remote control watcher called");
  
  shairport_sync_basic_remote_control_set_airplay_volume(shairportSyncBasicRemoteControlSkeleton, argc->airplay_volume);
  
  if (argc->dacp_server_active)
    shairport_sync_basic_remote_control_set_server(shairportSyncBasicRemoteControlSkeleton, argc->client_ip);
  else
    shairport_sync_basic_remote_control_set_server(shairportSyncBasicRemoteControlSkeleton, "");   
  
    GVariantBuilder *dict_builder, *aa;

  /* Build the metadata array */
  // debug(1,"Build metadata");
  dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

  // Make up the artwork URI if we have one
  if (argc->cover_art_pathname) {
    char artURIstring[1024];
    sprintf(artURIstring, "file://%s", argc->cover_art_pathname);
    // sprintf(artURIstring,"");
    // debug(1,"artURI String: \"%s\".",artURIstring);
    GVariant *artUrl = g_variant_new("s", artURIstring);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:artUrl", artUrl);
  }

  // Add the TrackID if we have one
  if (argc->item_id) {
    char trackidstring[128];
    // debug(1, "Set ID using mper ID: \"%u\".",argc->item_id);
    sprintf(trackidstring, "/org/gnome/ShairportSync/mper_%u", argc->item_id);
    GVariant *trackid = g_variant_new("o", trackidstring);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
  }
  
  // Add the track name if there is one
  if (argc->track_name) {
    // debug(1, "Track name set to \"%s\".", argc->track_name);
    GVariant *trackname = g_variant_new("s", argc->track_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:title", trackname);
  }

  // Add the album name if there is one
  if (argc->album_name) {
    // debug(1, "Album name set to \"%s\".", argc->album_name);
    GVariant *albumname = g_variant_new("s", argc->album_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:album", albumname);
  }

  // Add the artists if there are any (actually there will be at most one, but put it in an array)
  if (argc->artist_name) {
    /* Build the artists array */
    // debug(1,"Build artist array");
    aa = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(aa, "s", argc->artist_name);
    GVariant *artists = g_variant_builder_end(aa);
    g_variant_builder_unref(aa);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:artist", artists);
  }

  // Add the genres if there are any (actually there will be at most one, but put it in an array)
  if (argc->genre) {
    // debug(1,"Build genre");
    aa = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(aa, "s", argc->genre);
    GVariant *genres = g_variant_builder_end(aa);
    g_variant_builder_unref(aa);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:genre", genres);
  }

  GVariant *dict = g_variant_builder_end(dict_builder);
  g_variant_builder_unref(dict_builder);

  // debug(1,"Set metadata");
  shairport_sync_basic_remote_control_set_metadata(shairportSyncBasicRemoteControlSkeleton, dict);
}



static gboolean on_handle_fast_forward(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("beginff");
  shairport_sync_basic_remote_control_complete_fast_forward(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_rewind(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("beginrew");
  shairport_sync_basic_remote_control_complete_rewind(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_toggle_mute(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("mutetoggle");
  shairport_sync_basic_remote_control_complete_toggle_mute(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_next(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("nextitem");
  shairport_sync_basic_remote_control_complete_next(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_previous(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("previtem");
  shairport_sync_basic_remote_control_complete_previous(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_pause(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("pause");
  shairport_sync_basic_remote_control_complete_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play_pause(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("playpause");
  shairport_sync_basic_remote_control_complete_play_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("play");
  shairport_sync_basic_remote_control_complete_play(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_stop(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("stop");
  shairport_sync_basic_remote_control_complete_stop(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_resume(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("playresume");
  shairport_sync_basic_remote_control_complete_resume(skeleton, invocation);
  return TRUE;
}


static gboolean on_handle_shuffle_songs(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("shuffle_songs");
  shairport_sync_basic_remote_control_complete_shuffle_songs(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_volume_up(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("volumeup");
  shairport_sync_basic_remote_control_complete_volume_up(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_volume_down(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("volumedown");
  shairport_sync_basic_remote_control_complete_volume_down(skeleton, invocation);
  return TRUE;
}


/*
gboolean notify_elapsed_time_callback(ShairportSyncDiagnostics *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_elapsed_time_callback\" called.");
  if (shairport_sync_diagnostics_get_elapsed_time(skeleton)) {
    config.debugger_show_elapsed_time = 1;
    debug(1, ">> start including elapsed time in logs");
  } else {
    config.debugger_show_elapsed_time = 0;
    debug(1, ">> stop including elapsed time in logs");
  }
  return TRUE;
}

gboolean notify_delta_time_callback(ShairportSyncDiagnostics *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_delta_time_callback\" called.");
  if (shairport_sync_diagnostics_get_delta_time(skeleton)) {
    config.debugger_show_relative_time = 1;
    debug(1, ">> start including delta time in logs");
  } else {
    config.debugger_show_relative_time = 0;
    debug(1, ">> stop including delta time in logs");
  }
  return TRUE;
}

gboolean notify_statistics_callback(ShairportSyncDiagnostics *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_statistics_callback\" called.");
  if (shairport_sync_diagnostics_get_statistics(skeleton)) {
    debug(1, ">> start logging statistics");
    config.statistics_requested = 1;
  } else {
    debug(1, ">> stop logging statistics");
    config.statistics_requested = 0;
  }
  return TRUE;
}

gboolean notify_verbosity_callback(ShairportSyncDiagnostics *skeleton,
                                            __attribute__((unused)) gpointer user_data) {
  gint th = shairport_sync_diagnostics_get_verbosity(skeleton);
  if ((th >= 0) && (th <= 3)) {
    if (th==0)
      debug(1, ">> log verbosity set to %d.", th);
    debuglev = th;
    debug(1, ">> log verbosity set to %d.", th);
  } else {
    debug(1, ">> invalid log verbosity: %d. Ignored.", th);
  }
  return TRUE;
}
*/

void dbus_basic_remote_control_on_dbus_name_acquired(GDBusConnection *connection,
                                  __attribute__((unused))  const gchar *name,
                                  __attribute__((unused)) gpointer user_data) {
  debug(1,"dbus_basic_remote_control_on_dbus_name_acquired");
  shairportSyncBasicRemoteControlSkeleton = shairport_sync_basic_remote_control_skeleton_new();
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncBasicRemoteControlSkeleton), connection,
                                   "/org/gnome/ShairportSync", NULL);
  
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-fast-forward", G_CALLBACK(on_handle_fast_forward), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-rewind", G_CALLBACK(on_handle_rewind), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-toggle-mute", G_CALLBACK(on_handle_toggle_mute), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-next", G_CALLBACK(on_handle_next), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-previous", G_CALLBACK(on_handle_previous), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-pause", G_CALLBACK(on_handle_pause), NULL);
  g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-play-pause", G_CALLBACK(on_handle_play_pause), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-play", G_CALLBACK(on_handle_play), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-stop", G_CALLBACK(on_handle_stop), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-resume", G_CALLBACK(on_handle_resume), NULL);
 g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-shuffle-songs", G_CALLBACK(on_handle_shuffle_songs), NULL);
  g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-volume-up", G_CALLBACK(on_handle_volume_up), NULL);
  g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-volume-down", G_CALLBACK(on_handle_volume_down), NULL);
 
 add_metadata_watcher(dbus_basic_remote_control_metadata_watcher, NULL);

  /*                                 
  shairport_sync_diagnostics_set_verbosity(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton),
                                        debuglev);
                                        
  // debug(2,">> log verbosity is %d.",debuglev);

  if (config.statistics_requested == 0) {
    shairport_sync_diagnostics_set_statistics(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    // debug(1, ">> statistics logging is off");
  } else {
    shairport_sync_diagnostics_set_statistics(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    // debug(1, ">> statistics logging is on");
  }
  
  if (config.debugger_show_elapsed_time == 0) {
    shairport_sync_diagnostics_set_elapsed_time(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    // debug(1, ">> elapsed time is included in log entries");
  } else {
    shairport_sync_diagnostics_set_elapsed_time(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    // debug(1, ">> elapsed time is not included in log entries");
  }

  if (config.debugger_show_relative_time == 0) {
    shairport_sync_diagnostics_set_delta_time(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    // debug(1, ">> delta time is included in log entries");
  } else {
    shairport_sync_diagnostics_set_delta_time(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    // debug(1, ">> delta time is not included in log entries");
  }

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::verbosity",
                   G_CALLBACK(notify_verbosity_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::statistics",
                   G_CALLBACK(notify_statistics_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::elapsed-time",
                   G_CALLBACK(notify_elapsed_time_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::delta-time",
                   G_CALLBACK(notify_delta_time_callback), NULL);
 */
}

