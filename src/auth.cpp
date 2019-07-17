#include "auth.h"
#include "socksdef.h"
#include "server.h"


using namespace evsocks;


uint8_t DefaultServerHandler::auth_begin(const std::set<uint8_t> &methods) {
    (void)methods;
    return METHOD_NONE;
}

Error DefaultServerHandler::auth_perform(ClientConn &client, uint32_t &state) {
    (void)client;
    state = IServerHandler::AUTH_STATE_DONE;
    return Ok();
}

void DefaultServerHandler::auth_end(ClientConn &client) {
    (void)client;
}

uint8_t PasswordServerHandler::auth_begin(const std::set<uint8_t> &methods) {
    if (methods.count(METHOD_USERNAME)) {
        return METHOD_USERNAME;
    } else {
        return METHOD_NONE;
    }
}

Error PasswordServerHandler::auth_perform(ClientConn &client, uint32_t &state) {
    if (client.input.size() < 5) {
        state = IServerHandler::AUTH_STATE_CONT;
        return Ok();
    }

    // ver
    if (client.input[0] != 0x01) {
        return Error(ERR_BAD_USERNAME_AUTH_VERSION, 0,
            "PasswordServerHandler::auth_perform() error");
    }
    // username
    uint8_t ulen = client.input[1];
    size_t p_idx = 1 + 1 + ulen + 1;
    if (client.input.size() < p_idx + 1) {
        state = IServerHandler::AUTH_STATE_CONT;
        return Ok();
    }
    // password
    uint8_t plen = client.input[1 + 1 + ulen];
    if (client.input.size() < p_idx + plen) {
        state = IServerHandler::AUTH_STATE_CONT;
        return Ok();
    }
    // check
    std::string user(&client.input[1 + 1], &client.input[1 + 1 + ulen]);
    std::string pass(&client.input[p_idx], &client.input[p_idx] + plen);
    char response[2] = {0x01, 0x00};
    if (this->user2pass.count(user) && this->user2pass.at(user) == pass) {
        state = IServerHandler::AUTH_STATE_DONE;
    } else {
        state = IServerHandler::AUTH_STATE_FAIL;
        response[1] = 0x01;
    }
    // pop input
    client.input.pop(p_idx + plen);
    // reply
    return client.iochan.write(response, 2);
}

void PasswordServerHandler::auth_end(ClientConn &client) {
    (void)client;
}
