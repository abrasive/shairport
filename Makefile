ifeq ($(wildcard config.mk),)
$(warning config.mk does not exist, configuring.)
config.mk:
	sh ./configure
	$(MAKE) shairport
endif

-include config.mk

SRCS := shairport.c rtsp.c mdns.c common.c rtp.c player.c alac.c audio.c audio_dummy.c

ifdef CONFIG_AO
SRCS += audio_ao.c
endif

# default target
all: shairport

shairport: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o shairport

clean:
	rm -f shairport
