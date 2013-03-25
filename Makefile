# Uncomment this if you have the avahi client libraries installed and you want to
# link avahi directly to shairport rather than calling avahi-publish-service as
# a subprocess. The advantage of linking it directly is it's a cleaner approach
# than execp, and the avahi service will reliably unpublish when shairport dies.
#
# LINKAVAHI := 1


MY_CFLAGS= $(shell pkg-config --cflags ao)
MY_LDFLAGS= $(shell pkg-config --libs ao)
ifeq ($(shell uname),FreeBSD)
MY_LDFLAGS+= -lssl
else
MY_CFLAGS+= $(shell pkg-config --cflags openssl)
MY_LDFLAGS+= $(shell pkg-config --libs openssl)
endif

CFLAGS:=-O2 -Wall $(MY_CFLAGS)
LDFLAGS:=-lm -lpthread $(MY_LDFLAGS)
ifdef LINKAVAHI
    CFLAGS := $(CFLAGS) $(shell pkg-config --cflags avahi-client) -DLINKAVAHI
    LDFLAGS := $(LDFLAGS) $(shell pkg-config --libs avahi-client)
endif


OBJS=socketlib.o shairport.o alac.o hairtunes.o
all: hairtunes shairport

hairtunes: hairtunes.c alac.o
	$(CC) $(CFLAGS) -DHAIRTUNES_STANDALONE hairtunes.c alac.o -o $@ $(LDFLAGS)

shairport: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

clean:
	-@rm -rf hairtunes shairport $(OBJS)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


prefix=/usr/local
install: hairtunes shairport
	install -D -m 0755 hairtunes $(DESTDIR)$(prefix)/bin/hairtunes
	install -D -m 0755 shairport.pl $(DESTDIR)$(prefix)/bin/shairport.pl
	install -D -m 0755 shairport $(DESTDIR)$(prefix)/bin/shairport

.PHONY: all clean install

.SILENT: clean

