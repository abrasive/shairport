#ifndef _RTSP_H
#define _RTSP_H

void rtsp_listen_loop(void);
// void rtsp_shutdown_stream(void);
void rtsp_request_shutdown_stream(void);

// initialise the metadata stuff

void metadata_init(void);

// sends metadata out to the metadata pipe, if enabled.
// It is sent with the type 'ssnc' the given code, data and length
// The handler at the other end must kknow what to do with the data
// e.g. it it's malloced, to free it, etc.
// nothing is done automatically

int send_ssnc_metadata(uint32_t code, char *data, uint32_t length, int block);

#endif // _RTSP_H
