#ifndef _PLAYER_H
#define _PLAYER_H

#include "config.h"

#ifdef HAVE_LIBMBEDTLS
#include <mbedtls/aes.h>
#include <mbedtls/havege.h>
#endif

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/aes.h>
#endif

#include "audio.h"
#include "alac.h"

typedef uint16_t seq_t;

typedef struct audio_buffer_entry { // decoded audio packets
  int ready;
  int64_t timestamp;
  seq_t sequence_number;
  signed short *data;
  int length; // the length of the decoded data
} abuf_t;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
#define BUFFER_FRAMES 512

typedef struct {
  int encrypted;
  uint8_t aesiv[16], aeskey[16];
  int32_t fmtp[12];
} stream_cfg;

typedef struct {
  int fd;
  int authorized; // set if a password is required and has been supplied
  stream_cfg stream;
  SOCKADDR remote, local;
  int stop;
  int running;
  pthread_t thread;
  pthread_t player_thread;

  abuf_t audio_buffer[BUFFER_FRAMES];
  int max_frames_per_packet,input_num_channels,input_bit_depth,input_rate;
  int input_bytes_per_frame,output_bytes_per_frame,output_sample_ratio;
  int max_frame_size_change;
  int64_t previous_random_number;
  alac_file *decoder_info;
  uint32_t please_stop;
  uint64_t packet_count;
  int shutdown_requested;
  int connection_state_to_output;
  int player_thread_please_stop;
  int64_t first_packet_time_to_play, time_since_play_started; // nanoseconds
  // stats
	uint64_t missing_packets, late_packets, too_late_packets, resend_requests;
	int decoder_in_use;
	// debug variables
	int32_t last_seqno_read;
// mutexes and condition variables
	pthread_cond_t flowcontrol;
	pthread_mutex_t ab_mutex,flush_mutex;
	pthread_mutex_t vol_mutex;
	int fix_volume;
	uint32_t timestamp_epoch, last_timestamp, maximum_timestamp_interval; // timestamp_epoch of zero means not initialised, could start at 2
                                // or 1.
  int ab_buffering,ab_synced;
	int64_t first_packet_timestamp;
	int flush_requested;
	int64_t flush_rtp_timestamp;
	uint64_t time_of_last_audio_packet;
	seq_t ab_read, ab_write;

#ifdef HAVE_LIBMBEDTLS
  mbedtls_aes_context dctx;
#endif

#ifdef HAVE_LIBPOLARSSL
  aes_context dctx;
#endif

#ifdef HAVE_LIBSSL
  AES_KEY aes;
#endif
} rtsp_conn_info;

int player_play(pthread_t *thread, rtsp_conn_info* conn);
void player_stop(pthread_t *thread, rtsp_conn_info* conn);

void player_volume(double f, rtsp_conn_info* conn);
void player_flush(int64_t timestamp, rtsp_conn_info* conn);
void player_put_packet(seq_t seqno, int64_t timestamp, uint8_t *data, int len, rtsp_conn_info* conn);

int64_t monotonic_timestamp(uint32_t timestamp,rtsp_conn_info* conn); // add an epoch to the timestamp. The monotonic
                                                 // timestamp guaranteed to start between 2^32 2^33
                                                 // frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

uint64_t monotonic_seqno(uint16_t seq_no); // add an epoch to the seq_no. Uses the accompanying
                                           // timstamp to detemine the correct epoch

#endif //_PLAYER_H
