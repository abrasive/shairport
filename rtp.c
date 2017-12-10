/*
 * Apple RTP protocol handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Copyright (c) Mike Brady 2014 -- 2017
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "player.h"
#include "rtp.h"

void memory_barrier();

void rtp_initialise(rtsp_conn_info *conn) {

  conn->rtp_running = 0;
  // initialise the timer mutex
  int rc = pthread_mutex_init(&conn->reference_time_mutex, NULL);
  if (rc)
    debug(1, "Error initialising reference_time_mutex.");
}

void rtp_terminate(rtsp_conn_info *conn) {

  // destroy the timer mutex
  int rc = pthread_mutex_destroy(&conn->reference_time_mutex);
  if (rc)
    debug(1, "Error destroying reference_time_mutex variable.");
}

void *rtp_audio_receiver(void *arg) {
  debug(2, "Audio receiver -- Server RTP thread starting.");

  // we inherit the signal mask (SIGUSR1)
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  int32_t last_seqno = -1;
  uint8_t packet[2048], *pktp;

  uint64_t time_of_previous_packet_fp = 0;
  float longest_packet_time_interval_us = 0.0;

  // mean and variance calculations from "online_variance" algorithm at
  // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm

  int32_t stat_n = 0;
  float stat_mean = 0.0;
  float stat_M2 = 0.0;

  ssize_t nread;
  while (conn->please_stop == 0) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(conn->audio_socket, &readfds);
    do {
      memory_barrier();
    } while (conn->please_stop == 0 &&
             pselect(conn->audio_socket + 1, &readfds, NULL, NULL, NULL, &pselect_sigset) <= 0);
    if (conn->please_stop != 0) {
      break;
    }
    nread = recv(conn->audio_socket, packet, sizeof(packet), 0);

    uint64_t local_time_now_fp = get_absolute_time_in_fp();
    if (time_of_previous_packet_fp) {
      float time_interval_us =
          (((local_time_now_fp - time_of_previous_packet_fp) * 1000000) >> 32) * 1.0;
      time_of_previous_packet_fp = local_time_now_fp;
      if (time_interval_us > longest_packet_time_interval_us)
        longest_packet_time_interval_us = time_interval_us;
      stat_n += 1;
      float stat_delta = time_interval_us - stat_mean;
      stat_mean += stat_delta / stat_n;
      stat_M2 += stat_delta * (time_interval_us - stat_mean);
      if (stat_n % 2500 == 0) {
        debug(2, "Packet reception interval stats: mean, standard deviation and max for the last "
                 "2,500 packets in microseconds: %10.1f, %10.1f, %10.1f.",
              stat_mean, sqrtf(stat_M2 / (stat_n - 1)), longest_packet_time_interval_us);
        stat_n = 0;
        stat_mean = 0.0;
        stat_M2 = 0.0;
        time_of_previous_packet_fp = 0;
        longest_packet_time_interval_us = 0.0;
      }
    } else {
      time_of_previous_packet_fp = local_time_now_fp;
    }

    if (nread < 0)
      break;

    ssize_t plen = nread;
    uint8_t type = packet[1] & ~0x80;
    if (type == 0x60 || type == 0x56) { // audio data / resend
      pktp = packet;
      if (type == 0x56) {
        pktp += 4;
        plen -= 4;
      }
      seq_t seqno = ntohs(*(unsigned short *)(pktp + 2));
      // increment last_seqno and see if it's the same as the incoming seqno

      if (last_seqno == -1)
        last_seqno = seqno;
      else {
        last_seqno = (last_seqno + 1) & 0xffff;
        // if (seqno != last_seqno)
        //  debug(3, "RTP: Packets out of sequence: expected: %d, got %d.", last_seqno, seqno);
        last_seqno = seqno; // reset warning...
      }
      int64_t timestamp = monotonic_timestamp(ntohl(*(unsigned long *)(pktp + 4)), conn);

      // if (packet[1]&0x10)
      //	debug(1,"Audio packet Extension bit set.");

      pktp += 12;
      plen -= 12;

      // check if packet contains enough content to be reasonable
      if (plen >= 16) {
        player_put_packet(seqno, timestamp, pktp, plen, conn);
        continue;
      }
      if (type == 0x56 && seqno == 0) {
        debug(2, "resend-related request packet received, ignoring.");
        continue;
      }
      debug(1, "Audio receiver -- Unknown RTP packet of type 0x%02X length %d seqno %d", type,
            nread, seqno);
    }
    warn("Audio receiver -- Unknown RTP packet of type 0x%02X length %d.", type, nread);
  }

  debug(3, "Audio receiver -- Server RTP thread interrupted. terminating.");
  close(conn->audio_socket);

  return NULL;
}

void *rtp_control_receiver(void *arg) {
  // we inherit the signal mask (SIGUSR1)

  debug(2, "Control receiver -- Server RTP thread starting.");

  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  conn->reference_timestamp = 0; // nothing valid received yet
  uint8_t packet[2048], *pktp;
  struct timespec tn;
  uint64_t remote_time_of_sync, local_time_now, remote_time_now;
  int64_t sync_rtp_timestamp, rtp_timestamp_less_latency;
  ssize_t nread;
  while (conn->please_stop == 0) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(conn->control_socket, &readfds);
    do {
      memory_barrier();
    } while (conn->please_stop == 0 &&
             pselect(conn->control_socket + 1, &readfds, NULL, NULL, NULL, &pselect_sigset) <= 0);
    if (conn->please_stop != 0) {
      break;
    }
    nread = recv(conn->control_socket, packet, sizeof(packet), 0);
    local_time_now = get_absolute_time_in_fp();
    //        clock_gettime(CLOCK_MONOTONIC,&tn);
    //        local_time_now=((uint64_t)tn.tv_sec<<32)+((uint64_t)tn.tv_nsec<<32)/1000000000;

    if (nread < 0)
      break;

    ssize_t plen = nread;
    if (packet[1] == 0xd4) { // sync data
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
      if (conn->local_to_remote_time_difference) { // need a time packet to be interchanged first...

        remote_time_of_sync = (uint64_t)ntohl(*((uint32_t *)&packet[8])) << 32;
        remote_time_of_sync += ntohl(*((uint32_t *)&packet[12]));

        // debug(1,"Remote Sync Time: %0llx.",remote_time_of_sync);

        rtp_timestamp_less_latency = monotonic_timestamp(ntohl(*((uint32_t *)&packet[4])), conn);
        sync_rtp_timestamp = monotonic_timestamp(ntohl(*((uint32_t *)&packet[16])), conn);

        if (config.use_negotiated_latencies) {
          int64_t la = sync_rtp_timestamp - rtp_timestamp_less_latency + conn->staticLatencyCorrection;
          if (la != config.latency) {
            config.latency = la;
            debug(1,"Using negotiated latency of %lld frames and a static latency correction of %lld",sync_rtp_timestamp - rtp_timestamp_less_latency,conn->staticLatencyCorrection);
          }
        }

        if (packet[0] & 0x10) {
          // if it's a packet right after a flush or resume
          sync_rtp_timestamp += 352; // add frame_size -- can't see a reference to this anywhere,
                                     // but it seems to get everything into sync.
          // it's as if the first sync after a flush or resume is the timing of the next packet
          // after the one whose RTP is given. Weird.
        }
        pthread_mutex_lock(&conn->reference_time_mutex);
        conn->remote_reference_timestamp_time = remote_time_of_sync;
        conn->reference_timestamp_time =
            remote_time_of_sync - conn->local_to_remote_time_difference;
        conn->reference_timestamp = sync_rtp_timestamp;
        pthread_mutex_unlock(&conn->reference_time_mutex);
        // debug(1,"New Reference timestamp and timestamp time...");
        // get estimated remote time now
        // remote_time_now = local_time_now + local_to_remote_time_difference;

        // debug(1,"Sync Time is %lld us late (remote
        // times).",((remote_time_now-remote_time_of_sync)*1000000)>>32);
        // debug(1,"Sync Time is %lld us late (local
        // times).",((local_time_now-reference_timestamp_time)*1000000)>>32);
      } else {
        debug(1, "Sync packet received before we got a timing packet back.");
      }
    } else if (packet[1] == 0xd6) { // resent audio data in the control path -- whaale only?
      // debug(1, "Control Port -- Retransmitted Audio Data Packet received.");
      pktp = packet + 4;
      plen -= 4;
      seq_t seqno = ntohs(*(unsigned short *)(pktp + 2));

      int64_t timestamp = monotonic_timestamp(ntohl(*(unsigned long *)(pktp + 4)), conn);

      pktp += 12;
      plen -= 12;

      // check if packet contains enough content to be reasonable
      if (plen >= 16) {
        player_put_packet(seqno, timestamp, pktp, plen, conn);
        continue;
      } else {
        debug(3, "Too-short retransmitted audio packet received in control port, ignored.");
      }
    } else
      debug(1, "Control Port -- Unknown RTP packet of type 0x%02X length %d, ignored.", packet[1],
            nread);
  }

  debug(3, "Control RTP thread interrupted. terminating.");
  close(conn->control_socket);

  return NULL;
}

void *rtp_timing_sender(void *arg) {
  debug(2, "Timing sender thread starting.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  struct timing_request {
    char leader;
    char type;
    uint16_t seqno;
    uint32_t filler;
    uint64_t origin, receive, transmit;
  };

  uint64_t request_number = 0;

  struct timing_request req; // *not* a standard RTCP NACK

  req.leader = 0x80;
  req.type = 0xd2; // Timing request
  req.filler = 0;
  req.seqno = htons(7);

  conn->time_ping_count = 0;

  // we inherit the signal mask (SIGUSR1)
  while (conn->timing_sender_stop == 0) {
    // debug(1,"Send a timing request");

    if (!conn->rtp_running)
      debug(1, "rtp_timing_sender called without active stream in RTSP conversation thread %d!",
            conn->connection_number);

    // debug(1, "Requesting ntp timestamp exchange.");

    req.filler = 0;
    req.origin = req.receive = req.transmit = 0;

    //    clock_gettime(CLOCK_MONOTONIC,&dtt);
    conn->departure_time = get_absolute_time_in_fp();
    socklen_t msgsize = sizeof(struct sockaddr_in);
#ifdef AF_INET6
    if (conn->rtp_client_timing_socket.SAFAMILY == AF_INET6) {
      msgsize = sizeof(struct sockaddr_in6);
    }
#endif
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(conn->timing_socket, &writefds);
    do {
      memory_barrier();
    } while (conn->timing_sender_stop == 0 &&
             pselect(conn->timing_socket + 1, NULL, &writefds, NULL, NULL, &pselect_sigset) <= 0);
    if (conn->timing_sender_stop != 0) {
      break;
    }
    if (sendto(conn->timing_socket, &req, sizeof(req), 0,
               (struct sockaddr *)&conn->rtp_client_timing_socket, msgsize) == -1) {
      perror("Error sendto-ing to timing socket");
    }
    request_number++;
    if (request_number <= 4)
      usleep(500000);
    else
      sleep(3);
  }
  debug(3, "rtp_timing_sender thread interrupted. terminating.");
  return NULL;
}

void *rtp_timing_receiver(void *arg) {
  debug(2, "Timing receiver -- Server RTP thread starting.");
  // we inherit the signal mask (SIGUSR1)
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  uint8_t packet[2048], *pktp;
  ssize_t nread;
  conn->timing_sender_stop = 0;
  pthread_t timer_requester;
  pthread_create(&timer_requester, NULL, &rtp_timing_sender, arg);
  //    struct timespec att;
  uint64_t distant_receive_time, distant_transmit_time, arrival_time, return_time, transit_time,
      processing_time;
  local_to_remote_time_jitters = 0;
  local_to_remote_time_jitters_count = 0;
  uint64_t first_remote_time = 0;
  uint64_t first_local_time = 0;

  uint64_t first_local_to_remote_time_difference = 0;
  uint64_t first_local_to_remote_time_difference_time;
  uint64_t l2rtd = 0;
  while (conn->please_stop == 0) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(conn->timing_socket, &readfds);
    do {
      memory_barrier();
    } while (conn->please_stop == 0 &&
             pselect(conn->timing_socket + 1, &readfds, NULL, NULL, NULL, &pselect_sigset) <= 0);
    if (conn->please_stop != 0) {
      break;
    }
    nread = recv(conn->timing_socket, packet, sizeof(packet), 0);
    arrival_time = get_absolute_time_in_fp();
    //      clock_gettime(CLOCK_MONOTONIC,&att);

    if (nread < 0)
      break;

    ssize_t plen = nread;
    // debug(1,"Packet Received on Timing Port.");
    if (packet[1] == 0xd3) { // timing reply
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

      // arrival_time = ((uint64_t)att.tv_sec<<32)+((uint64_t)att.tv_nsec<<32)/1000000000;
      // departure_time = ((uint64_t)dtt.tv_sec<<32)+((uint64_t)dtt.tv_nsec<<32)/1000000000;

      return_time = arrival_time - conn->departure_time;

      // uint64_t rtus = (return_time*1000000)>>32; debug(1,"Time ping turnaround time: %lld
      // us.",rtus);

      // distant_receive_time =
      // ((uint64_t)ntohl(*((uint32_t*)&packet[16])))<<32+ntohl(*((uint32_t*)&packet[20]));

      distant_receive_time = (uint64_t)ntohl(*((uint32_t *)&packet[16])) << 32;
      distant_receive_time += ntohl(*((uint32_t *)&packet[20]));

      // distant_transmit_time =
      // ((uint64_t)ntohl(*((uint32_t*)&packet[24])))<<32+ntohl(*((uint32_t*)&packet[28]));

      distant_transmit_time = (uint64_t)ntohl(*((uint32_t *)&packet[24])) << 32;
      distant_transmit_time += ntohl(*((uint32_t *)&packet[28]));

      processing_time = distant_transmit_time - distant_receive_time;

      // debug(1,"Return trip time: %lluuS, remote processing time:
      // %lluuS.",(return_time*1000000)>>32,(processing_time*1000000)>>32);

      uint64_t local_time_by_remote_clock = distant_transmit_time + return_time / 2;

      unsigned int cc, chosen;
      for (cc = time_ping_history - 1; cc > 0; cc--) {
        conn->time_pings[cc] = conn->time_pings[cc - 1];
        conn->time_pings[cc].dispersion = (conn->time_pings[cc].dispersion * 110) /
                                          100; // make the dispersions 'age' by this rational factor
      }
      // these are for diagnostics only -- not used
      conn->time_pings[0].local_time = arrival_time;
      conn->time_pings[0].remote_time = distant_transmit_time;

      conn->time_pings[0].local_to_remote_difference = local_time_by_remote_clock - arrival_time;
      conn->time_pings[0].dispersion = return_time;
      if (conn->time_ping_count < time_ping_history)
        conn->time_ping_count++;

      uint64_t local_time_chosen = arrival_time;
      ;
      uint64_t remote_time_chosen = distant_transmit_time;
      // now pick the timestamp with the lowest dispersion
      uint64_t l2rtd = conn->time_pings[0].local_to_remote_difference;
      uint64_t tld = conn->time_pings[0].dispersion;
      chosen = 0;
      for (cc = 1; cc < conn->time_ping_count; cc++)
        if (conn->time_pings[cc].dispersion < tld) {
          l2rtd = conn->time_pings[cc].local_to_remote_difference;
          chosen = cc;
          tld = conn->time_pings[cc].dispersion;
          local_time_chosen = conn->time_pings[cc].local_time;
          remote_time_chosen = conn->time_pings[cc].remote_time;
        }
      int64_t ji;

      if (conn->time_ping_count > 1) {
        if (l2rtd > conn->local_to_remote_time_difference) {
          local_to_remote_time_jitters =
              local_to_remote_time_jitters + l2rtd - conn->local_to_remote_time_difference;
          ji = l2rtd - conn->local_to_remote_time_difference;
        } else {
          local_to_remote_time_jitters =
              local_to_remote_time_jitters + conn->local_to_remote_time_difference - l2rtd;
          ji = -(conn->local_to_remote_time_difference - l2rtd);
        }
        local_to_remote_time_jitters_count += 1;
      }
      // uncomment below to print jitter between client's clock and oour clock
      // int64_t rtus = (tld*1000000)>>32; ji = (ji*1000000)>>32; debug(1,"Choosing time difference
      // with dispersion of %lld us with delta of %lld us",rtus,ji);

      conn->local_to_remote_time_difference = l2rtd;
      if (first_local_to_remote_time_difference == 0) {
        first_local_to_remote_time_difference = conn->local_to_remote_time_difference;
        first_local_to_remote_time_difference_time = get_absolute_time_in_fp();
      }

      int64_t clock_drift, clock_drift_in_usec;
      double clock_drift_ppm = 0.0;
      if (first_local_time == 0) {
        first_local_time = local_time_chosen;
        first_remote_time = remote_time_chosen;
        clock_drift = 0;
      } else {
        uint64_t local_time_change = local_time_chosen - first_local_time;
        uint64_t remote_time_change = remote_time_chosen - first_remote_time;

        if (remote_time_change >= local_time_change)
          clock_drift = remote_time_change - local_time_change;
        else
          clock_drift = -(local_time_change - remote_time_change);
        if (clock_drift >= 0)
          clock_drift_in_usec = (clock_drift * 1000000) >> 32;
        else
          clock_drift_in_usec = -(((-clock_drift) * 1000000) >> 32);
        clock_drift_ppm = (1.0 * clock_drift_in_usec) / (local_time_change >> 32);
      }

      int64_t source_drift_usec;
      if (conn->play_segment_reference_frame != 0) {
        int64_t reference_timestamp;
        uint64_t reference_timestamp_time, remote_reference_timestamp_time;
        get_reference_timestamp_stuff(&reference_timestamp, &reference_timestamp_time,
                                      &remote_reference_timestamp_time, conn);
        uint64_t frame_difference = 0;
        if (reference_timestamp >= conn->play_segment_reference_frame)
          frame_difference =
              (uint64_t)reference_timestamp - (uint64_t)conn->play_segment_reference_frame;
        else // rollover
          frame_difference = (uint64_t)reference_timestamp + 0x100000000 -
                             (uint64_t)conn->play_segment_reference_frame;
        uint64_t frame_time_difference_calculated = (((uint64_t)frame_difference << 32) / 44100);
        uint64_t frame_time_difference_actual =
            remote_reference_timestamp_time -
            conn->play_segment_reference_frame_remote_time; // this is all done by reference to the
                                                            // sources' system clock
        // debug(1,"%llu frames since play started, %llu usec calculated, %llu usec
        // actual",frame_difference, (frame_time_difference_calculated*1000000)>>32,
        // (frame_time_difference_actual*1000000)>>32);
        if (frame_time_difference_calculated >=
            frame_time_difference_actual) // i.e. if the time it should have taken to send the
                                          // packets is greater than the actual time difference
                                          // measured on the source clock
          // then the source DAC's clock is running fast relative to the source system clock
          source_drift_usec = frame_time_difference_calculated - frame_time_difference_actual;
        else
          // otherwise the source DAC's clock is running slow relative to the source system clock
          source_drift_usec = -(frame_time_difference_actual - frame_time_difference_calculated);
      } else
        source_drift_usec = 0;
      source_drift_usec = (source_drift_usec * 1000000) >> 32; // turn it to microseconds

      // long current_delay = 0;
      // if (config.output->delay) {
      //       config.output->delay(&current_delay);
      //}
      //  Useful for troubleshooting:
      // debug(1, "clock_drift_ppm %f\tchosen %5d\tsource_drift_usec %10.1lld\treturn_time_in_usec
      // %10.1llu",
      // clock_drift_ppm,
      // chosen,
      //(session_corrections*1000000)/44100,
      // current_delay,
      // source_drift_usec,
      // buffer_occupancy,
      //(return_time*1000000)>>32);

    } else {
      debug(1, "Timing port -- Unknown RTP packet of type 0x%02X length %d.", packet[1], nread);
    }
  }

  debug(3, "Timing thread interrupted. terminating.");
  conn->timing_sender_stop = 1;
  void *retval;
  pthread_kill(timer_requester, SIGUSR1);
  debug(3, "Wait for timer requester to exit.");
  pthread_join(timer_requester, &retval);
  debug(3, "Closed and terminated timer requester thread.");
  debug(3, "Timing RTP thread terminated.");
  close(conn->timing_socket);

  return NULL;
}

static int bind_port(int ip_family, const char *self_ip_address, uint32_t scope_id, int *sock) {
  // look for a port in the range, if any was specified.
  int desired_port = config.udp_port_base;
  int ret;

  int local_socket = socket(ip_family, SOCK_DGRAM, IPPROTO_UDP);
  if (local_socket == -1)
    die("Could not allocate a socket.");
  SOCKADDR myaddr;
  do {
    memset(&myaddr, 0, sizeof(myaddr));
    if (ip_family == AF_INET) {
      struct sockaddr_in *sa = (struct sockaddr_in *)&myaddr;
      sa->sin_family = AF_INET;
      sa->sin_port = ntohs(desired_port);
      inet_pton(AF_INET, self_ip_address, &(sa->sin_addr));
      ret = bind(local_socket, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
    }
#ifdef AF_INET6
    if (ip_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&myaddr;
      sa6->sin6_family = AF_INET6;
      sa6->sin6_port = ntohs(desired_port);
      inet_pton(AF_INET6, self_ip_address, &(sa6->sin6_addr));
      sa6->sin6_scope_id = scope_id;
      ret = bind(local_socket, (struct sockaddr *)sa6, sizeof(struct sockaddr_in6));
    }
#endif

  } while ((ret < 0) && (errno == EADDRINUSE) && (desired_port != 0) &&
           (++desired_port < config.udp_port_base + config.udp_port_range));

  // debug(1,"UDP port chosen: %d.",desired_port);

  if (ret < 0) {
    close(local_socket);
    die("error: could not bind a UDP port! Check the udp_port_range is large enough (>= 10) or "
        "check for restrictive firewall settings or a bad router!");
  }

  int sport;
  SOCKADDR local;
  socklen_t local_len = sizeof(local);
  getsockname(local_socket, (struct sockaddr *)&local, &local_len);
#ifdef AF_INET6
  if (local.SAFAMILY == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&local;
    sport = ntohs(sa6->sin6_port);
  } else
#endif
  {
    struct sockaddr_in *sa = (struct sockaddr_in *)&local;
    sport = ntohs(sa->sin_port);
  }
  fcntl(local_socket, F_SETFL, O_NONBLOCK);

  *sock = local_socket;
  return sport;
}

void rtp_setup(SOCKADDR *local, SOCKADDR *remote, int cport, int tport, int *lsport, int *lcport,
               int *ltport, rtsp_conn_info *conn) {

  // this gets the local and remote ip numbers (and ports used for the TCD stuff)
  // we use the local stuff to specify the address we are coming from and
  // we use the remote stuff to specify where we're goint to

  if (conn->rtp_running)
    die("rtp_setup called with active stream!");

  debug(2, "rtp_setup: cport=%d tport=%d.", cport, tport);

  // print out what we know about the client
  void *client_addr, *self_addr;
  int client_port, self_port;
  char client_port_str[64];
  char self_addr_str[64];

  conn->connection_ip_family =
      remote->SAFAMILY; // keep information about the kind of ip of the client

#ifdef AF_INET6
  if (conn->connection_ip_family == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)remote;
    client_addr = &(sa6->sin6_addr);
    client_port = ntohs(sa6->sin6_port);
    sa6 = (struct sockaddr_in6 *)local;
    self_addr = &(sa6->sin6_addr);
    self_port = ntohs(sa6->sin6_port);
    conn->self_scope_id = sa6->sin6_scope_id;
  }
#endif
  if (conn->connection_ip_family == AF_INET) {
    struct sockaddr_in *sa4 = (struct sockaddr_in *)remote;
    client_addr = &(sa4->sin_addr);
    client_port = ntohs(sa4->sin_port);
    sa4 = (struct sockaddr_in *)local;
    self_addr = &(sa4->sin_addr);
    self_port = ntohs(sa4->sin_port);
  }

  inet_ntop(conn->connection_ip_family, client_addr, conn->client_ip_string,
            sizeof(conn->client_ip_string));
  inet_ntop(conn->connection_ip_family, self_addr, conn->self_ip_string,
            sizeof(conn->self_ip_string));

  debug(1, "Set up play connection from %s to self at %s on RTSP conversation thread %d.",
        conn->client_ip_string, conn->self_ip_string, conn->connection_number);

  // set up a the record of the remote's control socket
  struct addrinfo hints;
  struct addrinfo *servinfo;

  memset(&conn->rtp_client_control_socket, 0, sizeof(conn->rtp_client_control_socket));
  memset(&hints, 0, sizeof hints);
  hints.ai_family = conn->connection_ip_family;
  hints.ai_socktype = SOCK_DGRAM;
  char portstr[20];
  snprintf(portstr, 20, "%d", cport);
  if (getaddrinfo(conn->client_ip_string, portstr, &hints, &servinfo) != 0)
    die("Can't get address of client's control port");

#ifdef AF_INET6
  if (servinfo->ai_family == AF_INET6) {
    memcpy(&conn->rtp_client_control_socket, servinfo->ai_addr, sizeof(struct sockaddr_in6));
    // ensure the scope id matches that of remote. this is needed for link-local addresses.
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&conn->rtp_client_control_socket;
    sa6->sin6_scope_id = conn->self_scope_id;
  } else
#endif
    memcpy(&conn->rtp_client_control_socket, servinfo->ai_addr, sizeof(struct sockaddr_in));
  freeaddrinfo(servinfo);

  // set up a the record of the remote's timing socket
  memset(&conn->rtp_client_timing_socket, 0, sizeof(conn->rtp_client_timing_socket));
  memset(&hints, 0, sizeof hints);
  hints.ai_family = conn->connection_ip_family;
  hints.ai_socktype = SOCK_DGRAM;
  snprintf(portstr, 20, "%d", tport);
  if (getaddrinfo(conn->client_ip_string, portstr, &hints, &servinfo) != 0)
    die("Can't get address of client's timing port");
#ifdef AF_INET6
  if (servinfo->ai_family == AF_INET6) {
    memcpy(&conn->rtp_client_timing_socket, servinfo->ai_addr, sizeof(struct sockaddr_in6));
    // ensure the scope id matches that of remote. this is needed for link-local addresses.
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&conn->rtp_client_timing_socket;
    sa6->sin6_scope_id = conn->self_scope_id;
  } else
#endif
    memcpy(&conn->rtp_client_timing_socket, servinfo->ai_addr, sizeof(struct sockaddr_in));
  freeaddrinfo(servinfo);

  // now, we open three sockets -- one for the audio stream, one for the timing and one for the
  // control

  *lsport = bind_port(conn->connection_ip_family, conn->self_ip_string, conn->self_scope_id,
                      &conn->audio_socket);
  *lcport = bind_port(conn->connection_ip_family, conn->self_ip_string, conn->self_scope_id,
                      &conn->control_socket);
  *ltport = bind_port(conn->connection_ip_family, conn->self_ip_string, conn->self_scope_id,
                      &conn->timing_socket);

  debug(2, "listening for audio, control and timing on ports %d, %d, %d.", *lsport, *lcport,
        *ltport);

  conn->reference_timestamp = 0;
  // pthread_create(&rtp_audio_thread, NULL, &rtp_audio_receiver, NULL);
  // pthread_create(&rtp_control_thread, NULL, &rtp_control_receiver, NULL);
  // pthread_create(&rtp_timing_thread, NULL, &rtp_timing_receiver, NULL);

  conn->request_sent = 0;
  conn->rtp_running = 1;
}

void get_reference_timestamp_stuff(int64_t *timestamp, uint64_t *timestamp_time,
                                   uint64_t *remote_timestamp_time, rtsp_conn_info *conn) {
  // types okay
  pthread_mutex_lock(&conn->reference_time_mutex);
  *timestamp = conn->reference_timestamp;
  *timestamp_time = conn->reference_timestamp_time;
  // if ((*timestamp == 0) && (*timestamp_time == 0)) {
  //  debug(1,"Reference timestamp is invalid.");
  //}
  *remote_timestamp_time = conn->remote_reference_timestamp_time;
  pthread_mutex_unlock(&conn->reference_time_mutex);
}

void clear_reference_timestamp(rtsp_conn_info *conn) {
  pthread_mutex_lock(&conn->reference_time_mutex);
  conn->reference_timestamp = 0;
  conn->reference_timestamp_time = 0;
  pthread_mutex_unlock(&conn->reference_time_mutex);
}

void rtp_request_resend(seq_t first, uint32_t count, rtsp_conn_info *conn) {
  if (conn->rtp_running) {
    // if (!request_sent) {
    debug(3, "requesting resend of %d packets starting at %u.", count, first);
    //  request_sent = 1;
    //}

    char req[8]; // *not* a standard RTCP NACK
    req[0] = 0x80;
    req[1] = (char)0x55 | (char)0x80;            // Apple 'resend'
    *(unsigned short *)(req + 2) = htons(1);     // our seqnum
    *(unsigned short *)(req + 4) = htons(first); // missed seqnum
    *(unsigned short *)(req + 6) = htons(count); // count
    socklen_t msgsize = sizeof(struct sockaddr_in);
#ifdef AF_INET6
    if (conn->rtp_client_control_socket.SAFAMILY == AF_INET6) {
      msgsize = sizeof(struct sockaddr_in6);
    }
#endif
    if (sendto(conn->audio_socket, req, sizeof(req), 0,
               (struct sockaddr *)&conn->rtp_client_control_socket, msgsize) == -1) {
      perror("Error sendto-ing to audio socket");
    }
  } else {
    // if (!request_sent) {
    debug(2, "rtp_request_resend called without active stream!");
    //  request_sent = 1;
    //}
  }
}
