#pragma once

#include <time.h>
#include <iterator>

#include <boost/lexical_cast.hpp>
#include <boost/thread/tss.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/random/uniform_real.hpp>


namespace tz {
    using namespace std;

    template <class F, class T>
    inline bool try_cast(const F &from, T &to) {
        try {
            to = boost::lexical_cast<T>(from);
            return true;
        } catch (boost::bad_lexical_cast &) {
            return false;
        }
    }

    // specializations for 8bit int
    template <class T>
    inline bool try_cast(const uint8_t &from, T &to) {
        return try_cast(uint32_t(from), to);
    }

    template <class T>
    inline bool try_cast(const int8_t &from, T &to) {
        return try_cast(int32_t(from), to);
    }

    template <class F>
    inline bool try_cast(const F &from, uint8_t &to) {
        uint32_t u32 = 0;
        if (!try_cast(from, u32)) {
            return false;
        }
        if (u32 > 0xff) {
            return false;
        }

        to = u32;
        return true;
    }

    template <class F>
    inline bool try_cast(const F &from, int8_t &to) {
        int32_t i32 = 0;
        if (!try_cast(from, i32)) {
            return false;
        }
        if (i32 > 127 || i32 < -128) {
            return false;
        }

        to = i32;
        return true;
    }

    template <class F, class T>
    inline T cast(const F &from, const T &def) {
        T value;
        if (try_cast(from, value)) {
            return value;
        } else {
            return def;
        }
    }

    template <class MapType, class KeyType, class T>
    inline T map_get(const MapType &mapping, const KeyType &key, const T &def)
    {
        typename MapType::const_iterator it = mapping.find(key);
        if (it == mapping.end()) {
            return def;
        }
        return cast(it->second, def);
    }

    template <class MapType, class KeyType>
    inline const typename MapType::value_type::second_type &
    map_get(const MapType &mapping, const KeyType &key)
    {
        typename MapType::const_iterator it = mapping.find(key);
        assert(it != mapping.end());
        return it->second;
    }

    template <class IT>
    struct KeyIter {
        typedef typename iterator_traits<IT>::value_type PairType;
        typedef const typename PairType::first_type KeyType;

        typedef ptrdiff_t difference_type;
        typedef KeyType value_type;
        typedef value_type &reference;
        typedef value_type *pointer;
        typedef std::bidirectional_iterator_tag iterator_category;

        IT it;

        KeyIter(IT it) : it(it) {}

        // Pre-increment
        KeyIter &operator++() {
            ++it;
            return *this;
        }

        // Post-increment
        KeyIter operator++(int) {
            KeyIter tmp(this->it);
            it++;
            return tmp;
        }

        // comparison
        bool operator==(const KeyIter &rhs) const {
            return this->it == rhs.it;
        }

        bool operator!=(const KeyIter &rhs) const {
            return !(*this == rhs);
        }

        // dereference
        reference operator*() const {
            return this->it->first;
        }

        pointer operator->() const {
            return &this->operator*();
        }
    };

    template <class IT>
    KeyIter<IT> make_key_iter(IT it) {
        return KeyIter<IT>(it);
    }

#define KEY_VIEW(c) make_key_iter((c).begin()), make_key_iter((c).end())

    // uniform distribution between [0, 1)
    // ref: http://www.boost.org/doc/libs/1_46_1/libs/random/example/random_demo.cpp
    inline double get_random() {
        // This is a typedef for a random number generator.
        // Try boost::mt19937 or boost::ecuyer1988 instead of boost::minstd_rand
        typedef boost::minstd_rand base_generator_type;
        typedef boost::variate_generator<base_generator_type, boost::uniform_real<> > gen_type;

        static boost::thread_specific_ptr<gen_type> gen_holder;
        gen_type *gen = gen_holder.get();
        if (gen == NULL) {
            // Define a uniform random number distribution which produces "double"
            // values between 0 and 1 (0 inclusive, 1 exclusive).
            boost::uniform_real<> uni_dist(0, 1);
            gen_holder.reset(new gen_type(base_generator_type(0), uni_dist));
            gen = gen_holder.get();

            // seed generator with time ^ addr
            timespec tv = {0, 0};
            clock_gettime(CLOCK_MONOTONIC, &tv);
            unsigned int seed = tv.tv_nsec;
            seed ^= (uint32_t)(uint64_t)gen;
            gen->engine().seed(seed);
        }

        return (*gen)();
    }

}   // namespace tz
