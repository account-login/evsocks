#ifndef EVSOCKS_TCP_H
#define EVSOCKS_TCP_H

#include "addr.h"
#include "error.h"


namespace evsocks {
    Error net_set_nonblock(int fd);
    Error tcp_listen(int &outfd, const string &host, uint16_t port, int backlog);
    Error udp_listen(int &outfd, const string &host, uint16_t port, int backlog);
    Error net_accept(int &outfd, int fd, Addr &addr);
    Error tcp_connect(int &outfd, const Addr &addr);
    Error tcp_shutdown(int fd, int how);
    Error net_recvfrom(int fd, char *buf, size_t len, size_t &datalen, int flags, Addr &addr);
    Error net_sendto(int fd, const char *buf, size_t len, size_t &sent, int flags, const Addr &addr);
    Error net_local_addr(int fd, Addr &local_addr);
}


#endif //EVSOCKS_TCP_H
