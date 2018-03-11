#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>       // For vsnprintf
#include <cstdarg>      // For va_start, etc.

#include "stb_sprintf.h"

#include "conv_util.hpp"


namespace tz {

    // from https://stackoverflow.com/a/8098080
    inline std::string vstrfmt_old(const std::string &fmt_str, va_list ap) {
        int final_n;
        size_t n = fmt_str.size() * 2; /* Reserve two times as much as the length of the fmt_str */
        if (n < 16) {
            n = 16;
        }

        std::vector<char> buffer;
        while (true) {
            // init buffer
            buffer.resize(n, '\0');

            // copy va_list for reuse
            va_list ap_copy;
            va_copy(ap_copy, ap);
            final_n = vsnprintf(buffer.data(), n, fmt_str.c_str(), ap_copy);
            va_end(ap_copy);

            if (final_n >= (int)n) {
                n += abs(final_n - (int)n + 1);
            } else {
                // XXX: negative return value not handled
                break;
            }
        }
        return std::string(buffer.data(), (size_t)final_n);
    }

    inline char *__vstrfmt_cb(char *buf, void *user, int len) {
        std::string &output = *(std::string *)user;
        output.append(buf, (size_t)len);
        return buf;
    }

    inline std::string vstrfmt(const char *fmt, va_list ap) {
        std::string ans;

        char buf[STB_SPRINTF_MIN];
        int rv = stbsp_vsprintfcb(__vstrfmt_cb, &ans, buf, fmt, ap);
        assert(rv >= 0);
        return ans;
    }

    inline std::string strfmt(const char *fmt_str, ...)
    {
        va_list ap;
        va_start(ap, fmt_str);
        std::string ret = vstrfmt(fmt_str, ap);
        va_end(ap);
        return ret;
    }

    template <class T>
    inline std::string str(const T &value) {
        std::stringstream ss;
        ss << value;
        return ss.str();
    }

    // NOTE: uint8_t and int8_t is considered char type by stringstream
    template <>
    inline std::string str(const uint8_t &value) {
        return str(uint32_t(value));
    }

    template <>
    inline std::string str(const int8_t &value) {
        return str(int32_t(value));
    }

}   // namespace tz
