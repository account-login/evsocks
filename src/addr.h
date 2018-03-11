#pragma once

#include <sys/socket.h>
#include <stdint.h>
#include <string>


namespace evsocks {

    using std::string;

    struct Addr {
        struct sockaddr_storage data;

        Addr();

        uint16_t port() const;
        Addr &port(uint16_t port_num);
        string ip() const;
        string str() const;
        int family() const;
        const struct sockaddr *sockaddr() const;
        struct sockaddr *sockaddr();
        socklen_t socklen() const;
        const char *ip_data() const;
        size_t ip_size() const;

        bool operator==(const Addr &rhs) const;
        bool operator!=(const Addr &rhs) const {
            return !(*this == rhs);
        }

        bool is_unspecified() const;

        // big endian data
        static Addr from_ipv4(const char *data, uint16_t port);
        static Addr from_ipv6(const char *data, uint16_t port);

        static bool ip_eq(const Addr &lhs, const Addr &rhs);

        static socklen_t max_size() { return sizeof(struct sockaddr_storage); }
    };

}
