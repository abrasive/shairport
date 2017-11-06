#include <stdio.h>
#include <string.h>

#include "../../config.h"

#include "../../common.h"
#include "../../player.h"
#include "../../rtsp.h"

#include "../../rtp.h"

#include "dbus_service.h"

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
  gdouble vo = shairport_sync_get_volume(skeleton);
  if (((vo <= 0.0) && (vo >= -30.0)) || (vo == -144.0)) {
    debug(1, "Setting volume to %f.", vo);
    if (playing_conn)
      player_volume_without_notification(vo, playing_conn);
    else
      debug(1, "no thread playing -- ignored.");
  } else {
    debug(1, "Invalid volume: %f -- ignored.", vo);
  }
  return TRUE;
}

static gboolean on_handle_remote_command(ShairportSync *skeleton, GDBusMethodInvocation *invocation, const gchar *command, gpointer user_data) {
  debug(1,"RemoteCommand with command \"%s\".",command);
    if (playing_conn) {
      char server_reply[2000];
      ssize_t reply_size = rtp_send_client_command(playing_conn,command,server_reply,sizeof(server_reply));
        if (reply_size>=0) {
  // not interested in the response.
  //      if (strstr(server_reply, "HTTP/1.1 204") == server_reply) {
  //        debug(1,"Client response is No Content");
  //      } else if (strstr(server_reply, "HTTP/1.1 200 OK") != server_reply) {
  //        debug("Client response is OK, with content");
  //      } else {

        if (strstr(server_reply, "HTTP/1.1 204") != server_reply) {
          debug(1, "Client request to server responded with %d characters starting with this response:", strlen(server_reply));
          int i;       
          for (i=0;i<reply_size;i++)
          if (server_reply[i] < ' ')
            debug(1,"%d  %02x", i, server_reply[i]);
          else
            debug(1,"%d  %02x  '%c'", i, server_reply[i],server_reply[i]);
          //sprintf((char *)message + 2 * i, "%02x", server_reply[i]);
          //debug(1,"Content is \"%s\".",message);
        }
      } else {
        debug(1,"Error at rtp_send_client_command");
      }
    } else {
      debug(1, "no thread playing -- RemoteCommand ignored.");
    }
  shairport_sync_complete_remote_command(skeleton,invocation);
  return TRUE;
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {

  skeleton = shairport_sync_skeleton_new();

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton), connection,
                                   "/org/gnome/ShairportSync", NULL);

  shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(skeleton),
                                        config.loudness_reference_volume_db);
  debug(1, "Loudness threshold is %f.", config.loudness_reference_volume_db);

  if (config.loudness == 0) {
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(skeleton), FALSE);
    debug(1, "Loudness is off");
  } else {
    shairport_sync_set_loudness_filter_active(SHAIRPORT_SYNC(skeleton), TRUE);
    debug(1, "Loudness is on");
  }

  g_signal_connect(skeleton, "notify::loudness-filter-active",
                   G_CALLBACK(notify_loudness_filter_active_callback), NULL);
  g_signal_connect(skeleton, "notify::loudness-threshold",
                   G_CALLBACK(notify_loudness_threshold_callback), NULL);
  g_signal_connect(skeleton, "notify::volume", G_CALLBACK(notify_volume_callback), NULL);
  g_signal_connect(skeleton, "handle-remote-command", G_CALLBACK(on_handle_remote_command), NULL);
}

int start_dbus_service() {
  skeleton = NULL;
  g_bus_own_name(G_BUS_TYPE_SYSTEM, "org.gnome.ShairportSync", G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                 on_name_acquired, NULL, NULL, NULL);
  //  G_BUS_TYPE_SESSION or G_BUS_TYPE_SYSTEM
  return 0; // this is just to quieten a compiler warning
}
