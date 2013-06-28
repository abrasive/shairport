ifeq ($(wildcard config.mk),)
$(error config.mk does not exist, please run './configure')
endif

-include config.mk
-include platform-$(PLATFORM).mk

.PHONY: all install clean mrproper

# default target
all: shairport

SRCS := shairport.c daemon.c rtsp.c mdns.c common.c rtp.c player.c alac.c audio.c audio_dummy.c audio_pipe.c

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

clean:
	rm -f shairport

mrproper: clean
	rm -f config.mk config.h

$(PREFIX_PLUGINS):
	install -m 755 -d $@

shairport: $(SRCS) config.h config.mk
	$(CC) $(CFLAGS) $(APP_CFLAGS) $(SRCS) $(LDFLAGS) $(APP_LDFLAGS) -o shairport

