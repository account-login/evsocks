#ifndef EVSOCKS_BUFQUEUE_H
#define EVSOCKS_BUFQUEUE_H

#include <cassert>
#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>


namespace evsocks {

    using std::vector;
    using std::string;
    using std::copy;


    class BufQueue {
    public:
        BufQueue() : start(0) {}

        const char *data() const {
            return this->buf.data() + start;
        }

        const char &operator[](size_t index) const {
            assert(this->start + index < this->buf.size());
            return this->data()[index];
        }

        void push(const char *data, size_t count) {
            this->buf.insert(this->buf.end(), data, data + count);
        }

        void push(const string &data) {
            return this->push(data.c_str(), data.size());
        }

        void peek(const char *&data, size_t &count) {
            assert(this->start <= buf.size());
            data = this->buf.data() + start;
            count = buf.size() - this->start;
        }

        void pop(size_t count) {
            assert(this->start + count <= buf.size());
            this->start += count;
        }

        size_t size() const {
            assert(this->start <= buf.size());
            return this->buf.size() - this->start;
        }

        bool empty() const {
            return this->size() == 0;
        }

        void shrink() {
            assert(this->start <= buf.size());
            if (start * 2 > this->buf.size()) {
                copy(this->buf.begin() + this->start, this->buf.end(), this->buf.begin());
                this->buf.resize(this->buf.size() - this->start);
                this->start = 0;
            }
        }

        void swap(BufQueue &rhs) {
            std::swap(this->start, rhs.start);
            this->buf.swap(rhs.buf);
        }

    private:
        size_t start;
        vector<char> buf;
    };
}

#endif //EVSOCKS_BUFQUEUE_H
