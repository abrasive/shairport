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
#include <unistd.h>
#include <memory.h>
#include <openssl/md5.h>
#include "common.h"
#include "rtsp.h"
#include "mdns.h"

static int shutting_down = 0;
void shairport_shutdown(void) {
    if (shutting_down)
        return;
    shutting_down = 1;
    printf("Shutting down...\n");
    mdns_unregister();
    rtsp_shutdown_stream();
    config.output->deinit();
    exit(0);
}

static void sig_ignore(int foo, siginfo_t *bar, void *baz) {
}
static void sig_shutdown(int foo, siginfo_t *bar, void *baz) {
    shairport_shutdown();
}

void usage(char *progname) {
    printf("Usage: %s [options...] [-- [output options...]]\n\n", progname);
    printf("Available options:\n"
           "    -h          show this help\n"
           "    -p port     set RTSP listening port\n"
           "    -a name     set advertised name\n"
           "    -o output   set audio output\n"
           "    -b fill     set how full the buffer must be before audio output starts\n"
           "                    This value is in frames; default %d\n"
           "    -d          fork (daemonise)\n"
           "                    The PID of the child process is written to stdout\n"
           "Run %s -o <output> -h to find the available options for a specific output\n"
           "\n", config.buffer_start_fill, progname);

    if (config.output_name)
        config.output = audio_get_output(config.output_name);
    if (config.output) {
        printf("Options for output %s:\n", config.output_name);
        config.output->help();
    } else {
        audio_ls_outputs();
    }
}

int parse_options(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "+hdvp:a:o:b:")) > 0) {
        switch (opt) {
            default:
                printf("Unknown argument -%c\n", optopt);
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
            case 'b':
                config.buffer_start_fill = atoi(optarg);
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
    sigdelset(&set, SIGSTOP);
    sigdelset(&set, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    // setting this to SIG_IGN would prevent signalling any threads.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = &sig_ignore;
    sigaction(SIGUSR1, &sa, NULL);
    
    sa.sa_sigaction = &sig_shutdown;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}

int main(int argc, char **argv) {
    signal_setup();
    memset(&config, 0, sizeof(config));

    // set defaults
    config.buffer_start_fill = 220;
    config.port = 9000;
    char hostname[100];
    gethostname(hostname, 100);
    config.apname = malloc(20 + 100);
    snprintf(config.apname, 20 + 100, "Shairport on %s", hostname);

    int audio_arg = parse_options(argc, argv);
    
    config.output = audio_get_output(config.output_name);
    if (!config.output) {
        audio_ls_outputs();
        die("Invalid audio output specified!\n");
    }
    config.output->init(argc-audio_arg, argv+audio_arg);

    uint8_t ap_md5[16];
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, config.apname, strlen(config.apname));
    MD5_Final(ap_md5, &ctx);
    memcpy(config.hw_addr, ap_md5, sizeof(config.hw_addr));


    rtsp_listen_loop();

    // should not.
    shairport_shutdown();
    return 1;
}
