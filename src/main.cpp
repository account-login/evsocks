#include <signal.h>

#include "ctxlog/ctxlog_evsocks.hpp"
#include "server.h"


using namespace evsocks;


void setup() {
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


int main() {
    setup();

    Server server(NULL);

    // use the default event loop unless you have special needs
    struct ev_loop *loop = EV_DEFAULT;
    Error err = server.start(loop, "", 1080);
    if (!err.ok()) {
        CTXLOG_ERR("%s", err.str().c_str());
        return 1;
    }

    CTXLOG_INFO("starting server...");
    ev_run(loop, 0);
    // TODO: clean up

    return 0;
}
