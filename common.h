#ifndef _COMMON_H
#define _COMMON_H

#include <openssl/rsa.h>
#include <stdint.h>
#include "audio.h"

typedef struct {
    char *password;
    char *apname;
    char hw_addr[6];
    int port;
    char *output_name;
    audio_output *output;
    int buffer_start_fill;
} shairport_cfg;

extern int debuglev;
void die(char *format, ...);
void warn(char *format, ...);
void debug(char *format, ...);

uint8_t *base64_dec(char *input, int *outlen);
char *base64_enc(uint8_t *input, int length);

#define RSA_MODE_AUTH (0)
#define RSA_MODE_KEY  (1)
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode);

extern shairport_cfg config;

#endif // _COMMON_H
