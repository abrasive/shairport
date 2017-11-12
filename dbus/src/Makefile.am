if USE_DBUS_CLIENT
  noinst_PROGRAMS = shairport-sync-dbus-client
  shairport_sync_dbus_client_SOURCES = shairport-sync-dbus-interface.c shairport-sync-dbus-interface.h shairport-sync-dbus-client.c
endif

BUILT_SOURCES = shairport-sync-dbus-interface.h shairport-sync-dbus-interface.c
# We don't want to install this header
noinst_HEADERS = $(BUILT_SOURCES)

# Correctly clean the generated headers, but keep the xml description
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = org.gnome.ShairportSync.xml

#Rule to generate the binding headers
shairport-sync-dbus-interface.h:  org.gnome.ShairportSync.xml
	gdbus-codegen --interface-prefix org.gnome --generate-c-code shairport-sync-dbus-interface org.gnome.ShairportSync.xml

shairport-sync-dbus-interface.c:  org.gnome.ShairportSync.xml
	gdbus-codegen --interface-prefix org.gnome --generate-c-code shairport-sync-dbus-interface org.gnome.ShairportSync.xml

# Dbus service file

if USE_DBUS
interfacesdir = $(DESTDIR)/usr/local/share/dbus-1/interfaces
interfaces_DATA = org.gnome.ShairportSync.xml
endif
