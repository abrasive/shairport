PKGFLAGS:=$(shell pkg-config --cflags --libs openssl ao)
CFLAGS:=-O2 -Wall
LDFLAGS:=-lm -lpthread

all: hairtunes shairport

hairtunes: hairtunes.c alac.c
	$(CC) $(CFLAGS) hairtunes.c alac.c -o $@ $(PKGFLAGS) $(LDFLAGS)

shairport: socketlib.c shairport.c alac.c
	$(CC) $(CFLAGS) alac.c socketlib.c shairport.c -o $@ $(PKGFLAGS) $(LDFLAGS)

clean:
	-@rm -rf hairtunes shairport

prefix=/usr/local
install: hairtunes
	install -m 0755 hairtunes $(prefix)/bin
	install -m 0755 shairport.pl $(prefix)/bin
	install -m 0755 shairport $(prefix)/bin

.PTHONY: all clean install

.SILENT: clean

