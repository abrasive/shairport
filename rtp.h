#ifndef _RTP_H
#define _RTP_H

#include <sys/socket.h>

int rtp_setup(SOCKADDR *remote, int *controlport, int *timingport);
void rtp_record(int rtp_mode);
void rtp_shutdown(void);
void rtp_request_resend(seq_t first, seq_t last);
long long get_sync_time();
long long tstp_us();

#endif // _RTP_H
