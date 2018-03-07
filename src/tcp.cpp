#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>

#include "ctxlog/ctxlog_evsocks.hpp"
#include "tcp.h"


namespace evsocks {

    Error net_set_nonblock(int fd) {
        int val = ::fcntl(fd, F_GETFL, 0);
        if (val < 0) {
            return Error(ERR_FCNTL, errno, "fcntl(fd, F_GETFL, 0) error");
        }
        val = ::fcntl(fd, F_SETFL, val | O_NONBLOCK);
        if (val < 0) {
            return Error(ERR_FCNTL, errno, "fcntl(fd, F_SETFL, val | O_NONBLOCK) error");
        }
        return Ok();
    }

    static Error close_fd(int fd) {
        CTXLOG_PUSH_FUNC();
        Error err;
        if (::close(fd) != 0) {
            err = Error(ERR_CLOSE, errno, strfmt("close() failed for [fd:%d]", fd));
            CTXLOG_ERR("%s", err.str().c_str());
        }
        return err;
    }

    static Error _net_listen(
        int &outfd, const string &host, uint16_t port,
        int backlog, int socktype, bool reuseport)
    {
        Error err;
        int fd = -1;

        // first, load up address structs with getaddrinfo():
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;        // use IPv4 or IPv6, whichever
        hints.ai_socktype = socktype;
        hints.ai_flags = AI_PASSIVE;        // fill in my IP for me

        // FIXME: blocking getaddrinfo call
        struct addrinfo *res = NULL;
        int32_t rv = ::getaddrinfo(
            host.empty() ? NULL : host.c_str(), tz::str(port).c_str(),
            &hints, &res);
        if (rv != 0) {
            err = Error(ERR_GET_ADDR_INFO, rv, ::gai_strerror(rv));
            goto L_RETURN;
        }

        // bind
        for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
            // XXX: SOCK_NONBLOCK is linux only
            fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
            if (fd == -1) {
                err = Error(ERR_SOCKET, errno, "socket() error");
                continue;
            }

            // set SO_REUSEADDR
            int yes = 1;
            if (socktype != SOCK_DGRAM || reuseport) {
                rv = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&yes, sizeof(yes));
            }
            if (reuseport) {
#ifdef SO_REUSEPORT
                rv = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof(yes));
#endif
            }

            rv = ::bind(fd, ai->ai_addr, ai->ai_addrlen);
            if (rv == -1) {
                err = Error(ERR_BIND, errno, "bind() error");
                close_fd(fd);
                // continue for next addrinfo
            } else {
                err = Ok();
                break;  // success
            }
        }   // for each addrinfo

        if (err.ok() && fd == -1) {
            err = Error(ERR_NO_ADDR, 0, "no addr to bind");
        }
        if (!err.ok()) {
            goto L_RETURN;
        }

        // listen
        if (socktype == SOCK_STREAM || socktype == SOCK_SEQPACKET) {
            rv = ::listen(fd, backlog);
            if (rv == -1) {
                err = Error(ERR_LISTEN, errno, "listen() error");
                goto L_RETURN;
            }
        }

        // success
        outfd = fd;

    L_RETURN:
        if (!err.ok() && fd != -1) {
            close_fd(fd);
        }
        ::freeaddrinfo(res);
        return err;
    }

    Error tcp_listen(int &outfd, const string &host, uint16_t port, int backlog) {
        return _net_listen(outfd, host, port, backlog, SOCK_STREAM, true);
    }

    Error udp_listen(int &outfd, const string &host, uint16_t port, int backlog) {
        return _net_listen(outfd, host, port, backlog, SOCK_DGRAM, false);
    }

    Error net_accept(int &outfd, int fd, Addr &addr) {
        socklen_t addr_size = Addr::max_size();
        outfd = ::accept4(fd, addr.sockaddr(), &addr_size, SOCK_NONBLOCK);
        if (outfd == -1) {
            return Error(ERR_ACCEPT, errno, "accept() error");
        }
        return Error();
    }

    Error tcp_connect(int &outfd, const Addr &addr) {
        int fd = ::socket(addr.family(), SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd == -1) {
            return Error(ERR_SOCKET, errno, "socket() error");
        }

        // connect
        if (-1 == ::connect(fd, addr.sockaddr(), addr.socklen())
            && errno != EINPROGRESS)
        {
            Error err(ERR_CONNECT, errno, "connect() error");
            close_fd(fd);
            return err;
        }

        // success
        outfd = fd;
        return Ok();
    }

    Error net_local_addr(int fd, Addr &addr) {
        socklen_t socklen = Addr::max_size();
        int rv = ::getsockname(fd, addr.sockaddr(), &socklen);
        if (rv != 0) {
            return Error(ERR_GET_SOCK_NAME, errno, "getsockname() error");
        }
        return Ok();
    }

    Error tcp_shutdown(int fd, int how) {
        int rv = ::shutdown(fd, how);
        if (rv != 0) {
            return Error(ERR_SHUTDOWN, errno, "shutdown() error");
        }
        return Ok();
    }

    Error net_recvfrom(int fd, char *buf, size_t len, size_t &datalen, int flags, Addr &addr) {
        assert(len > 0);
        socklen_t socklen = addr.max_size();
        ssize_t rv = ::recvfrom(fd, buf, len, flags, addr.sockaddr(), &socklen);
        if (rv < 0) {
            return Error(ERR_RECVFROM, errno, "recvfrom() error");
        }

        datalen = (size_t)rv;
        return Ok();
    }

    Error net_sendto(int fd, const char *buf, size_t len, size_t &sent, int flags, const Addr &addr) {
        assert(len > 0);
        ssize_t rv = ::sendto(fd, buf, len, flags, addr.sockaddr(), addr.socklen());
        if (rv < 0) {
            return Error(ERR_SENDTO, errno, "sendto() error");
        }

        sent = (size_t)rv;
        return Ok();
    }

}   // ::evsocks
