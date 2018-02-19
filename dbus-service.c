#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"

#include "dbus-service.h"

gboolean notify_loudness_filter_active_callback(ShairportSync *skeleton, gpointer user_data) {
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

gboolean notify_loudness_threshold_callback(ShairportSync *skeleton, gpointer user_data) {
  gdouble th = shairport_sync_get_loudness_threshold(skeleton);
  if ((th <= 0.0) && (th >= -100.0)) {
    debug(1, "Setting loudness threshhold to %f.", th);
    config.loudness_reference_volume_db = th;
  } else {
    debug(1, "Invalid loudness threshhold: %f. Ignored.", th);
  }
  return TRUE;
}

gboolean notify_volume_callback(ShairportSync *skeleton, gpointer user_data) {
  gint vo = shairport_sync_get_volume(skeleton);
  if ((vo >= 0) && (vo <= 100)) {
    if (playing_conn) {
      if (vo !=
          playing_conn
              ->dacp_volume) { // this is to stop an infinite loop of setting->checking->setting...
        // debug(1, "Remote-setting volume to %d.", vo);
        // get the information we need -- the absolute volume, the speaker list, our ID
        struct dacp_speaker_stuff speaker_info[50];
        int32_t overall_volume;
        int http_response = dacp_get_client_volume(&overall_volume);
        int speaker_count;
        http_response = dacp_get_speaker_list((dacp_spkr_stuff *)&speaker_info, 50, &speaker_count);

        // get our machine number
        uint16_t *hn = (uint16_t *)config.hw_addr;
        uint32_t *ln = (uint32_t *)(config.hw_addr + 2);
        uint64_t t1 = ntohs(*hn);
        uint64_t t2 = ntohl(*ln);
        int64_t machine_number = (t1 << 32) + t2; // this form is useful

        // Let's find our own speaker in the array and pick up its relative volume
        int i;
        int32_t relative_volume = 0;
        int32_t active_speakers = 0;
        for (i = 0; i < speaker_count; i++) {
          if (speaker_info[i].speaker_number == machine_number) {
            // debug(1,"Our speaker number found: %ld.",machine_number);
            relative_volume = speaker_info[i].volume;
          }
          if (speaker_info[i].active == 1) {
            active_speakers++;
          }
        }

        if (active_speakers == 1) {
          // must be just this speaker
          dacp_set_include_speaker_volume(machine_number, vo);
        } else if (active_speakers == 0) {
          debug(1, "No speakers!");
        } else {
          // debug(1, "Speakers: %d, active: %d",speaker_count,active_speakers);
          if (vo >= overall_volume) {
            // debug(1,"Multiple speakers active, but desired new volume is highest");
            dacp_set_include_speaker_volume(machine_number, vo);
          } else {
            // the desired volume is less than the current overall volume and there is more than one
            // speaker
            // we must find out the highest other speaker volume.
            // If the desired volume is less than it, we must set the current_overall volume to that
            // highest volume
            // and set our volume relative to it.
            // If the desired volume is greater than the highest current volume, then we can just go
            // ahead
            // with dacp_set_include_speaker_volume, setting the new current overall volume to the
            // desired new level
            // with the speaker at 100%

            int32_t highest_other_volume = 0;
            for (i = 0; i < speaker_count; i++) {
              if ((speaker_info[i].speaker_number != machine_number) &&
                  (speaker_info[i].active == 1) &&
                  (speaker_info[i].volume > highest_other_volume)) {
                highest_other_volume = speaker_info[i].volume;
              }
            }
            highest_other_volume = (highest_other_volume * overall_volume + 50) / 100;
            if (highest_other_volume <= vo) {
              // debug(1,"Highest other volume %d is less than or equal to the desired new volume
              // %d.",highest_other_volume,vo);
              dacp_set_include_speaker_volume(machine_number, vo);
            } else {
              // debug(1,"Highest other volume %d is greater than the desired new volume
              // %d.",highest_other_volume,vo);
              // if the present overall volume is higher than the highest other volume at present,
              // then bring it down to it.
              if (overall_volume > highest_other_volume) {
                // debug(1,"Lower overall volume to new highest volume.");
                dacp_set_include_speaker_volume(
                    machine_number,
                    highest_other_volume); // set the overall volume to the highest one
              }
              int32_t desired_relative_volume =
                  (vo * 100 + (highest_other_volume / 2)) / highest_other_volume;
              // debug(1,"Set our speaker volume relative to the highest volume.");
              dacp_set_speaker_volume(
                  machine_number,
                  desired_relative_volume); // set the overall volume to the highest one
            }
          }
        }
        //     } else {
        //       debug(1, "No need to remote-set volume to %d, as it is already set to this
        //       value.",playing_conn->dacp_volume);
      }
    } else
      debug(1, "no thread playing -- ignored.");
  } else {
    debug(1, "Invalid volume: %d -- ignored.", vo);
  }
  return TRUE;
}

static gboolean on_handle_remote_command(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                                         const gchar *command, gpointer user_data) {
  debug(1, "RemoteCommand with command \"%s\".", command);
  send_simple_dacp_command((const char *)command);
  shairport_sync_complete_remote_command(skeleton, invocation);
  return TRUE;
}

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name,
                                  gpointer user_data) {

  // debug(1, "Shairport Sync native D-Bus interface \"%s\" acquired on the %s bus.", name, (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
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
  g_signal_connect(shairportSyncSkeleton, "notify::volume", G_CALLBACK(notify_volume_callback),
                   NULL);
  g_signal_connect(shairportSyncSkeleton, "handle-remote-command",
                   G_CALLBACK(on_handle_remote_command), NULL);
  debug(1, "Shairport Sync native D-Bus service started at \"%s\" on the %s bus.", name, (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
}

static void on_dbus_name_lost_again(GDBusConnection *connection, const gchar *name,
                                    gpointer user_data) {
  warn("Could not acquire a Shairport Sync native D-Bus interface \"%s\" on the %s bus.", name, (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
}

static void on_dbus_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  //debug(1, "Could not acquire a Shairport Sync native D-Bus interface \"%s\" on the %s bus -- will try adding the process "
  //         "number to the end of it.",
  //      name, (config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  pid_t pid = getpid();
  char interface_name[256] = "";
  sprintf(interface_name, "org.gnome.ShairportSync.i%d", pid);
  GBusType dbus_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.dbus_service_bus_type == DBT_session)
    dbus_bus_type = G_BUS_TYPE_SESSION;
  //debug(1, "Looking for a Shairport Sync native D-Bus interface \"%s\" on the %s bus.", interface_name,(config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  g_bus_own_name(dbus_bus_type, interface_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                 on_dbus_name_acquired, on_dbus_name_lost_again, NULL, NULL);
}

int start_dbus_service() {
  shairportSyncSkeleton = NULL;
  GBusType dbus_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.dbus_service_bus_type == DBT_session)
    dbus_bus_type = G_BUS_TYPE_SESSION;
 // debug(1, "Looking for a Shairport Sync native D-Bus interface \"org.gnome.ShairportSync\" on the %s bus.",(config.dbus_service_bus_type == DBT_session) ? "session" : "system");
  g_bus_own_name(dbus_bus_type, "org.gnome.ShairportSync", G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                 on_dbus_name_acquired, on_dbus_name_lost, NULL, NULL);
  return 0; // this is just to quieten a compiler warning
}
