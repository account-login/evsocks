#include <cassert>
#include <sys/socket.h>
#include <unistd.h>

#include "iochannel.h"
#include "tcp.h"
#include "ctxlog/ctxlog_evsocks.hpp"


using namespace evsocks;


static bool is_again(int32_t err) {
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}


Error IOChannel::write(const char *data, size_t count) {
    assert(this->consumer != NULL);
    assert(!this->producer_eof);

    size_t written = 0;
    if (this->buf.empty()) {
        // bypass write buffer
        ssize_t n = ::write(this->consumer->fd, data, count);
        if (n < 0) {
            if (!is_again(errno)) {
                return Error(ERR_WRITE, errno, "IOChannel::write() error");
            }
            written = 0;
        } else if (n == 0 || (size_t)n > count) {
            // not possible
            return Error(ERR_WRITE, errno, "IOChannel::write() bad return value of write()");
        } else {
            written = (size_t)n;
        }
    }

    if (count - written > 0) {
        this->buf.push(data + written, count - written);
    }

    if (!this->buf.empty()) {
        ev_io_start(this->loop, this->consumer);
    }
    if (this->buf.size() >= this->max_buf && this->producer != NULL && !this->producer_eof) {
        CTXLOG_DBG("buffer full, pause producer");
        ev_io_stop(this->loop, this->producer);
    }
    return Ok();
}

Error IOChannel::on_write() {
    Error err = this->flush();
    if (!err.ok()) {
        return err;
    }

    if (this->buf.empty()) {
        ev_io_stop(this->loop, this->consumer);
        if (this->producer_eof) {
            err = tcp_shutdown(this->consumer->fd, SHUT_WR);
            if (!err.ok()) {
                return err;
            }
        }
    }

    // resume possibly paused producer if buffer is not full
    if (this->producer != NULL && !this->producer_eof && this->buf.size() < this->max_buf) {
        ev_io_start(this->loop, this->producer);
    }
    return Ok();
}

Error IOChannel::flush() {
    assert(this->consumer != NULL);

    while (!this->buf.empty()) {
        const char *data;
        size_t count;
        this->buf.peek(data, count);
        ssize_t n = ::write(this->consumer->fd, data, count);
        if (n < 0) {
            if (!is_again(errno)) {
                return Error(ERR_WRITE, errno, "IOChannel::flush() error");
            }
            break;
        } else if (n == 0) {
            // not possible
            return Error(ERR_WRITE, errno, "IOChannel::flush() zero write error");
        } else {
            this->buf.pop((size_t)n);
        }
    }
    this->buf.shrink();
    return Ok();
}

Error IOChannel::producer_done() {
    assert(!this->producer_eof);
    this->producer_eof = true;
    if (this->buf.empty()) {
        return tcp_shutdown(this->consumer->fd, SHUT_WR);
    }
    return Ok();
}
