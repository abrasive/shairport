#pragma once
#include "common.h"
#include "config.h"
#include <sys/socket.h>

#include "player.h"

typedef struct dacp_speaker_stuff {
  int64_t speaker_number;
  int active;
  int32_t volume;
  char *name; // this is really just for debugging
} dacp_spkr_stuff;

uint32_t dacp_tlv_crawl(
    char **p,
    int32_t *length); // return the code of the next TLV entity and advance the pointer beyond it.
ssize_t dacp_send_client_command(rtsp_conn_info *conn, const char *command, char *response,
                                 size_t max_response_length);
int32_t dacp_get_client_volume(rtsp_conn_info *conn); // return the overall volume from the client
int dacp_set_include_speaker_volume(rtsp_conn_info *conn, int64_t machine_number, int32_t vo);
int dacp_set_speaker_volume(rtsp_conn_info *conn, int64_t machine_number, int32_t vo);
int dacp_get_speaker_list(rtsp_conn_info *conn, dacp_spkr_stuff *speaker_array,
                          int max_size_of_array);
