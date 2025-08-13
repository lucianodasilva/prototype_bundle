#pragma once
#ifndef BINALLOC_CONFIG_H
#define BINALLOC_CONFIG_H

#include <cstdint>

namespace sgc2 {

    struct config_t {
        explicit config_t (std::uint32_t page_size);

        std::uint32_t page_size;            ///< System memory page size in bytes
        std::uint32_t page_header_size;     ///< Page header reserved size in bytes
        std::uint32_t page_min_block_size;  ///< Minimum allocatable block size in bytes
        std::uint32_t page_max_block_size;  ///< Maximum allocatable block size in bytes

        std::uint32_t slab_page_count;      ///< Number of pages in a slab
        std::uint32_t slab_size;            ///< Slab size in bytes

        std::uint32_t bin_count;            ///< Number of bins in a thread local bin storage
    };

    /// Returns allocator runtime configuration values
    config_t const & config();

}

#endif