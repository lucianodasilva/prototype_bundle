#pragma once
#ifndef BINALLOC_CONFIG_H
#define BINALLOC_CONFIG_H

#include <bit>
#include <cstdint>
#include <limits>

namespace sgc2 {

    constexpr std::size_t NO_BIN = std::numeric_limits<std::size_t>::max();

    struct config_t {
        explicit config_t(std::uint32_t page_size);

        std::uint32_t page_size; ///< System memory page size in bytes
        std::uint32_t page_meta_size; ///< Page header reserved size in bytes
        std::uint32_t page_min_block_size; ///< Minimum allocatable block size in bytes
        std::uint32_t page_max_block_size; ///< Maximum allocatable block size in bytes

        std::uint32_t cluster_page_count; ///< Number of pages in a slab
        std::uint32_t cluster_page_block_size; ///< Size of a page block in bytes
        std::uint32_t cluster_size; ///< Slab size in bytes

        int bin_count; ///< Number of bins in a thread local bin storage
        int bin_offset; ///< Offset to convert size to bin index

        [[nodiscard]] std::size_t bin_index(std::size_t size) const;

        [[nodiscard]] std::size_t bin_index_max_size(std::size_t index) const;

        [[nodiscard]] std::size_t bin_object_count(std::size_t index) const;
    };

    /// Returns allocator runtime configuration values
    config_t const &config();

}

#endif
