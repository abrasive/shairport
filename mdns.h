#ifndef _MDNS_H
#define _MDNS_H

extern int mdns_pid;

void mdns_unregister(void);
void mdns_register(void);
void mdns_ls_backends(void);

typedef struct {
    char *name;
    int (*mdns_register)(char *apname, int port);
    void (*mdns_unregister)(void);
} mdns_backend;

// text and progress only -- picture feed really buggy from iTunes

#define METADATA_EXPRESSION config.get_coverart ? "md=0,1,2" : "md=0,2"

#define MDNS_RECORD_WITH_METADATA "tp=UDP", "sm=false", "ek=1", "et=0,1", "cn=0,1", "ch=2", METADATA_EXPRESSION , \
                "ss=16", "sr=44100", "vn=3", "txtvers=1", \
                config.password ? "pw=true" : "pw=false"

#define MDNS_RECORD_WITHOUT_METADATA "tp=UDP", "sm=false", "ek=1", "et=0,1", "cn=0,1", "ch=2", \
                "ss=16", "sr=44100", "vn=3", "txtvers=1", \
                config.password ? "pw=true" : "pw=false"

#endif // _MDNS_H
