#ifndef EVSOCKS_DLIST_H
#define EVSOCKS_DLIST_H

#include <cstddef>
#include <cassert>
#include <iterator>

#include <boost/noncopyable.hpp>


namespace tz {

    struct DListNode : private boost::noncopyable {
        DListNode *prev;
        DListNode *next;

        DListNode() : prev(this), next(this)
        {}

        void insert_after(DListNode &back) {
            assert(back.is_detached());
            DListNode *backback = this->next;
            back.next = backback;
            back.prev = this;
            this->next = &back;
            backback->prev = &back;
        }

        void insert_before(DListNode &front) {
            this->prev->insert_after(front);
        }

        DListNode &detach() {
            DListNode *front = this->prev;
            DListNode *back = this->next;
            front->next = back;
            back->prev = front;
            this->next = this;
            this->prev = this;
            return *this;
        }

        bool is_detached() const {
            return this->prev == this;
        }
    };

    template <class T, size_t offset>
    struct DList {
        typedef T value_type;
        typedef value_type &reference;
        typedef const value_type &const_reference;
        typedef value_type *pointer;
        typedef const value_type *const_pointer;

    private:
        template <template <class, class> class IterT, class ValueType, class NodeType>
        struct base_iterator_impl {
            typedef ptrdiff_t difference_type;
            typedef ValueType value_type;
            typedef value_type &reference;
            typedef value_type *pointer;
            typedef std::bidirectional_iterator_tag iterator_category;

            typedef IterT<ValueType, NodeType> IterType;

            NodeType *node;

            // construct from DListNode
            explicit base_iterator_impl(NodeType *node) : node(node) {}

            // Pre-increment & decrement
            IterType &operator++() {
                static_cast<IterType &>(*this)._inc();
                return static_cast<IterType &>(*this);
            }
            IterType &operator--() {
                static_cast<IterType &>(*this)._dec();
                return static_cast<IterType &>(*this);
            }

            // Post-increment & decrement
            IterType operator++(int) {
                IterType tmp(this->node);
                static_cast<IterType &>(*this)._inc();
                return tmp;
            }
            IterType operator--(int) {
                IterType tmp(this->node);
                static_cast<IterType &>(*this)._dec();
                return tmp;
            }

            // comparison
            // FIXME: compare with const_iterator
            bool operator==(const IterType &rhs) const {
                return this->node == rhs.node;
            }
            bool operator!=(const IterType &rhs) const {
                return !(*this == rhs);
            }

            // dereference
            reference operator*() const {
                return DList::get_value(*this->node);
            }
            pointer operator->() const {
                return &this->operator*();
            }
        };

        template <class ValueType, class NodeType>
        struct iterator_impl : base_iterator_impl<iterator_impl, ValueType, NodeType> {
            iterator_impl(NodeType *node)
                : base_iterator_impl<iterator_impl, ValueType, NodeType>(node)
            {}
            // Pre-increment & decrement
            void _inc() { this->node = this->node->next; }
            void _dec() { this->node = this->node->prev; }
        };

        template <class ValueType, class NodeType>
        struct rev_iterator_impl : base_iterator_impl<rev_iterator_impl, ValueType, NodeType> {
            rev_iterator_impl(NodeType *node)
                : base_iterator_impl<rev_iterator_impl, ValueType, NodeType>(node)
            {}
            // Pre-increment & decrement
            void _inc() { this->node = this->node->prev; }
            void _dec() { this->node = this->node->next; }
        };

    public:
        typedef iterator_impl<T, DListNode> iterator;
        typedef iterator_impl<const T, const DListNode> const_iterator;
        typedef rev_iterator_impl<T, DListNode> reverse_iterator;
        typedef rev_iterator_impl<const T, const DListNode> const_reverse_iterator;

        // forward iterator
        iterator begin() { return iterator(this->sentry.next); }
        iterator end() { return iterator(&this->sentry); }
        const_iterator begin() const { return const_iterator(this->sentry.next); }
        const_iterator end() const { return const_iterator(&this->sentry); }

        // reverse iterator
        reverse_iterator rbegin() { return reverse_iterator(this->sentry.prev); }
        reverse_iterator rend() { return reverse_iterator(&this->sentry); }
        const_reverse_iterator rbegin() const { return const_reverse_iterator(this->sentry.prev); }
        const_reverse_iterator rend() const { return const_reverse_iterator(&this->sentry); }

        void push_front(reference value) {
            this->sentry.insert_after(get_node(value));
        }

        void push_back(reference value) {
            this->sentry.insert_before(get_node(value));
        }

        const_reference front() const {
            assert(!this->empty());
            return get_value((const DListNode &)*this->sentry.next);
        }
        reference front() {
            assert(!this->empty());
            return get_value(*this->sentry.next);
        }
        const_reference back() const {
            assert(!this->empty());
            return get_value((const DListNode &)*this->sentry.prev);
        }
        reference back() {
            assert(!this->empty());
            return get_value(*this->sentry.prev);
        }

        reference pop_front() {
            assert(!this->empty());
            return get_value(this->sentry.next->detach());
        }

        reference pop_back() {
            assert(!this->empty());
            return get_value(this->sentry.prev->detach());
        }

        iterator insert(iterator pos, reference value) {
            DListNode &node = get_node(value);
            assert(node.is_detached());
            get_node(*pos).insert_before(node);
            return iterator(&node);
        }
        // TODO: insert range

        iterator erase(iterator pos) {
            iterator next = pos;
            ++next;
            get_node(*pos).detach();
            return next;
        }
        iterator erase(reference value) {
            return this->erase(iterator(&get_node(value)));
        }
        // TODO: erase range
        // TODO: splice

        bool empty() const {
            return this->sentry.prev == &this->sentry;
        }

        static bool is_linked(const_reference value) {
            return !get_node(value).is_detached();
        }

    private:
        DListNode sentry;

        static DListNode &get_node(reference value) {
            return *(DListNode *)((char *)&value + offset);
        }
        static const DListNode &get_node(const_reference value) {
            return *(const DListNode *)((const char *)&value + offset);
        }

        static const_reference get_value(const DListNode &node) {
            return *(const_pointer)((const char *)&node - offset);
        }
        static reference get_value(DListNode &node) {
            return *(pointer)((char *)&node - offset);
        }
    };

#define TZ_DLIST(T, member) tz::DList<T, offsetof(T, member)>

}

#endif //EVSOCKS_DLIST_H
