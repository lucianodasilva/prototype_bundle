#pragma once
#ifndef BINALLOC_PAGE_H
#define BINALLOC_PAGE_H

#include <bit>

#include "config.h"
#include "utils.h"

namespace sgc2 {

    struct page : addressable {

        struct block : addressable {
            block *next;
        };

        struct header : addressable {
            explicit header(std::size_t block_size, block *free_stack_head);

            [[nodiscard]] sgc2::page *page();

            void *alloc();

            void free(void *address);

            header *             next{nullptr}; // pointer to the next page header in the slab
            std::atomic<block *> free_stack; // pointer to the first free block in this page
            std::size_t const    block_bin; // what bin index does this block bellong to
        };

        // -- cannot do static asserts --
        // static_assert (sizeof (page::header) <= config().page_size, "Page header too large to fit in allowed max size");
    };

}

#endif
