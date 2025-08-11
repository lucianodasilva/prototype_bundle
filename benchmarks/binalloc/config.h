#pragma once
#ifndef BINALLOC_CONFIG_H
#define BINALLOC_CONFIG_H

#include <cstddef>

namespace sgc2 {

    constexpr std::size_t page_header_size = 32; // TODO: arbitrary. Perhaps if compiled for 32 bits, it can be made smaller

    /// Number of pages in a slab
    extern std::size_t const slab_page_count;

    /// Size of a slab in bytes
    extern std::size_t const slab_size;

    /// Minimum size of a bin
    constexpr std::size_t page_min_block_size = 24; // TODO: this is arbitrary. Try to figure out something based on the total page size

    /// Maximum size for page blocks
    extern std::size_t const page_max_block_size;

    /// Number of bins per thread local allocator
    extern std::size_t const bin_count;



}

#endif