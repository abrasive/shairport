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

/*
static gboolean on_handle_shuffle_songs(ShairportSyncBasicRemoteControl *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("shuffle_songs");
  shairport_sync_basic_remote_control_complete_shuffle_songs(skeleton, invocation);
  return TRUE;
}
*/
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
// g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-shuffle-songs", G_CALLBACK(on_handle_shuffle_songs), NULL);
  g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-volume-up", G_CALLBACK(on_handle_volume_up), NULL);
  g_signal_connect(shairportSyncBasicRemoteControlSkeleton, "handle-volume-down", G_CALLBACK(on_handle_volume_down), NULL);

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

