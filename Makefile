PKGFLAGS:=$(shell pkg-config --cflags --libs openssl ao)
CFLAGS:=-D__i386 -O2
LDFLAGS:=-lm -lpthread
PAUFLAGS:=-lportaudio

all: hairtunes

hairtunes: hairtunes.c alac.c
	$(CC) $(CFLAGS) $(PKGFLAGS) $(LDFLAGS) $? -o $@

clean:
	-@rm -rf hairtunes

.PTHONY: all clean

.SILENT: clean

