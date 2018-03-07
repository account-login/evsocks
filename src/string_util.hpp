#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>       // For vsnprintf
#include <cstdarg>      // For va_start, etc.

#include "stb_sprintf.h"

#include "misc.hpp"


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

#ifdef JSON_FORWARDS_H_INCLUDED
#include <json/writer.h>
    template <>
    inline std::string str(const Json::Value &value) {
        return Json::FastWriter().write(value);
    }
#endif

    struct KVBuffer {
        string buffer;

        template <class T>
        KVBuffer &set(const string &key, const T &value) {
            string val;
            if (!try_cast(value, val)) {
                val = "!BAD!";
            }
            this->buffer.append(strfmt("[%s:%s]", key.c_str(), val.c_str()));
            return *this;
        }

        const string &get() const {
            return buffer;
        }
    };

    template <class C>
    inline string repr_set(const C &c, size_t limit = 5)
    {
        string ans = "{";
        const char *comma = "";
        size_t count = 0;
        for (typename C::const_iterator it = c.begin(); it != c.end(); ++it) {
            if (count++ < limit) {
                ans += comma + str(*it);
                comma = ", ";
            } else {
                ans += comma + string("...");
                break;
            }
        }
        ans += "}";
        return ans;
    }

    template <class M>
    inline string repr_map(const M &mapping) {
        string ans;
        for (typename M::const_iterator it = mapping.begin(); it != mapping.end(); ++it) {
            ans += "[" + str(it->first) + ":" + str(it->second) + "]";
        }
        return ans;
    }

    // https://en.wikipedia.org/wiki/UTF-8
    inline bool utf8_validate(const string &input, size_t &count) {
        count = 0;
        for (size_t i = 0; i < input.size(); )
        {
            // check leading byte
            size_t len = 0;
            uint8_t c = input[i];
            if (c < 128) {
                len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                len = 4;
            } else {    // 5, 6 byte utf8 not supported
                return false;
            }

            // truncated data
            if (i + len > input.size()) {
                return false;
            }

            // check follwing byte
            for (size_t j = i + 1; j < i + len; ++j) {
                if ((uint8_t(input[j]) >> 6) != 2) {   // 0b10 prefix
                    return false;
                }
            }

            i += len;
            count++;
        }

        return true;
    }

}   // namespace tz
