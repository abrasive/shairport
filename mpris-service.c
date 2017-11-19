#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"

#include "mpris-service.h"
int send_simple_dacp_command(const char *command) {
  int reply = 0;
  if (playing_conn) {
    char server_reply[2000];
    debug(1, "Sending command \"%s\".", command);
    ssize_t reply_size =
        dacp_send_client_command(playing_conn, command, server_reply, sizeof(server_reply));
    if (reply_size >= 0) {
      // not interested in the response.
      //      if (strstr(server_reply, "HTTP/1.1 204") == server_reply) {
      //        debug(1,"Client response is No Content");
      //      } else if (strstr(server_reply, "HTTP/1.1 200 OK") != server_reply) {
      //        debug("Client response is OK, with content");
      //      } else {

      if (strstr(server_reply, "HTTP/1.1 204") != server_reply) {
        debug(1,
              "Client request to server responded with %d characters starting with this response:",
              strlen(server_reply));
        int i;
        for (i = 0; i < reply_size; i++)
          if (server_reply[i] < ' ')
            debug(1, "%d  %02x", i, server_reply[i]);
          else
            debug(1, "%d  %02x  '%c'", i, server_reply[i], server_reply[i]);
        // sprintf((char *)message + 2 * i, "%02x", server_reply[i]);
        // debug(1,"Content is \"%s\".",message);
      }
    } else {
      debug(1, "Error at rtp_send_client_command");
      reply = -1;
    }
  } else {
    debug(1, "no thread playing -- RemoteCommand ignored.");
    reply = -1;
  }
  return reply;
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

  media_player2_player_set_playback_status(mprisPlayerPlayerSkeleton, "stop");
  media_player2_player_set_loop_status(mprisPlayerPlayerSkeleton, "off");
  media_player2_player_set_volume(mprisPlayerPlayerSkeleton, 0.5);
  media_player2_player_set_minimum_rate(mprisPlayerPlayerSkeleton, 1.0);
  media_player2_player_set_maximum_rate(mprisPlayerPlayerSkeleton, 1.0);
  media_player2_player_set_can_go_next(mprisPlayerPlayerSkeleton, FALSE);
  media_player2_player_set_can_go_previous(mprisPlayerPlayerSkeleton, FALSE);
  media_player2_player_set_can_play(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_pause(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_seek(mprisPlayerPlayerSkeleton, FALSE);
  media_player2_player_set_can_control(mprisPlayerPlayerSkeleton, TRUE);

  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-play", G_CALLBACK(on_handle_play), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-pause", G_CALLBACK(on_handle_pause), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-play-pause", G_CALLBACK(on_handle_play_pause),
                   NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-stop", G_CALLBACK(on_handle_stop), NULL);

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
  debug(1, "Looking for well-known interface name \"%s\".", interface_name);
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
