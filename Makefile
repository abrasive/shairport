PKGFLAGS:=$(shell pkg-config --cflags --libs openssl ao)
CFLAGS:=-O2 -Wall
LDFLAGS:=-lm -lpthread

all: hairtunes

hairtunes: hairtunes.c alac.c
	$(CC) $(CFLAGS) hairtunes.c alac.c -o $@ $(PKGFLAGS) $(LDFLAGS)

clean:
	-@rm -rf hairtunes

prefix=/usr/local
install: hairtunes
	install -m 0755 hairtunes $(prefix)/bin
	install -m 0755 shairport.pl $(prefix)/bin

.PTHONY: all clean install

.SILENT: clean

