// only one RTP session can be active at a time.
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <memory.h>
#include <sys/socket.h>
#include <netdb.h>
#include "common.h"
#include "player.h"

static int running = 0;
static int please_shutdown;

static struct sockaddr rtp_client;
static int sock;
static pthread_t rtp_thread;

static void *rtp_receiver(void *arg) {
    // we inherit the signal mask (SIGUSR1)
    uint8_t packet[2048], *pktp;

    ssize_t nread;
    while (1) {
        if (please_shutdown)
            break;
        nread = recv(sock, packet, sizeof(packet), 0);
        if (nread < 0)
            break;

        ssize_t plen = nread;
        uint8_t type = packet[1] & ~0x80;
        if (type == 0x60 || type == 0x56) {   // audio data / resend
            pktp = packet;
            if (type==0x56) {
                pktp += 4;
                plen -= 4;
            }
            seq_t seqno = ntohs(*(unsigned short *)(pktp+2));

            pktp += 12;
            plen -= 12;

            // check if packet contains enough content to be reasonable
            if (plen >= 16) {
                player_put_packet(seqno, pktp, plen);
                continue;
            }
            if (type == 0x56 && seqno == 0) {
                debug("Suspected resync request packet received.\n");
                player_flush();
                continue;
            }
        }
        warn("Unknown RTP packet of type 0x%02X length %d\n", type, nread);
    }

    debug("RTP thread interrupted. terminating.\n");
    close(sock);

    return NULL;
}

static int bind_port(struct sockaddr *remote) {
    struct addrinfo hints, *info;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = remote->sa_family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    int ret = getaddrinfo(NULL, "0", &hints, &info);

    if (ret < 0)
        die("failed to get usable addrinfo?! %s", gai_strerror(ret));

    sock = socket(remote->sa_family, SOCK_DGRAM, IPPROTO_UDP);
    ret = bind(sock, info->ai_addr, info->ai_addrlen);

    if (ret < 0)
        die("could not bind a UDP port!");

    int sport;
    struct sockaddr local;
    socklen_t local_len = sizeof(local);
    getsockname(sock, &local, &local_len);
#ifdef AF_INET6
    if (local.sa_family == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)&local;
        sport = htons(sa6->sin6_port);
    } else
#endif
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)&local;
        sport = htons(sa->sin_port);
    }

    return sport;
}


int rtp_setup(struct sockaddr *remote, int cport, int tport) {
    if (running)
        die("rtp_setup called with active stream!\n");

    debug("rtp_setup: cport=%d tport=%d\n", cport, tport);

    // we do our own timing and ignore the timing port.
    // an audio perfectionist may wish to learn the protocol.
    
    memcpy(&rtp_client, remote, sizeof(rtp_client));
#ifdef AF_INET6
    if (rtp_client.sa_family == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)&rtp_client;
        sa6->sin6_port = htons(cport);
    } else
#endif
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)&rtp_client;
        sa->sin_port = htons(cport);
    }

    int sport = bind_port(remote);

    debug("rtp listening on port %d\n", sport);

    please_shutdown = 0;
    pthread_create(&rtp_thread, NULL, &rtp_receiver, NULL);

    running = 1;
    return sport;
}

void rtp_shutdown(void) {
    if (!running)
        die("rtp_shutdown called without active stream!\n");

    please_shutdown = 1;
    pthread_kill(rtp_thread, SIGUSR1);
    void *retval;
    pthread_join(rtp_thread, &retval);
    running = 0;
}

void rtp_request_resend(seq_t first, seq_t last) {
    if (!running)
        die("rtp_request_resend called without active stream!\n");

    warn("requesting resend on %d packets\n", last-first+1);
    char req[8];    // *not* a standard RTCP NACK
    req[0] = 0x80;
    req[1] = 0x55|0x80;  // Apple 'resend'
    *(unsigned short *)(req+2) = htons(1);  // our seqnum
    *(unsigned short *)(req+4) = htons(first);  // missed seqnum
    *(unsigned short *)(req+6) = htons(last-first+1);  // count

    sendto(sock, req, sizeof(req), 0, &rtp_client, sizeof(rtp_client));
}
