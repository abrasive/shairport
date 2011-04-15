PKGFLAGS:=$(shell pkg-config --cflags --libs openssl ao)
CFLAGS:=-O2
LDFLAGS:=-lm -lpthread
PAUFLAGS:=-lportaudio

all: hairtunes

hairtunes: hairtunes.c alac.c
	$(CC) $(CFLAGS) $(PKGFLAGS) $(LDFLAGS) $? -o $@

clean:
	-@rm -rf hairtunes

.PTHONY: all clean

prefix=/usr/local
install: hairtunes
	install -m 0755 hairtunes $(prefix)/bin
	install -m 0755 shairport.pl $(prefix)/bin
.PHONY: install

.SILENT: clean

