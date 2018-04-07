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
#include <inttypes.h>
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
  int players_connection_thread_index; // the connection thread index when a player thread is
                                       // associated with this, zero otherwise
  int scan_enable;                     // set to 1 if if sacanning should be considered
  char dacp_id[256];                   // the DACP ID string
  uint16_t port;                       // zero if no port discovered
  short connection_family;             // AF_INET6 or AF_INET
  uint32_t scope_id;                   // if it's an ipv6 connection, this will be its scope id
  char ip_string[INET6_ADDRSTRLEN];    // the ip string pointing to the client
  uint32_t active_remote_id;           // send this when you want to send remote control commands
  void *port_monitor_private_storage;
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

void *response_realloc(__attribute__((unused)) void *opaque, void *ptr, int size) {
  void *t = realloc(ptr, size);
  if ((t == NULL) && (size != 0))
    debug(1, "Response realloc of size %d failed!", size);
  return t;
}

void response_body(void *opaque, const char *data, int size) {
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

static void
response_header(__attribute__((unused)) void *opaque, __attribute__((unused)) const char *ckey,
                __attribute__((unused)) int nkey, __attribute__((unused)) const char *cvalue,
                __attribute__((unused)) int nvalue) { /* example doesn't care about headers */
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
  //  492 Argument out of range

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

    int mutex_reply = sps_pthread_mutex_timedlock(&dacp_conversation_lock, 2000000, command, 1);
    // int mutex_reply = pthread_mutex_lock(&dacp_conversation_lock);
    if (mutex_reply == 0) {
      // debug(1,"dacp_conversation_lock acquired for command \"%s\".",command);

      // make a socket:
      sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

      if (sockfd == -1) {
        // debug(1, "DACP socket could not be created -- error %d: \"%s\".",errno,strerror(errno));
        response.code = 497; // Can't establish a socket to the DACP server
      } else {
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == -1)
          debug(1, "Error %d setting receive timeout for DACP service.", errno);
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv) == -1)
          debug(1, "Error %d setting send timeout for DACP service.", errno);

        // connect!
        // debug(1, "DACP socket created.");
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
          debug(3, "DACP connect failed with errno %d.", errno);
          response.code = 496; // Can't connect to the DACP server
        } else {
          // debug(1,"DACP connect succeeded.");

          sprintf(message,
                  "GET /ctrl-int/1/%s HTTP/1.1\r\nHost: %s:%u\r\nActive-Remote: %u\r\n\r\n",
                  command, dacp_server.ip_string, dacp_server.port, dacp_server.active_remote_id);

          // Send command
          // debug(1,"DACP connect message: \"%s\".",message);
          if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv) == -1)
            debug(1, "Error %d setting send timeout for DACP service.", errno);
          if (send(sockfd, message, strlen(message), 0) != (ssize_t)strlen(message)) {
            // debug(1, "Send failed");
            response.code = 493; // Client failed to send a message

          } else {

            response.body = malloc(2048); // it can resize this if necessary
            response.malloced_size = 2048;

            struct http_roundtripper rt;
            http_init(&rt, responseFuncs, &response);

            int needmore = 1;
            int looperror = 0;
            char buffer[8192];
            memset(buffer, 0, sizeof(buffer));
            while (needmore && !looperror) {
              const char *data = buffer;
              if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == -1)
                debug(1, "Error %d setting receive timeout for DACP service.", errno);
              int ndata = recv(sockfd, buffer, sizeof(buffer), 0);
              // debug(1,"Received %d bytes: \"%s\".",ndata,buffer);
              if (ndata <= 0) {
                debug(1, "dacp_send_command -- error receiving response for command \"%s\".",
                      command);
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
      // debug(1,"Sent command\"%s\" with a response body of size %d.",command,response.size);
      // debug(1,"dacp_conversation_lock released.");
    } else {
      debug(3, "Could not acquire a lock on the dacp transmit/receive section when attempting to "
               "send the command \"%s\". Possible timeout?",
            command);
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
  debug(3, "Sending command \"%s\".", command);
  ssize_t reply_size = 0;
  reply = dacp_send_command(command, &server_reply, &reply_size);
  if (server_reply) {
    free(server_reply);
    server_reply = NULL;
  }
  return reply;
}

void relinquish_dacp_server_information(rtsp_conn_info *conn) {
  // this will set the dacp_server.players_connection_thread_index to zero iff it has the same value
  // as the conn's connection number
  // this is to signify that the player has stopped, but only if another thread (with a different
  // index) hasn't already taken over the dacp service
  sps_pthread_mutex_timedlock(
      &dacp_server_information_lock, 500000,
      "set_dacp_server_information couldn't get DACP server information lock in 0.5 second!.", 2);
  if (dacp_server.players_connection_thread_index == conn->connection_number)
    dacp_server.players_connection_thread_index = 0;
  pthread_mutex_unlock(&dacp_server_information_lock);
}

// this will be running on the thread of its caller, not of the conversation thread...
// tell the DACP conversation thread what DACP server we are listening to
// if the active_remote_id is the same we don't change anything apart from
// the conversation number
// Thus, we can keep the DACP port that might have previously been discovered
void set_dacp_server_information(rtsp_conn_info *conn) {
  sps_pthread_mutex_timedlock(
      &dacp_server_information_lock, 500000,
      "set_dacp_server_information couldn't get DACP server information lock in 0.5 second!.", 2);
  dacp_server.players_connection_thread_index = conn->connection_number;
  if (strcmp(conn->dacp_id, dacp_server.dacp_id) != 0) {
    strncpy(dacp_server.dacp_id, conn->dacp_id, sizeof(dacp_server.dacp_id));
    dacp_server.port = 0;
    dacp_server.scan_enable = 0;
    dacp_server.connection_family = conn->connection_ip_family;
    dacp_server.scope_id = conn->self_scope_id;
    strncpy(dacp_server.ip_string, conn->client_ip_string, INET6_ADDRSTRLEN);
    debug(2, "set_dacp_server_information set IP to \"%s\" and DACP id to \"%s\".",
          dacp_server.ip_string, dacp_server.dacp_id);

    if (dacp_server.port_monitor_private_storage) // if there's is a monitor already active...
      mdns_dacp_dont_monitor(dacp_server.port_monitor_private_storage); // let it go.
    dacp_server.port_monitor_private_storage =
        mdns_dacp_monitor(dacp_server.dacp_id); // create a new one for us

    metadata_hub_modify_prolog();
    int ch = metadata_store.dacp_server_active != dacp_server.scan_enable;
    metadata_store.dacp_server_active = dacp_server.scan_enable;
    if ((metadata_store.client_ip == NULL) ||
        (strncmp(metadata_store.client_ip, conn->client_ip_string, INET6_ADDRSTRLEN) != 0)) {
      if (metadata_store.client_ip)
        free(metadata_store.client_ip);
      metadata_store.client_ip = strndup(conn->client_ip_string, INET6_ADDRSTRLEN);
      debug(3, "MH Client IP set to: \"%s\"", metadata_store.client_ip);
      ch = 1;
    }
    metadata_hub_modify_epilog(ch);
  } else {
    if (dacp_server.port) {
      debug(1, "Enable scanning.");
      dacp_server.scan_enable = 1;
      //      metadata_hub_modify_prolog();
      //      int ch = metadata_store.dacp_server_active != dacp_server.scan_enable;
      //      metadata_store.dacp_server_active = dacp_server.scan_enable;
      //      metadata_hub_modify_epilog(ch);
    }
  }
  dacp_server.active_remote_id = conn->dacp_active_remote; // even if the dacp_id remains the same,
                                                           // the active remote will change.
  debug(2, "set_dacp_server_information set active-remote id to %" PRIu32 ".",
        dacp_server.active_remote_id);
  pthread_cond_signal(&dacp_server_information_cv);
  pthread_mutex_unlock(&dacp_server_information_lock);
}

void dacp_monitor_port_update_callback(char *dacp_id, uint16_t port) {
  debug(2, "dacp_monitor_port_update_callback with Remote ID \"%s\" and port number %d.", dacp_id,
        port);
  sps_pthread_mutex_timedlock(
      &dacp_server_information_lock, 500000,
      "dacp_monitor_port_update_callback couldn't get DACP server information lock in 0.5 second!.",
      2);
  if (strcmp(dacp_id, dacp_server.dacp_id) == 0) {
    dacp_server.port = port;
    if (port == 0)
      dacp_server.scan_enable = 0;
    else {
      dacp_server.scan_enable = 1;
      debug(2, "dacp_monitor_port_update_callback enables scan");
    }
    //    metadata_hub_modify_prolog();
    //    int ch = metadata_store.dacp_server_active != dacp_server.scan_enable;
    //    metadata_store.dacp_server_active = dacp_server.scan_enable;
    //    metadata_hub_modify_epilog(ch);
  } else {
    debug(1, "dacp port monitor reporting on an out-of-use remote.");
  }
  pthread_cond_signal(&dacp_server_information_cv);
  pthread_mutex_unlock(&dacp_server_information_lock);
}
void *dacp_monitor_thread_code(__attribute__((unused)) void *na) {
  int scan_index = 0;
  // char server_reply[10000];
  // debug(1, "DACP monitor thread started.");
  // wait until we get a valid port number to begin monitoring it
  int32_t revision_number = 1;
  int bad_result_count = 0;
  int idle_scan_count = 0;
  while (1) {
    int result = 0;
    sps_pthread_mutex_timedlock(
        &dacp_server_information_lock, 500000,
        "dacp_monitor_thread_code couldn't get DACP server information lock in 0.5 second!.", 2);
    if (dacp_server.scan_enable == 0) {
      metadata_hub_modify_prolog();
      int ch = (metadata_store.dacp_server_active != 0) ||
               (metadata_store.advanced_dacp_server_active != 0);
      metadata_store.dacp_server_active = 0;
      metadata_store.advanced_dacp_server_active = 0;
      debug(2, "setting dacp_server_active and advanced_dacp_server_active to 0 with an update "
               "flag value of %d",
            ch);
      metadata_hub_modify_epilog(ch);
      while (dacp_server.scan_enable == 0) {
        // debug(1, "dacp_monitor_thread_code wait for an event to possible enable scan");
        pthread_cond_wait(&dacp_server_information_cv, &dacp_server_information_lock);
        //      debug(1,"dacp_monitor_thread_code wake up.");
      }
      bad_result_count = 0;
      idle_scan_count = 0;
    }
    scan_index++;
    int32_t the_volume;
    result = dacp_get_volume(&the_volume); // just want the http code

    if ((result == 496) || (result == 403) || (result == 501)) {
      bad_result_count++;
      // debug(1,"Bad Scan : %d.",result);
    } else
      bad_result_count = 0;

    if (metadata_store.player_thread_active == 0)
      idle_scan_count++;
    else
      idle_scan_count = 0;

    debug(3, "Scan Result: %d, Bad Scan Count: %d, Idle Scan Count: %d.", result, bad_result_count,
          idle_scan_count);

    if ((bad_result_count == config.scan_max_bad_response_count) ||
        (idle_scan_count == config.scan_max_inactive_count)) {
      debug(1, "DACP server status scanning stopped.");
      dacp_server.scan_enable = 0;
    }
    pthread_mutex_unlock(&dacp_server_information_lock);
    // debug(1, "DACP Server ID \"%u\" at \"%s:%u\", scan %d.", dacp_server.active_remote_id,
    //      dacp_server.ip_string, dacp_server.port, scan_index);

    if (dacp_server.scan_enable ==
        1) { // if it hasn't been turned off, continue looking for information.
      int transient_problem =
          (result == 494) ||
          (result == 495); // this just means that it couldn't send the query because something
                           // else
                           // was sending a command or something
      if ((!transient_problem) && (bad_result_count == 0) && (idle_scan_count == 0) &&
          (dacp_server.scan_enable == 1)) {

        // Any other result means that the DACP server is active, but not providing advanced
        // features

        metadata_hub_modify_prolog();
        int inactive = metadata_store.dacp_server_active == 0;
        if (inactive) {
          metadata_store.dacp_server_active = 1;
          debug(2, "Setting dacp_server_active to active because of a response of %d.", result);
        }
        int same = metadata_store.advanced_dacp_server_active == (result == 200);
        if (!same) {
          metadata_store.advanced_dacp_server_active = (result == 200);
          debug(2, "Setting dacp_advanced_server_active to %d because of a response of %d.",
                (result == 200), result);
        }
        metadata_hub_modify_epilog(inactive + (!same));
      }

      if (result == 200) {
        metadata_hub_modify_prolog();
        int diff = metadata_store.speaker_volume != the_volume;
        if (diff)
          metadata_store.speaker_volume = the_volume;
        metadata_hub_modify_epilog(diff);

        ssize_t le;
        char *response = NULL;
        int32_t item_size;
        char command[1024] = "";
        snprintf(command, sizeof(command) - 1, "playstatusupdate?revision-number=%d",
                 revision_number);
        // debug(1,"Command: \"%s\"",command);
        result = dacp_send_command(command, &response, &le);
        // debug(1,"Response to \"%s\" is %d.",command,result);
        if (result == 200) {
          // if (0) {
          char *sp = response;
          if (le >= 8) {
            // here start looking for the contents of the status update
            if (dacp_tlv_crawl(&sp, &item_size) == 'cmst') { // status
              // here, we know that we are receiving playerstatusupdates, so set a flag
              metadata_hub_modify_prolog();
              // debug(1, "playstatusupdate release track metadata");
              // metadata_hub_reset_track_metadata();
              // metadata_store.playerstatusupdates_are_received = 1;
              sp -= item_size; // drop down into the array -- don't skip over it
              le -= 8;
              // char typestring[5];
              // we need to acquire the metadata data structure and possibly update it
              while (le >= 8) {
                uint32_t type = dacp_tlv_crawl(&sp, &item_size);
                le -= item_size + 8;
                char *t;
                // char u;
                // char *st;
                int32_t r;
                // uint64_t v;
                // int i;

                switch (type) {
                case 'cmsr': // revision number
                  t = sp - item_size;
                  revision_number = ntohl(*(uint32_t *)(t));
                  // debug(1,"    Serial Number: %d", revision_number);
                  break;
                case 'caps': // play status
                  t = sp - item_size;
                  r = *(unsigned char *)(t);
                  switch (r) {
                  case 2:
                    if (metadata_store.play_status != PS_STOPPED) {
                      metadata_store.play_status = PS_STOPPED;
                      debug(2, "Play status is \"stopped\".");
                    }
                    break;
                  case 3:
                    if (metadata_store.play_status != PS_PAUSED) {
                      metadata_store.play_status = PS_PAUSED;
                      debug(2, "Play status is \"paused\".");
                    }
                    break;
                  case 4:
                    if (metadata_store.play_status != PS_PLAYING) {
                      metadata_store.play_status = PS_PLAYING;
                      debug(2, "Play status changed to \"playing\".");
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
                      debug(2, "Shuffle status is \"off\".");
                    }
                    break;
                  case 1:
                    if (metadata_store.shuffle_status != SS_ON) {
                      metadata_store.shuffle_status = SS_ON;
                      debug(2, "Shuffle status is \"on\".");
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
                    if (metadata_store.repeat_status != RS_OFF) {
                      metadata_store.repeat_status = RS_OFF;
                      debug(2, "Repeat status is \"none\".");
                    }
                    break;
                  case 1:
                    if (metadata_store.repeat_status != RS_ONE) {
                      metadata_store.repeat_status = RS_ONE;
                      debug(2, "Repeat status is \"one\".");
                    }
                    break;
                  case 2:
                    if (metadata_store.repeat_status != RS_ALL) {
                      metadata_store.repeat_status = RS_ALL;
                      debug(2, "Repeat status is \"all\".");
                    }
                    break;
                  default:
                    debug(1, "Unrecognised repeat status %d received.", r);
                    break;
                  }
                  break;
                /*
                case 'cann': // track name
                  t = sp - item_size;
                  if ((metadata_store.track_name == NULL) ||
                      (strncmp(metadata_store.track_name, t, item_size) != 0)) {
                    if (metadata_store.track_name)
                      free(metadata_store.track_name);
                    metadata_store.track_name = strndup(t, item_size);
                    debug(1, "Track name changed to: \"%s\"", metadata_store.track_name);
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
                    debug(1, "Artist name changed to: \"%s\"", metadata_store.artist_name);
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
                    debug(1, "Album name changed to: \"%s\"", metadata_store.album_name);
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
                    debug(1, "Genre changed to: \"%s\"", metadata_store.genre);
                    metadata_store.genre_changed = 1;
                    metadata_store.changed = 1;
                  }
                  break;
                case 'canp': // nowplaying 4 ids: dbid, plid, playlistItem, itemid (from mellowware
                --
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
                    // debug(1, "Item composite ID set to 0x%s.", st);
                    metadata_store.item_id_changed = 1;
                    metadata_store.changed = 1;
                  }
                  break;
                */
                case 'astm':
                  t = sp - item_size;
                  r = ntohl(*(uint32_t *)(t));
                  if (metadata_store.track_metadata)
                    metadata_store.track_metadata->songtime_in_milliseconds =
                        ntohl(*(uint32_t *)(t));
                  break;

                /*
                            case 'mstt':
                            case 'cant':
                            case 'cast':
                            case 'cmmk':
                            case 'caas':
                            case 'caar':
                              t = sp - item_size;
                              r = ntohl(*(uint32_t *)(t));
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
              metadata_hub_modify_epilog(1);
            } else {
              debug(1, "Status Update not found.\n");
            }
          } else {
            debug(1, "Can't find any content in playerstatusupdate request");
          }
        } /* else {
          if (result != 403)
            debug(1, "Unexpected response %d to playerstatusupdate request", result);
        } */
        if (response) {
          free(response);
          response = NULL;
        };
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
      if (metadata_store.player_thread_active)
        sleep(config.scan_interval_when_active);
      else
        sleep(config.scan_interval_when_inactive);
    }
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
  *length = ntohl(*(uint32_t *)*p);
  *p += 4 + *length;
  // debug(1,"Type seen: '%s' of length %d",typecode,*length);
  return type;
}

int dacp_get_client_volume(int32_t *result) {
  // debug(1,"dacp_get_client_volume");
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
            overall_volume = ntohl(*(uint32_t *)(t));
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
  } /* else {
    debug(1, "Unexpected response %d to dacp volume control request", response);
  } */
  if (result) {
    *result = overall_volume;
    // debug(1,"dacp_get_client_volume returns: %" PRId32 ".",overall_volume);
  }
  return response;
}

int dacp_set_include_speaker_volume(int64_t machine_number, int32_t vo) {
  debug(2, "dacp_set_include_speaker_volume to %" PRId32 ".", vo);
  char message[1000];
  memset(message, 0, sizeof(message));
  sprintf(message, "setproperty?include-speaker-id=%" PRId64 "&dmcp.volume=%" PRId32 "",
          machine_number, vo);
  debug(2, "sending \"%s\"", message);
  return send_simple_dacp_command(message);
  // should return 204
}

int dacp_set_speaker_volume(int64_t machine_number, int32_t vo) {
  char message[1000];
  memset(message, 0, sizeof(message));
  sprintf(message, "setproperty?speaker-id=%" PRId64 "&dmcp.volume=%" PRId32 "", machine_number,
          vo);
  debug(2, "sending \"%s\"", message);
  return send_simple_dacp_command(message);
  // should return 204
}

int dacp_get_speaker_list(dacp_spkr_stuff *speaker_info, int max_size_of_array,
                          int *actual_speaker_count) {
  // char typestring[5];
  char *server_reply = NULL;
  int speaker_index = -1; // will be incremented before use
  int speaker_count = -1; // will be fixed if there is no problem
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
              return 413; // Payload Too Large -- too many speakers
            speaker_info[speaker_index].active = 0;
            speaker_info[speaker_index].speaker_number = 0;
            speaker_info[speaker_index].volume = 0;
            speaker_info[speaker_index].name = NULL;
          } else {
            le -= item_size + 8;
            char *t;
            // char u;
            int32_t r;
            int64_t s, v;
            switch (type) {
            case 'minm':
              t = sp - item_size;
              speaker_info[speaker_index].name = strndup(t, item_size);
              // debug(1," \"%s\"",speaker_info[speaker_index].name);
              break;
            case 'cmvo':
              t = sp - item_size;
              r = ntohl(*(uint32_t *)(t));
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
                            case 'cads':

                              *(uint32_t *)typestring = htonl(type);
                              typestring[4] = 0;



                              t = sp-item_size;
                              u = *t;
                              debug(1,"Type: '%s' Value: \"%d\".",typestring,u);
                              break;
            */
            default:
              break;
            }
          }
        }
        // debug(1,"Total of %d speakers found. Here are the active ones:",speaker_index+1);
        speaker_count = speaker_index + 1; // number of speaker entries in the array
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
  if (actual_speaker_count)
    *actual_speaker_count = speaker_count;
  return response;
}

int dacp_get_volume(int32_t *the_actual_volume) {
  // get the speaker volume information from the DACP source and store it in the metadata_hub
  // A volume command has been sent from the client
  // let's get the master volume from the DACP remote control
  struct dacp_speaker_stuff speaker_info[50];
  // we need the overall volume and the speakers information to get this device's relative volume to
  // calculate the real volume

  int32_t overall_volume = 0;
  int32_t actual_volume = 0;
  int http_response = dacp_get_client_volume(&overall_volume);
  if (http_response == 200) {
    // debug(1,"Overall volume is: %u.",overall_volume);
    int speaker_count = 0;
    http_response = dacp_get_speaker_list((dacp_spkr_stuff *)&speaker_info, 50, &speaker_count);
    if (http_response == 200) {
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
          relative_volume = speaker_info[i].volume;
          /*
          debug(1,"Our speaker was found with a relative volume of: %u.",relative_volume);

          if (speaker_info[i].active)
            debug(1,"Our speaker is active.");
          else
            debug(1,"Our speaker is inactive.");
          */
        }
      }
      actual_volume = (overall_volume * relative_volume + 50) / 100;
      // debug(1,"Overall volume: %d, relative volume: %d%, actual volume:
      // %d.",overall_volume,relative_volume,actual_volume);
      // debug(1,"Our actual speaker volume is %d.",actual_volume);
      // metadata_hub_modify_prolog();
      // metadata_store.speaker_volume = actual_volume;
      // metadata_hub_modify_epilog(1);
    } else {
      debug(1, "Unexpected return code %d from dacp_get_speaker_list.", http_response);
    }
  } else {
    debug(3, "Unexpected return code %d from dacp_get_client_volume.", http_response);
  }
  if (the_actual_volume) {
    // debug(1,"dacp_get_volume returns %d.",actual_volume);
    *the_actual_volume = actual_volume;
  }
  return http_response;
}

int dacp_set_volume(int32_t vo) {
  int http_response = 492; // argument out of range
  if ((vo >= 0) && (vo <= 100)) {
    // get the information we need -- the absolute volume, the speaker list, our ID
    struct dacp_speaker_stuff speaker_info[50];
    int32_t overall_volume;
    http_response = dacp_get_client_volume(&overall_volume);
    if (http_response == 200) {
      int speaker_count;
      http_response = dacp_get_speaker_list((dacp_spkr_stuff *)&speaker_info, 50, &speaker_count);
      if (http_response == 200) {
        // get our machine number
        uint16_t *hn = (uint16_t *)config.hw_addr;
        uint32_t *ln = (uint32_t *)(config.hw_addr + 2);
        uint64_t t1 = ntohs(*hn);
        uint64_t t2 = ntohl(*ln);
        int64_t machine_number = (t1 << 32) + t2; // this form is useful

        // Let's find our own speaker in the array and pick up its relative volume
        int i;
        int32_t active_speakers = 0;
        for (i = 0; i < speaker_count; i++) {
          if (speaker_info[i].speaker_number == machine_number) {
            debug(2, "Our speaker number found: %ld with relative volume.", machine_number,
                  speaker_info[i].volume);
          }
          if (speaker_info[i].active == 1) {
            active_speakers++;
          }
        }

        if (active_speakers == 1) {
          // must be just this speaker
          debug(2, "Remote-setting volume to %d on just one speaker.", vo);
          http_response = dacp_set_include_speaker_volume(machine_number, vo);
        } else if (active_speakers == 0) {
          debug(2, "No speakers!");
        } else {
          debug(2, "Speakers: %d, active: %d", speaker_count, active_speakers);
          if (vo >= overall_volume) {
            debug(2, "Multiple speakers active, but desired new volume is highest");
            http_response = dacp_set_include_speaker_volume(machine_number, vo);
          } else {
            // the desired volume is less than the current overall volume and there is more than
            // one
            // speaker
            // we must find out the highest other speaker volume.
            // If the desired volume is less than it, we must set the current_overall volume to
            // that
            // highest volume
            // and set our volume relative to it.
            // If the desired volume is greater than the highest current volume, then we can just
            // go
            // ahead
            // with dacp_set_include_speaker_volume, setting the new current overall volume to the
            // desired new level
            // with the speaker at 100%

            int32_t highest_other_volume = 0;
            for (i = 0; i < speaker_count; i++) {
              if ((speaker_info[i].speaker_number != machine_number) &&
                  (speaker_info[i].active == 1) &&
                  (speaker_info[i].volume > highest_other_volume)) {
                highest_other_volume = speaker_info[i].volume;
              }
            }
            highest_other_volume = (highest_other_volume * overall_volume + 50) / 100;
            if (highest_other_volume <= vo) {
              debug(2,
                    "Highest other volume %d is less than or equal to the desired new volume %d.",
                    highest_other_volume, vo);
              http_response = dacp_set_include_speaker_volume(machine_number, vo);
            } else {
              debug(2, "Highest other volume %d is greater than the desired new volume %d.",
                    highest_other_volume, vo);
              // if the present overall volume is higher than the highest other volume at present,
              // then bring it down to it.
              if (overall_volume > highest_other_volume) {
                debug(2, "Lower overall volume to new highest volume.");
                http_response = dacp_set_include_speaker_volume(
                    machine_number,
                    highest_other_volume); // set the overall volume to the highest one
              }
              int32_t desired_relative_volume =
                  (vo * 100 + (highest_other_volume / 2)) / highest_other_volume;
              debug(2, "Set our speaker volume relative to the highest volume.");
              http_response = dacp_set_speaker_volume(
                  machine_number,
                  desired_relative_volume); // set the overall volume to the highest one
            }
          }
        }
      } else {
        debug(2, "Can't get speakers list");
      }
    } else {
      debug(2, "Can't get client volume");
    }

  } else {
    debug(2, "Invalid volume: %d -- ignored.", vo);
  }
  return http_response;
}
