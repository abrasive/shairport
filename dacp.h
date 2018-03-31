#pragma once
#include "common.h"
#include "config.h"
#include <pthread.h>
#include <sys/socket.h>

#include "player.h"

typedef struct dacp_speaker_stuff {
  int64_t speaker_number;
  int active;
  int32_t volume;
  char *name; // this is really just for debugging
} dacp_spkr_stuff;

void dacp_monitor_start();

uint32_t dacp_tlv_crawl(
    char **p,
    int32_t *length); // return the code of the next TLV entity and advance the pointer beyond it.

int dacp_set_speaker_volume(int64_t machine_number, int32_t vo);

int dacp_get_speaker_list(dacp_spkr_stuff *speaker_array, int max_size_of_array,
                          int *actual_speaker_count);
void set_dacp_server_information(rtsp_conn_info *conn); // tell the DACP conversation thread that
                                                        // the dacp server information has been set
                                                        // or changed
void dacp_monitor_port_update_callback(
    char *dacp_id, uint16_t port); // a callback to say the port is no longer in use
int send_simple_dacp_command(const char *command);

int dacp_set_include_speaker_volume(int64_t machine_number, int32_t vo);
int dacp_get_client_volume(int32_t *result);
int dacp_get_volume(
    int32_t *the_actual_volume); // get the speaker volume information from the DACP source
int dacp_set_volume(int32_t vo); // set the volume of our speaker
