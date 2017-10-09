# Dbus service file

if USE_DBUS
servicedir = /usr/local/share/dbus-1/services
service_in_files = org.gnome.ShairportSync.service.in
service_DATA = $(service_in_files:.service.in=.service)
EXTRA_DIST = $(service_in_files)

# Rule to make the service file with bindir expanded
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $<> $@

clean-local:
	rm -rf $(service_DATA)

endif
