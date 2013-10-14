ifeq ($(wildcard config.mk),)
$(warning config.mk does not exist, configuring.)
config.mk:
	sh ./configure
	$(MAKE) shairport
endif

-include config.mk

PREFIX ?= /usr/local

SRCS := shairport.c daemon.c rtsp.c mdns.c common.c rtp.c player.c alac.c audio.c audio_dummy.c audio_pipe.c

ifdef CONFIG_SNDIO
SRCS += audio_sndio.c
endif

ifdef CONFIG_AO
SRCS += audio_ao.c
endif

ifdef CONFIG_PULSE
SRCS += audio_pulse.c
endif

ifdef CONFIG_ALSA
SRCS += audio_alsa.c
endif

ifdef CONFIG_AVAHI
SRCS += avahi.c
endif

ifndef CONFIG_HAVE_GETOPT_H
SRCS += getopt_long.c
endif

# default target
all: shairport

install: install-exe install-init
install-exe: shairport
	install -m 755 -d $(PREFIX)/bin
	install -m 755 shairport $(PREFIX)/bin/shairport
install-init: install-exe
	if [[ "$(uname)" = "Linux" ]]; then\
		if [[ "$(shell cat /proc/version)" = *Gentoo* ]]; then\
			cp scripts/gentoo/openrc/init.d.sh /etc/init.d/shairport\
			cp scripts/gentoo/openrc/conf.d.cfg /etc/conf.d/shairport\
		fi;\
	fi

shairport: $(SRCS) config.h config.mk
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o shairport

clean:
	rm -f shairport
