
#ifndef DBUS_DIAGNOSTICS_H
#define DBUS_DIAGNOSTICS_H

#include "dbus-diagnostics-interface.h"
ShairportSyncDiagnostics *shairportSyncDiagnosticsSkeleton;

void dbus_diagnostics_on_dbus_name_acquired(GDBusConnection *connection, const gchar *name, gpointer user_data);

#endif /* #ifndef DBUS_SERVICE_H */
