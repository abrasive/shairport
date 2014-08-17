/*
 * Apple RTP protocol handler. This file is part of Shairport.
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

#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "common.h"
#include "player.h"
#include "rtp.h"


typedef struct {
	uint32_t seconds;	
	uint32_t fraction;
} ntp_timestamp;

typedef struct time_ping_record {
  uint64_t local_to_remote_difference;
  uint64_t dispersion;
} time_ping_record;

// only one RTP session can be active at a time.
static int running = 0;
static int please_shutdown;

static SOCKADDR rtp_client_control_socket; // a socket pointing to the control port of the client
static SOCKADDR rtp_client_timing_socket; // a socket pointing to the timing port of the client
static int audio_socket; // our local [server] audio socket
static int control_socket; // our local [server] control socket
static int timing_socket; // local timing socket
static pthread_t rtp_audio_thread, rtp_control_thread, rtp_timing_thread;

static uint32_t reference_timestamp;
static uint64_t reference_timestamp_time;

// debug variables
static int request_sent;

#define time_ping_history  8
#define time_ping_fudge_factor  100000

static uint8_t time_ping_count;
struct time_ping_record time_pings[time_ping_history];

static struct timespec dtt; // dangerous -- this assumes that there will never be two timing request in flight at the same time

static pthread_mutex_t reference_time_mutex = PTHREAD_MUTEX_INITIALIZER;

uint64_t static local_to_remote_time_difference; // used to switch between local and remote clocks

static void *rtp_audio_receiver(void *arg) {
    // we inherit the signal mask (SIGUSR1)
    uint8_t packet[2048], *pktp;

    ssize_t nread;
    while (1) {
        if (please_shutdown)
            break;
        nread = recv(audio_socket, packet, sizeof(packet), 0);
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
            uint32_t timestamp = ntohl(*(unsigned long *)(pktp+4));
            
            //if (packet[1]&0x10)
            //	debug(1,"Audio packet Extension bit set.");

            pktp += 12;
            plen -= 12;

            // check if packet contains enough content to be reasonable
            if (plen >= 16) {
                player_put_packet(seqno,timestamp, pktp, plen);
                continue;
            }
            if (type == 0x56 && seqno == 0) {
                debug(2, "resend-related request packet received, ignoring.");
                continue;
            }
            debug(1, "Audio receiver -- Unknown RTP packet of type 0x%02X length %d seqno %d", type, nread, seqno);
        }
        warn("Audio receiver -- Unknown RTP packet of type 0x%02X length %d.", type, nread);
    }

    debug(1, "Audio receiver -- Server RTP thread interrupted. terminating.");
    close(audio_socket);

    return NULL;
}

static void *rtp_control_receiver(void *arg) {
    // we inherit the signal mask (SIGUSR1)
    reference_timestamp=0; // nothing valid received yet
    uint8_t packet[2048];
    struct timespec tn;
    uint64_t remote_time_of_sync,local_time_now, remote_time_now;
    uint32_t sync_rtp_timestamp,rtp_timestamp_less_latency;
    ssize_t nread;
    while (1) {
        if (please_shutdown)
            break;
        nread = recv(control_socket, packet, sizeof(packet), 0);
        clock_gettime(CLOCK_MONOTONIC,&tn);
        local_time_now=((uint64_t)tn.tv_sec<<32)+((uint64_t)tn.tv_nsec<<32)/1000000000;
        
        if (nread < 0)
            break;

        ssize_t plen = nread;
        if (packet[1] == 0xd4) {   // sync data
        /*
        char obf[4096];
        char *obfp = obf;
        int obfc;
        for (obfc=0;obfc<plen;obfc++) {
          sprintf(obfp,"%02X",packet[obfc]);
          obfp+=2;
        };
        *obfp=0;
        debug(1,"Sync Packet Received: \"%s\"",obf);
        */
          if (local_to_remote_time_difference) { // need a time packet to be interchanged first...
          	
          	remote_time_of_sync = (uint64_t)ntohl(*((uint32_t*)&packet[8]))<<32;
            remote_time_of_sync += ntohl(*((uint32_t*)&packet[12]));
            
            // debug(1,"Remote Sync Time: %0llx.",remote_time_of_sync);
          
          	rtp_timestamp_less_latency = ntohl(*((uint32_t*)&packet[4]));
           	sync_rtp_timestamp = ntohl(*((uint32_t*)&packet[16]));
           	
           	if (packet[0]&0x10) {
           	  // if it's a packet right after a flush or resume
           	  sync_rtp_timestamp += 352; // add frame_size -- can't see a reference to this anywhere, but it seems to get everything into sync.
           	  // it's as if the first sync after a flush or resume is the timing of the next packet after the one whose RTP is given. Weird.
           	}
          	pthread_mutex_lock(&reference_time_mutex);
          	reference_timestamp_time = remote_time_of_sync-local_to_remote_time_difference;
          	reference_timestamp = sync_rtp_timestamp;
          	pthread_mutex_unlock(&reference_time_mutex);         	
          	// get estimated remote time now
          	remote_time_now = local_time_now+local_to_remote_time_difference;          	
          	
             //debug(1,"Sync Time is %lld us late (remote times).",((remote_time_now-remote_time_of_sync)*1000000)>>32);      				
             //debug(1,"Sync Time is %lld us late (local times).",((local_time_now-reference_timestamp_time)*1000000)>>32);      				
          } else {
            debug(1,"Sync packet received before we got a timing packet back.");
          }
        } else 
          debug(1,"Control Port -- Unknown RTP packet of type 0x%02X length %d.", packet[1], nread);
    }

    debug(1, "Control RTP thread interrupted. terminating.");
    close(control_socket);

    return NULL;
}

static void *rtp_timing_sender(void *arg) {
    struct timing_request {
    char leader;
    char type;
    uint16_t seqno;
    uint32_t filler;
    uint64_t origin,receive,transmit;
  };
  
  uint64_t request_number=0;
  
  debug(1, "Timing requester startup.");

  struct timing_request req;  // *not* a standard RTCP NACK
 
  req.leader = 0x80;
  req.type = 0xd2;  // Timing request
  req.filler = 0;
  req.seqno=htons(7);
  
  time_ping_count = 0;

  // we inherit the signal mask (SIGUSR1)
  while (1) {
    if (please_shutdown)
      break;

    if (!running)
      die("rtp_timing_sender called without active stream!");

    //debug(1, "Requesting ntp timestamp exchange.");

    req.filler = 0;
    req.origin = req.receive = req.transmit=0;

    clock_gettime(CLOCK_MONOTONIC,&dtt);
    sendto(timing_socket, &req, sizeof(req), 0, (struct sockaddr*)&rtp_client_timing_socket, sizeof(rtp_client_timing_socket));
    request_number++;
    if (request_number<=4)
      usleep(500000);
    else
      sleep(3);
  }
  debug(1, "rtp_timing_sender thread interrupted. terminating.");    
  return NULL;
}

static void *rtp_timing_receiver(void *arg) {
    // we inherit the signal mask (SIGUSR1)
    uint8_t packet[2048], *pktp;
    ssize_t nread;
    pthread_t timer_requester;
    pthread_create(&timer_requester, NULL, &rtp_timing_sender, NULL);
    struct timespec att; 
    uint64_t distant_receive_time,distant_transmit_time,arrival_time,departure_time,return_time,transit_time,processing_time;
    local_to_remote_time_difference=0;
    while (1) {
      if (please_shutdown)
          break;
      nread = recv(timing_socket, packet, sizeof(packet), 0);
      clock_gettime(CLOCK_MONOTONIC,&att);
      
      if (nread < 0)
          break;

      ssize_t plen = nread;
      //debug(1,"Packet Received on Timing Port.");
      if (packet[1] == 0xd3) {   // timing reply
        /*
        char obf[4096];
        char *obfp = obf;
        int obfc;
        for (obfc=0;obfc<plen;obfc++) {
          sprintf(obfp,"%02X",packet[obfc]);
          obfp+=2;
        };
        *obfp=0;
        //debug(1,"Timing Packet Received: \"%s\"",obf);
        */  
        
        arrival_time = ((uint64_t)att.tv_sec<<32)+((uint64_t)att.tv_nsec<<32)/1000000000;
        departure_time = ((uint64_t)dtt.tv_sec<<32)+((uint64_t)dtt.tv_nsec<<32)/1000000000;
        
        return_time = arrival_time-departure_time;

        // uint64_t rtus = (return_time*1000000)>>32; debug(1,"Time ping turnaround time: %lld us.",rtus); 
        
        //distant_receive_time = ((uint64_t)ntohl(*((uint32_t*)&packet[16])))<<32+ntohl(*((uint32_t*)&packet[20]));
        
        distant_receive_time = (uint64_t)ntohl(*((uint32_t*)&packet[16]))<<32;
        distant_receive_time += ntohl(*((uint32_t*)&packet[20]));
        
        //distant_transmit_time = ((uint64_t)ntohl(*((uint32_t*)&packet[24])))<<32+ntohl(*((uint32_t*)&packet[28]));
        
        distant_transmit_time = (uint64_t)ntohl(*((uint32_t*)&packet[24]))<<32;
        distant_transmit_time += ntohl(*((uint32_t*)&packet[28]));
        
        processing_time = distant_transmit_time-distant_receive_time;
                
        // debug(1,"Return trip time: %lluuS, remote processing time: %lluuS.",(return_time*1000000)>>32,(processing_time*1000000)>>32); 

        uint64_t local_time_by_remote_clock = distant_transmit_time+return_time/2;
        
        unsigned int cc;       
        for (cc=time_ping_history-1;cc>0;cc--) {
          time_pings[cc]=time_pings[cc-1];
        }
        time_pings[0].local_to_remote_difference = local_time_by_remote_clock-arrival_time;
        time_pings[0].dispersion = return_time;
        if (time_ping_count<time_ping_history)
          time_ping_count++;
        
        
        // now pick the timestamp with the lowest dispersion
        local_to_remote_time_difference = time_pings[0].local_to_remote_difference;
        uint64_t tld = time_pings[0].dispersion;
        for (cc=1;cc<time_ping_count;cc++)
          if (time_pings[cc].dispersion<tld) {
            local_to_remote_time_difference=time_pings[cc].local_to_remote_difference;
            tld=time_pings[cc].dispersion;
          }
        // rtus = (tld*1000000)>>32; debug(1,"Choosing time difference with dispersion of %lld us.",rtus);
            

        } else {
      	debug(1, "Timing port -- Unknown RTP packet of type 0x%02X length %d.", packet[1], nread);
      }
    }

    debug(1, "Timing RTP thread interrupted. terminating.");
    void *retval;
    pthread_kill(timer_requester, SIGUSR1);
    pthread_join(timer_requester, &retval);
    debug(1,"Closed and terminated timer requester thread.");
    debug(1, "Timing RTP thread terminated.");
    close(timing_socket);

    return NULL;
}

static int bind_port(SOCKADDR *remote,int *sock, int desired_port ) {
    struct 
    addrinfo hints, *info;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = remote->SAFAMILY;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;
    
    char buffer[10];
    snprintf(buffer, 10, "%d", desired_port);
 
    int ret = getaddrinfo(NULL,buffer, &hints, &info);

    if (ret < 0)
        die("failed to get usable addrinfo?! %s.", gai_strerror(ret));

    *sock = socket(remote->SAFAMILY, SOCK_DGRAM, IPPROTO_UDP);
    ret = bind(*sock, info->ai_addr, info->ai_addrlen);

    freeaddrinfo(info);

    if (ret < 0)
        die("could not bind a UDP port!");

    int sport;
    SOCKADDR local;
    socklen_t local_len = sizeof(local);
    getsockname(*sock, (struct sockaddr*)&local, &local_len);
#ifdef AF_INET6
    if (local.SAFAMILY == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)&local;
        sport = ntohs(sa6->sin6_port);
    } else
#endif
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)&local;
        sport = ntohs(sa->sin_port);
    }

    return sport;
}


void rtp_setup(SOCKADDR *remote, int cport, int tport, int *lsport, int *lcport, int *ltport) {
    if (running)
        die("rtp_setup called with active stream!");

    debug(2, "rtp_setup: cport=%d tport=%d.", cport, tport);

    // we do our own timing and ignore the timing port.
    // an audio perfectionist may wish to learn the protocol.
    
    // set up a the record of the remote's control socket

    memcpy(&rtp_client_control_socket, remote, sizeof(rtp_client_control_socket));
#ifdef AF_INET6
    if (rtp_client_control_socket.SAFAMILY == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)&rtp_client_control_socket;
        sa6->sin6_port = htons(cport);
    } else
#endif
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)&rtp_client_control_socket;
        sa->sin_port = htons(cport);
    }

    // set up a the record of the remote's timing socket

    memcpy(&rtp_client_timing_socket, remote, sizeof(rtp_client_timing_socket));
#ifdef AF_INET6
    if (rtp_client_timing_socket.SAFAMILY == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6*)&rtp_client_timing_socket;
        sa6->sin6_port = htons(tport);
    } else
#endif
    {
        struct sockaddr_in *sa = (struct sockaddr_in*)&rtp_client_timing_socket;
        sa->sin_port = htons(tport);
    }
    
    // now, we open three sockets -- one for the audio stream, one for the timing and one for the control
    // bind to an available port
     *lsport = bind_port(remote,&audio_socket,0);
     *lcport = bind_port(remote,&control_socket,0);
     *ltport = bind_port(remote,&timing_socket,0);

    debug(2, "listening for audio, control and timing on ports %d, %d, %d.", *lsport, *lcport, *ltport);

    please_shutdown = 0;
    reference_timestamp=0;
    pthread_create(&rtp_audio_thread, NULL, &rtp_audio_receiver, NULL);
    pthread_create(&rtp_control_thread, NULL, &rtp_control_receiver, NULL);
    pthread_create(&rtp_timing_thread, NULL, &rtp_timing_receiver, NULL);

    running = 1;
    request_sent=0;
}

void get_reference_timestamp_stuff(uint32_t *timestamp,uint64_t *timestamp_time) {
  pthread_mutex_lock(&reference_time_mutex);
    *timestamp=reference_timestamp;
    *timestamp_time = reference_timestamp_time;
  pthread_mutex_unlock(&reference_time_mutex);
}

void clear_reference_timestamp(void) {
  pthread_mutex_lock(&reference_time_mutex);
  reference_timestamp=0;
  reference_timestamp_time=0;
  pthread_mutex_unlock(&reference_time_mutex);
}

void rtp_shutdown(void) {
    if (!running)
        die("rtp_shutdown called without active stream!");

    debug(2, "shutting down RTP thread");
    please_shutdown = 1;
    void *retval;
    reference_timestamp=0;
    pthread_kill(rtp_audio_thread, SIGUSR1);
    pthread_join(rtp_audio_thread, &retval);
    pthread_kill(rtp_control_thread, SIGUSR1);
    pthread_join(rtp_control_thread, &retval);
    pthread_kill(rtp_timing_thread, SIGUSR1);
    pthread_join(rtp_timing_thread, &retval);
    running = 0;
}

void rtp_request_resend(seq_t first, seq_t last) {
    if (running) {
      if (!request_sent) {
        debug(2, "requesting resend on %d packets (%04X:%04X).",
           seq_diff(first,last) + 1, first, last);
        request_sent=1;
      }

      char req[8];    // *not* a standard RTCP NACK
      req[0] = 0x80;
      req[1] = 0x55|0x80;  // Apple 'resend'
      *(unsigned short *)(req+2) = htons(1);  // our seqnum
      *(unsigned short *)(req+4) = htons(first);  // missed seqnum
      *(unsigned short *)(req+6) = htons(last-first+1);  // count

      sendto(audio_socket, req, sizeof(req), 0, (struct sockaddr*)&rtp_client_control_socket, sizeof(rtp_client_control_socket));
    } else {
      if (!request_sent) {
        debug(2,"rtp_request_resend called without active stream!");
        request_sent=1;
      }
    }
}
