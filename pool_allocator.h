#pragma once
#ifndef POOL_ALLOCATOR_H
#define POOL_ALLOCATOR_H

#include <array>
#include <atomic>

namespace trace::details {

    template < typename type >
    struct pool_allocator {
    public:
        using value_type = type;
        using pointer = type *;
        using const_pointer = const type *;
        using reference = type &;
        using const_reference = const type &;

        pool_allocator () = default;

        template < typename other >
        pool_allocator & operator = (const pool_allocator < other > &) {
            return *this;
        }

        pointer allocate (std::size_t n) {
            return static_cast <pointer> (operator new (n * sizeof (type)));
        }

        void deallocate (pointer p, std::size_t) {
            operator delete (p);
        }

        template < typename other >
        bool operator == (const pool_allocator < other > &) const {
            return true;
        }

        template < typename other >
        bool operator != (const pool_allocator < other > &) const {
            return false;
        }

    private:

        struct page_node {
            page_node * next;
        };

        constexpr std::size_t PAGE_DATA_SIZE = (sizeof (page_node) > sizeof (type) ? sizeof (page_node) : sizeof (type));
        constexpr std::size_t PAGE_DATA_ALIGNMENT = (alignof (page_node) > alignof (type) ? alignof (page_node) : alignof (type));
        constexpr std::size_t PAGE_SIZE = 256;

        struct page {
            std::array < type, PAGE_DATA_SIZE > alignas (PAGE_DATA_ALIGNMENT) data;
            page * next;
            std::atomic_size_t count;
        };


    };

}

#endif //POOL_ALLOCATOR_H