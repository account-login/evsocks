#pragma once

#include <cassert>
#include <string>
#include <vector>

#include <boost/thread/tss.hpp>

#include "../string_util.hpp"
#include "../conv_util.hpp"


namespace tz { namespace ctxlog {

    using namespace std;
    using namespace tz;

    class ContextData;

    template <class>
    struct _ContextDataHolder {
        static boost::thread_specific_ptr<ContextData> instance;
    };
    template <class T>
    boost::thread_specific_ptr<ContextData> _ContextDataHolder<T>::instance;

    struct ContextData {
        std::string buf;
        vector<size_t> stack;

        void push() {
            this->stack.push_back(this->buf.size());
        }

        void append_kv(const std::string &key, const std::string &value) {
            buf.push_back('[');
            buf.append(key);
            buf.push_back(':');
            buf.append(value);
            buf.push_back(']');
        }

        void append(const std::string &data) {
            buf.append(data);
        }

        void pop() {
            assert(!this->stack.empty());
            buf.resize(this->stack.back());
            this->stack.pop_back();
            // buf do not shrink
        }

        const string &data() const {
            return this->buf;
        }

        static ContextData &get() {
            boost::thread_specific_ptr<ContextData> &instance = _ContextDataHolder<void>::instance;
            if (instance.get() == NULL) {
                instance.reset(new ContextData);
            }
            return *instance.get();
        }
    };

    struct Context {
        ContextData &ctxdata;

        explicit Context(ContextData &ctxdata) : ctxdata(ctxdata) {
            this->ctxdata.push();
        }

        ~Context() {
            this->ctxdata.pop();
        }

        template <class T>
        Context &set(const string &key, const T &value) {
            string val;
            if (!try_cast(value, val)) {
                val = "!BAD!";
            }
            this->ctxdata.append_kv(key, val);
            return *this;
        }

        Context &push(const string &value) {
            this->ctxdata.append(value);
            return *this;
        }
    };


#define CTXLOG_CAT_INNER(a, b) a ## b
#define CTXLOG_CAT(a, b) CTXLOG_CAT_INNER(a, b)

#define CTXLOG_CREATE() \
    tz::ctxlog::Context CTXLOG_CAT(__logctx_, __LINE__)(tz::ctxlog::ContextData::get()); \
    CTXLOG_CAT(__logctx_, __LINE__)

#define CTXLOG_SET(key, value) CTXLOG_CREATE().set(key, value)
#define CTXLOG_PUSH(value) CTXLOG_CREATE().push(value)
#define CTXLOG_PUSH_FUNC() CTXLOG_PUSH(string("[") + __FUNCTION__ + "]")

#define CTXLOG_LOG(level, fmt, ...) \
    CTXLOG_LOG_IMPL(level, "%s " fmt, tz::ctxlog::ContextData::get().data().c_str(), ##__VA_ARGS__)

#define CTXLOG_DBG(fmt, ...)    CTXLOG_LOG(tz::ctxlog::LVL_DEBUG,   fmt, ##__VA_ARGS__)
#define CTXLOG_INFO(fmt, ...)   CTXLOG_LOG(tz::ctxlog::LVL_INFO,    fmt, ##__VA_ARGS__)
#define CTXLOG_NOTICE(fmt, ...) CTXLOG_LOG(tz::ctxlog::LVL_NOTICE,  fmt, ##__VA_ARGS__)
#define CTXLOG_WARN(fmt, ...)   CTXLOG_LOG(tz::ctxlog::LVL_WARN,    fmt, ##__VA_ARGS__)
#define CTXLOG_ERR(fmt, ...)    CTXLOG_LOG(tz::ctxlog::LVL_ERROR,   fmt, ##__VA_ARGS__)
#define CTXLOG_FATAL(fmt, ...)  CTXLOG_LOG(tz::ctxlog::LVL_FATAL,   fmt, ##__VA_ARGS__)

}}  // namespace tz::ctxlog
