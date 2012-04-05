#ifndef _COMMON_H
#define _COMMON_H

#define die(x) { \
    fprintf(stderr, "FATAL: %s\n", x); \
    exit(1); \
}

#endif