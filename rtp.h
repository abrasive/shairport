#ifndef _RTP_H
#define _RTP_H

#include <sys/socket.h>

#include "player.h"

void rtp_initialise(rtsp_conn_info *conn);
void rtp_terminate(rtsp_conn_info *conn);

void *rtp_audio_receiver(void *arg);
void *rtp_control_receiver(void *arg);
void *rtp_timing_receiver(void *arg);

void rtp_setup(SOCKADDR *local, SOCKADDR *remote, int controlport, int timingport,
               int *local_server_port, int *local_control_port, int *local_timing_port,
               rtsp_conn_info *conn);
void rtp_request_resend(seq_t first, uint32_t count, rtsp_conn_info *conn);
void rtp_request_client_pause(rtsp_conn_info *conn); // ask the client to pause

void get_reference_timestamp_stuff(int64_t *timestamp, uint64_t *timestamp_time,
                                   uint64_t *remote_timestamp_time, rtsp_conn_info *conn);
void clear_reference_timestamp(rtsp_conn_info *conn);

uint64_t static local_to_remote_time_jitters;
uint64_t static local_to_remote_time_jitters_count;

#endif // _RTP_H
