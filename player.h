#ifndef _PLAYER_H
#define _PLAYER_H

#include <arpa/inet.h>
#include <pthread.h>

#include "config.h"
#include "definitions.h"

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

#include "alac.h"
#include "audio.h"

#define time_ping_history 8

#if defined(HAVE_DBUS) || defined(HAVE_MPRIS)
enum session_status_type {
  SST_stopped = 0, // not playing anything
  SST_paused,      // paused
  SST_playing,
} sst_type;
#endif

typedef struct time_ping_record {
  uint64_t local_to_remote_difference;
  uint64_t dispersion;
  uint64_t local_time;
  uint64_t remote_time;
} time_ping_record;

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
  int connection_number; // for debug ID purposes, nothing else...
  int64_t staticLatencyCorrection; // it seems iTunes needs some offset before it's more or less right. Odd.
#if defined(HAVE_DBUS) || defined(HAVE_MPRIS)
  enum session_status_type play_state;
#endif
  int fd;
  int authorized; // set if a password is required and has been supplied
  stream_cfg stream;
  SOCKADDR remote, local;
  int stop;
  int running;
  pthread_t thread;

  // pthread_t *ptp;
  pthread_t *player_thread;

  abuf_t audio_buffer[BUFFER_FRAMES];
  int max_frames_per_packet, input_num_channels, input_bit_depth, input_rate;
  int input_bytes_per_frame, output_bytes_per_frame, output_sample_ratio;
  int max_frame_size_change;
  int64_t previous_random_number;
  alac_file *decoder_info;
  uint32_t please_stop;
  uint64_t packet_count;
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
  pthread_mutex_t ab_mutex, flush_mutex;
  pthread_mutex_t vol_mutex;
  int fix_volume;
  uint32_t timestamp_epoch, last_timestamp,
      maximum_timestamp_interval; // timestamp_epoch of zero means not initialised, could start at 2
                                  // or 1.
  int ab_buffering, ab_synced;
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

#ifdef HAVE_DBUS
  int32_t dacp_volume;
#endif

  int amountStuffed;

  int32_t framesProcessedInThisEpoch;
  int32_t framesGeneratedInThisEpoch;
  int32_t correctionsRequestedInThisEpoch;
  int64_t syncErrorsInThisEpoch;

  // RTP stuff
  // only one RTP session can be active at a time.
  int rtp_running;

  char client_ip_string[INET6_ADDRSTRLEN]; // the ip string pointing to the client
  char self_ip_string[INET6_ADDRSTRLEN];   // the ip string being used by this program -- it
                                           // could be one of many, so we need to know it
  uint32_t self_scope_id;                  // if it's an ipv6 connection, this will be its scope
  short connection_ip_family;              // AF_INET / AF_INET6
  uint32_t client_active_remote;           // used when you want to control the client...

  SOCKADDR rtp_client_control_socket; // a socket pointing to the control port of the client
  SOCKADDR rtp_client_timing_socket;  // a socket pointing to the timing port of the client
  int audio_socket;                   // our local [server] audio socket
  int control_socket;                 // our local [server] control socket
  int timing_socket;                  // local timing socket

  int64_t reference_timestamp;
  uint64_t reference_timestamp_time;
  uint64_t remote_reference_timestamp_time;

  // debug variables
  int request_sent;

  uint8_t time_ping_count;
  struct time_ping_record time_pings[time_ping_history];

  uint64_t departure_time; // dangerous -- this assumes that there will never be two timing
                           // request in flight at the same time

  pthread_mutex_t reference_time_mutex;

  uint64_t local_to_remote_time_difference; // used to switch between local and remote clocks

  int timing_sender_stop; // for asking the timing-sending thread to stop
  int last_stuff_request;

  int64_t play_segment_reference_frame;
  uint64_t play_segment_reference_frame_remote_time;

  int32_t buffer_occupancy; // allow it to be negative because seq_diff may be negative
  int64_t session_corrections;

  int play_number_after_flush;

  // remote control stuff. The port to which to send commands is not specified, so you have to use
  // mdns to find it.
  // at present, only avahi can do this

  char *dacp_id;               // id of the client -- used to find the port to be used
  uint16_t dacp_port;          // port on the client to send remote control messages to, else zero
  uint32_t dacp_active_remote; // key to send to the remote controller
  void *mdns_private_pointer;  // private storage (just a pointer) for the dacp_port resolver

} rtsp_conn_info;

int player_play(rtsp_conn_info *conn);
void player_stop(rtsp_conn_info *conn);

void player_volume(double f, rtsp_conn_info *conn);
void player_volume_without_notification(double f, rtsp_conn_info *conn);
void player_flush(int64_t timestamp, rtsp_conn_info *conn);
void player_put_packet(seq_t seqno, int64_t timestamp, uint8_t *data, int len,
                       rtsp_conn_info *conn);

int64_t monotonic_timestamp(uint32_t timestamp,
                            rtsp_conn_info *conn); // add an epoch to the timestamp. The monotonic
// timestamp guaranteed to start between 2^32 2^33
// frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

#endif //_PLAYER_H
