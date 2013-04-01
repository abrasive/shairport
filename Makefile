SRCS := shairport.c rtsp.c mdns.c audio.c common.c rtp.c player.c alac.c $(wildcard audio_*.c)

LIBS := -lcrypto -lm -lao -lpthread

shairport: $(SRCS)
	gcc -ggdb -Wall -Wno-unused-value $(SRCS) $(LIBS) -o shairport
