/*
 * Utility routines. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * The volume to attenuation function vol2attn copyright (c) Mike Brady 2014
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <memory.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include "common.h"
#include "daemon.h"

shairport_cfg config;

int debuglev = 0;

void die(char *format, ...) {
    fprintf(stderr, "FATAL: ");

    va_list args;
    va_start(args, format);

    vfprintf(stderr, format, args);
    if (config.daemonise)
        daemon_fail(format, args); // Send error message to parent

    va_end(args);

    fprintf(stderr, "\n");
    shairport_shutdown(1);
}

void warn(char *format, ...) {
    fprintf(stderr, "WARNING: ");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void debug(int level, char *format, ...) {
    if (level > debuglev)
        return;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}


char *base64_enc(uint8_t *input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    char *buf = (char *)malloc(bptr->length);
    if (bptr->length) {
        memcpy(buf, bptr->data, bptr->length-1);
        buf[bptr->length-1] = 0;
    }

    BIO_free_all(bmem);

    return buf;
}

uint8_t *base64_dec(char *input, int *outlen) {
    BIO *bmem, *b64;
    int inlen = strlen(input);

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    // Apple cut the padding off their challenges; restore it
    BIO_write(bmem, input, inlen);
    while (inlen++ & 3)
        BIO_write(bmem, "=", 1);
    BIO_flush(bmem);

    int bufsize = strlen(input)*3/4 + 1;
    uint8_t *buf = malloc(bufsize);
    int nread;

    nread = BIO_read(b64, buf, bufsize);

    BIO_free_all(bmem);

    *outlen = nread;
    return buf;
}

static char super_secret_key[] =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
"wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
"wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
"/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
"UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
"BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
"LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
"NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
"lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
"aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
"a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
"oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
"oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
"k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
"AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
"cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
"54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
"17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
"1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
"LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
"2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
"-----END RSA PRIVATE KEY-----";

uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode) {
    static RSA *rsa = NULL;

    if (!rsa) {
        BIO *bmem = BIO_new_mem_buf(super_secret_key, -1);
        rsa = PEM_read_bio_RSAPrivateKey(bmem, NULL, NULL, NULL);
        BIO_free(bmem);
    }

    uint8_t *out = malloc(RSA_size(rsa));
    switch (mode) {
        case RSA_MODE_AUTH:
            *outlen = RSA_private_encrypt(inlen, input, out, rsa,
                                          RSA_PKCS1_PADDING);
            break;
        case RSA_MODE_KEY:
            *outlen = RSA_private_decrypt(inlen, input, out, rsa,
                                          RSA_PKCS1_OAEP_PADDING);
            break;
        default:
            die("bad rsa mode");
    }
    return out;
}

// Given a volume (0 to -30) and high and low attenuations available in the mixer in dB, return an attenuation depending on the volume and the function's transfer function
// See http://tangentsoft.net/audio/atten.html for data on good attenuators.
// We want a smooth attenuation function, like, for example, the ALPS RK27 Potentiometer transfer functions referred to at the link above.

double vol2attn(double vol, long max_db, long min_db) { 

// We use a little coordinate geometry to build a transfer function from the volume passed in to the device's dynamic range.
// The x axis is the "volume in" which will be from -30 to 0. The y axis will be the "volume out" which will be from the bottom of the range to the top.
// We build the transfer function from one or more lines. We characterise each line with two numbers:
// the first is where on x the line starts when y=0 (from 0 to -30); the second is where on y the line stops when when x is -30.
// thus, if the line was characterised as {0,-30}, it would be an identity transfer.
// Assuming, for example, a dynamic range of lv=-70 to hv=0
// Typically we'll use three lines -- a three order transfer function
// First: {0,25} giving a gentle slope
// Second: {-17,-25-(lv+25)/2} giving a faster slope from y=0 at x=-17 to y=-50 at x=-30
// Third: {-23,lv} giving a fast slope from y=0 at x=-23 to y=-70 at x=-30

#define order 3

  double vol_setting = max_db;
  
  if ((vol<=0.0) && (vol>=-30.0)) {
    long range_db = max_db-min_db; // this will be a positive nunmber
    // debug(1,"Volume min %ddB, max %ddB, range %ddB.\n",min_db,max_db,range_db);
    double first_slope = -2500.0; // this is the slope of the attenuation at the high end -- 25dB for the full rotation.
    if (-range_db>first_slope)
      first_slope = range_db;
    double lines[order][2] = {{0,first_slope},{-17,first_slope-(range_db+first_slope)/2},{-23,-range_db}};
    int i;
    for (i=0;i<order;i++) {
      if (vol<=lines[i][0]) {
        double tvol = lines[i][1]*(vol-lines[i][0])/(-30-lines[i][0]);
	      // debug(1,"On line %d, end point of %f, input vol %f yields output vol %f.\n",i,lines[i][1],vol,tvol);
        if (tvol<vol_setting)
          vol_setting=tvol;
      }
    }
    vol_setting+=max_db;
  } else if (vol!=-144.0) {
    debug(1,"Volume request value %f is out of range: should be from 0.0 to -30.0 or -144.0.\n",vol);
  } else {
    vol_setting = min_db; // for safety, return the lowest setting...
  }
  // debug(1,"returning an attenuation of %f.\n",vol_setting);
  return vol_setting;
}

