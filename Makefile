ifeq ($(wildcard config.mk),)
$(warning config.mk does not exist, configuring.)
config.mk:
	sh ./configure
	$(MAKE) shairport
endif

-include config.mk

# default target
all: shairport

PLUGIN_DIR = plugins

PREFIX ?= /usr/local

SRCS := shairport.c daemon.c rtsp.c mdns.c common.c rtp.c player.c alac.c audio.c audio_dummy.c audio_pipe.c
CLEAN := 

ifdef CONFIG_AO
PLUGIN_SRCS := audio_ao.c
PLUGIN_NAME := audio_ao
-include plugin.mk
endif

ifdef CONFIG_PULSE
PLUGIN_SRCS := audio_pulse.c
PLUGIN_NAME := audio_pulse
-include plugin.mk
endif

ifdef CONFIG_ALSA
PLUGIN_SRCS := audio_alsa.c
PLUGIN_NAME := audio_alsa
-include plugin.mk
endif

ifdef CONFIG_AVAHI
SRCS += avahi.c
endif

ifndef CONFIG_HAVE_GETOPT_H
SRCS += getopt_long.c
endif

install: shairport
	install -m 755 -d $(PREFIX)/bin
	install -m 755 shairport $(PREFIX)/bin/shairport

shairport: $(SRCS) config.h config.mk
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o shairport

clean:
	rm -f shairport
	rm -f $(CLEAN)
