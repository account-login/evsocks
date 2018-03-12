#ifndef EVSOCKS_ERROR_H
#define EVSOCKS_ERROR_H


#include <cerrno>
#include <cassert>
#include <string>
#include <memory>

#include "string_util.hpp"


namespace evsocks {
    using std::string;

    using tz::str;
    using tz::strfmt;


    enum ErrorType {
        OK = 0,
        ERR_GET_ADDR_INFO,
        ERR_SOCKET,
        ERR_CONNECT,
        ERR_NO_ADDR,
        ERR_EOF,
        ERR_READ,
        ERR_RECVFROM,
        ERR_WRITE,
        ERR_SENDTO,
        ERR_CLOSE,
        ERR_BIND,
        ERR_ACCEPT,
        ERR_LISTEN,
        ERR_SETSOCKOPT,
        ERR_FD_NOT_FOUND,
        ERR_FD_INVALID,
        ERR_SHUTDOWN,
        ERR_PIPE,
        ERR_FCNTL,
        ERR_BAD_VERSION,
        ERR_BAD_METHOD_NUM,
        ERR_BAD_ATYPE,
        ERR_CMD_UNSUPPORTED,
        ERR_LOGIC,
        ERR_AUTH,
        ERR_GET_SOCK_NAME,
        ERR_SIGNAL,
        ERR_TIMEOUT,
        ERR_UNEXPECTED_DATA,
        ERR_BAD_PACKET,
    };

    inline const char *ErrType2Str(ErrorType errtype) {
        switch (errtype) {
#define CASE_ARM(name) case name: return #name
        CASE_ARM(OK);
        CASE_ARM(ERR_GET_ADDR_INFO);
        CASE_ARM(ERR_SOCKET);
        CASE_ARM(ERR_CONNECT);
        CASE_ARM(ERR_NO_ADDR);
        CASE_ARM(ERR_EOF);
        CASE_ARM(ERR_READ);
        CASE_ARM(ERR_RECVFROM);
        CASE_ARM(ERR_WRITE);
        CASE_ARM(ERR_SENDTO);
        CASE_ARM(ERR_CLOSE);
        CASE_ARM(ERR_BIND);
        CASE_ARM(ERR_ACCEPT);
        CASE_ARM(ERR_LISTEN);
        CASE_ARM(ERR_SETSOCKOPT);
        CASE_ARM(ERR_FD_NOT_FOUND);
        CASE_ARM(ERR_FD_INVALID);
        CASE_ARM(ERR_SHUTDOWN);
        CASE_ARM(ERR_PIPE);
        CASE_ARM(ERR_FCNTL);
        CASE_ARM(ERR_BAD_VERSION);
        CASE_ARM(ERR_BAD_METHOD_NUM);
        CASE_ARM(ERR_BAD_ATYPE);
        CASE_ARM(ERR_CMD_UNSUPPORTED);
        CASE_ARM(ERR_LOGIC);
        CASE_ARM(ERR_AUTH);
        CASE_ARM(ERR_GET_SOCK_NAME);
        CASE_ARM(ERR_SIGNAL);
        CASE_ARM(ERR_TIMEOUT);
        CASE_ARM(ERR_UNEXPECTED_DATA);
        CASE_ARM(ERR_BAD_PACKET);
#undef CASE_ARM
        default:
            assert(!"Unreachable");
            return "!BAD!";
        }
    }

    struct ErrorContent {
        ErrorType type;
        int32_t code;
        std::string msg;

        ErrorContent(ErrorType type, int32_t code, const std::string &msg)
            : type(type), code(code), msg(msg)
        {}
    };

    // global var in header
    template <class>
    struct __g_error__ {
        static string empty;
    };

    template <class T>
    string __g_error__<T>::empty;

    struct Error {
        mutable std::auto_ptr<ErrorContent> ptr;    // should be immutable unique_ptr in c++11

        Error() : ptr(NULL) {}

        Error(ErrorType type, int32_t code, const std::string &msg)
            : ptr(new ErrorContent(type, code, msg))
        {}

        // transferred
        Error(const Error &rhs) : ptr(rhs.ptr) {}
        Error &operator=(const Error &rhs) {
            this->ptr = rhs.ptr;
            return *this;
        }

        bool ok() const {
            return !this->ptr.get();
        }

        ErrorType type() const {
            if (ErrorContent *ec = this->ptr.get()) {
                return ec->type;
            } else {
                return OK;
            }
        }

        int32_t code() const {
            if (ErrorContent *ec = this->ptr.get()) {
                return ec->code;
            } else {
                return 0;
            }
        }

        const std::string &msg() const {
            if (ErrorContent *ec = this->ptr.get()) {
                return ec->msg;
            } else {
                return __g_error__<void>::empty;
            }
        }

        std::string str() const {
            if (ErrorContent *ec = this->ptr.get()) {
                // strerror_r is stupid
                const char *errmsg = 0 <= ec->code && ec->code < sys_nerr
                    ? sys_errlist[ec->code]
                    : "Unknown error";
                return strfmt("[%s:%d]: %s (%s)",
                    ErrType2Str(ec->type), ec->code, ec->msg.c_str(), errmsg);
            } else {
                return "";
            }
        }
    };

    inline Error Ok() {
        return Error();
    }

}

#endif //EVSOCKS_ERROR_H
