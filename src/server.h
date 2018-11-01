#ifndef EVSOCKS_SERVER_H
#define EVSOCKS_SERVER_H


#include <string>
#include <memory>

#include <ev.h>

#include "socksdef.h"
#include "auth.h"
#include "iochannel.h"
#include "bufqueue.h"
#include "addr.h"
#include "dlist.hpp"
#include "timeout_list.hpp"
#include "error.h"


namespace evsocks {
    using namespace std;


    struct Server;
    struct RemoteConn;
    struct UDPPeer;

    struct ClientConn {
        enum State {
            INIT = 0,   // receiving methods
            AUTH,       // doing auth
            CMD,        // receiving cmd
            STREAM,     // connect cmd got
            UDP,        // udp association cmd got
        };

        ev_io reader_io;
        ev_io writer_io;
        IOChannel iochan;

        int fd;
        Addr addr;
        string addr_str;    // for logging

        Server *server;
        RemoteConn *remote;
        UDPPeer *udp_client;
        UDPPeer *udp_remote;
        Addr udp_client_from;

        TimeoutTracer timeout_tracer;
        TimeoutTracer idle_timeout_tracer;

        uint8_t state;
        void *auth_ctx;
        BufQueue input;

        ClientConn()
            : fd(-1), server(NULL), remote(NULL), udp_client(NULL), udp_remote(NULL)
            , state(INIT), auth_ctx(NULL)
        {}

        Error reply(uint8_t code, const Addr &addr);
        void cmd_connect(const Addr &remote_addr);
        void cmd_udp(const Addr &client_from);
    };

    struct RemoteConn {
        ev_io reader_io;
        ev_io writer_io;
        IOChannel iochan;

        int fd;
        Addr addr;
        string addr_str;    // for logging

        ClientConn *client;

        TimeoutTracer timeout_tracer;

        RemoteConn() : fd(-1), client(NULL) {}
    };

    struct UDPPeer {
        ev_io reader_io;

        int fd;
        Addr addr;

        ClientConn *client;

        UDPPeer() : fd(-1), client(NULL) {}
    };

    // TODO: config timeout
    struct Server {
        // public
        IServerHandler *handler;
        typedef void (*TermCb)(void *userdata);
        bool term_req;
        TermCb term_cb;
        void *term_userdata;

        // private
        struct ev_loop *loop;

        ev_io listen_io;
        int listen_fd;

        ev_timer timer;

        typedef EVSOCKS_TIMEOUT_LIST(ClientConn, timeout_tracer) ClientTimeoutList;
        ClientTimeoutList client_timeouts;
        typedef EVSOCKS_TIMEOUT_LIST(RemoteConn, timeout_tracer) RemoteTimeoutList;
        RemoteTimeoutList remote_timeouts;
        typedef EVSOCKS_TIMEOUT_LIST(ClientConn, idle_timeout_tracer) IdleTimeoutList;
        IdleTimeoutList idle_timeouts;

        // public
        Server(struct ev_loop *loop, IServerHandler *handler);

        Error init();
        Error start_listen(const string &host, uint16_t port);
        Error stop_listen();
        void term(TermCb cb, void *userdata);

        size_t clients() const;

        // private
        void on_connection(int fd, const Addr &addr);
        void on_client_error(ClientConn &client, Error err);
        void on_client_done(ClientConn &client);
        void on_remote_done(RemoteConn &remote);
        void on_udp_peer_done(UDPPeer &peer);
        void on_client_eof(ClientConn &client);
        void on_remote_eof(ClientConn &client);
        void on_timer();
        void update_client_timeout(ClientConn &client);
        void update_remote_timeout(RemoteConn &remote);
        void update_idle_timeout(ClientConn &client);
    };
}


#endif //EVSOCKS_SERVER_H
