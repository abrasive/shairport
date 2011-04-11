CFLAGS = `pkg-config --cflags --libs openssl` -lportaudio
#CFLAGS = `pkg-config --cflags --libs openssl ao` -DHAVE_AO

hairtunes: hairtunes.c alac.c
	gcc hairtunes.c alac.c -D__i386 -lm $(CFLAGS) -o hairtunes
