#ifndef _RTP_H
#define _RTP_H

#include <sys/socket.h>

void rtp_setup(SOCKADDR *remote, int controlport, int timingport, int *local_server_port, int *local_control_port, int *local_timing_port);
void rtp_shutdown(void);
void rtp_request_resend(seq_t first, seq_t last);

void get_reference_timestamp_stuff(uint32_t *timestamp,uint64_t *timestamp_time);
void clear_reference_timestamp(void); 

#endif // _RTP_H
