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

// Information about the four-character codes is from many sources, with thanks, including
// https://github.com/melloware/dacp-net/blob/master/Melloware.DACP/

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

#include "metadata_hub.h"
#include "tinyhttp/http.h"

typedef struct {
  uint16_t port;
  short connection_family;          // AF_INET6 or AF_INET
  uint32_t scope_id;                // if it's an ipv6 connection, this will be its scope id
  char ip_string[INET6_ADDRSTRLEN]; // the ip string pointing to the client
  uint32_t active_remote_id;        // send this when you want to send remote control commands
} dacp_server_record;

pthread_t dacp_monitor_thread;
dacp_server_record dacp_server;

// HTTP Response data/funcs (See the tinyhttp example.cpp file for more on this.)
struct HttpResponse {
  void *body;            // this will be a malloc'ed pointer
  ssize_t malloced_size; // this will be its allocated size
  ssize_t size;          // the current size of the content
  int code;
};

static void *response_realloc(void *opaque, void *ptr, int size) { return realloc(ptr, size); }

static void response_body(void *opaque, const char *data, int size) {
  struct HttpResponse *response = (struct HttpResponse *)opaque;

  ssize_t space_available = response->malloced_size - response->size;
  if (space_available < size) {
    // debug(1,"Getting more space for the response -- need %d bytes but only %ld bytes left.\n",
    // size,
    //       size - space_available);
    ssize_t size_requested = size - space_available + response->malloced_size + 16384;
    void *t = realloc(response->body, size_requested);
    response->malloced_size = size_requested;
    if (t)
      response->body = t;
    else {
      debug(1, "Can't allocate any more space for parser.\n");
      exit(-1);
    }
  }
  memcpy(response->body + response->size, data, size);
  response->size += size;
}

static void response_header(void *opaque, const char *ckey, int nkey, const char *cvalue,
                            int nvalue) { /* example doesn't care about headers */
}

static void response_code(void *opaque, int code) {
  struct HttpResponse *response = (struct HttpResponse *)opaque;
  response->code = code;
}

static const struct http_funcs responseFuncs = {
    response_realloc, response_body, response_header, response_code,
};

static pthread_mutex_t dacp_conversation_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dacp_server_information_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dacp_server_information_cv = PTHREAD_COND_INITIALIZER;

int dacp_send_command(const char *command, char **body, ssize_t *bodysize) {

  // will malloc space for the body or set it to NULL -- the caller should free it.

  // Using some custom HTTP-like return codes
  //  498 Bad Address information for the DACP server
  //  497 Can't establish a socket to the DACP server
  //  496 Can't connect to the DACP server
  //  495 Error receiving response
  //  494 This client is already busy
  //  493 Client failed to send a message

  // try to do this transaction on the DACP server, but don't wait for more than 20 ms to be allowed
  // to do it.
  struct timespec mutex_wait_time;
  mutex_wait_time.tv_sec = 0;
  mutex_wait_time.tv_nsec = 20000000; // 20 ms

  struct addrinfo hints, *res;
  int sockfd;

  struct HttpResponse response;
  response.body = NULL;
  response.malloced_size = 0;
  response.size = 0;
  response.code = 0;

  char portstring[10], server[256], message[1024];
  memset(&portstring, 0, sizeof(portstring));
  if (dacp_server.connection_family == AF_INET6) {
    sprintf(server, "%s%%%u", dacp_server.ip_string, dacp_server.scope_id);
  } else {
    strcpy(server, dacp_server.ip_string);
  }
  sprintf(portstring, "%u", dacp_server.port);

  // first, load up address structs with getaddrinfo():

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  // debug(1, "DHCP port string is \"%s:%s\".", server, portstring);

  int ires = getaddrinfo(server, portstring, &hints, &res);
  if (ires) {
    // debug(1,"Error %d \"%s\" at getaddrinfo.",ires,gai_strerror(ires));
    response.code = 498; // Bad Address information for the DACP server
  } else {

    // only do this one at a time -- not sure it is necessary, but better safe than sorry

    int mutex_reply = pthread_mutex_timedlock(&dacp_conversation_lock, &mutex_wait_time);
    if (mutex_reply == 0) {

      // make a socket:
      sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

      if (sockfd == -1) {
        // debug(1, "DACP socket could not be created -- error %d: \"%s\".",errno,strerror(errno));
        response.code = 497; // Can't establish a socket to the DACP server
      } else {

        // connect!
        // debug(1, "DACP socket created.");
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
          // debug(1, "DACP connect failed.");
          response.code = 496; // Can't connect to the DACP server
        } else {
          // debug(1,"DACP connect succeeded.");

          sprintf(message,
                  "GET /ctrl-int/1/%s HTTP/1.1\r\nHost: %s:%u\r\nActive-Remote: %u\r\n\r\n",
                  command, dacp_server.ip_string, dacp_server.port, dacp_server.active_remote_id);

          // Send command
          // debug(1,"DACP connect message: \"%s\".",message);
          if (send(sockfd, message, strlen(message), 0) != strlen(message)) {
            // debug(1, "Send failed");
            response.code = 493; // Client failed to send a message

          } else {

            response.body = malloc(2048); // it can resize this if necessary
            response.malloced_size = 2048;

            struct http_roundtripper rt;
            http_init(&rt, responseFuncs, &response);

            int needmore = 1;
            int looperror = 0;
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            while (needmore && !looperror) {
              const char *data = buffer;
              int ndata = recv(sockfd, buffer, sizeof(buffer), 0);
              // debug(1,"Received %d bytes: \"%s\".",ndata,buffer);
              if (ndata <= 0) {
                debug(1, "Error receiving data.");
                free(response.body);
                response.body = NULL;
                response.malloced_size = 0;
                response.size = 0;
                response.code = 495; // Error receiving response
                looperror = 1;
              }

              while (needmore && ndata && !looperror) {
                int read;
                needmore = http_data(&rt, data, ndata, &read);
                ndata -= read;
                data += read;
              }
            }

            if (http_iserror(&rt)) {
              debug(1, "Error parsing data.");
              free(response.body);
              response.body = NULL;
              response.malloced_size = 0;
              response.size = 0;
            }
            // debug(1,"Size of response body is %d",response.size);
            http_free(&rt);
          }
        }
        close(sockfd);
        // debug(1,"DACP socket closed.");
      }
      pthread_mutex_unlock(&dacp_conversation_lock);
    } else {
      // debug(1, "Could not acquire a lock on the dacp transmit/receive section. Possible
      // timeout?");
      response.code = 494; // This client is already busy
    }
  }
  *body = response.body;
  *bodysize = response.size;
  return response.code;
}

int send_simple_dacp_command(const char *command) {
  int reply = 0;
  char *server_reply = NULL;
  debug(1, "Sending command \"%s\".", command);
  ssize_t reply_size = 0;
  reply = dacp_send_command(command, &server_reply, &reply_size);
  if (server_reply) {
    free(server_reply);
    server_reply = NULL;
  }
  return reply;
}

// this will be running on the thread of its caller, not of the conversation thread...
void set_dacp_server_information(rtsp_conn_info *conn) { // tell the DACP conversation thread that
                                                         // the port has been set or changed
  pthread_mutex_lock(&dacp_server_information_lock);

  dacp_server.port = conn->dacp_port;
  dacp_server.connection_family = conn->connection_ip_family;
  dacp_server.scope_id = conn->self_scope_id;
  strncpy(dacp_server.ip_string, conn->client_ip_string, INET6_ADDRSTRLEN);
  dacp_server.active_remote_id = conn->dacp_active_remote;

  pthread_cond_signal(&dacp_server_information_cv);
  pthread_mutex_unlock(&dacp_server_information_lock);
}

void *dacp_monitor_thread_code(void *na) {
  int scan_index = 0;
  char server_reply[10000];
  debug(1, "DACP monitor thread started.");
  // wait until we get a valid port number to begin monitoring it
  int32_t revision_number = 1;
  while (1) {
    pthread_mutex_lock(&dacp_server_information_lock);
    while (dacp_server.port == 0) {
      debug(1, "Wait for a valid DACP port");
      pthread_cond_wait(&dacp_server_information_cv, &dacp_server_information_lock);
    }
    pthread_mutex_unlock(&dacp_server_information_lock);
    // debug(1, "DACP Server ID \"%u\" at \"%s:%u\", scan %d.", dacp_server.active_remote_id,
    //      dacp_server.ip_string, dacp_server.port, scan_index);
    scan_index++;
    ssize_t le;
    char *response = NULL;
    int32_t item_size;
    char command[1024] = "";
    snprintf(command, sizeof(command) - 1, "playstatusupdate?revision-number=%d", revision_number);
    // debug(1,"Command: \"%s\"",command);
    int result = dacp_send_command(command, &response, &le);
    if (result == 200) {
      char *sp = response;
      if (le >= 8) {
        // here start looking for the contents of the status update
        if (dacp_tlv_crawl(&sp, &item_size) == 'cmst') { // status
					// here, we know that we are receiving playerstatusupdates, so set a flag
					metadata_hub_modify_prolog();
					metadata_store.playerstatusupdates_are_received = 1;
          sp -= item_size; // drop down into the array -- don't skip over it
          le -= 8;
          char typestring[5];
          // we need to acquire the metadata data structure and possibly update it
          while (le >= 8) {
            uint32_t type = dacp_tlv_crawl(&sp, &item_size);

            //*(uint32_t *)typestring = htonl(type);
            // typestring[4] = 0;
            // printf("\"%s\" %4d", typestring, item_size);

            le -= item_size + 8;
            char *t;
            char u;
            char *st;
            int32_t r;
            uint64_t s, v;
            int i;

            switch (type) {
            case 'cmsr': // revision number
              t = sp - item_size;
              revision_number = ntohl(*(int32_t *)(t));
              // debug(1,"    Serial Number: %d", revision_number);
              break;
            case 'caps': // play status
              t = sp - item_size;
              r = *(unsigned char *)(t);
              switch (r) {
              case 2:
                if (metadata_store.play_status != PS_STOPPED) {
                  metadata_store.play_status = PS_STOPPED;
                  metadata_store.play_status_changed = 1;
                  debug(1, "Play status set to \"stopped\".");
                  metadata_store.changed = 1;
                }
                break;
              case 3:
                if (metadata_store.play_status != PS_PAUSED) {
                  metadata_store.play_status = PS_PAUSED;
                  metadata_store.play_status_changed = 1;
                  debug(1, "Play status set to \"paused\".");
                  metadata_store.changed = 1;
                }
                break;
              case 4:
                if (metadata_store.play_status != PS_PLAYING) {
                  metadata_store.play_status = PS_PLAYING;
                  metadata_store.play_status_changed = 1;
                  debug(1, "Play status set to \"playing\".");
                  metadata_store.changed = 1;
                }
                break;
              default:
                debug(1, "Unrecognised play status %d received.", r);
                break;
              }
              break;
            case 'cash': // shuffle status
              t = sp - item_size;
              r = *(unsigned char *)(t);
              switch (r) {
              case 0:
                if (metadata_store.shuffle_status != SS_OFF) {
                  metadata_store.shuffle_status = SS_OFF;
                  metadata_store.shuffle_status_changed = 1;
                  debug(1, "Shuffle status set to \"off\".");
                  metadata_store.changed = 1;
                }
                break;
              case 1:
                if (metadata_store.shuffle_status != SS_ON) {
                  metadata_store.shuffle_status = SS_ON;
                  metadata_store.shuffle_status_changed = 1;
                  debug(1, "Shuffle status set to \"on\".");
                  metadata_store.changed = 1;
                }
                break;
              default:
                debug(1, "Unrecognised shuffle status %d received.", r);
                break;
              }
              break;
            case 'carp': // repeat status
              t = sp - item_size;
              r = *(unsigned char *)(t);
              switch (r) {
              case 0:
                if (metadata_store.repeat_status != RS_NONE) {
                  metadata_store.repeat_status = RS_NONE;
                  metadata_store.repeat_status_changed = 1;
                  debug(1, "Repeat status set to \"none\".");
                  metadata_store.changed = 1;
                }
                break;
              case 1:
                if (metadata_store.repeat_status != RS_SINGLE) {
                  metadata_store.repeat_status = RS_SINGLE;
                  metadata_store.repeat_status_changed = 1;
                  debug(1, "Repeat status set to \"single\".");
                  metadata_store.changed = 1;
                }
                break;
              case 2:
                if (metadata_store.repeat_status != RS_ALL) {
                  metadata_store.repeat_status = RS_ALL;
                  metadata_store.repeat_status_changed = 1;
                  debug(1, "Repeat status set to \"all\".");
                  metadata_store.changed = 1;
                }
                break;
              default:
                debug(1, "Unrecognised repeat status %d received.", r);
                break;
              }
              break;
            case 'cann': // track name
              t = sp - item_size;
              if ((metadata_store.track_name == NULL) ||
                  (strncmp(metadata_store.track_name, t, item_size) != 0)) {
                if (metadata_store.track_name)
                  free(metadata_store.track_name);
                metadata_store.track_name = strndup(t, item_size);
                debug(1, "Track name set to: \"%s\"", metadata_store.track_name);
                metadata_store.track_name_changed = 1;
                metadata_store.changed = 1;
              }
              break;
            case 'cana': // artist name
              t = sp - item_size;
              if ((metadata_store.artist_name == NULL) ||
                  (strncmp(metadata_store.artist_name, t, item_size) != 0)) {
                if (metadata_store.artist_name)
                  free(metadata_store.artist_name);
                metadata_store.artist_name = strndup(t, item_size);
                debug(1, "Artist name set to: \"%s\"", metadata_store.artist_name);
                metadata_store.artist_name_changed = 1;
                metadata_store.changed = 1;
              }
              break;
            case 'canl': // album name
              t = sp - item_size;
              if ((metadata_store.album_name == NULL) ||
                  (strncmp(metadata_store.album_name, t, item_size) != 0)) {
                if (metadata_store.album_name)
                  free(metadata_store.album_name);
                metadata_store.album_name = strndup(t, item_size);
                debug(1, "Album name set to: \"%s\"", metadata_store.album_name);
                metadata_store.album_name_changed = 1;
                metadata_store.changed = 1;
              }
              break;
            case 'cang': // genre
              t = sp - item_size;
              if ((metadata_store.genre == NULL) ||
                  (strncmp(metadata_store.genre, t, item_size) != 0)) {
                if (metadata_store.genre)
                  free(metadata_store.genre);
                metadata_store.genre = strndup(t, item_size);
                debug(1, "Genre set to: \"%s\"", metadata_store.genre);
                metadata_store.genre_changed = 1;
                metadata_store.changed = 1;
              }
              break;
            case 'canp': // nowplaying 4 ids: dbid, plid, playlistItem, itemid (from mellowware --
                         // see reference above)
              t = sp - item_size;
              if (memcmp(metadata_store.item_composite_id, t,
                         sizeof(metadata_store.item_composite_id)) != 0) {
                memcpy(metadata_store.item_composite_id, t,
                       sizeof(metadata_store.item_composite_id));

                char st[33];
                char *pt = st;
                int it;
                for (it = 0; it < 16; it++) {
                  sprintf(pt, "%02X", metadata_store.item_composite_id[it]);
                  pt += 2;
                }
                *pt = 0;
                debug(1, "Item composite ID set to 0x%s.", st);
                metadata_store.item_id_changed = 1;
                metadata_store.changed = 1;
              }
              break;
            case 'astm':
              t = sp - item_size;
              r = ntohl(*(int32_t *)(t));
              metadata_store.songtime_in_milliseconds = ntohl(*(uint32_t *)(t));
              break;

            /*
                        case 'mstt':
                        case 'cant':
                        case 'cast':
                        case 'cmmk':
                        case 'caas':
                        case 'caar':
                          t = sp - item_size;
                          r = ntohl(*(int32_t *)(t));
                          printf("    %d", r);
                          printf("    (0x");
                          t = sp - item_size;
                          for (i = 0; i < item_size; i++) {
                            printf("%02x", *t & 0xff);
                            t++;
                          }
                          printf(")");
                          break;
                        case 'asai':
                          t = sp - item_size;
                          s = ntohl(*(uint32_t *)(t));
                          s = s << 32;
                          t += 4;
                          v = (ntohl(*(uint32_t *)(t))) & 0xffffffff;
                          s += v;
                          printf("    %lu", s);
                          printf("    (0x");
                          t = sp - item_size;
                          for (i = 0; i < item_size; i++) {
                            printf("%02x", *t & 0xff);
                            t++;
                          }
                          printf(")");
                          break;
             */
            default:
              /*
                printf("    0x");
                t = sp - item_size;
                for (i = 0; i < item_size; i++) {
                  printf("%02x", *t & 0xff);
                  t++;
                }
               */
              break;
            }
            // printf("\n");
          }

          // finished possibly writing to the metadata hub
      		metadata_hub_modify_epilog();
        } else {
          printf("Status Update not found.\n");
        }
      } else {
        debug(1, "Can't find any content in playerstatusupdate request");
      }
    } else {
      if (result != 403)
        debug(1, "Unexpected response %d to playerstatusupdate request", result);
    }
    if (response) {
      free(response);
      response = NULL;
    };
    /*
    strcpy(command,"nowplayingartwork?mw=320&mh=320");
    debug(1,"Command: \"%s\", result is %d",command, dacp_send_command(command, &response, &le));
    if (response) {
      free(response);
      response = NULL;
    }
    strcpy(command,"getproperty?properties=dmcp.volume");
    debug(1,"Command: \"%s\", result is %d",command, dacp_send_command(command, &response, &le));
    if (response) {
      free(response);
      response = NULL;
    }
    strcpy(command,"setproperty?dmcp.volume=100.000000");
    debug(1,"Command: \"%s\", result is %d",command, dacp_send_command(command, &response, &le));
    if (response) {
      free(response);
      response = NULL;
    }
    */
    sleep(2);
  }
  debug(1, "DACP monitor thread exiting.");
  pthread_exit(NULL);
}

void dacp_monitor_start() {
  memset(&dacp_server, 0, sizeof(dacp_server_record));
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

int32_t dacp_get_client_volume(void) {
  char *server_reply = NULL;
  int32_t overall_volume = -1;
  ssize_t reply_size;
  int response =
      dacp_send_command("getproperty?properties=dmcp.volume", &server_reply, &reply_size);
  if (response == 200) { // if we get an okay
    char *sp = server_reply;
    int32_t item_size;
    if (reply_size >= 8) {
      if (dacp_tlv_crawl(&sp, &item_size) == 'cmgt') {
        sp -= item_size; // drop down into the array -- don't skip over it
        reply_size -= 8;
        while (reply_size >= 8) {
          uint32_t type = dacp_tlv_crawl(&sp, &item_size);
          reply_size -= item_size + 8;
          if (type == 'cmvo') { // drop down into the dictionary -- don't skip over it
            char *t = sp - item_size;
            overall_volume = ntohl(*(int32_t *)(t));
          }
        }
      } else {
        debug(1, "Unexpected payload response from getproperty?properties=dmcp.volume");
      }
    } else {
      debug(1, "Too short a response from getproperty?properties=dmcp.volume");
    }
    // debug(1, "Overall Volume is %d.", overall_volume);
    free(server_reply);
  } else {
    debug(1, "Unexpected response %d to dacp volume control request", response);
  }
  return overall_volume;
}

int dacp_set_include_speaker_volume(int64_t machine_number, int32_t vo) {
  char message[1000];
  memset(message, 0, sizeof(message));
  sprintf(message, "setproperty?include-speaker-id=%lld&dmcp.volume=%d", machine_number, vo);
  // debug(1,"sending \"%s\"",message);
  return send_simple_dacp_command(message);
  // should return 204
}

int dacp_set_speaker_volume(int64_t machine_number, int32_t vo) {
  char message[1000];
  memset(message, 0, sizeof(message));
  sprintf(message, "setproperty?speaker-id=%lld&dmcp.volume=%d", machine_number, vo);
  // debug(1,"sending \"%s\"",message);
  return send_simple_dacp_command(message);
  // should return 204
}

int dacp_get_speaker_list(dacp_spkr_stuff *speaker_info, int max_size_of_array) {
  char *server_reply = NULL;
  int speaker_index = -1; // will be incremented before use
  int reply = -1;         // will bve fixed if there is no problem
  ssize_t le;

  int response = dacp_send_command("getspeakers", &server_reply, &le);
  if (response == 200) {
    char *sp = server_reply;
    int32_t item_size;
    if (le >= 8) {
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
              // debug(1,"The individual volume of speaker \"%s\" is
              // \"%d\".",speaker_info[speaker_index].name,r);
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
    free(server_reply);
    server_reply = NULL;
  } else {
    debug(1, "Unexpected response %d to dacp speakers request", response);
  }
  return reply;
}

void dacp_get_volume(void) {
  // get the speaker volume information from the DACP source and store it in the metadata_hub
  // A volume command has been sent from the client
  // let's get the master volume from the DACP remote control
  struct dacp_speaker_stuff speaker_info[50];
  // we need the overall volume and the speakers information to get this device's relative volume to
  // calculate the real volume

  int32_t overall_volume = dacp_get_client_volume();
  // debug(1, "DACP Volume: %d.", overall_volume);
  int speaker_count = dacp_get_speaker_list((dacp_spkr_stuff *)&speaker_info, 50);
  // debug(1,"DACP Speaker Count: %d.",speaker_count);

  // get our machine number
  uint16_t *hn = (uint16_t *)config.hw_addr;
  uint32_t *ln = (uint32_t *)(config.hw_addr + 2);
  uint64_t t1 = ntohs(*hn);
  uint64_t t2 = ntohl(*ln);
  int64_t machine_number = (t1 << 32) + t2; // this form is useful

  // Let's find our own speaker in the array and pick up its relative volume
  int i;
  int32_t relative_volume = 0;
  for (i = 0; i < speaker_count; i++) {
    if (speaker_info[i].speaker_number == machine_number) {
      // debug(1,"Our speaker number found: %ld.",machine_number);
      relative_volume = speaker_info[i].volume;
    }
  }
  int32_t actual_volume = (overall_volume * relative_volume + 50) / 100;
  // debug(1,"Overall volume: %d, relative volume: %d%, actual volume:
  // %d.",overall_volume,relative_volume,actual_volume);
  // debug(1,"Our actual speaker volume is %d.",actual_volume);
  metadata_hub_modify_prolog();
  metadata_store.speaker_volume = actual_volume;
  metadata_hub_modify_epilog();
}
