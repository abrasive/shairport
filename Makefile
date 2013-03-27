SRCS := shairport.c rtsp.c common.c rtp.c player.c alac.c $(wildcard audio_*.c)

LIBS := -lcrypto -lm -lao -lpthread

shairport: $(SRCS)
	gcc -ggdb -Wall $(SRCS) $(LIBS) -o shairport
