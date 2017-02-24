#ifndef __APPLE_ALAC_H
#define __APPLE_ALAC_H

#include "config.h"
#include <stdint.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC int apple_alac_init(int32_t fmtp[12]);
EXTERNC int apple_alac_terminate();
EXTERNC int apple_alac_decode_frame(unsigned char *sampleBuffer, uint32_t bufferLength,
                                    unsigned char *dest, int *outsize);

#undef EXTERNC

#endif /* __APPLE_ALAC_H */
