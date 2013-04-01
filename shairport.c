#include <signal.h>
#include <unistd.h>
#include <memory.h>
#include "common.h"
#include "rtsp.h"
#include "mdns.h"

static int shutting_down = 0;
static void shutdown(void) {
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
    shutdown();
}

void usage(char *progname) {
    printf("Usage: %s [options...] [-- [output options...]]\n\n", progname);
    printf("Available options:\n"
           "    -h          show this help\n"
           "    -p port     set RTSP listening port\n"
           "    -n name     set advertised name\n"
           "    -o output   set audio output\n"
           "    -s fill     set how full the buffer must be before audio output starts\n"
           "                    This value is in frames; default 220\n"
           "Run %s -o <output> -h to find the available options for a specific output\n"
           "\n", progname);

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
    char opt;
    while ((opt = getopt(argc, argv, "+hp:n:o:s:")) > 0) {
        switch (opt) {
            default:
                printf("Unknown argument -%c\n", optopt);
            case 'h':
                usage(argv[0]);
                exit(1);
            case 'v':
                debuglev++;
                break;
            case 'p':
                config.port = atoi(optarg);
                break;
            case 'n':
                config.apname = optarg;
                break;
            case 'o':
                config.output_name = optarg;
                break;
            case 's':
                config.buffer_start_fill = atoi(optarg);
                break;
        }
    }
    return optind;
}


int main(int argc, char **argv) {
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

    rtsp_listen_loop();

    // should not.
    shutdown();
    return 1;
}
