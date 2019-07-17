#include <signal.h>
#include <getopt.h>
#include <stdio.h>
#include <string>

#include "ctxlog/ctxlog_evsocks.hpp"
#include "conv_util.hpp"
#include "server.h"


using namespace evsocks;


static void setup() {
    CTXLOG_PUSH_FUNC();

    // ignore sigpipe
    typedef void (*sighandler_t)(int);
    sighandler_t rv = ::signal(SIGPIPE, SIG_IGN);
    if (rv == SIG_ERR) {
        CTXLOG_ERR("%s", Error(ERR_SIGNAL, errno, "signal(SIGPIPE, SIG_IGN) error").str().c_str());
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


struct Argument {
    std::string listen;
    std::string username;
    std::string password;
};

static void usage(const char *prog) {
    const char *text =
        "Usage: %s [-l IP:PORT] [-u USER -p PASS]\n"
        "Arguments:\n"
        "   -l, --listen IP:PORT\n"
        "       Server address.\n"
        "   -u, --username\n"
        "   -p, --password\n"
        "       Authentication.\n";
    fprintf(stdout, text, prog);
}

static Argument get_args(int argc, char *argv[]) {
    Argument args;
    args.listen = ":1080";

    // https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html
    while (true) {
        struct option long_options[] = {
            /* These options don't set a flag.
                We distinguish them by their indices. */
            {"help",  no_argument, 0, 'h'},
            {"listen",  required_argument, 0, 'l'},
            {"username", required_argument, 0, 'u'},
            {"password", required_argument, 0, 'p'},
            {0, 0, 0, 0}
        };

        /* getopt_long stores the option index here. */
        int option_index = 0;
        int c = getopt_long(argc, argv, "hl:u:p:", long_options, &option_index);
        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
        case 'l':
            args.listen = optarg;
            break;
        case 'u':
            args.username = optarg;
            break;
        case 'p':
            args.password = optarg;
            break;
        case '?':
            /* getopt_long already printed an error message. */
            // fallthrough
        case 'h':
            // fallthrough
        default:
            usage(argv[0]);
            exit(1);
        }
    }

    return args;
}

int main(int argc, char **argv) {
    // parse args
    Argument args = get_args(argc, argv);
    std::string listen_ip;
    uint16_t listen_port = 0;
    {
        size_t pos = args.listen.find(':');
        if (pos == std::string::npos) {
            CTXLOG_ERR("illegal args: --listen IP:PORT");
            return 1;
        }

        listen_ip = args.listen.substr(0, pos);
        listen_port = tz::cast<std::string, uint16_t>(args.listen.substr(pos + 1), 0u);
    }

    // global env setup
    setup();

    // use the default event loop unless you have special needs
    struct ev_loop *loop = EV_DEFAULT;

    // auth
    DefaultServerHandler default_handler;
    PasswordServerHandler pass_handler;
    IServerHandler *handler;
    if (args.username.empty() && args.password.empty()) {
        handler = &default_handler;
    } else {
        pass_handler.user2pass[args.username] = args.password;
        handler = &pass_handler;
    }

    Server server(loop, handler);

    SigCatcher sigcatcher;
    sigcatcher.server = &server;
    ev_signal_init(&sigcatcher.watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &sigcatcher.watcher);

    TRY(server.init());
    TRY(server.start_listen(listen_ip, listen_port));

    CTXLOG_INFO("starting server...");
    ev_run(loop, 0);

    // clean up
    assert(server.clients() == 0);
    ev_signal_stop(loop, &sigcatcher.watcher);
    ev_loop_destroy(loop);

    return 0;
}
