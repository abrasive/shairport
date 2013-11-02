#ifndef _METADATA_H
#define _METADATA_H

typedef struct {
    char *artist;
    char *title;
    char *album;
    char *comment;
    char *genre;
} metadata;

metadata * metadata_init(void);

void metadata_free(metadata *meta);

#endif // _METADATA_H
