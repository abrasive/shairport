CFLAGS:=-O2 -Wall $(shell pkg-config --cflags openssl ao)
LDFLAGS:=-lm -lpthread $(shell pkg-config --libs openssl ao)
OBJS=socketlib.o shairport.o alac.o hairtunes.o
all: hairtunes-bin shairport

hairtunes-bin: hairtunes.c alac.o
	$(CC) $(CFLAGS) -DHAIRTUNES_STANDALONE hairtunes.c alac.o -o $@ $(LDFLAGS)

shairport: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

clean:
	-@rm -rf hairtunes-bin shairport $(OBJS)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


prefix=/usr/local
install: hairtunes-bin shairport
	install -D -m 0755 hairtunes-bin $(DESTDIR)$(prefix)/bin/hairtunes-bin
	install -D -m 0755 hairtunes $(DESTDIR)$(prefix)/bin/hairtunes
	install -D -m 0755 shairport.pl $(DESTDIR)$(prefix)/bin/shairport.pl
	install -D -m 0755 shairport $(DESTDIR)$(prefix)/bin/shairport

.PHONY: all clean install

.SILENT: clean

