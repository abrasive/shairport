CFLAGS = `pkg-config --cflags --libs openssl` -lportaudio

hairtunes: hairtunes.c alac.c
	gcc hairtunes.c alac.c -D__i386 -lm $(CFLAGS) -o hairtunes
