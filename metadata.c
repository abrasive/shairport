/*
 * Metadate structure and utility methods. This file is part of Shairport.
 * Copyright (c) Benjamin Maus 2013
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

#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"

#ifdef HAVE_LIBSSL
#include <openssl/md5.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/md5.h>
#endif

#include "common.h"
#include "metadata.h"


// including a simple base64 encoder to minimise malloc/free activity

// From Stack Overflow, with thanks:
// http://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
// minor mods to make independent of C99.
// more significant changes make it not malloc memory
// needs to initialise the docoding table first

// add _so to end of name to avoid confusion with polarssl's implementation

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};

static int mod_table[] = {0, 2, 1};

// pass in a pointer to the data, its length, a pointer to the output buffer and a pointer to an int containing its maximum length
// the actual length will be returned.

char *base64_encode_so(const unsigned char *data,
                    size_t input_length,
                    char *encoded_data,
                    size_t *output_length) {

    size_t calculated_output_length = 4 * ((input_length + 2) / 3);    
    if (calculated_output_length> *output_length)
      return(NULL);
    *output_length = calculated_output_length;
    
    int i,j;
    for (i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';

    return encoded_data;
}

// with thanks!
//

metadata player_meta;
static int fd = -1;
static int dirty = 0;

void metadata_set(char** field, const char* value) {
    if (*field) {
        if (!strcmp(*field, value))
            return;
        free(*field);
    }
    *field = strdup(value);
    dirty = 1;
}

void metadata_create(void) {
    if (!config.meta_dir)
        return;

    const char fn[] = "shairport_sync_metadata_pipe";
    size_t pl = strlen(config.meta_dir) + 1 + strlen(fn);

    char* path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s", config.meta_dir, fn);

    if (mkfifo(path, 0644) && errno != EEXIST)
        die("Could not create metadata FIFO %s", path);

    free(path);
}

void metadata_open(void) {
    if (!config.meta_dir)
        return;

    const char fn[] = "shairport_sync_metadata_pipe";
    size_t pl = strlen(config.meta_dir) + 1 + strlen(fn);

    char* path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s", config.meta_dir, fn);

    fd = open(path, O_WRONLY | O_NONBLOCK);
    if (fd < 0)
        debug(1, "Could not open metadata FIFO %s. Will try again later.", path);

    free(path);
}

static void metadata_close(void) {
    close(fd);
    fd = -1;
}

void metadata_init(void) {
    if (!config.meta_dir)
        return;
    metadata_create();  
}

static void print_one(const char *name, const char *value) {
  int ignore;
    ignore = write(fd, name, strlen(name));
    ignore = write(fd, "=", 1);
    if (value)
        ignore = write(fd, value, strlen(value));
    ignore = write(fd, "\n", 1);
}

#define write_one(name) \
    print_one(#name, player_meta.name)

void metadata_write(void) {
    int ret;

    // readers may go away and come back
    if (fd < 0)
        metadata_open();
    if (fd < 0)
        return;

    if (!dirty)
        return;

    dirty = 0;

    write_one(artist);
    write_one(title);
    write_one(album);
    write_one(artwork);
    write_one(genre);
    write_one(comment);

    ret = write(fd, "\n", 1);
    if (ret < 1)    // no reader
        metadata_close();
}

void metadata_cover_image(const char *buf, int len, const char *ext) {
    if (!config.meta_dir)
        return;

    if (buf) {
        debug(2, "Cover Art set\n");
    } else {
        debug(2, "Cover Art cleared\n");
        return;
    }

    uint8_t img_md5[16];
    
    
#ifdef HAVE_LIBSSL
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, buf, len);
    MD5_Final(img_md5, &ctx);
#endif

    
#ifdef HAVE_LIBPOLARSSL
    md5_context tctx;
    md5_starts(&tctx);
    md5_update(&tctx, buf, len);
    md5_finish(&tctx, img_md5);
#endif

    char img_md5_str[33];
    int i;
    for (i = 0; i < 16; i++)
        sprintf(&img_md5_str[i*2], "%02x", (uint8_t)img_md5[i]);

    char *dir = config.meta_dir;
    char *prefix = "cover-";

    size_t pl = strlen(dir) + 1 + strlen(prefix) + strlen(img_md5_str) + 1 + strlen(ext);

    char *path = malloc(pl+1);
    snprintf(path, pl+1, "%s/%s%s.%s", dir, prefix, img_md5_str, ext);

    int cover_fd = open(path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP| S_IROTH | S_IWOTH);

    if (cover_fd < 0) {
        warn("Could not open file %s for writing cover art", path);
        return;
    }

    if (write(cover_fd, buf, len) < len) {
        warn("writing %s failed\n", path);
        free(path);
        return;
    }
    close(cover_fd);

    debug(2, "Cover Art file is \"%s\".\n", path);
    metadata_set(&player_meta.artwork, path+strlen(dir)+1);

    free(path);
}


// Metadata is not used by shairport-sync.
// Instead we send all metadata to a fifo pipe, so that other apps can listen to the pipe and use the metadata.

// We use two 4-character codes to identify each piece of data and we send the data itself in base64 form.

// The first 4-character code, called the "type", is either:
//    'core' for all the regular metadadata coming from iTunes, etc., or 
//    'ssnc' (for 'shairport-sync') for all metadata coming from Shairport Sync itself, such as start/end delimiters, etc.

// For 'core' metadata, the second 4-character code is the 4-character metadata code coming from iTunes etc.
// For 'ssnc' metadata, the second 4-character code is used to distinguish the messages.

// Cover art is not tagged in the same way as other metadata, it seems, so is sent as an 'ssnc' type metadata message with the code 'PICT'
// The three kinds of 'ssnc' metadata at present are 'strt', 'stop' and 'PICT' for metadata package start, metadata package stop and cover art, respectively.

// Metadata is sent in two disctinct parts:
//    (1) a line with type, code and length information surrounded by XML-type tags and
//    (2) the data itself, if any, in base64 form, surrounded by XML-style data tags.

void metadata_process(uint32_t type,uint32_t code,char *data,uint32_t length) {
  debug(2,"Process metadata with type %x, code %x and length %u.",type,code,length);
  int ret;
  // readers may go away and come back
  if (fd < 0)
    metadata_open();
  if (fd < 0)
    return;
  char thestring[1024];
  snprintf(thestring,1024,"<type>%x</type><code>%x</code><length>%u</length>\n",type,code,length);
  ret = write(fd, thestring, strlen(thestring));
  if (ret < 1)    // possibly the pipe is running out of memory because the reader is too slow
    debug(1,"Error writing to pipe");
  if (length>0) {
    snprintf(thestring,1024,"<data encoding=\"base64\">\n");
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 1)    // no reader
      debug(1,"Error writing to pipe");
    // here, we write the data in base64 form using our nice base64 encoder
    // but, we break it into lines of 76 output characters, except for the last one.
    // thus, we send groups of (76/4)*3 =  57 bytes to the encoder at a time
    size_t remaining_count = length;
    char *remaining_data = data;
    size_t towrite_count;
    char outbuf[76];
    while ((remaining_count) && (ret>=0)) {
     	size_t towrite_count = remaining_count;
    	if (towrite_count>57) 
    		towrite_count = 57;
    	size_t outbuf_size = 76; // size of output buffer on entry, length of result on exit
    	if (base64_encode_so(remaining_data, towrite_count, outbuf, &outbuf_size)==NULL)
    		debug(1,"Error encoding base64 data.");
   		//debug(1,"Remaining count: %d ret: %d, outbuf_size: %d.",remaining_count,ret,outbuf_size);    	
     	ret = write(fd,outbuf,outbuf_size);
     	if (ret<0)
     		debug(1,"Error writing base64 data to pipe: \"%s\".",strerror(errno));
    	remaining_data+=towrite_count;
    	remaining_count-=towrite_count;
      // ret = write(fd,"\r\n",2);
      // if (ret<0)
     	//	debug(1,"Error writing base64 cr/lf to pipe.");
    }
    snprintf(thestring,1024,"</data>\n");
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 1)    // no reader
      debug(1,"Error writing to pipe");
  }
}
