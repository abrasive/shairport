#ifndef _METADATA_H
#define _METADATA_H

#include <stdio.h>

typedef struct {
    char *artist;
    char *title;
    char *album;
    char *artwork;
    char *comment;
    char *genre;
  
    unsigned int paused;
    unsigned int position;
    unsigned int start;
    unsigned int curr;
    unsigned int end;
} metadata;

void metadata_set(char** field, const char* value);
void metadata_open(void);
void metadata_write(void);
void metadata_cover_image(const char *buf, int len, const char *ext);
void metadata_position_write(void);

extern metadata player_meta;

#endif // _METADATA_H
