/*
 * Shairport, an Apple Airplay receiver
 * Copyright (c) James Laird 2013
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
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

#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/wait.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"

#ifdef HAVE_LIBPOLARSSL
#include <polarssl/md5.h>
#endif

#ifdef HAVE_LIBSSL
#include <openssl/md5.h>
#endif

#include "common.h"
#include "rtsp.h"
#include "rtp.h"
#include "mdns.h"

#include <libdaemon/dfork.h>
#include <libdaemon/dsignal.h>
#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dexec.h>


static int shutting_down = 0;

void shairport_shutdown() {
	if (shutting_down)
		return;
	shutting_down = 1;
	mdns_unregister();
	rtsp_shutdown_stream();
	if (config.output)
		config.output->deinit();
}

static void sig_ignore(int foo, siginfo_t *bar, void *baz) {
}
static void sig_shutdown(int foo, siginfo_t *bar, void *baz) {
	daemon_log(LOG_NOTICE, "Shutdown requested...");
	shairport_shutdown();
	daemon_log(LOG_NOTICE, "Exit...");
	daemon_retval_send(255);
	daemon_pid_file_remove();
	exit(0);
}

static void sig_child(int foo, siginfo_t *bar, void *baz) {
	pid_t pid;
	while ((pid = waitpid((pid_t)-1, 0, WNOHANG)) > 0) {
		if (pid == mdns_pid && !shutting_down) {
			die("MDNS child process died unexpectedly!");
		}
	}
}

static void sig_logrotate(int foo, siginfo_t *bar, void *baz) {
//    log_setup();
}

static void sig_pause_client(int foo, siginfo_t *bar, void *baz) {
  rtp_request_client_pause();
}

void print_version(void) {
  char version_string[200];
	strcpy(version_string,PACKAGE_VERSION);
#ifdef HAVE_LIBPOLARSSL
	strcat(version_string,"-polarssl");
#endif
#ifdef HAVE_LIBSSL
	strcat(version_string,"-openssl");
#endif
#ifdef CONFIG_TINYSVCMDNS
	strcat(version_string,"-tinysvcmdns");
#endif
#ifdef CONFIG_AVAHI
	strcat(version_string,"-Avahi");
#endif
#ifdef CONFIG_DNS_SD
	strcat(version_string,"-dns_sd");
#endif
#ifdef CONFIG_ALSA
	strcat(version_string,"-ALSA");
#endif
#ifdef CONFIG_SNDIO
	strcat(version_string,"-sndio");
#endif
#ifdef CONFIG_AO
	strcat(version_string,"-ao");
#endif
#ifdef CONFIG_PULSE
	strcat(version_string,"-pulse");
#endif
#ifdef HAVE_LIBSOXR
	strcat(version_string,"-soxr");
#endif
  printf("%s\n",version_string);
}

void usage(char *progname) {
    printf("Usage: %s [options...]\n", progname);
    printf("  or:  %s [options...] -- [audio output-specific options]\n", progname);

    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");

    printf("\n");
    printf("Options:\n");
    printf("    -h, --help              show this help\n");
    printf("    -V, --version           show version information\n");
    printf("    -v, --verbose           -v print debug information; -vv more; -vvv lots\n");
    printf("    -p, --port=PORT         set RTSP listening port\n");
    printf("    -a, --name=NAME         set advertised name\n");
    printf("    -A, --AirPlayLatency=FRAMES set how many frames between a just-received frame and audio output\n");
    printf("                            if the source's User Agent is \"AirPlay\". The default value is %u frames.\n", config.AirPlayLatency);
    printf("    -i, --iTunesLatency=FRAMES set how many frames between a just-received frame and audio output\n");
    printf("                            if the source's User Agent is \"iTunes\". The default value is %u frames.\n", config.iTunesLatency);
    printf("    -L, --latency=FRAMES set how many frames between a just-received frame and audio output\n");
    printf("                            starts. This value is in frames; if non-zero overrides all other latencies.\n");
    printf("                            Default latency without \"AirPlay\" or \"iTunes\" User Agent is %u frames.\n", config.AirPlayLatency);
    printf("    -S, --stuffing=MODE set how to adjust current latency to match desired latency \n");
    printf("                            \"basic\" (default) inserts or deletes audio frames from packet frames with low processor overhead.\n");
    printf("                            \"soxr\" uses libsoxr to minimally resample packet frames -- moderate processor overhead.\n");
    printf("                            \"soxr\" option only available if built with soxr support.\n");
    printf("    -d, --daemon            daemonise.\n");
    printf("    -k, --kill              kill the existing shairport daemon.\n");
    printf("    -P, --pause             send a pause request to the audio source.\n");
    printf("    -B, --on-start=PROGRAM  run PROGRAM when playback is about to begin.\n");
    printf("    -E, --on-stop=PROGRAM   run PROGRAM when playback has ended.\n");
    printf("                            For -B and -E options, specify the full path to the program, e.g. /usr/bin/logger.\n");
    printf("                            Executable scripts work, but must have #!/bin/sh (or whatever) in the headline.\n");
    printf("    -w, --wait-cmd          wait until the -B or -E programs finish before continuing\n");
    printf("    -o, --output=BACKEND    select audio output method\n");
    printf("    -m, --mdns=BACKEND      force the use of BACKEND to advertize the service\n");
    printf("                            if no mdns provider is specified,\n");
    printf("                            shairport tries them all until one works.\n");
    printf("    -r, --resync=THRESHOLD  resync if error exceeds this number of frames. Set to 0 to stop resyncing.\n");
    printf("    -t, --timeout=SECONDS   go back to idle mode from play mode after a break in communications of this many seconds (default 120). Set to 0 never to exit play mode.\n");

    printf("\n");
    mdns_ls_backends();
    printf("\n");
    audio_ls_outputs();
}

int parse_options(int argc, char **argv) {
    // prevent unrecognised arguments from being shunted to the audio driver
    setenv("POSIXLY_CORRECT", "", 1);
    char version_string[200];
    static struct option long_options[] = {
        {"help",    no_argument,        NULL, 'h'},
        {"version",	no_argument,     		NULL, 'V'},
        {"verbose",	no_argument,     		NULL, 'v'},
        {"daemon",  no_argument,        NULL, 'd'},
        {"pause",    no_argument,       NULL, 'P'},
        {"kill",    no_argument,        NULL, 'k'},
        {"port",    required_argument,  NULL, 'p'},
        {"name",    required_argument,  NULL, 'a'},
        {"output",  required_argument,  NULL, 'o'},
        {"on-start",required_argument,  NULL, 'B'},
        {"on-stop", required_argument,  NULL, 'E'},
        {"wait-cmd",no_argument,        NULL, 'w'},
        {"mdns",    required_argument,  NULL, 'm'},
        {"latency", required_argument,  NULL, 'L'},
        {"stuffing", required_argument,	NULL,	'S'},
        {"AirPlayLatency", required_argument,  NULL, 'A'},
        {"iTunesLatency", required_argument,  NULL, 'i'},
        {"resync",    required_argument,  NULL, 'r'},
        {"timeout",    required_argument,  NULL, 't'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv,
                              "+hdvVPkp:a:o:b:B:E:wm:L:S:A:i:r:t:",
                              long_options, NULL)) > 0) {
        switch (opt) {       
        // Note -- Version, Kill, Pause and Help done separately
            case 't':
            	config.timeout = atoi(optarg);
            	break;
            case 'r':
            	config.resyncthreshold = atoi(optarg);
            	break;
            case 'd':
                config.daemonise = 1;
                break;
            case 'v':
                debuglev++;
                break;
            case 'p':
                config.port = atoi(optarg);
                break;
            case 'a':
                config.apname = optarg;
                break;
            case 'o':
                config.output_name = optarg;
                break;
            case 'L':
                config.userSuppliedLatency = atoi(optarg);
                break;
            case 'A':
                config.AirPlayLatency = atoi(optarg);
                break;
            case 'i':
                config.iTunesLatency = atoi(optarg);
                break;
            case 'B':
                config.cmd_start = optarg;
                break;
            case 'E':
                config.cmd_stop = optarg;
                break;
            case 'S':
            		if (strcmp(optarg,"basic")==0)           		
                	config.packet_stuffing = ST_basic;
                else if (strcmp(optarg,"soxr")==0)
#ifdef HAVE_LIBSOXR
                	config.packet_stuffing = ST_soxr;
#else
                	die("soxr option not available -- this version of shairport-sync was built without libsoxr support");
#endif
                else
                	die("Illegal stuffing option -- must be \"basic\" or \"soxr\"");

                break;
            case 'w':
 								config.cmd_blocking = 1;
								break;
            case 'm':
                config.mdns_name = optarg;
                break;
            case '?':
                /* getopt_long already printed an error message. */
                exit(1);
                break; // unnecessary
            default:
                die("Unexpected option character."); 
                break; //unnecessary         
        }
    }
    return optind;
}

void signal_setup(void) {
    // mask off all signals before creating threads.
    // this way we control which thread gets which signals.
    // for now, we don't care which thread gets the following.
    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, SIGINT);
    sigdelset(&set, SIGTERM);
    sigdelset(&set, SIGHUP);
    sigdelset(&set, SIGSTOP);
    sigdelset(&set, SIGCHLD);
    sigdelset(&set, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // setting this to SIG_IGN would prevent signalling any threads.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = &sig_ignore;
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = &sig_shutdown;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_sigaction = &sig_logrotate;
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_sigaction = &sig_pause_client;
    sigaction(SIGUSR2, &sa, NULL);

    sa.sa_sigaction = &sig_child;
    sigaction(SIGCHLD, &sa, NULL);
}

// forked daemon lets the spawner know it's up and running OK
// should be called only once!
void shairport_startup_complete(void) {
    if (config.daemonise) {
//        daemon_ready();
    }
}

int main(int argc, char **argv) {

    daemon_set_verbosity(LOG_DEBUG);
    
    /* Check if we are called with -V or --version parameter */
    if (argc >= 2 && ((strcmp(argv[1], "-V")==0) || (strcmp(argv[1], "--version")==0))) {
      print_version();
      exit(1);
    }

    /* Check if we are called with -h or --help parameter */
    if (argc >= 2 && ((strcmp(argv[1], "-h")==0) || (strcmp(argv[1], "--help")==0))) {
		  usage(argv[0]);
		  exit(1);
    }

    pid_t pid;

    /* Reset signal handlers */
    if (daemon_reset_sigs(-1) < 0) {
        daemon_log(LOG_ERR, "Failed to reset all signal handlers: %s", strerror(errno));
        return 1;
    }

    /* Unblock signals */
    if (daemon_unblock_sigs(-1) < 0) {
        daemon_log(LOG_ERR, "Failed to unblock all signals: %s", strerror(errno));
        return 1;
    }

    /* Set indentification string for the daemon for both syslog and PID file */
    daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(argv[0]);

    /* Check if we are called with -P or --pause parameter */
    if (argc >= 2 && ((strcmp(argv[1], "-P")==0) || (strcmp(argv[1], "--pause")==0))) {
      if ((pid = daemon_pid_file_is_running()) >= 0) {
        if (kill(pid,SIGUSR2)!=0) {  // try to send the signal
          daemon_log(LOG_WARNING, "Failed trying to send pause request to daemon pid: %d: %s",pid, strerror(errno));
        }
      } else {
        daemon_log(LOG_WARNING, "Can't send a pause request -- Failed to find daemon: %s", strerror(errno));
      }
      exit(1);
    }
      
    /* Check if we are called with -k or --kill parameter */
    if (argc >= 2 && ((strcmp(argv[1], "-k")==0) || (strcmp(argv[1], "--kill")==0))) {
      int ret;

      /* Kill daemon with SIGTERM */
      /* Check if the new function daemon_pid_file_kill_wait() is available, if it is, use it. */
      if ((ret = daemon_pid_file_kill_wait(SIGTERM, 5)) < 0)
        daemon_log(LOG_WARNING, "Failed to kill daemon: %s", strerror(errno));
      else
        daemon_pid_file_remove(); // 
      return ret < 0 ? 1 : 0;
    }

    /* Check that the daemon is not running twice at the same time */
    if ((pid = daemon_pid_file_is_running()) >= 0) {
        daemon_log(LOG_ERR, "Daemon already running on PID file %u", pid);
        return 1;
    }

    memset(&config, 0, sizeof(config));

    // set defaults
    config.latency = 99400; // iTunes
    config.userSuppliedLatency = 0; // zero means none supplied
    config.iTunesLatency = 99400; // this seems to work pretty well for iTunes -- two left-ear headphones, one from the iMac jack, one from an NSLU2 running a cheap "3D Sound" USB Soundcard
    config.AirPlayLatency = 88200; // this seems to work pretty well for AirPlay -- Syncs sound and vision on AppleTV, but also used for iPhone/iPod/iPad sources
    config.resyncthreshold = 441*5; // this number of frames is 50 ms
    config.timeout = 120; // this number of seconds to wait for [more] audio before switching to idle.
    config.buffer_start_fill = 220;
    config.port = 5000;
    config.packet_stuffing = ST_basic; // simple interpolation or deletion
    char hostname[100];
    gethostname(hostname, 100);
    config.apname = malloc(20 + 100);
    snprintf(config.apname, 20 + 100, "Shairport Sync on %s", hostname);

    // parse arguments into config
    int audio_arg = parse_options(argc, argv);

    // mDNS supports maximum of 63-character names (we append 13).
    if (strlen(config.apname) > 50)
        die("Supplied name too long (max 50 characters)");
   
    /* here, daemonise with libdaemon */
    
    if (config.daemonise) {
      /* Prepare for return value passing from the initialization procedure of the daemon process */
      if (daemon_retval_init() < 0) {
          daemon_log(LOG_ERR, "Failed to create pipe.");
          return 1;
      }

      /* Do the fork */
      if ((pid = daemon_fork()) < 0) {

          /* Exit on error */
          daemon_retval_done();
          return 1;

      } else if (pid) { /* The parent */
          int ret;

          /* Wait for 20 seconds for the return value passed from the daemon process */
          if ((ret = daemon_retval_wait(20)) < 0) {
              daemon_log(LOG_ERR, "Could not receive return value from daemon process: %s", strerror(errno));
              return 255;
          }

          if (ret != 0) 
          	daemon_log(ret != 0 ? LOG_ERR : LOG_INFO, "Daemon returned %i as return value.", ret);
          return ret;

      } else { /* The daemon */

          /* Close FDs */
          if (daemon_close_all(-1) < 0) {
              daemon_log(LOG_ERR, "Failed to close all file descriptors: %s", strerror(errno));

              /* Send the error condition to the parent process */
              daemon_retval_send(1);
              goto finish;
          }
          
          /* Create the PID file */
          if (daemon_pid_file_create() < 0) {
              daemon_log(LOG_ERR, "Could not create PID file (%s).", strerror(errno));
              daemon_retval_send(2);
              goto finish;
          }
                    
          /* Send OK to parent process */
          daemon_retval_send(0);
      }
      /* end libdaemon stuff */
    }

    signal_setup();

    config.output = audio_get_output(config.output_name);
    if (!config.output) {
        audio_ls_outputs();
        die("Invalid audio output specified!");
    }
    config.output->init(argc-audio_arg, argv+audio_arg);

    daemon_log(LOG_NOTICE, "Successful startup.");

    uint8_t ap_md5[16];

#ifdef HAVE_LIBSSL
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, config.apname, strlen(config.apname));
    MD5_Final(ap_md5, &ctx);
#endif

#ifdef HAVE_LIBPOLARSSL
     md5_context tctx;
     md5_starts(&tctx);
     md5_update(&tctx, (unsigned char *)config.apname, strlen(config.apname));
     md5_finish(&tctx, ap_md5);
#endif
    memcpy(config.hw_addr, ap_md5, sizeof(config.hw_addr));

    rtsp_listen_loop();

    // should not reach this...
    shairport_shutdown();
finish:
    daemon_log(LOG_NOTICE, "Unexpected exit...");
    daemon_retval_send(255);
    daemon_pid_file_remove();
    return 1;

}
