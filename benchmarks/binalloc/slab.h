#pragma once
#ifndef BINALLOC_SLAB_H
#define BINALLOC_SLAB_H

#include <bit>
#include <cstdint>

#include "page.h"
#include "utils.h"

namespace sgc2 {

    struct slab_address_table {

        static slab_address_table from (std::byte * address);
        static slab_address_table from (page * page_ptr);
        static slab_address_table from (page::header * header_ptr);

        struct slab * const slab_ptr;
        page * const page_ptr;
        page::header * const header_ptr;
    };

    struct slab : addressable {

        static slab * reserve ();

        page::header * alloc (std::size_t bin_index);
        void free (page::header * header);

        slab * next {nullptr}; // pointer to the next slab in the stack
        std::atomic < page::header * > free_stack {nullptr}; // pointer to the first free page header in this slab

    private:
        slab();
    };

    namespace slab_stack {
        page::header * alloc (std::size_t bin_index);
        void free (page::header * header);

        static std::atomic < slab * > free_stack;
    }

}

#endif