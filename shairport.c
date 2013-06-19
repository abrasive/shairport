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
#include <sys/wait.h>
#include "common.h"
#include "rtsp.h"
#include "mdns.h"
#include "getopt_long.h"

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

static void sig_child(int foo, siginfo_t *bar, void *baz) {
    pid_t pid;
    while ((pid = waitpid((pid_t)-1, 0, WNOHANG)) > 0) {
        if (pid == mdns_pid && !shutting_down) {
            die("MDNS child process died unexpectedly!");
        }
    }
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
    printf("    -b FILL             set how full the buffer must be before audio output\n");
    printf("                        starts. This value is in frames; default %d\n", config.buffer_start_fill);
    printf("    -d, --daemon        fork (daemonise). The PID of the child process is\n");
    printf("                        written to stdout\n");
    printf("    -B, --on-start=COMMAND  run a shell command when playback begins\n");
    printf("    -E, --on-stop=COMMAND   run a shell command when playback ends\n");

    printf("    -o, --output=BACKEND    select audio output method\n");

    printf("\n");
    audio_ls_outputs();
}

int parse_options(int argc, char **argv) {
    // prevent unrecognised arguments from being shunted to the audio driver
    setenv("POSIXLY_CORRECT", "", 1);

    static struct option long_options[] = {
        {"help",    no_argument,        NULL, 'h'},
        {"daemon",  no_argument,        NULL, 'd'},
        {"port",    required_argument,  NULL, 'p'},
        {"name",    required_argument,  NULL, 'a'},
        {"output",  required_argument,  NULL, 'o'},
        {"on-start",required_argument,  NULL, 'B'},
        {"on-stop", required_argument,  NULL, 'E'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv,
                              "+hdvp:a:o:b:B:E:",
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
            case 'b':
                config.buffer_start_fill = atoi(optarg);
                break;
            case 'B':
                config.cmd_start = optarg;
                break;
            case 'E':
                config.cmd_stop = optarg;
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

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = &sig_child;
    sigaction(SIGCHLD, &sa, NULL);
}

static int daemon_pipe[2] = {-1, -1};
// forked daemon lets the spawner know it's up and running OK
// should be called only once!
void shairport_startup_complete(void) {
    if (config.daemonise) {
        write(daemon_pipe[1], "ok", 2);
        close(daemon_pipe[1]);
    }
}

int main(int argc, char **argv) {
    signal_setup();
    memset(&config, 0, sizeof(config));

    // set defaults
    config.buffer_start_fill = 220;
    config.port = 5002;
    char hostname[100];
    gethostname(hostname, 100);
    config.apname = malloc(20 + 100);
    snprintf(config.apname, 20 + 100, "Shairport on %s", hostname);

    // parse arguments into config
    int audio_arg = parse_options(argc, argv);

    int ret;
    if (config.daemonise) {
        ret = pipe(daemon_pipe);
        if (ret < 0)
            die("couldn't create a pipe?!");

        pid_t pid = fork();
        if (pid < 0)
            die("failed to fork!");

        if (pid) {
            char buf[8];
            ret = read(daemon_pipe[0], buf, sizeof(buf));
            if (ret < 0) {
                printf("Spawning the daemon failed.\n");
                exit(1);
            }

            printf("%d\n", pid);
            exit(0);
        }
    }

    config.output = audio_get_output(config.output_name);
    if (!config.output) {
        audio_ls_outputs();
        die("Invalid audio output specified!");
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
