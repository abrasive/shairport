#pragma once
#include "common.h"
#include "config.h"
#include <pthread.h>
#include <sys/socket.h>

#include "player.h"

static pthread_mutex_t dacp_server_information_lock;
static pthread_cond_t dacp_server_information_cv;

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

int dacp_get_speaker_list(dacp_spkr_stuff *speaker_array,
                          int max_size_of_array);
void set_dacp_server_information(rtsp_conn_info *conn); // tell the DACP conversation thread that
                                                        // the dacp server information has been set
                                                        // or changed
int send_simple_dacp_command(const char *command);

void dacp_get_volume(void); // get the speaker volume information from the DACP source and store it in the metadata_hub
