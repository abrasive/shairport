#QVS modified to work with OPENWRT

LDFLAGS+=-lm -lpthread -lao $(shell pkg-config --libs openssl)


SRCS := shairport.c rtsp.c mdns.c common.c rtp.c player.c alac.c audio.c audio_dummy.c

ifdef CONFIG_AO
SRCS += audio_ao.c
endif

shairport: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o shairport

clean:
	rm shairport
