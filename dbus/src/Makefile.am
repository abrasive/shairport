if USE_DBUS_CLIENT
  noinst_PROGRAMS = shairport-sync-dbus-client
  shairport_sync_dbus_client_SOURCES = shairportsync.c shairportsync.h shairport-sync-dbus-client.c
endif

BUILT_SOURCES = shairportsync.h shairportsync.c
# We don't want to install this header
noinst_HEADERS = $(BUILT_SOURCES)

# Correctly clean the generated headers, but keep the xml description
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = org.gnome.ShairportSync.xml

#Rule to generate the binding headers
shairportsync.h:  org.gnome.ShairportSync.xml
	gdbus-codegen --interface-prefix org.gnome --generate-c-code shairportsync org.gnome.ShairportSync.xml

shairportsync.c:  org.gnome.ShairportSync.xml
	gdbus-codegen --interface-prefix org.gnome --generate-c-code shairportsync org.gnome.ShairportSync.xml

# Dbus service file

if USE_DBUS
interfacesdir = $(DESTDIR)/usr/local/share/dbus-1/interfaces
interfaces_DATA = org.gnome.ShairportSync.xml
endif
