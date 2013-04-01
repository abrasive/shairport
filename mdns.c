#include <signal.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

static int mdns_pid = 0;

void mdns_unregister(void) {
    if (mdns_pid)
        kill(mdns_pid, SIGTERM);
    mdns_pid = 0;
}

void mdns_register(void) {
    if (mdns_pid)
        die("attempted to register with mDNS twice");

    // XXX todo: native avahi support, if available at compile time

    if ((mdns_pid = fork()))
        return;
    
    char *mdns_apname = malloc(strlen(config.apname) + 14);
    char *p = mdns_apname;
    int i;
    for (i=0; i<6; i++) {
        sprintf(p, "%02X", config.hw_addr[i]);
        p += 2;
    }
    *p++ = '@';
    strcpy(p, config.apname);

    char mdns_port[6];
    sprintf(mdns_port, "%d", config.port);

#define RECORD  "tp=UDP", "sm=false", "ek=1", "et=0,1", "cn=0,1", "ch=2", \
                "ss=16", "sr=44100", "vn=3", "txtvers=1", \
                config.password ? "pw=true" : "pw=false"

    char *argv[] = {
        NULL, mdns_apname, "_raop._tcp", mdns_port, RECORD
    };

    argv[0] = "avahi-publish-service";
    execvp(argv[0], argv);

    argv[0] = "mDNSPublish";
    execvp(argv[0], argv);

    char *mac_argv[] = {"dns-sd", "-R", mdns_apname, "_raop._tcp", ".",
                        mdns_port, RECORD};
    execvp(mac_argv[0], mac_argv);

    die("Could not establish mDNS advertisement!\n");
}
