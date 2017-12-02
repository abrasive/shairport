/*
 * DACP protocol handler. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017
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

#include "dacp.h"
#include "common.h"
#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct  {
  uint16_t port;
  short connection_family; // AF_INET6 or AF_INET
  uint32_t scope_id; // if it's an ipv6 connection, this will be its scope id
  char ip_string[INET6_ADDRSTRLEN]; // the ip string pointing to the client
  uint32_t active_remote_id; // send this when you want to send remote control commands
} dacp_server_record;

pthread_t dacp_monitor_thread;
dacp_server_record dacp_server;

static pthread_mutex_t dacp_conversation_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dacp_server_information_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dacp_server_information_cv = PTHREAD_COND_INITIALIZER;

// this will be running on the thread of its caller, not of the conversation thread...
void set_dacp_server_information(rtsp_conn_info* conn) { // tell the DACP conversation thread that the port has been set or changed
  pthread_mutex_lock(&dacp_server_information_lock);
  
  dacp_server.port = conn->dacp_port;
  dacp_server.connection_family = conn->connection_ip_family;
  dacp_server.scope_id = conn-> self_scope_id;
  strncpy(dacp_server.ip_string,conn->client_ip_string,INET6_ADDRSTRLEN);
  dacp_server.active_remote_id = conn->dacp_active_remote;
  
  pthread_cond_signal(&dacp_server_information_cv);
  pthread_mutex_unlock(&dacp_server_information_lock);
}

void *dacp_monitor_thread_code(void *na) {
  int scan_index = 0;
  debug(1, "DACP monitor thread started.");
  // wait until we get a valid port number to begin monitoring it
  while (1) {
    pthread_mutex_lock(&dacp_server_information_lock);
    while (dacp_server.port == 0) {
      debug(1,"Wait for a valid DACP port");
      pthread_cond_wait(&dacp_server_information_cv, &dacp_server_information_lock);
    }
    pthread_mutex_unlock(&dacp_server_information_lock);
    debug(1,"DACP Server ID \"%u\" at \"%s:%u\", scan %d.",dacp_server.active_remote_id,dacp_server.ip_string,dacp_server.port,scan_index++);
    sleep(3);
  }
  debug(1, "DACP monitor thread exiting.");
  pthread_exit(NULL);
}

void dacp_monitor_start() {
  memset(&dacp_server,0,sizeof(dacp_server_record));
  pthread_create(&dacp_monitor_thread, NULL, dacp_monitor_thread_code, NULL);
}

uint32_t dacp_tlv_crawl(char **p, int32_t *length) {
  char typecode[5];
  memcpy(typecode, *p, 4);
  typecode[4] = '\0';
  uint32_t type = ntohl(*(uint32_t *)*p);
  *p += 4;
  *length = ntohl(*(int32_t *)*p);
  *p += 4 + *length;
  // debug(1,"Type seen: '%s' of length %d",typecode,*length);
  return type;
}

ssize_t dacp_send_client_command(rtsp_conn_info *conn, const char *command, char *response,
                                 size_t max_response_length) {
  ssize_t reply_size = -1;
  // try to do this transaction on the DACP server, but don't wait for more than 20 ms to be allowed
  // to do it.
  struct timespec mutex_wait_time;
  mutex_wait_time.tv_sec = 0;
  mutex_wait_time.tv_nsec = 20000000; // 20 ms

  if (conn->rtp_running) {
    if (conn->dacp_port == 0) {
      debug(1, "Can't send a remote request: no valid active remote.");
    } else {

      struct addrinfo hints, *res;
      int sockfd;

      char message[20000], server_reply[2000], portstring[10], server[256];
      memset(&message, 0, sizeof(message));
      if ((response) && (max_response_length))
        memset(response, 0, max_response_length);
      else
        memset(&server_reply, 0, sizeof(server_reply));
      memset(&portstring, 0, sizeof(portstring));

      if (conn->connection_ip_family == AF_INET6) {
        sprintf(server, "%s%%%u", conn->client_ip_string, conn->self_scope_id);
      } else {
        strcpy(server, conn->client_ip_string);
      }

      sprintf(portstring, "%u", conn->dacp_port);

      // first, load up address structs with getaddrinfo():

      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;

      getaddrinfo(server, portstring, &hints, &res);

      // only do this one at a time -- not sure it is necessary, but better safe than sorry

      int mutex_reply = pthread_mutex_timedlock(&dacp_conversation_lock, &mutex_wait_time);
      if (mutex_reply == 0) {

        // make a socket:

        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        if (sockfd == -1) {
          debug(1, "Could not create socket");
        } else {

          // connect!

          if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
            debug(1, "connect failed. Error");
          } else {

            sprintf(message,
                    "GET /ctrl-int/1/%s HTTP/1.1\r\nHost: %s:%u\r\nActive-Remote: %u\r\n\r\n",
                    command, conn->client_ip_string, conn->dacp_port, conn->dacp_active_remote);

            // Send command

            if (send(sockfd, message, strlen(message), 0) < 0) {
              debug(1, "Send failed");
            }

            // Receive a reply from the server
            if ((response) && (max_response_length))
              reply_size = recv(sockfd, response, max_response_length, 0);
            else
              reply_size = recv(sockfd, server_reply, sizeof(server_reply), 0);
            if (reply_size < 0) {
              debug(1, "recv failed");
            }
            close(sockfd);
          }
        }
        pthread_mutex_unlock(&dacp_conversation_lock);
      } else {
        debug(1,
              "Could not acquire a lock on the dacp transmit/receive section. Possible timeout?");
      }
    }
  } else {
    debug(1, "Request to pause non-existent play stream -- ignored.");
  }
  return reply_size;
}

int32_t dacp_get_client_volume(rtsp_conn_info *conn) {
  char server_reply[2000];
  int32_t overall_volume = -1;

  ssize_t reply_size = dacp_send_client_command(conn, "getproperty?properties=dmcp.volume",
                                                server_reply, sizeof(server_reply));
  if (reply_size >= 0) {
    if (strstr(server_reply, "HTTP/1.1 200") == server_reply) { // if we get an okay
      char *sp = strstr(server_reply, "Content-Length: ");
      if (sp) { // there is something there
        sp += strlen("Content-Length: ");
        int le = atoi(sp);
        if (le == 32) {
          sp = strstr(sp, "\r") + 4 + 28;
          uint32_t *np = (uint32_t *)sp;
          overall_volume = ntohl(*np);
          // debug(1,"Overall Volume is %d.",overall_volume);
        } else {
          debug(1, "Can't find the volume tag");
        }
      } else {
        debug(1, "Can't find any content in volume control request");
      }
    } else {
      debug(1, "Unexpected response to dacp volume control request");
    }
  } else {
    debug(1, "Error asking for dacp volume.");
  }
  return overall_volume;
}

int dacp_set_include_speaker_volume(rtsp_conn_info *conn, int64_t machine_number, int32_t vo) {
  char server_reply[2000];
  int reply = -1; // will bve fixed if there is no problem
  char message[1000];
  memset(message, 0, sizeof(message));
  sprintf(message, "setproperty?include-speaker-id=%ld&dmcp.volume=%d", machine_number, vo);
  // debug(1,"sending \"%s\"",message);
  ssize_t reply_size = dacp_send_client_command(conn, message, server_reply, sizeof(server_reply));
  if (reply_size >= 0) {
    if (strstr(server_reply, "HTTP/1.1 204") == server_reply) {
      // debug(1,"dacp_set_include_speaker_volume successful.");
      reply = 0;
    }
  } else {
    debug(1, "dacp_set_include_speaker_volume unsuccessful.");
  }
  return reply;
}

int dacp_set_speaker_volume(rtsp_conn_info *conn, int64_t machine_number, int32_t vo) {
  char server_reply[2000];
  int reply = -1; // will bve fixed if there is no problem
  char message[1000];
  memset(message, 0, sizeof(message));
  sprintf(message, "setproperty?speaker-id=%ld&dmcp.volume=%d", machine_number, vo);
  // debug(1,"sending \"%s\"",message);
  ssize_t reply_size = dacp_send_client_command(conn, message, server_reply, sizeof(server_reply));
  if (reply_size >= 0) {
    if (strstr(server_reply, "HTTP/1.1 204") == server_reply) {
      // debug(1,"dacp_set_speaker_volume successful.");
      reply = 0;
    }
  } else {
    debug(1, "dacp_set_speaker_volume unsuccessful.");
  }
  return reply;
}

int dacp_get_speaker_list(rtsp_conn_info *conn, dacp_spkr_stuff *speaker_info,
                          int max_size_of_array) {
  char server_reply[2000];
  int speaker_index = -1; // will be incremented before use
  int reply = -1;         // will bve fixed if there is no problem
  ssize_t reply_size =
      dacp_send_client_command(conn, "getspeakers", server_reply, sizeof(server_reply));
  if (reply_size >= 0) {
    if (strstr(server_reply, "HTTP/1.1 200") == server_reply) { // if we get an okay
      char *sp = strstr(server_reply, "Content-Length: ");
      if (sp) { // there is something there
        sp += strlen("Content-Length: ");
        int32_t le = atoi(sp);
        int32_t item_size = 0;
        sp = strstr(sp, "\r") + 4;
        if (dacp_tlv_crawl(&sp, &item_size) == 'casp') {
          //          debug(1,"Speakers:",item_size);
          sp -= item_size; // drop down into the array -- don't skip over it
          le -= 8;
          while (le >= 8) {
            uint32_t type = dacp_tlv_crawl(&sp, &item_size);
            if (type == 'mdcl') { // drop down into the dictionary -- don't skip over it
              // debug(1,">>>> Dictionary:");
              sp -= item_size;
              le -= 8;
              speaker_index++;
              if (speaker_index == max_size_of_array)
                return -1; // too many speakers
              speaker_info[speaker_index].active = 0;
              speaker_info[speaker_index].speaker_number = 0;
              speaker_info[speaker_index].volume = 0;
              speaker_info[speaker_index].name = NULL;
            } else {
              le -= item_size + 8;
              char *t;
              char u;
              int32_t r;
              int64_t s, v;
              switch (type) {
              case 'minm':
                t = sp - item_size;
                speaker_info[speaker_index].name = strndup(t, item_size);
                // debug(1," \"%s\"",speaker_info[speaker_index].name);
                break;
              /*
                              case 'cads':
                                t = sp-item_size;
                                r = ntohl(*(int32_t*)(t));
                                //debug(1,"CADS: \"%d\".",r);
                                break;
              */
              case 'cmvo':
                t = sp - item_size;
                r = ntohl(*(int32_t *)(t));
                speaker_info[speaker_index].volume = r;
                // debug(1,"Volume: \"%d\".",r);
                break;
              case 'msma':
                t = sp - item_size;
                s = ntohl(*(uint32_t *)(t));
                s = s << 32;
                t += 4;
                v = (ntohl(*(uint32_t *)(t))) & 0xffffffff;
                s += v;
                speaker_info[speaker_index].speaker_number = s;
                // debug(1,"Speaker machine number: %ld",s);
                break;

              case 'caia':
                speaker_info[speaker_index].active = 1;
                break;
              /*
                              case 'caip':
                              case 'cavd':
                              case 'caiv':
                                t = sp-item_size;
                                u = *t;
                                //debug(1,"Value: \"%d\".",u);
                                break;
              */
              default:
                break;
              }
            }
          }
          // debug(1,"Total of %d speakers found. Here are the active ones:",speaker_index+1);
          reply = speaker_index + 1; // number of speaker entries in the array
        } else {
          debug(1, "Speaker array not found.");
        }
        /*
                int i;
                for (i=0;i<le;i++) {
                  if (*sp < ' ')
                    debug(1,"%d  %02x", i, *sp);
                  else
                    debug(1,"%d  %02x  '%c'", i, *sp,*sp);
                  sp++;
                }
        */

      } else {
        debug(1, "Can't find any content in dacp speakers request");
      }
    } else {
      debug(1, "Unexpected response to dacp speakers request");
    }
  } else {
    debug(1, "Error asking for dacp speakers.");
  }
  return reply;
}
