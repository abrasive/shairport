#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"

#include "shairport-sync-mpris-service.h"

static void on_mpris_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {

const char* empty_string_array[] = {
    NULL
};

  debug(1,"MPRIS well-known interface name \"%s\" acquired for %s.",name,config.appName);
  mprisPlayerSkeleton = org_mpris_media_player2_skeleton_new();
  mprisPlayerPlayerSkeleton = org_mpris_media_player2_player_skeleton_new();

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);
                                   
  org_mpris_media_player2_set_desktop_entry(mprisPlayerSkeleton, "shairport-sync");
  org_mpris_media_player2_set_identity(mprisPlayerSkeleton, "Shairport Sync");
  org_mpris_media_player2_set_can_quit(mprisPlayerSkeleton, FALSE);
  org_mpris_media_player2_set_can_raise(mprisPlayerSkeleton, FALSE);
  org_mpris_media_player2_set_has_track_list(mprisPlayerSkeleton, FALSE);
  org_mpris_media_player2_set_supported_uri_schemes (mprisPlayerSkeleton,empty_string_array);
  org_mpris_media_player2_set_supported_mime_types (mprisPlayerSkeleton, empty_string_array);
  
  org_mpris_media_player2_player_set_playback_status (mprisPlayerPlayerSkeleton, "stop");
  org_mpris_media_player2_player_set_loop_status (mprisPlayerPlayerSkeleton, "off");
  org_mpris_media_player2_player_set_volume (mprisPlayerPlayerSkeleton, 0.5);
  org_mpris_media_player2_player_set_minimum_rate (mprisPlayerPlayerSkeleton,1.0);
  org_mpris_media_player2_player_set_maximum_rate (mprisPlayerPlayerSkeleton,1.0);
  org_mpris_media_player2_player_set_can_go_next (mprisPlayerPlayerSkeleton,FALSE);
  org_mpris_media_player2_player_set_can_go_previous (mprisPlayerPlayerSkeleton,FALSE);
  org_mpris_media_player2_player_set_can_play (mprisPlayerPlayerSkeleton, TRUE);
  org_mpris_media_player2_player_set_can_pause (mprisPlayerPlayerSkeleton,FALSE);
  org_mpris_media_player2_player_set_can_seek (mprisPlayerPlayerSkeleton, FALSE);
  org_mpris_media_player2_player_set_can_control (mprisPlayerPlayerSkeleton, TRUE); 
  debug(1,"MPRIS service started on interface \"%s\".",name);
}

static void on_mpris_name_lost_again(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  warn("Could not acquire an MPRIS interface.");
}

static void on_mpris_name_lost(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  debug(1,"Could not acquire well-known interface name \"%s\" -- will try adding the process number to the end of it.",name);
  pid_t pid = getpid();
  char interface_name[256] = "";
  sprintf(interface_name,"org.mpris.MediaPlayer2.ShairportSync.i%d",pid);
  GBusType mpris_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.mpris_service_bus_type==DBT_session)
    mpris_bus_type = G_BUS_TYPE_SESSION;
  debug(1,"Looking for well-known interface name \"%s\".",interface_name);
  g_bus_own_name(mpris_bus_type, interface_name, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                on_mpris_name_acquired, on_mpris_name_lost_again, NULL, NULL);
}

int start_mpris_service() {
  mprisPlayerSkeleton = NULL;
  mprisPlayerPlayerSkeleton = NULL;
  GBusType mpris_bus_type = G_BUS_TYPE_SYSTEM;
  if (config.mpris_service_bus_type==DBT_session)
    mpris_bus_type = G_BUS_TYPE_SESSION;
  debug(1,"Looking for well-known name \"org.mpris.MediaPlayer2.ShairportSync\".");
  g_bus_own_name(mpris_bus_type, "org.mpris.MediaPlayer2.ShairportSync", G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                on_mpris_name_acquired, on_mpris_name_lost, NULL, NULL);
  return 0; // this is just to quieten a compiler warning
}
