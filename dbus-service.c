#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"
#include "metadata_hub.h"

#include "dbus-service.h"

#ifdef HAVE_DBUS_DIAGNOSTICS
  #include "dbus-diagnostics.h"
#endif

#ifdef HAVE_DBUS_BASIC_REMOTE_CONTROL
  #include "dbus-basic-remote-control.h"
#endif

void dbus_metadata_watcher(struct metadata_bundle *argc, __attribute__((unused)) void *userdata) {
  // debug(1, "DBUS metadata watcher called");
  shairport_sync_set_volume(shairportSyncSkeleton, argc->speaker_volume);
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
  }
  return TRUE;
}

static gboolean on_handle_set_volume(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                                     const gint volume,
                                     __attribute__((unused)) gpointer user_data) {
  // debug(1, "1 Set volume to d.", volume);
  dacp_set_volume(volume);
  shairport_sync_complete_set_volume(skeleton, invocation);
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

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {

  // debug(1, "Shairport Sync native D-Bus interface \"%s\" acquired on the %s bus.", name,
  // (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  shairportSyncSkeleton = shairport_sync_skeleton_new();

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncSkeleton), connection,
                                   "/org/gnome/ShairportSync", NULL);

  shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(shairportSyncSkeleton),
                                        config.loudness_reference_volume_db);
  debug(1, "Loudness threshold is %f.", config.loudness_reference_volume_db);

  if (config.loudness == 0) {
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(shairportSyncSkeleton), FALSE);
    debug(1, "Loudness is off");
  } else {
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(shairportSyncSkeleton), TRUE);
    debug(1, "Loudness is on");
  }

  g_signal_connect(shairportSyncSkeleton, "notify::loudness-filter-active",
                   G_CALLBACK(notify_loudness_filter_active_callback), NULL);
  g_signal_connect(shairportSyncSkeleton, "notify::loudness-threshold",
                   G_CALLBACK(notify_loudness_threshold_callback), NULL);
  g_signal_connect(shairportSyncSkeleton, "handle-remote-command",
                   G_CALLBACK(on_handle_remote_command), NULL);
  g_signal_connect(shairportSyncSkeleton, "handle-set-volume", G_CALLBACK(on_handle_set_volume),
                   NULL);

  add_metadata_watcher(dbus_metadata_watcher, NULL);
  
#ifdef HAVE_DBUS_DIAGNOSTICS
  dbus_diagnostics_on_dbus_name_acquired(connection,name,user_data);
#endif

#ifdef HAVE_DBUS_BASIC_REMOTE_CONTROL
  dbus_basic_remote_control_on_dbus_name_acquired(connection,name,user_data);
#endif

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
