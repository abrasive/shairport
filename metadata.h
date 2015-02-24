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
} metadata;

void metadata_set(char** field, const char* value);
void metadata_open(void);
void metadata_write(void);
void metadata_cover_image(const char *buf, int len, const char *ext);

void metadata_process(uint32_t type,uint32_t code,char *data,uint32_t length);

extern metadata player_meta;

#endif // _METADATA_H
