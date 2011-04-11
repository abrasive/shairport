CFLAGS = `pkg-config --cflags --libs ao openssl`

hairtunes: hairtunes.c alac.c
	gcc hairtunes.c alac.c -D__i386 -lm $(CFLAGS) -o hairtunes
