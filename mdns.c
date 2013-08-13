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


#include <signal.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "common.h"
#include "mdns.h"
#ifdef CONFIG_AVAHI
#include "avahi.h"
#endif

int mdns_pid = 0;

void mdns_unregister(void) {
#ifdef CONFIG_AVAHI
    avahi_unregister();
#endif

    if (mdns_pid)
        kill(mdns_pid, SIGTERM);
    mdns_pid = 0;
}

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

#ifdef CONFIG_AVAHI
    if (avahi_register(mdns_apname))
        return;
    warn("avahi_register failed, falling back to external programs");
#endif

    free(mdns_apname);

    if ((mdns_pid = fork()))
        return;

    char mdns_port[6];
    sprintf(mdns_port, "%d", config.port);

    char *argv[] = {
        NULL, mdns_apname, "_raop._tcp", mdns_port, MDNS_RECORD, NULL
    };

    argv[0] = "avahi-publish-service";
    execvp(argv[0], argv);

    argv[0] = "mDNSPublish";
    execvp(argv[0], argv);

    char *mac_argv[] = {"dns-sd", "-R", mdns_apname, "_raop._tcp", ".",
                        mdns_port, MDNS_RECORD, NULL};
    execvp(mac_argv[0], mac_argv);

    die("Could not establish mDNS advertisement!");
}
