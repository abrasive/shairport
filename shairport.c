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

#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <memory.h>
#include <openssl/md5.h>
#include <sys/wait.h>
#include <getopt.h>
#include "common.h"
#include "rtsp.h"
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

void usage(char *progname) {
    printf("Usage: %s [options...]\n", progname);
    printf("  or:  %s [options...] -- [audio output-specific options]\n", progname);

    printf("\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");

    printf("\n");
    printf("Options:\n");
    printf("    -h, --help          show this help\n");
    printf("    -p, --port=PORT     set RTSP listening port\n");
    printf("    -a, --name=NAME     set advertised name\n");
    printf("    -L  --latency=FRAMES set how many frames between a just-received frame and audio output\n");
    printf("                        starts. This value is in frames; default %d\n", config.latency);
    printf("    -d, --daemon        daemonise.\n");
    printf("    -k, --kill          kill the existing shairport daemon.\n");
    printf("    -B, --on-start=COMMAND  run a shell command when playback begins\n");
    printf("    -E, --on-stop=COMMAND   run a shell command when playback ends\n");

    printf("    -o, --output=BACKEND    select audio output method\n");
    printf("    -m, --mdns=BACKEND      force the use of BACKEND to advertize the service\n");
    printf("                            if no mdns provider is specified,\n");
    printf("                            shairport tries them all until one works.\n");

    printf("\n");
    mdns_ls_backends();
    printf("\n");
    audio_ls_outputs();
}

int parse_options(int argc, char **argv) {
    // prevent unrecognised arguments from being shunted to the audio driver
    setenv("POSIXLY_CORRECT", "", 1);

    static struct option long_options[] = {
        {"help",    no_argument,        NULL, 'h'},
        {"daemon",  no_argument,        NULL, 'd'},
        {"kill",    no_argument,        NULL, 'k'},
        {"port",    required_argument,  NULL, 'p'},
        {"name",    required_argument,  NULL, 'a'},
        {"output",  required_argument,  NULL, 'o'},
        {"on-start",required_argument,  NULL, 'B'},
        {"on-stop", required_argument,  NULL, 'E'},
        {"mdns",    required_argument,  NULL, 'm'},
        {"latency", required_argument,  NULL, 'L'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv,
                              "+hdvkp:a:o:b:B:E:m:L:",
                              long_options, NULL)) > 0) {
        switch (opt) {
            default:
            case 'h':
                usage(argv[0]);
                exit(1);
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
                config.latency = atoi(optarg);
                break;
            case 'B':
                config.cmd_start = optarg;
                break;
            case 'E':
                config.cmd_stop = optarg;
                break;
            case 'm':
                config.mdns_name = optarg;
                break;
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

    pid_t pid;
    
    daemon_set_verbosity(LOG_DEBUG);

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

    /* Check if we are called with -k parameter */
    if (argc >= 2 && !strcmp(argv[1], "-k")) {
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
    config.latency = 99400; // this seems to work pretty well -- two left-ear headphones, one from the iMac jack, one from an NSLU2 running a cheap "3D Sound" USB Soundcard
    config.buffer_start_fill = 220;
    config.port = 5002;
    char hostname[100];
    gethostname(hostname, 100);
    config.apname = malloc(20 + 100);
    snprintf(config.apname, 20 + 100, "%s Shairport Sync", hostname);

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
              daemon_log(LOG_ERR, "Could not recieve return value from daemon process: %s", strerror(errno));
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
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, config.apname, strlen(config.apname));
    MD5_Final(ap_md5, &ctx);
    memcpy(config.hw_addr, ap_md5, sizeof(config.hw_addr));


    rtsp_listen_loop();

    // should not rerach this...
    shairport_shutdown();
finish:
    daemon_log(LOG_NOTICE, "Unexpected exit...");
    daemon_retval_send(255);
    daemon_pid_file_remove();
    return 1;

}
