#ifndef _DACP_H
#define _DACP_H

#include <stdio.h>

typedef struct {
    char *dacp_id;
    char *active_remote;
} dacpdata;

void dacp_set(char** field, const char* value);
void dacp_open(void);
void dacp_write(void);

extern dacpdata player_dacp;

#endif // _DACP_H
