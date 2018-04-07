#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"
#include "metadata_hub.h"

#include "dbus-service.h"

ShairportSyncDiagnostics *shairportSyncDiagnosticsSkeleton = NULL;
ShairportSyncRemoteControl *shairportSyncRemoteControlSkeleton = NULL;
ShairportSyncAdvancedRemoteControl *shairportSyncAdvancedRemoteControlSkeleton = NULL;

void dbus_metadata_watcher(struct metadata_bundle *argc, __attribute__((unused)) void *userdata) {
  char response[100];
  const char *th;
  shairport_sync_advanced_remote_control_set_volume(shairportSyncAdvancedRemoteControlSkeleton,
                                                    argc->speaker_volume);

  shairport_sync_remote_control_set_airplay_volume(shairportSyncRemoteControlSkeleton,
                                                   argc->airplay_volume);

  shairport_sync_remote_control_set_server(shairportSyncRemoteControlSkeleton, argc->client_ip);

  if (argc->dacp_server_active) {
    shairport_sync_remote_control_set_available(shairportSyncRemoteControlSkeleton, TRUE);
  } else {
    shairport_sync_remote_control_set_available(shairportSyncRemoteControlSkeleton, FALSE);
  }

  if (argc->advanced_dacp_server_active) {
    shairport_sync_advanced_remote_control_set_available(shairportSyncAdvancedRemoteControlSkeleton,
                                                         TRUE);
  } else {
    shairport_sync_advanced_remote_control_set_available(shairportSyncAdvancedRemoteControlSkeleton,
                                                         FALSE);
  }

  switch (argc->player_state) {
  case PS_NOT_AVAILABLE:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton,
                                                   "Not Available");
    break;
  case PS_STOPPED:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton, "Stopped");
    break;
  case PS_PAUSED:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton, "Paused");
    break;
  case PS_PLAYING:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton, "Playing");
    break;
  default:
    debug(1, "This should never happen.");
  }

  switch (argc->play_status) {
  case PS_NOT_AVAILABLE:
    strcpy(response, "Not Available");
    break;
  case PS_STOPPED:
    strcpy(response, "Stopped");
    break;
  case PS_PAUSED:
    strcpy(response, "Paused");
    break;
  case PS_PLAYING:
    strcpy(response, "Playing");
    break;
  default:
    debug(1, "This should never happen.");
  }

  th = shairport_sync_advanced_remote_control_get_playback_status(
      shairportSyncAdvancedRemoteControlSkeleton);

  // only set this if it's different
  if ((th == NULL) || (strcasecmp(th, response) != 0)) {
    debug(3, "Playback Status should be changed");
    shairport_sync_advanced_remote_control_set_playback_status(
        shairportSyncAdvancedRemoteControlSkeleton, response);
  }

  switch (argc->repeat_status) {
  case RS_NOT_AVAILABLE:
    strcpy(response, "Not Available");
    break;
  case RS_OFF:
    strcpy(response, "Off");
    break;
  case RS_ONE:
    strcpy(response, "One");
    break;
  case RS_ALL:
    strcpy(response, "All");
    break;
  default:
    debug(1, "This should never happen.");
  }
  th = shairport_sync_advanced_remote_control_get_loop_status(
      shairportSyncAdvancedRemoteControlSkeleton);

  // only set this if it's different
  if ((th == NULL) || (strcasecmp(th, response) != 0)) {
    debug(3, "Loop Status should be changed");
    shairport_sync_advanced_remote_control_set_loop_status(
        shairportSyncAdvancedRemoteControlSkeleton, response);
  }

  switch (argc->shuffle_status) {
  case SS_NOT_AVAILABLE:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       FALSE);
    break;
  case SS_OFF:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       FALSE);
    break;
  case SS_ON:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       TRUE);
    break;
  default:
    debug(1, "This should never happen.");
  }

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
  if ((argc->track_metadata) && (argc->track_metadata->item_id)) {
    char trackidstring[128];
    // debug(1, "Set ID using mper ID: \"%u\".",argc->item_id);
    sprintf(trackidstring, "/org/gnome/ShairportSync/mper_%u", argc->track_metadata->item_id);
    GVariant *trackid = g_variant_new("o", trackidstring);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
  }

  // Add the track name if there is one
  if ((argc->track_metadata) && (argc->track_metadata->track_name)) {
    // debug(1, "Track name set to \"%s\".", argc->track_name);
    GVariant *trackname = g_variant_new("s", argc->track_metadata->track_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:title", trackname);
  }

  // Add the album name if there is one
  if ((argc->track_metadata) && (argc->track_metadata->album_name)) {
    // debug(1, "Album name set to \"%s\".", argc->album_name);
    GVariant *albumname = g_variant_new("s", argc->track_metadata->album_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:album", albumname);
  }

  // Add the artists if there are any (actually there will be at most one, but put it in an array)
  if ((argc->track_metadata) && (argc->track_metadata->artist_name)) {
    /* Build the artists array */
    // debug(1,"Build artist array");
    aa = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(aa, "s", argc->track_metadata->artist_name);
    GVariant *artists = g_variant_builder_end(aa);
    g_variant_builder_unref(aa);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:artist", artists);
  }

  // Add the genres if there are any (actually there will be at most one, but put it in an array)
  if ((argc->track_metadata) && (argc->track_metadata->genre)) {
    // debug(1,"Build genre");
    aa = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(aa, "s", argc->track_metadata->genre);
    GVariant *genres = g_variant_builder_end(aa);
    g_variant_builder_unref(aa);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:genre", genres);
  }

  GVariant *dict = g_variant_builder_end(dict_builder);
  g_variant_builder_unref(dict_builder);

  // debug(1,"Set metadata");
  shairport_sync_remote_control_set_metadata(shairportSyncRemoteControlSkeleton, dict);
}

static gboolean on_handle_set_volume(ShairportSyncAdvancedRemoteControl *skeleton,
                                     GDBusMethodInvocation *invocation, const gint volume,
                                     __attribute__((unused)) gpointer user_data) {
  debug(1, "Set volume to %d.", volume);
  dacp_set_volume(volume);
  shairport_sync_advanced_remote_control_complete_set_volume(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_fast_forward(ShairportSyncRemoteControl *skeleton,
                                       GDBusMethodInvocation *invocation,
                                       __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("beginff");
  shairport_sync_remote_control_complete_fast_forward(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_rewind(ShairportSyncRemoteControl *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("beginrew");
  shairport_sync_remote_control_complete_rewind(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_toggle_mute(ShairportSyncRemoteControl *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("mutetoggle");
  shairport_sync_remote_control_complete_toggle_mute(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_next(ShairportSyncRemoteControl *skeleton,
                               GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("nextitem");
  shairport_sync_remote_control_complete_next(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_previous(ShairportSyncRemoteControl *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("previtem");
  shairport_sync_remote_control_complete_previous(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_pause(ShairportSyncRemoteControl *skeleton,
                                GDBusMethodInvocation *invocation,
                                __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("pause");
  shairport_sync_remote_control_complete_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play_pause(ShairportSyncRemoteControl *skeleton,
                                     GDBusMethodInvocation *invocation,
                                     __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("playpause");
  shairport_sync_remote_control_complete_play_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play(ShairportSyncRemoteControl *skeleton,
                               GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("play");
  shairport_sync_remote_control_complete_play(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_stop(ShairportSyncRemoteControl *skeleton,
                               GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("stop");
  shairport_sync_remote_control_complete_stop(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_resume(ShairportSyncRemoteControl *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("playresume");
  shairport_sync_remote_control_complete_resume(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_shuffle_songs(ShairportSyncRemoteControl *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("shuffle_songs");
  shairport_sync_remote_control_complete_shuffle_songs(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_volume_up(ShairportSyncRemoteControl *skeleton,
                                    GDBusMethodInvocation *invocation,
                                    __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("volumeup");
  shairport_sync_remote_control_complete_volume_up(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_volume_down(ShairportSyncRemoteControl *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("volumedown");
  shairport_sync_remote_control_complete_volume_down(skeleton, invocation);
  return TRUE;
}

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
    if (th == 0)
      debug(1, ">> log verbosity set to %d.", th);
    debuglev = th;
    debug(1, ">> log verbosity set to %d.", th);
  } else {
    debug(1, ">> invalid log verbosity: %d. Ignored.", th);
    shairport_sync_diagnostics_set_verbosity(skeleton, debuglev);
  }
  return TRUE;
}

gboolean notify_loudness_filter_active_callback(ShairportSync *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  debug(1, "\"notify_loudness_filter_active_callback\" called.");
  if (shairport_sync_get_loudness_filter_active(skeleton)) {
    debug(1, "activating loudness filter");
    config.loudness = 1;
  } else {
    debug(1, "deactivating loudness filter");
    config.loudness = 0;
  }
  return TRUE;
}

gboolean notify_loudness_threshold_callback(ShairportSync *skeleton,
                                            __attribute__((unused)) gpointer user_data) {
  gdouble th = shairport_sync_get_loudness_threshold(skeleton);
  if ((th <= 0.0) && (th >= -100.0)) {
    debug(1, "Setting loudness threshhold to %f.", th);
    config.loudness_reference_volume_db = th;
  } else {
    debug(1, "Invalid loudness threshhold: %f. Ignored.", th);
    shairport_sync_set_loudness_threshold(skeleton, config.loudness_reference_volume_db);
  }
  return TRUE;
}

gboolean notify_alacdecoder_callback(ShairportSync *skeleton,
                                     __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_alacdecoder(skeleton);
#ifdef HAVE_APPLE_ALAC
  if (strcasecmp(th, "hammerton") == 0)
    config.use_apple_decoder = 0;
  else if (strcasecmp(th, "apple") == 0)
    config.use_apple_decoder = 1;
  else {
    warn("An unrecognised ALAC decoder: \"%s\" was requested via D-Bus interface.", th);
    if (config.use_apple_decoder == 0)
      shairport_sync_set_alacdecoder(skeleton, "hammerton");
    else
      shairport_sync_set_alacdecoder(skeleton, "apple");
  }
// debug(1,"Using the %s ALAC decoder.", ((config.use_apple_decoder==0) ? "Hammerton" : "Apple"));
#else
  if (strcasecmp(th, "hammerton") == 0) {
    config.use_apple_decoder = 0;
    // debug(1,"Using the Hammerton ALAC decoder.");
  } else {
    warn("An unrecognised ALAC decoder: \"%s\" was requested via D-Bus interface. (Possibly "
         "support for this decoder was not compiled "
         "into this version of Shairport Sync.)",
         th);
    shairport_sync_set_alacdecoder(skeleton, "hammerton");
  }
#endif
  return TRUE;
}

gboolean notify_interpolation_callback(ShairportSync *skeleton,
                                       __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_interpolation(skeleton);
#ifdef HAVE_LIBSOXR
  if (strcasecmp(th, "basic") == 0)
    config.packet_stuffing = ST_basic;
  else if (strcasecmp(th, "soxr") == 0)
    config.packet_stuffing = ST_soxr;
  else {
    warn("An unrecognised interpolation method: \"%s\" was requested via the D-Bus interface.", th);
    switch (config.packet_stuffing) {
    case ST_basic:
      shairport_sync_set_interpolation(skeleton, "basic");
      break;
    case ST_soxr:
      shairport_sync_set_interpolation(skeleton, "soxr");
      break;
    default:
      debug(1, "This should never happen!");
      shairport_sync_set_interpolation(skeleton, "basic");
      break;
    }
  }
#else
  if (strcasecmp(th, "basic") == 0)
    config.packet_stuffing = ST_basic;
  else {
    warn("An unrecognised interpolation method: \"%s\" was requested via the D-Bus interface. "
         "(Possibly support for this method was not compiled "
         "into this version of Shairport Sync.)",
         th);
    shairport_sync_set_interpolation(skeleton, "basic");
  }
#endif
  return TRUE;
}

gboolean notify_volume_control_profile_callback(ShairportSync *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_volume_control_profile(skeleton);
  //  enum volume_control_profile_type previous_volume_control_profile =
  //  config.volume_control_profile;
  if (strcasecmp(th, "standard") == 0)
    config.volume_control_profile = VCP_standard;
  else if (strcasecmp(th, "flat") == 0)
    config.volume_control_profile = VCP_flat;
  else {
    warn("Unrecognised Volume Control Profile: \"%s\".", th);
    switch (config.volume_control_profile) {
    case VCP_standard:
      shairport_sync_set_volume_control_profile(skeleton, "standard");
      break;
    case VCP_flat:
      shairport_sync_set_volume_control_profile(skeleton, "flat");
      break;
    default:
      debug(1, "This should never happen!");
      shairport_sync_set_volume_control_profile(skeleton, "standard");
      break;
    }
  }
  return TRUE;
}

gboolean notify_shuffle_callback(ShairportSyncAdvancedRemoteControl *skeleton,
                                 __attribute__((unused)) gpointer user_data) {
  // debug(1,"notify_shuffle_callback called");
  if (shairport_sync_advanced_remote_control_get_shuffle(skeleton))
    send_simple_dacp_command("setproperty?dacp.shufflestate=1");
  else
    send_simple_dacp_command("setproperty?dacp.shufflestate=0");
  return TRUE;
}

gboolean notify_loop_status_callback(ShairportSyncAdvancedRemoteControl *skeleton,
                                     __attribute__((unused)) gpointer user_data) {
  // debug(1,"notify_loop_status_callback called");
  char *th = (char *)shairport_sync_advanced_remote_control_get_loop_status(skeleton);
  //  enum volume_control_profile_type previous_volume_control_profile =
  //  config.volume_control_profile;
  // debug(1, "notify_loop_status_callback called with loop status of \"%s\".", th);
  if (strcasecmp(th, "off") == 0)
    send_simple_dacp_command("setproperty?dacp.repeatstate=0");
  else if (strcasecmp(th, "one") == 0)
    send_simple_dacp_command("setproperty?dacp.repeatstate=1");
  else if (strcasecmp(th, "all") == 0)
    send_simple_dacp_command("setproperty?dacp.repeatstate=2");
  else if (strcasecmp(th, "not available") != 0) {
    warn("Illegal Loop Request: \"%s\".", th);
    switch (metadata_store.repeat_status) {
    case RS_NOT_AVAILABLE:
      shairport_sync_advanced_remote_control_set_loop_status(skeleton, "Not Available");
      break;
    case RS_OFF:
      shairport_sync_advanced_remote_control_set_loop_status(skeleton, "Off");
      break;
    case RS_ONE:
      shairport_sync_advanced_remote_control_set_loop_status(skeleton, "One");
      break;
    case RS_ALL:
      shairport_sync_advanced_remote_control_set_loop_status(skeleton, "All");
      break;
    default:
      debug(1, "This should never happen!");
      shairport_sync_advanced_remote_control_set_loop_status(skeleton, "Off");
      break;
    }
  }
  return TRUE;
}

static gboolean on_handle_remote_command(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                                         const gchar *command,
                                         __attribute__((unused)) gpointer user_data) {
  debug(1, "RemoteCommand with command \"%s\".", command);
  send_simple_dacp_command((const char *)command);
  shairport_sync_complete_remote_command(skeleton, invocation);
  return TRUE;
}

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name,
                                  __attribute__((unused)) gpointer user_data) {

  // debug(1, "Shairport Sync native D-Bus interface \"%s\" acquired on the %s bus.", name,
  // (config.dbus_service_bus_type == DBT_session) ? "session" : "system");

  shairportSyncSkeleton = shairport_sync_skeleton_new();
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncSkeleton), connection,
                                   "/org/gnome/ShairportSync", NULL);

  shairportSyncDiagnosticsSkeleton = shairport_sync_diagnostics_skeleton_new();
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncDiagnosticsSkeleton),
                                   connection, "/org/gnome/ShairportSync", NULL);

  shairportSyncRemoteControlSkeleton = shairport_sync_remote_control_skeleton_new();
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncRemoteControlSkeleton),
                                   connection, "/org/gnome/ShairportSync", NULL);

  shairportSyncAdvancedRemoteControlSkeleton =
      shairport_sync_advanced_remote_control_skeleton_new();

  g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(shairportSyncAdvancedRemoteControlSkeleton), connection,
      "/org/gnome/ShairportSync", NULL);

  g_signal_connect(shairportSyncSkeleton, "notify::interpolation",
                   G_CALLBACK(notify_interpolation_callback), NULL);
  g_signal_connect(shairportSyncSkeleton, "notify::alacdecoder",
                   G_CALLBACK(notify_alacdecoder_callback), NULL);
  g_signal_connect(shairportSyncSkeleton, "notify::volume-control-profile",
                   G_CALLBACK(notify_volume_control_profile_callback), NULL);
  g_signal_connect(shairportSyncSkeleton, "notify::loudness-filter-active",
                   G_CALLBACK(notify_loudness_filter_active_callback), NULL);
  g_signal_connect(shairportSyncSkeleton, "notify::loudness-threshold",
                   G_CALLBACK(notify_loudness_threshold_callback), NULL);

  g_signal_connect(shairportSyncSkeleton, "handle-remote-command",
                   G_CALLBACK(on_handle_remote_command), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::verbosity",
                   G_CALLBACK(notify_verbosity_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::statistics",
                   G_CALLBACK(notify_statistics_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::elapsed-time",
                   G_CALLBACK(notify_elapsed_time_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::delta-time",
                   G_CALLBACK(notify_delta_time_callback), NULL);

  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-fast-forward",
                   G_CALLBACK(on_handle_fast_forward), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-rewind",
                   G_CALLBACK(on_handle_rewind), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-toggle-mute",
                   G_CALLBACK(on_handle_toggle_mute), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-next", G_CALLBACK(on_handle_next),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-previous",
                   G_CALLBACK(on_handle_previous), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-pause", G_CALLBACK(on_handle_pause),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-play-pause",
                   G_CALLBACK(on_handle_play_pause), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-play", G_CALLBACK(on_handle_play),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-stop", G_CALLBACK(on_handle_stop),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-resume",
                   G_CALLBACK(on_handle_resume), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-shuffle-songs",
                   G_CALLBACK(on_handle_shuffle_songs), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-volume-up",
                   G_CALLBACK(on_handle_volume_up), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-volume-down",
                   G_CALLBACK(on_handle_volume_down), NULL);

  g_signal_connect(shairportSyncAdvancedRemoteControlSkeleton, "handle-set-volume",
                   G_CALLBACK(on_handle_set_volume), NULL);

  g_signal_connect(shairportSyncAdvancedRemoteControlSkeleton, "notify::shuffle",
                   G_CALLBACK(notify_shuffle_callback), NULL);

  g_signal_connect(shairportSyncAdvancedRemoteControlSkeleton, "notify::loop-status",
                   G_CALLBACK(notify_loop_status_callback), NULL);

  add_metadata_watcher(dbus_metadata_watcher, NULL);

  shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(shairportSyncSkeleton),
                                        config.loudness_reference_volume_db);

#ifdef HAVE_APPLE_ALAC
  if (config.use_apple_decoder == 0)
    shairport_sync_set_alacdecoder(SHAIRPORT_SYNC(shairportSyncSkeleton), "hammerton");
  else
    shairport_sync_set_alacdecoder(SHAIRPORT_SYNC(shairportSyncSkeleton), "apple");
#else
  shairport_sync_set_alacdecoder(SHAIRPORT_SYNC(shairportSyncSkeleton), "hammerton");
#endif

#ifdef HAVE_SOXR
  if (config.packet_stuffing == ST_basic)
    shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "basic");
  else
    shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "soxr");
#else
  shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "basic");
#endif

  if (config.volume_control_profile == VCP_standard)
    shairport_sync_set_volume_control_profile(SHAIRPORT_SYNC(shairportSyncSkeleton), "standard");
  else
    shairport_sync_set_volume_control_profile(SHAIRPORT_SYNC(shairportSyncSkeleton), "flat");

  if (config.loudness == 0) {
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(shairportSyncSkeleton), FALSE);
  } else {
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(shairportSyncSkeleton), TRUE);
  }

  shairport_sync_set_version(SHAIRPORT_SYNC(shairportSyncSkeleton), PACKAGE_VERSION);
  char *vs = get_version_string();
  shairport_sync_set_version_string(SHAIRPORT_SYNC(shairportSyncSkeleton), vs);
  if (vs)
    free(vs);

  shairport_sync_diagnostics_set_verbosity(
      SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), debuglev);

  // debug(2,">> log verbosity is %d.",debuglev);

  if (config.statistics_requested == 0) {
    shairport_sync_diagnostics_set_statistics(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    // debug(1, ">> statistics logging is off");
  } else {
    shairport_sync_diagnostics_set_statistics(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    // debug(1, ">> statistics logging is on");
  }

  if (config.debugger_show_elapsed_time == 0) {
    shairport_sync_diagnostics_set_elapsed_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    // debug(1, ">> elapsed time is included in log entries");
  } else {
    shairport_sync_diagnostics_set_elapsed_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    // debug(1, ">> elapsed time is not included in log entries");
  }

  if (config.debugger_show_relative_time == 0) {
    shairport_sync_diagnostics_set_delta_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    // debug(1, ">> delta time is included in log entries");
  } else {
    shairport_sync_diagnostics_set_delta_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    // debug(1, ">> delta time is not included in log entries");
  }

  shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton,
                                                 "Not Available");
  shairport_sync_advanced_remote_control_set_playback_status(
      shairportSyncAdvancedRemoteControlSkeleton, "Not Available");

  shairport_sync_advanced_remote_control_set_loop_status(shairportSyncAdvancedRemoteControlSkeleton,
                                                         "Not Available");

  debug(1, "Shairport Sync native D-Bus service started at \"%s\" on the %s bus.", name,
        (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
}

static void on_dbus_name_lost_again(__attribute__((unused)) GDBusConnection *connection,
                                    __attribute__((unused)) const gchar *name,
                                    __attribute__((unused)) gpointer user_data) {
  warn("Could not acquire a Shairport Sync native D-Bus interface \"%s\" on the %s bus.", name,
       (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
}

static void on_dbus_name_lost(__attribute__((unused)) GDBusConnection *connection,
                              __attribute__((unused)) const gchar *name,
                              __attribute__((unused)) gpointer user_data) {
  // debug(1, "Could not acquire a Shairport Sync native D-Bus interface \"%s\" on the %s bus --
  // will try adding the process "
  //         "number to the end of it.",
  //      name, (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  pid_t pid = getpid();
  char interface_name[256] = "";
  sprintf(interface_name, "org.gnome.ShairportSync.i%d", pid);
  GBusType dbus_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.dbus_service_bus_type == DBT_session)
    dbus_bus_type = G_BUS_TYPE_SESSION;
  // debug(1, "Looking for a Shairport Sync native D-Bus interface \"%s\" on the %s bus.",
  // interface_name,(config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  g_bus_own_name(dbus_bus_type, interface_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                 on_dbus_name_acquired, on_dbus_name_lost_again, NULL, NULL);
}

int start_dbus_service() {
  //  shairportSyncSkeleton = NULL;
  GBusType dbus_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.dbus_service_bus_type == DBT_session)
    dbus_bus_type = G_BUS_TYPE_SESSION;
  // debug(1, "Looking for a Shairport Sync native D-Bus interface \"org.gnome.ShairportSync\" on
  // the %s bus.",(config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  g_bus_own_name(dbus_bus_type, "org.gnome.ShairportSync", G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                 on_dbus_name_acquired, on_dbus_name_lost, NULL, NULL);
  return 0; // this is just to quieten a compiler warning
}
