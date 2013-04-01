#include <signal.h>
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


int main(int argc, char **argv) {
    memset(&config, 0, sizeof(config));
//    config.password = "hello";
    memcpy(config.hw_addr, "\0\x11\x22\x33\x44\x55", 6);
    config.buffer_start_fill = 220;
    config.port = 9000;
    config.apname = "hellothere";

    config.output = &audio_dummy;
    //config.output = &audio_ao;

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

    config.output->init(0, 0);

    rtsp_listen_loop();

    // should not.
    shutdown();
    return 1;
}
