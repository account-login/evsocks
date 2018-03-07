#pragma once

#include <time.h>
#include <stdio.h>
#include <sys/time.h>
#include <string>
#include <vector>
#include <iostream>

#include <boost/algorithm/string/replace.hpp>

#include "../string_util.hpp"


namespace tz { namespace ctxlog {

    enum CtxlogLevel {
        LVL_DEBUG = 1,
        LVL_INFO,
        LVL_NOTICE,
        LVL_WARN,
        LVL_ERROR,
        LVL_FATAL,
    };

}}

namespace evsocks { namespace log {

    using std::string;
    using std::vector;
    using std::cerr;
    using std::endl;

    using tz::strfmt;
    using tz::vstrfmt;
    using tz::ctxlog::CtxlogLevel;


    struct Duration {
        int64_t sec;
        int64_t usec;

        Duration(int64_t sec, int64_t usec) : sec(sec), usec(usec) {}

        string str(const string &fmt = "%S.%f") const {
            // hacks
            string ans = boost::replace_all_copy(fmt, "%S", strfmt("%ld", this->sec));
            ans = boost::replace_all_copy(ans, "%f", strfmt("%03u", this->usec / 1000));
            return ans;
        }

        double to_seconds() const {
            return double(this->sec) + double(this->usec) / 1000000;
        }

        static Duration US(int64_t usec) {
            return Duration(usec / 1000000, usec % 1000000);
        }
    };


    struct Time {
        uint64_t seconds;
        uint64_t micro_seconds;

        Time() : seconds(0), micro_seconds(0) {}
        explicit Time(uint64_t seconds, uint64_t micro_seconds = 0)
            : seconds(seconds), micro_seconds(micro_seconds)
        {}

        static Time Now() {
            struct timeval tv;
            ::gettimeofday(&tv, NULL);
            return Time(tv.tv_sec, tv.tv_usec);
        }

        string str(const string &fmt = "%Y-%m-%d %H:%M:%S.%f") const {
            struct tm stm;
            time_t ts = this->seconds;
            localtime_r(&ts, &stm);

            // hack
            string new_fmt = boost::replace_all_copy(fmt, "%f", strfmt("%03u", this->micro_seconds / 1000));

            vector<char> buf(100, '\0');
            size_t count = 0;
            do {
                count = ::strftime(buf.data(), buf.size(), new_fmt.c_str(), &stm);
                if (count == 0) {
                    buf.resize(buf.size() * 2);
                }
            } while (count == 0);

            return string(buf.data());
        }

        Duration operator-(const Time &rhs) const {
            int64_t us = (this->seconds - rhs.seconds) * 1000000 + this->micro_seconds - rhs.micro_seconds;
            return Duration::US(us);
        }
    };


    inline const char *prefix_from_level(int level) {
        switch (level) {
        case tz::ctxlog::LVL_DEBUG:     return "DEBUG ";
        case tz::ctxlog::LVL_INFO:      return "INFO  ";
        case tz::ctxlog::LVL_NOTICE:    return "NOTICE";
        case tz::ctxlog::LVL_WARN:      return "WARN  ";
        case tz::ctxlog::LVL_ERROR:     return "ERROR ";
        case tz::ctxlog::LVL_FATAL:     return "FATAL ";
        default:                        return "!BAD! ";
        }
    }


    inline void log(int level, const char *fmt, ...) {
        std::string buf = Time::Now().str();
        buf.reserve(buf.size() + 1 + 6 + 1);
        buf.push_back(' ');
        buf.append(prefix_from_level(level));
        buf.push_back(' ');

        va_list ap;
        va_start(ap, fmt);
        buf.append(vstrfmt(fmt, ap));
        va_end(ap);
        buf.push_back('\n');
        fwrite(buf.data(), 1, buf.size(), stdout);
    }
}}


#define CTXLOG_LOG_IMPL(level, fmt, ...) \
    evsocks::log::log(level, fmt, ##__VA_ARGS__)


#include "framework.hpp"
