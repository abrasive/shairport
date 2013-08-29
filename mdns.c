/*
 * mDNS registration handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
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
#include <string.h>
#include "config.h"
#include "common.h"
#include "mdns.h"

#ifdef CONFIG_AVAHI
extern mdns_backend mdns_avahi;
#endif

extern mdns_backend mdns_external_avahi;
extern mdns_backend mdns_external_dns_sd;

static mdns_backend *mdns_backends[] = {
#ifdef CONFIG_AVAHI
    &mdns_avahi,
#endif
    &mdns_external_avahi,
    &mdns_external_dns_sd,
    NULL
};

static mdns_backend *backend = NULL;

void mdns_register(void) {
    char *mdns_apname = malloc(strlen(config.apname) + 14);
    char *p = mdns_apname;
    int i;
    for (i=0; i<6; i++) {
        sprintf(p, "%02X", config.hw_addr[i]);
        p += 2;
    }
    *p++ = '@';
    strcpy(p, config.apname);

    backend = NULL;
    mdns_backend **s = NULL;
    for (s = mdns_backends; *s; s++)
    {
        int error = (*s)->mdns_register(mdns_apname, config.port);
        if (error == 0)
        {
            backend = *s;
            break;
        }
    }

    if (backend == NULL)
        die("Could not establish mDNS advertisement!");
}

void mdns_unregister(void) {
    if (backend) {
        backend->mdns_unregister();
    }
}


