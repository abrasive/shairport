CFLAGS+=-Wall $(shell pkg-config --cflags openssl) -DCONFIG_AO

LDFLAGS+=-lm -lpthread $(shell pkg-config --libs openssl) $(shell pkg-config --libs ao)


PREFIX ?= /usr/local

SRCS := shairport.c rtsp.c mdns.c common.c rtp.c player.c alac.c audio.c audio_dummy.c audio_ao.c 


# default target
all: shairport

install: shairport
	install -m 755 -d $(PREFIX)/bin
	install -m 755 shairport $(PREFIX)/bin/shairport

shairport: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o shairport

clean:
	rm -f shairport
