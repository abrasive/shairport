#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"
#include "metadata_hub.h"

#include "dbus-diagnostics.h"
#include "dbus-diagnostics-interface.h"

ShairportSyncDiagnostics *shairportSyncDiagnosticsSkeleton;

gboolean notify_include_statistics_in_log_callback(ShairportSyncDiagnostics *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  debug(1, "\"notify_include_statistics_in_log_callback\" called.");
  if (shairport_sync_diagnostics_get_include_statistics_in_log(skeleton)) {
    debug(1, ">> start logging statistics");
    config.statistics_requested = 1;
  } else {
    debug(1, ">> stop logging statistics");
    config.statistics_requested = 0;
  }
  return TRUE;
}

gboolean notify_log_verbosity_callback(ShairportSyncDiagnostics *skeleton,
                                            __attribute__((unused)) gpointer user_data) {
  gint th = shairport_sync_diagnostics_get_log_verbosity(skeleton);
  if ((th >= 0) && (th <= 3)) {
    debug(1, "Setting log verbosity to %d.", th);
    debuglev = th;
  } else {
    debug(1, "Invalid log verbosity: %d. Ignored.", th);
  }
  return TRUE;
}

void dbus_diagnostics_on_dbus_name_acquired(GDBusConnection *connection,
                                  __attribute__((unused))  const gchar *name,
                                  __attribute__((unused)) gpointer user_data) {
  debug(1,"dbus_diagnostics_on_dbus_name_acquired");
  shairportSyncDiagnosticsSkeleton = shairport_sync_diagnostics_skeleton_new();
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncDiagnosticsSkeleton), connection,
                                   "/org/gnome/ShairportSync/Diagnostics", NULL);
                                   
  shairport_sync_diagnostics_set_log_verbosity(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton),
                                        debuglev);
                                        
  debug(1,"Log verbosity is %d.",debuglev);

  if (config.statistics_requested == 0) {
    shairport_sync_diagnostics_set_include_statistics_in_log(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
    debug(1, "Statistics Logging is off");
  } else {
    shairport_sync_diagnostics_set_include_statistics_in_log(SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
    debug(1, "Statistics Logging is on");
  }
  
  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::log-verbosity",
                   G_CALLBACK(notify_log_verbosity_callback), NULL);


  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::include-statistics-in-log",
                   G_CALLBACK(notify_include_statistics_in_log_callback), NULL);


}

