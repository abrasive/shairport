
#ifndef DBUS_BASIC_REMOTE_CONTROL_H
#define DBUS_BASIC_REMOTE_CONTROL_H

#include "dbus-basic-remote-control-interface.h"
ShairportSyncBasicRemoteControl *shairportSyncBasicRemoteControlSkeleton;

void dbus_basic_remote_control_on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data);

#endif /* #ifndef DBUS_BASIC_REMOTE_CONTROL_ */
