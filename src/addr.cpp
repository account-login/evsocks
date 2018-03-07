#include "addr.h"

#include <arpa/inet.h>

#include "string_util.hpp"


using namespace evsocks;


Addr::Addr() {
    sockaddr_in &sockaddr = (sockaddr_in &)this->data;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = 0;
    sockaddr.sin_port = htons(0);
}

uint16_t Addr::port() const {
    return ntohs(
        this->family() == AF_INET
        ? ((sockaddr_in &)this->data).sin_port
        : ((sockaddr_in6 &)this->data).sin6_port);
}

Addr &Addr::port(uint16_t port_num) {
    if (this->family() == AF_INET) {
        ((sockaddr_in &)this->data).sin_port = htons(port_num);
    } else {
        ((sockaddr_in6 &)this->data).sin6_port = htons(port_num);
    }
    return *this;
}

static string addr2ipstr(int family, const char *data) {
    if (family == AF_INET) {
        return tz::strfmt("%u.%u.%u.%u",
            (uint8_t)data[0], (uint8_t)data[1], (uint8_t)data[2], (uint8_t)data[2]);
    } else {
        char buf[INET6_ADDRSTRLEN];
        if (const char *ip = inet_ntop(family, data, buf, sizeof(buf))) {
            return ip;
        } else {
            return "";
        }
    }
}

string Addr::ip() const {
    // NOTE: inet_ntop is slow
    return addr2ipstr(this->data.ss_family, this->ip_data());

//    char buf[INET6_ADDRSTRLEN] = {};
//    void *addr;
//    if (this->data.ss_family == AF_INET) {
//        addr = &((sockaddr_in &)this->data).sin_addr;
//    } else {
//        addr = &((sockaddr_in6 &)this->data).sin6_addr;
//    }
//
//    if (const char *ip = inet_ntop(this->data.ss_family, addr, buf, sizeof(buf))) {
//        return ip;
//    } else {
//        return "";  // error
//    }
}


string Addr::str() const {
    // TODO: ipv6
    return tz::strfmt("%s:%u", this->ip().c_str(), this->port());
}

int Addr::family() const {
    return this->data.ss_family;
}

const struct sockaddr *Addr::sockaddr() const {
    return (const struct sockaddr *)(&this->data);
}

struct sockaddr *Addr::sockaddr() {
    return (struct sockaddr *)(&this->data);
}

socklen_t Addr::socklen() const {
    return this->family() == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

const char* Addr::ip_data() const {
    if (this->family() == AF_INET) {
        sockaddr_in &sockaddr = (sockaddr_in &)this->data;
        return (const char *)&sockaddr.sin_addr.s_addr;
    } else {
        sockaddr_in6 &sockaddr = (sockaddr_in6 &)this->data;
        return (const char *)&sockaddr.sin6_addr.s6_addr;
    }
}

size_t Addr::ip_size() const {
    return this->family() == AF_INET ? 4 : 16;
}

static const char g_zero16[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

bool Addr::operator==(const Addr &rhs) const {
    return ip_eq(*this, rhs) && this->port() == rhs.port();
}

bool Addr::is_unspecified() const {
    return ::memcmp(this->ip_data(), g_zero16, this->ip_size()) == 0;
}

Addr Addr::from_ipv4(const char *data, uint16_t port) {
    Addr addr;
    sockaddr_in &sockaddr = (sockaddr_in &)addr.data;
    sockaddr.sin_family = AF_INET;
    ::memcpy(&sockaddr.sin_addr.s_addr, data, 4);
    sockaddr.sin_port = htons(port);
    return addr;
}

Addr Addr::from_ipv6(const char *data, uint16_t port) {
    Addr addr;
    sockaddr_in6 &sockaddr = (sockaddr_in6 &)addr.data;
    sockaddr.sin6_family = AF_INET6;
    ::memcpy(&sockaddr.sin6_addr.s6_addr, data, 16);
    sockaddr.sin6_port = htons(port);
    return addr;
}

bool Addr::ip_eq(const Addr &lhs, const Addr &rhs) {
    return lhs.family() == rhs.family()
        && ::memcmp(lhs.ip_data(), rhs.ip_data(), lhs.ip_size()) == 0;
}
