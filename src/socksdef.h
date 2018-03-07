#ifndef EVSOCKS_SOCKSDEF_H
#define EVSOCKS_SOCKSDEF_H

#include <stdint.h>
#include <string>


namespace evsocks {

    enum AuthMethod {
        METHOD_NONE = 0,
        METHOD_GSSAPI = 1,
        METHOD_USERNAME = 2,
        METHOD_PRIVATE_BEGIN = 0x80,
        METHOD_REJECT = 0xff,
    };

    enum AType {
        ATYPE_IPV4 = 1,
        ATYPE_DOMAIN = 3,
        ATYPE_IPV6 = 4,
    };

    enum SocksCmd {
        CMD_CONNECT = 1,
        CMD_BIND = 2,
        CMD_UDP = 3,
    };

    enum SocksReply {
        REPLY_OK = 0,
        REPLY_ERR = 1,
    };

    struct SocksAddr {
        uint8_t atype;
        std::string data;
    };

    struct SocksMsg {
        uint8_t code;
        uint16_t port;
        SocksAddr socksaddr;
    };

}   // ::evsocks

#endif //EVSOCKS_SOCKSDEF_H
