#ifndef _METADATA_H
#define _METADATA_H

#include <stdio.h>

typedef struct {
    char *artist;
    char *title;
    char *album;
    char *comment;
    char *genre;
} metadata;

metadata* metadata_init(void);
void      metadata_free(metadata* meta);
FILE*     metadata_open(const char* mode);
void      metadata_write(metadata* meta, const char* dir);

#endif // _METADATA_H
