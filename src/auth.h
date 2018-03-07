#ifndef EVSOCKS_AUTH_H
#define EVSOCKS_AUTH_H

#include <stdint.h>
#include <set>

#include "error.h"


namespace evsocks {

    class ClientConn;

    struct IServerHandler {
        enum AuthState {
            AUTH_STATE_NONE = 0,
            AUTH_STATE_DONE = 1,
            AUTH_STATE_FAIL = 2,
            AUTH_STATE_CONT = 3,
        };

        // choose auth method.
        // If METHOD_REJECT is chosen, auth_perform or auth_end will not be called
        virtual uint8_t auth_begin(std::set<uint8_t> &methods) = 0;
        // perform authentication
        virtual Error auth_perform(ClientConn &client, uint32_t &state) = 0;
        // clean up ClientConn.auth_ctx
        virtual void auth_end(ClientConn &client) = 0;

        virtual ~IServerHandler() {}
    };

    struct DefaultServerHandler : IServerHandler {
        virtual uint8_t auth_begin(std::set<uint8_t> &methods);
        virtual Error auth_perform(ClientConn &client, uint32_t &state);
        virtual void auth_end(ClientConn &client);
    };

}

#endif //EVSOCKS_AUTH_H
