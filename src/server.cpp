#include <unistd.h>

#include "server.h"
#include "net.h"
#include "addr.h"
#include "error.h"
#include "ctxlog/ctxlog_evsocks.hpp"


using namespace evsocks;


static Error close_fd(int fd) {
    CTXLOG_PUSH_FUNC();
    Error err;
    if (::close(fd) != 0) {
        err = Error(ERR_CLOSE, errno, strfmt("close() failed for [fd:%d]", fd));
        CTXLOG_ERR("%s", err.str().c_str());
    }
    return err;
}

static bool is_again(int32_t err) {
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}


// libev callbacks
static void server_accept_cb(EV_P_ ev_io *w, int revents);
static void server_timer_cb(EV_P_ ev_timer *w, int revents);
static void client_send_cb(EV_P_ ev_io *io, int revents);
static void client_recv_cb(EV_P_ ev_io *io, int revents);
static void remote_send_cb(EV_P_ ev_io *io, int revents);
static void remote_recv_cb(EV_P_ ev_io *io, int revents);
static void udp_client_recv_cb(EV_P_ ev_io *io, int revents);
static void udp_remote_recv_cb(EV_P_ ev_io *io, int revents);


static const size_t k_read_buf_size = 1024 * 16;
static const size_t k_udp_read_buf_size = 1024 * 64;
static const size_t k_write_buf_max_size = 1024 * 64;

static DefaultServerHandler g_default_handler;


Server::Server(IServerHandler *handler)
    : handler(handler ? handler : static_cast<IServerHandler *>(&g_default_handler))
    , loop(NULL), listen_fd(-1)
    , client_timeouts(5.0), remote_timeouts(5.0), idle_timeouts(60 * 10)
{
    ev_init(&this->listen_io, server_accept_cb);
}

Error Server::start(EV_P_ const string &host, uint16_t port) {
    assert(this->loop == NULL);

    Error err = tcp_listen(this->listen_fd, host, port, SOMAXCONN);
    if (!err.ok()) {
        return err;
    }

    this->loop = EV_A;

    ev_io_set(&this->listen_io, this->listen_fd, EV_READ);
    ev_io_start(this->loop, &this->listen_io);

    ev_tstamp min_timeout = std::min(std::min(
        this->client_timeouts.timeout,
        this->remote_timeouts.timeout),
        this->idle_timeouts.timeout);
    ev_timer_init(&this->timer, server_timer_cb, min_timeout, 0);
    ev_timer_start(this->loop, &this->timer);

    return Ok();
}

static void server_accept_cb(EV_P_ ev_io *w, int revents) {
    CTXLOG_PUSH_FUNC();

    if (!(revents & EV_READ)) {
        return;
    }

    Server &server = *(Server *)((char *)w - offsetof(Server, listen_io));

    int connfd = -1;
    Addr addr;
    Error err = net_accept(connfd, server.listen_fd, addr);
    if (!err.ok()) {
        CTXLOG_ERR("[listenfd:%d] %s", server.listen_fd, err.str().c_str());
        return;
    }

    server.on_connection(connfd, addr);
}

static void server_timer_cb(EV_P_ ev_timer *w, int revents) {
    CTXLOG_PUSH_FUNC();

    if (!(revents & EV_TIMER)) {
        return;
    }

    Server &server = *(Server *)((char *)w - offsetof(Server, timer));
    server.on_timer();
}

static void client_recv_cb(EV_P_ ev_io *io, int revents) {
    if (!(revents & EV_READ)) {
        return;
    }
    ClientConn &client = *(ClientConn *)((char *)io - offsetof(ClientConn, reader_io));
    Server &server = *client.server;

    CTXLOG_PUSH_FUNC().set("client", client.addr_str);

    char buf[k_read_buf_size];
    ssize_t data_size = ::read(client.fd, buf, sizeof(buf));
    if (data_size < 0) {
        if (is_again(errno)) {
            CTXLOG_WARN("unexpected EAGAIN!");
            return;
        }
        return server.on_client_error(client,
            Error(ERR_READ, errno, "client_recv_cb() read() error"));
    } else if (data_size == 0) {    // eof
        if (client.state != ClientConn::STREAM && client.state != ClientConn::UDP) {
            CTXLOG_ERR("unexpected eof. [state:%u]", client.state);
            server.on_client_error(client,
                Error(ERR_EOF, 0, "client_recv_cv() eof error"));
        } else {
            server.on_client_eof(client);
        }
        return;
    }

    // stream data, skip state machine
    if (client.state == ClientConn::STREAM) {
        Error err = client.remote->iochan.write(buf, (size_t)data_size);
        if (!err.ok()) {
            return server.on_client_error(client, err);
        }
        server.update_remote_timeout(*client.remote);
        server.update_idle_timeout(client);
        return;
    }

    client.input.push(buf, (size_t)data_size);

    while (!client.input.empty()) {
        switch (client.state) {
        case ClientConn::INIT: {    // receive methods
            if (client.input.size() < 3) {
                return;
            }
            if (client.input[0] != 5) {
                return server.on_client_error(client,
                    Error(ERR_BAD_VERSION, 0, "client_recv_cb() error on receiving methods"));
            }

            uint8_t method_num = (uint8_t)client.input[1];
            if (method_num == 0 || method_num > 10) {
                return server.on_client_error(client,
                    Error(ERR_BAD_METHOD_NUM, 0, "client_recv_cb() error"));
            }
            if (client.input.size() < 2 + (size_t)client.input[1]) {
                return;
            }

            // choose method
            set<uint8_t> methods(client.input.data() + 2, client.input.data() + 2 + method_num);
            client.input.pop(2 + method_num);
            uint8_t chosen_method = server.handler->auth_begin(methods);
            char response[] = {5, (char)chosen_method};

            Error err = client.iochan.write(response, 2);
            if (!err.ok()) {
                return server.on_client_error(client, err);
            }

            if (chosen_method == METHOD_REJECT) {
                return server.on_client_error(client, Error(ERR_AUTH, 0, "auth methods rejected"));
//            } else if (chosen_method == METHOD_NONE) {
//                client.state = ClientConn::CMD;
            } else {
                client.state = ClientConn::AUTH;
            }
        } break;
        case ClientConn::AUTH: {
            uint32_t auth_state = IServerHandler::AUTH_STATE_NONE;
            Error err = server.handler->auth_perform(client, auth_state);
            if (!err.ok()) {
                return server.on_client_error(client, err);
            }

            switch (auth_state) {
            case IServerHandler::AUTH_STATE_DONE:
                server.handler->auth_end(client);
                client.state = ClientConn::CMD;
                break;
            case IServerHandler::AUTH_STATE_CONT:
                return;
            case IServerHandler::AUTH_STATE_FAIL:
                // auth_end() will be called
                return server.on_client_error(client, Error(ERR_AUTH, 0, "auth failure"));
            default:
                assert(!"unknown auth state");
                return server.on_client_error(client, Error(ERR_AUTH, 0, "unknown auth state"));
            }
        } break;
        case ClientConn::CMD: { // receive cmds
            if (client.input.size() < 4) {
                return;
            }

            if (client.input[0] != 5) {
                return server.on_client_error(client,
                    Error(ERR_BAD_VERSION, 0, "client_recv_cb() error on receiving cmd"));
            }

            uint8_t cmd = (uint8_t)client.input[1];
            uint8_t atype = (uint8_t)client.input[3];
            Addr remote_addr;

            size_t idx = 4;
            switch (atype) {
            case ATYPE_IPV4: {
                if (client.input.size() < idx + 4 + 2) {
                    return;
                }
                remote_addr = Addr::from_ipv4(&client.input[idx], 0);
                idx += 4;
            } break;
            case ATYPE_IPV6: {
                if (client.input.size() < idx + 16 + 2) {
                    return;
                }
                remote_addr = Addr::from_ipv6(&client.input[idx], 0);
                idx += 16;
            } break;
            case ATYPE_DOMAIN: {
                if (client.input.size() < idx + 1 + 1 + 2) {
                    return;
                }
                uint8_t domain_len = (uint8_t)client.input[idx];
                if (client.input.size() < idx + 1 + domain_len + 2) {
                    return;
                }
                ; // TODO: resolve domain name
                idx += 1 + domain_len;
            } break;
            default:
                return server.on_client_error(client,
                    Error(ERR_BAD_ATYPE, 0, "client_recv_cb() error on receiving cmd"));
            }
            remote_addr.port((uint16_t(client.input[idx]) << 8) | uint16_t(client.input[idx + 1]));
            idx += 2;
            client.input.pop(idx);

            // handle cmd
            switch (cmd) {
            case CMD_CONNECT:
                client.cmd_connect(remote_addr);
                assert(client.input.empty());
                break;
            case CMD_UDP:
                if (!client.input.empty()) {
                    return server.on_client_error(client,
                        Error(ERR_UNEXPECTED_DATA, 0, "unexpected data after udp association cmd"));
                }
                client.cmd_udp(remote_addr);
                break;
            default:
                CTXLOG_ERR("%s", Error(ERR_CMD_UNSUPPORTED, 0, "client_recv_cb() error").str().c_str());
                client.reply(REPLY_ERR, Addr());
                return;
            }
        } break;
        case ClientConn::UDP:
            return server.on_client_error(client,
                Error(ERR_UNEXPECTED_DATA, 0, "unexpected data after udp association cmd"));
        case ClientConn::STREAM: {
            assert(!"impossible state");
        } break;
        default:
            CTXLOG_ERR("unknown client state: %d", client.state);
            assert(!"unknown client state");
        } // switch state
    } // while input not empty

    client.input.shrink();
}

void ClientConn::cmd_connect(const Addr &remote_addr) {
    CTXLOG_PUSH_FUNC();
    CTXLOG_INFO("connecting to [remote:%s]", remote_addr.str().c_str());

    Server &server = *this->server;

    int connfd = -1;
    Error err = tcp_connect(connfd, remote_addr);
    if (!err.ok()) {
        CTXLOG_ERR("%s", err.str().c_str());
        this->reply(REPLY_ERR, Addr());
        return;
    }

    Addr local_addr;
    err = net_local_addr(connfd, local_addr);
    if (!err.ok()) {
        CTXLOG_ERR("%s", err.str());
    }

    // reply
    err = this->reply(REPLY_OK, local_addr);
    if (!err.ok()) {
        server.on_client_error(*this, err);
        return;
    }

    CTXLOG_INFO("cmd_connect: success");
    this->state = ClientConn::STREAM;

    // clear handshake timeout
    server.update_client_timeout(*this);
    // idle list
    server.update_idle_timeout(*this);

    RemoteConn &remote = *(new RemoteConn());
    remote.fd = connfd;
    remote.addr = remote_addr;
    remote.addr_str = remote.addr.str();
    remote.client = this;

    this->remote = &remote;
    this->iochan.producer = &remote.reader_io;

    remote.iochan.init(server.loop, k_write_buf_max_size);
    remote.iochan.producer = &this->reader_io;
    remote.iochan.consumer = &remote.writer_io;
    remote.iochan.buf.swap(this->input);    // transfer data after cmd
    assert(this->input.empty());

    ev_io_init(&remote.reader_io, remote_recv_cb, remote.fd, EV_READ);
    ev_io_init(&remote.writer_io, remote_send_cb, remote.fd, EV_WRITE);

    ev_io_start(server.loop, &remote.reader_io);
    if (!remote.iochan.buf.empty()) {
        ev_io_start(server.loop, &remote.writer_io);
    }
}

static Error create_udp_peer(UDPPeer *&peer) {
    int listenfd = -1;
    Error err = udp_listen(listenfd, "", 0, SOMAXCONN);
    if (!err.ok()) {
        return err;
    }

    Addr local_addr;
    err = net_local_addr(listenfd, local_addr);
    if (!err.ok()) {
        return err;
    }

    UDPPeer &p = *new UDPPeer();
    p.fd = listenfd;
    p.addr = local_addr;
    peer = &p;
    return Ok();
}

void ClientConn::cmd_udp(const Addr &client_from) {
    CTXLOG_PUSH_FUNC();
    CTXLOG_INFO("[client_from:%s]", client_from.str().c_str());

    Server &server = *this->server;

    Error err = create_udp_peer(this->udp_client);
    if (!err.ok()) {
        CTXLOG_ERR("%s", err.str().c_str());
        this->reply(REPLY_ERR, Addr());
        return;
    }

    err = create_udp_peer(this->udp_remote);
    if (!err.ok()) {
        CTXLOG_ERR("%s", err.str().c_str());
        this->reply(REPLY_ERR, Addr());
        return;
    }

    // reply
    err = this->reply(REPLY_OK, this->udp_client->addr);
    if (!err.ok()) {
        server.on_client_error(*this, err);
        return;
    }

    CTXLOG_INFO("[udp_client_listen:%s][udp_remote_listen:%s] cmd_udp: success",
        this->udp_client->addr.str().c_str(), this->udp_remote->addr.str().c_str());
    this->state = ClientConn::UDP;

    // clear handshake timeout
    server.client_timeouts.remove(*this);
    // idle list
    server.update_idle_timeout(*this);

    this->udp_client->client = this;
    this->udp_remote->client = this;

    ev_io_init(&this->udp_client->reader_io, udp_client_recv_cb, this->udp_client->fd, EV_READ);
    ev_io_init(&this->udp_remote->reader_io, udp_remote_recv_cb, this->udp_remote->fd, EV_READ);
    ev_io_start(server.loop, &this->udp_client->reader_io);
    ev_io_start(server.loop, &this->udp_remote->reader_io);
}

Error ClientConn::reply(uint8_t code, const Addr &addr) {
    char buf[4 + 16 + 2] = {5, (char)code, 0};
    buf[3] = addr.family() == AF_INET ? ATYPE_IPV4 : ATYPE_IPV6;
    size_t ip_size = addr.ip_size();
    assert(ip_size <= 16);
    ::memcpy(&buf[4], addr.ip_data(), ip_size);
    buf[4 + ip_size + 0] = (char)(addr.port() >> 8);
    buf[4 + ip_size + 1] = (char)(addr.port() & 0xff);
    size_t reply_len = 4 + ip_size + 2;
    return this->iochan.write(buf, reply_len);
}

void Server::update_client_timeout(ClientConn &client) {
    assert(client.state == ClientConn::STREAM);
    if (!client.iochan.buf.empty()) {
        this->client_timeouts.touch(ev_now(this->loop), client);
    } else {
        this->client_timeouts.remove(client);
    }
}

// XXX: identical to update_client_timeout
void Server::update_remote_timeout(RemoteConn &remote) {
    if (!remote.iochan.buf.empty()) {
        this->remote_timeouts.touch(ev_now(this->loop), remote);
    } else {
        this->remote_timeouts.remove(remote);
    }
}

void Server::update_idle_timeout(ClientConn &client) {
    this->idle_timeouts.touch(ev_now(this->loop), client);
}

static void on_client_timeout_cb(ClientConn &client) {
    CTXLOG_SET("client", client.addr_str).set("remote", client.remote ? client.remote->addr_str : "nil");
    CTXLOG_DBG("client timeout. [ts:%f][now:%f]", client.timeout_tracer.last_activity, ev_now(client.server->loop));
    client.server->on_client_error(client, Error(ERR_TIMEOUT, 0, "client io timeout"));
}

static void on_remote_timeout_cb(RemoteConn &remote) {
    CTXLOG_SET("client", remote.client->addr_str).set("remote", remote.addr_str);
    CTXLOG_DBG("remote timeout. [ts:%f][now:%f]", remote.timeout_tracer.last_activity, ev_now(remote.client->server->loop));
    remote.client->server->on_client_error(*remote.client, Error(ERR_TIMEOUT, 0, "remote io timeout"));
}

static void on_idle_timeout_cb(ClientConn &client) {
    CTXLOG_SET("client", client.addr_str).set("remote", client.remote ? client.remote->addr_str : "nil");
    CTXLOG_DBG("kick idle session. [ts:%f][now:%f]", client.idle_timeout_tracer.last_activity, ev_now(client.server->loop));
    client.server->on_client_error(client, Error(ERR_TIMEOUT, 0, "kick idle session"));
}

void Server::on_timer() {
    ev_tstamp now = ev_now(this->loop);
    ev_tstamp next_check = std::min(std::min(
        this->client_timeouts.each_timeouts(now, on_client_timeout_cb),
        this->remote_timeouts.each_timeouts(now, on_remote_timeout_cb)),
        this->idle_timeouts.each_timeouts(now, on_idle_timeout_cb));
    CTXLOG_DBG("[next_check:%g]", next_check);
    ev_timer_set(&this->timer, next_check, 0);
    ev_timer_start(this->loop, &this->timer);
}

static void client_send_cb(EV_P_ ev_io *io, int revents) {
    if (!(revents & EV_WRITE)) {
        return;
    }

    ClientConn &client = *(ClientConn *)((char *)io - offsetof(ClientConn, writer_io));
    CTXLOG_PUSH_FUNC().set("client", client.addr_str);

    Error err = client.iochan.on_write();
    if (!err.ok()) {
        return client.server->on_client_error(client, err);
    }

    client.server->update_client_timeout(client);
}

static void remote_recv_cb(EV_P_ ev_io *io, int revents) {
    if (!(revents & EV_READ)) {
        return;
    }

    RemoteConn &remote = *(RemoteConn *)((char *)io - offsetof(RemoteConn, reader_io));
    ClientConn &client = *remote.client;
    Server &server = *client.server;
    CTXLOG_PUSH_FUNC()
        .set("client", client.addr_str)
        .set("remote", remote.addr_str);

    char buf[k_read_buf_size];
    ssize_t n = ::read(remote.fd, buf, sizeof(buf));
    if (n < 0) {
        if (is_again(errno)) {
            CTXLOG_WARN("unexpected EAGAIN!");
            return;
        }
        return server.on_client_error(client,
            Error(ERR_READ, errno, "remote_recv_cb() read() error"));
    } else if (n == 0) {
        return server.on_remote_eof(client);
    } else {
        Error err = client.iochan.write(buf, (size_t)n);
        if (!err.ok()) {
            return server.on_client_error(client, err);
        }

        // update timeout list
        server.update_client_timeout(client);
        server.update_idle_timeout(client);
    }
}

static void remote_send_cb(EV_P_ ev_io *io, int revents) {
    if (!(revents & EV_WRITE)) {
        return;
    }

    RemoteConn &remote = *(RemoteConn *)((char *)io - offsetof(RemoteConn, writer_io));
    CTXLOG_PUSH_FUNC()
        .set("client", remote.client->addr_str)
        .set("remote", remote.addr_str);

    Error err = remote.iochan.on_write();
    if (!err.ok()) {
        return remote.client->server->on_client_error(*remote.client, err);
    }

    remote.client->server->update_remote_timeout(remote);
}

static Error parse_udp_packet(
    const char *buf, size_t size,
    uint8_t &atype, string &socksaddr, uint16_t &port, const char *&data, size_t &datalen)
{
    if (size < 4 + 2 + 2) {
        return Error(ERR_BAD_PACKET, 0, "udp packet too short");
    }
    if (buf[2] != 0) {
        return Error(ERR_BAD_PACKET, 0, "FRAG field unsupported");
    }

    const char *end = buf + size;
    atype = (uint8_t)buf[3];
    buf += 4;
    if (atype == ATYPE_IPV4) {
        if (buf + 4 + 2 > end) {
            return Error(ERR_BAD_PACKET, 0, "DST.ADDR or DST.PORT too short");
        }
        socksaddr.append(buf, 4);
        buf += 4;
    } else if (atype == ATYPE_IPV6) {
        if (buf + 16 + 2 > end) {
            return Error(ERR_BAD_PACKET, 0, "DST.ADDR or DST.PORT too short");
        }
        socksaddr.append(buf, 16);
        buf += 16;
    } else if (atype == ATYPE_DOMAIN) {
        uint8_t domain_len = (uint8_t)buf[0];
        if (buf + 1 + domain_len + 2 > end) {
            return Error(ERR_BAD_PACKET, 0, "DST.ADDR or DST.PORT too short");
        }
        socksaddr.append(buf + 1, domain_len);
        buf += 1 + domain_len;
    } else {
        return Error(ERR_BAD_ATYPE, 0, "bad atype");
    }

    assert(buf + 2 <= end);
    port = (uint16_t)buf[0] << 8 | (uint16_t)(buf[1]);
    buf += 2;
    data = buf;
    datalen = end - buf;
    return Ok();
}

static void udp_client_recv_cb(EV_P_ ev_io *io, int revents) {
    if (!(revents & EV_READ)) {
        return;
    }

    UDPPeer &udp_client = *(UDPPeer *)((char *)io - offsetof(UDPPeer, reader_io));
    ClientConn &client = *udp_client.client;
    UDPPeer &udp_remote = *client.udp_remote;

    CTXLOG_PUSH_FUNC()
        .set("client", client.addr_str)
        .set("udp_client_listen", udp_client.addr.str())
        .set("udp_remote_listen", udp_remote.addr.str());

    // recvfrom
    char buf[k_udp_read_buf_size];
    size_t datalen = 0;
    Addr addr;
    Error err = net_recvfrom(udp_client.fd, buf, sizeof(buf), datalen, MSG_DONTWAIT, addr);
    if (!err.ok()) {
        if (!is_again(err.code())) {
            CTXLOG_ERR("%s", err.str().c_str());
        }
        return;
    }

    // check source ip
    if (!Addr::ip_eq(client.addr, addr)) {
        CTXLOG_WARN("[tcp_from_ip:%s] != [udp_from_ip:%s] drop packet",
            client.addr.ip().c_str(), addr.ip().c_str());
        return;
    }

    // update source ip
    if (client.udp_client_from.is_unspecified()) {
        CTXLOG_INFO("[udp_client_from:%s] got client from addr", addr.str().c_str());
        client.udp_client_from = addr;
    } else if (client.udp_client_from != addr) {
        CTXLOG_WARN("[udp_client_from_origin:%s][udp_client_from_new:%s] updating client from addr",
            client.udp_client_from.str().c_str(), addr.str().c_str());
        client.udp_client_from = addr;
    }

    // parse packet
    uint8_t atype = 0;
    string socksaddr;
    uint16_t port = 0;
    const char *payload = NULL;
    size_t payload_len = 0;
    err = parse_udp_packet(buf, datalen, atype, socksaddr, port, payload, payload_len);
    if (!err.ok()) {
        CTXLOG_WARN("%s", err.str().c_str());
        return;
    }

    Addr to_addr;
    if (atype == ATYPE_IPV4) {
        to_addr = Addr::from_ipv4(socksaddr.data(), port);
    } else if (atype == ATYPE_IPV6) {
        to_addr = Addr::from_ipv6(socksaddr.data(), port);
    } else {
        // TODO: domain
        return;
    }

    // send payload
    size_t sent = 0;
    err = net_sendto(udp_remote.fd, payload, payload_len, sent, MSG_DONTWAIT, to_addr);
    if (!err.ok()) {
        if (is_again(err.code())) {
            CTXLOG_WARN("send to remote got EAGAIN, drop packet");
        } else {
            CTXLOG_ERR("send to remote error: %s", err.str().c_str());
        }
        return;
    }
    if (payload_len != sent) {
        CTXLOG_ERR("[payload_size:%zu] != [truncated:%zu]", payload_len, sent);
    }

    // update timeout
    client.server->update_idle_timeout(client);
}

static void pack_udp_packet(vector<char> &buf, const Addr &addr, const char *payload, size_t size) {
    size_t ip_size = addr.ip_size();
    buf.resize(4 + ip_size + 2 + size);
    buf[3] = addr.family() == AF_INET ? ATYPE_IPV4 : ATYPE_IPV6;
    char *pbuf = &buf[4];
    ::memcpy(pbuf, addr.ip_data(), ip_size);
    pbuf += ip_size;
    *pbuf++ = (char)(addr.port() >> 8);
    *pbuf++ = (char)(addr.port() & 0xff);
    assert(pbuf + size <= buf.data() + buf.size());
    ::memcpy(pbuf, payload, size);
}

static void udp_remote_recv_cb(EV_P_ ev_io *io, int revents) {
    if (!(revents & EV_READ)) {
        return;
    }

    UDPPeer &udp_remote = *(UDPPeer *)((char *)io - offsetof(UDPPeer, reader_io));
    ClientConn &client = *udp_remote.client;
    UDPPeer &udp_client = *client.udp_client;

    CTXLOG_PUSH_FUNC()
        .set("client", client.addr_str)
        .set("udp_client_listen", udp_client.addr.str())
        .set("udp_remote_listen", udp_remote.addr.str());

    // recvfrom
    char buf[k_udp_read_buf_size];
    size_t datalen = 0;
    Addr addr;
    Error err = net_recvfrom(udp_remote.fd, buf, sizeof(buf), datalen, MSG_DONTWAIT, addr);
    if (!err.ok()) {
        if (!is_again(err.code())) {
            CTXLOG_ERR("%s", err.str().c_str());
        }
        return;
    }

    if (client.udp_client_from.is_unspecified()) {
        CTXLOG_WARN("received remote udp from %s while udp_client_from is unspecified",
            addr.str().c_str());
        return;
    }

    CTXLOG_DBG("[udp_remote_from:%s][size:%zu]", addr.str().c_str(), datalen);

    vector<char> packet;
    pack_udp_packet(packet, addr, buf, datalen);
    // send packet
    size_t sent = 0;
    err = net_sendto(udp_remote.fd, packet.data(), packet.size(), sent, MSG_DONTWAIT, client.udp_client_from);
    if (!err.ok()) {
        if (is_again(err.code())) {
            CTXLOG_WARN("send to client got EAGAIN, drop packet");
        } else {
            CTXLOG_ERR("send to client error: %s", err.str().c_str());
        }
        return;
    }
    if (packet.size() != sent) {
        CTXLOG_ERR("[packet_size:%zu] != [truncated:%zu]", packet.size(), sent);
    }

    // update timeout
    client.server->update_idle_timeout(client);
}

void Server::on_connection(int fd, const Addr &addr) {
    CTXLOG_PUSH_FUNC().set("client", addr.str());
    CTXLOG_INFO("got client [fd:%d]", fd);

    ClientConn &client = *new ClientConn();
    this->client_timeouts.touch(ev_now(this->loop), client);

    client.fd = fd;
    client.addr = addr;
    client.addr_str = client.addr.str();
    client.server = this;
    client.state = ClientConn::INIT;

    client.iochan.init(this->loop, k_write_buf_max_size);
    client.iochan.consumer = &client.writer_io;
    client.iochan.producer = &client.reader_io;

    ev_io_init(&client.reader_io, client_recv_cb, fd, EV_READ);
    ev_io_init(&client.writer_io, client_send_cb, fd, EV_WRITE);
    ev_io_start(this->loop, &client.reader_io);
}

void Server::on_client_eof(ClientConn &client) {
    CTXLOG_INFO("client eof");

    // check for remote eof
    if (client.remote == NULL || client.iochan.is_producer_done()) {
        return this->on_client_done(client);
    }

    if (client.remote != NULL) {
        Error err = client.remote->iochan.producer_done();
        if (!err.ok()) {
            // impossible
            CTXLOG_ERR("%s", err.str().c_str());
        }
    }
    ev_io_stop(this->loop, &client.reader_io);
}

void Server::on_remote_eof(ClientConn &client) {
    assert(client.remote != NULL);
    CTXLOG_INFO("remote eof");

    if (client.remote->iochan.is_producer_done()) {
        return this->on_client_done(client);
    }

    Error err = client.iochan.producer_done();
    if (!err.ok()) {
        // impossible
        CTXLOG_ERR("%s", err.str().c_str());
    }
    ev_io_stop(this->loop, &client.remote->reader_io);
}

void Server::on_udp_peer_done(UDPPeer &peer) {
    ev_io_stop(this->loop, &peer.reader_io);
    close_fd(peer.fd);
    delete &peer;
}

void Server::on_client_done(ClientConn &client) {
    CTXLOG_INFO("client done");

    ev_io_stop(this->loop, &client.reader_io);
    ev_io_stop(this->loop, &client.writer_io);
    close_fd(client.fd);

    if (client.state == ClientConn::AUTH) {
        client.server->handler->auth_end(client);
    }

    // connect cmd
    if (client.remote != NULL) {
        this->on_remote_done(*client.remote);
    }
    // udp cmd
    if (client.udp_client != NULL) {
        this->on_udp_peer_done(*client.udp_client);
    }
    if (client.udp_remote != NULL) {
        this->on_udp_peer_done(*client.udp_remote);
    }

    this->client_timeouts.remove(client);
    this->idle_timeouts.remove(client);
    delete &client;
}

void Server::on_remote_done(RemoteConn &remote) {
    ev_io_stop(this->loop, &remote.reader_io);
    ev_io_stop(this->loop, &remote.writer_io);
    close_fd(remote.fd);
    this->remote_timeouts.remove(remote);
    delete &remote;
}

void Server::on_client_error(ClientConn &client, Error err) {
    CTXLOG_ERR("client error: %s", err.str().c_str());
    this->on_client_done(client);
}
