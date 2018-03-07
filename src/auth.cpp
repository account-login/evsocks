#include "auth.h"
#include "socksdef.h"


using namespace evsocks;


uint8_t DefaultServerHandler::auth_begin(std::set<uint8_t> &methods) {
    return METHOD_NONE;
}

Error DefaultServerHandler::auth_perform(ClientConn &client, uint32_t &state) {
    state = IServerHandler::AUTH_STATE_DONE;
    return Ok();
}

void DefaultServerHandler::auth_end(ClientConn &client) {}
