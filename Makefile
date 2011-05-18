CC=gcc
CFLAGS:=-O2 -Wall $(shell pkg-config --cflags openssl ao)
LDFLAGS:=-lm -lpthread $(shell pkg-config --libs openssl ao)
OBJS=socketlib.o shairport.o alac.o hairtunes.o
all: hairtunes shairport

hairtunes: hairtunes.c alac.o
	$(CC) $(CFLAGS) -DHAIRTUNES_STANDALONE hairtunes.c alac.c -o $@ $(LDFLAGS)

shairport: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

clean:
	-@rm -rf hairtunes shairport $(OBJS)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


prefix=/usr/local
install: hairtunes
	install -m 0755 hairtunes $(prefix)/bin
	install -m 0755 shairport.pl $(prefix)/bin
	install -m 0755 shairport $(prefix)/bin

.PTHONY: all clean install

.SILENT: clean

