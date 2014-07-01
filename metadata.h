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

void  metadata_set(char** field, const char* value);
FILE* metadata_open(const char* mode);
void  metadata_write(const char* dir);

extern metadata player_meta;

#endif // _METADATA_H
