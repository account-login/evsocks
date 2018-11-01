#pragma once

#include <cstddef>

#include <ev.h>     // for ev_tstamp

#include "dlist.hpp"


namespace evsocks {

    struct TimeoutTracer {
        tz::DListNode node;
        ev_tstamp last_activity;

        TimeoutTracer() : last_activity(0) {}
    };

    template <class T, size_t offset>
    struct TimeoutList {
        // param
        ev_tstamp timeout;
        // readonly
        size_t size;

        typedef tz::DList<T, offset + offsetof(TimeoutTracer, node)> ListType;
        ListType list;

        explicit TimeoutList(ev_tstamp timeout) : timeout(timeout), size(0) {}

        void touch(ev_tstamp now, T &obj) {
            get_tracer(obj).last_activity = now;
            if (this->list.is_linked(obj)) {
                this->list.erase(obj);
            } else {
                this->size++;
            }
            this->list.push_back(obj);
        }

        void remove(T &obj) {
            if (this->list.is_linked(obj)) {
                this->list.erase(obj);
                this->size--;
            }
        }

        // caller should remove timeouts
        template <class CB>
        ev_tstamp each_timeouts(ev_tstamp now, CB cb) {
            typename ListType::iterator it = this->list.begin();
            while (it != this->list.end()) {
                T &obj = *it;
                if (get_tracer(obj).last_activity + this->timeout <= now) {
                    ++it;
                    cb(obj);
                } else {
                    break;
                }
            }

            if (it != this->list.end()) {
                return get_tracer(*it).last_activity + this->timeout - now;
            } else {
                return this->timeout;
            }
        }

        static TimeoutTracer &get_tracer(T &obj) {
            return *(TimeoutTracer *)((char *)&obj + offset);
        }
    };

#define EVSOCKS_TIMEOUT_LIST(T, member) evsocks::TimeoutList<T, offsetof(T, member)>

}
