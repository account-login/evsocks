#include <signal.h>

#include "ctxlog/ctxlog_evsocks.hpp"
#include "server.h"


using namespace evsocks;


static void setup() {
    CTXLOG_PUSH_FUNC();

    // ignore sigpipe
    typedef void (*sighandler_t)(int);
    sighandler_t rv = ::signal(SIGPIPE, SIG_IGN);
    if (rv == SIG_ERR) {
        CTXLOG_ERR("%s", Error(ERR_SIGNAL, errno, "signal(SIGPIPE, SIG_IGN) error"));
    }

    // log
    std::ios_base::sync_with_stdio(false);
}


struct SigCatcher {
    ev_signal watcher;
    Server *server;
    uint32_t int_count;

    SigCatcher() : server(NULL), int_count(0) {}
};


static void term_cb(void *userdata) {
    CTXLOG_INFO("exiting loop");
    struct ev_loop *loop = (struct ev_loop *)userdata;
    ev_break(loop, EVBREAK_ALL);
}


static void sigint_cb(struct ev_loop *loop, ev_signal *w, int revents) {
    (void)revents;

    SigCatcher *catcher = (SigCatcher *)(void *)w;
    catcher->int_count++;
    Server *server = catcher->server;
    CTXLOG_INFO("interuption #%u. stop listening. current clients: %zu", catcher->int_count, server->clients());

    Error err;
    if (catcher->int_count == 1) {
        err = server->term(term_cb, loop);
    } else {
        err = server->force_term();
    }

    if (!err.ok()) {
        CTXLOG_ERR("%s", err.str().c_str());
    }
}


#define TRY(exp) do { \
        Error err = exp; \
        if (!err.ok()) { \
            CTXLOG_ERR("%s", err.str().c_str()); \
            return 1; \
        } \
    } while (0)


int main() {
    setup();

    // use the default event loop unless you have special needs
    struct ev_loop *loop = EV_DEFAULT;

    Server server(loop, NULL);

    SigCatcher sigcatcher;
    sigcatcher.server = &server;
    ev_signal_init(&sigcatcher.watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &sigcatcher.watcher);

    TRY(server.init());
    TRY(server.start_listen("", 1080));

    CTXLOG_INFO("starting server...");
    ev_run(loop, 0);

    // clean up
    assert(server.clients() == 0);
    ev_signal_stop(loop, &sigcatcher.watcher);
    ev_loop_destroy(loop);

    return 0;
}
