CFLAGS = `pkg-config --cflags --libs ao openssl`

hairtunes: hairtunes.c alac.c
	gcc hairtunes.c alac.c -lm $(CFLAGS) -o hairtunes
