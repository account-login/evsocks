#ifndef EVSOCKS_IOCHANNEL_H
#define EVSOCKS_IOCHANNEL_H

#include <ev.h>

#include "bufqueue.h"
#include "error.h"


namespace evsocks {

    struct IOChannel {
        struct ev_loop *loop;
        ev_io *producer;    // reader
        ev_io *consumer;    // writer

        bool producer_eof;

        size_t max_buf;
        BufQueue buf;

        IOChannel()
            : loop(NULL), producer(NULL), consumer(NULL), producer_eof(false), max_buf(0)
        {}

        void init(EV_P_ size_t max_buf) {
            this->loop = EV_A;
            this->max_buf = max_buf;
        }

        Error write(const char *data, size_t count);
        Error on_write();
        Error flush();
        Error producer_done();
        bool is_producer_done() const { return this->producer_eof; }
    };

}

#endif //EVSOCKS_IOCHANNEL_H
