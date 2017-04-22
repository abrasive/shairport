SUBDIRS = man

bin_PROGRAMS = shairport-sync
shairport_sync_SOURCES = shairport.c rtsp.c mdns.c mdns_external.c common.c rtp.c player.c alac.c audio.c loudness.c

AM_CFLAGS = -Wno-multichar -DSYSCONFDIR=\"$(sysconfdir)\" -O2
if BUILD_FOR_FREEBSD
  AM_CPPFLAGS = -I/usr/local/include -Wno-multichar -DSYSCONFDIR=\"$(sysconfdir)\" -O2
else
  AM_CPPFLAGS = -Wno-multichar -DSYSCONFDIR=\"$(sysconfdir)\" -O2
endif

if USE_APPLE_ALAC
  shairport_sync_SOURCES += apple_alac.cpp
endif

if USE_CUSTOMPIDDIR
AM_CFLAGS+= \
	-DPIDDIR=\"$(CUSTOM_PID_DIR)\"
endif

if USE_AVAHI
shairport_sync_SOURCES += mdns_avahi.c
endif

if USE_TINYSVCMDNS
shairport_sync_SOURCES += mdns_tinysvcmdns.c tinysvcmdns.c
endif

if USE_ALSA
shairport_sync_SOURCES += audio_alsa.c
endif

if USE_SNDIO
shairport_sync_SOURCES += audio_sndio.c
endif

if USE_STDOUT
shairport_sync_SOURCES += audio_stdout.c
endif

if USE_PIPE
shairport_sync_SOURCES += audio_pipe.c
endif

if USE_DUMMY
shairport_sync_SOURCES += audio_dummy.c
endif

if USE_AO
shairport_sync_SOURCES += audio_ao.c
endif

if USE_SOUNDIO
shairport_sync_SOURCES += audio_soundio.c
endif

if USE_PULSE
shairport_sync_SOURCES += audio_pulse.c
endif

if USE_CONVOLUTION
shairport_sync_SOURCES += FFTConvolver/AudioFFT.cpp FFTConvolver/FFTConvolver.cpp FFTConvolver/Utilities.cpp FFTConvolver/convolver.cpp
AM_CXXFLAGS = -std=c++11 -O2
endif

if USE_DNS_SD
shairport_sync_SOURCES += mdns_dns_sd.c
endif

install-exec-hook:
if INSTALL_CONFIG_FILES
	[ -e $(DESTDIR)$(sysconfdir) ] || mkdir $(DESTDIR)$(sysconfdir)
	cp scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf.sample
	[ -f $(DESTDIR)$(sysconfdir)/shairport-sync.conf ] || cp scripts/shairport-sync.conf $(DESTDIR)$(sysconfdir)/shairport-sync.conf
endif
if INSTALL_SYSTEMV
	getent group shairport-sync &>/dev/null || groupadd -r shairport-sync >/dev/null
	getent passwd shairport-sync &> /dev/null || useradd -r -M -g shairport-sync -s /usr/bin/nologin -G audio shairport-sync >/dev/null
	[ -e /var/run/shairport-sync ] || mkdir -p /var/run/shairport-sync
	chown shairport-sync:shairport-sync /var/run/shairport-sync
	[ -e $(DESTDIR)$(sysconfdir)/init.d ] || mkdir -p $(DESTDIR)$(sysconfdir)/init.d
	[ -f $(DESTDIR)$(sysconfdir)/init.d/shairport-sync ] || cp scripts/shairport-sync $(DESTDIR)$(sysconfdir)/init.d/
endif
if INSTALL_SYSTEMD
	getent group shairport-sync &>/dev/null || groupadd -r shairport-sync >/dev/null
	getent passwd shairport-sync &> /dev/null || useradd -r -M -g shairport-sync -s /usr/bin/nologin -G audio shairport-sync >/dev/null
	[ -e $(DESTDIR)$(systemdsystemunitdir) ] || mkdir -p $(DESTDIR)$(systemdsystemunitdir)
	[ -f $(DESTDIR)$(systemdsystemunitdir)/shairport-sync.service ] || cp scripts/shairport-sync.service $(DESTDIR)$(systemdsystemunitdir)
endif
if INSTALL_FREEBSD_SERVICE
	pw showgroup shairport-sync > /dev/null 2>&1 || pw addgroup  shairport-sync > /dev/null 2>&1
	pw showuser shairport-sync > /dev/null 2>&1 || pw adduser  shairport-sync > /dev/null 2>&1
	[ -e /var/run/shairport-sync ] || mkdir -p /var/run/shairport-sync
	chown shairport-sync:shairport-sync /var/run/shairport-sync
	[ -f /usr/local/etc/rc.d/shairport_sync ] || cp scripts/shairport-sync.freebsd /usr/local/etc/rc.d/shairport_sync
	chmod 555 /usr/local/etc/rc.d/shairport_sync 
endif
