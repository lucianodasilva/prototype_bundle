#include "config.h"
#include "utils.h"
#include "page.h"

namespace sgc2 {

    constexpr int bin_offset_count(uintmax_t const size) {
        constexpr auto neg_bits      = sizeof(size) * 8 - 1;
        auto           leading_zeros = std::countl_zero(size);

        if(!std::has_single_bit(size)) {
            --leading_zeros;
        }

        return static_cast<int>(neg_bits - leading_zeros);
    }

    config_t::config_t(std::uint32_t const page_size) :
        page_size{page_size},
        page_meta_size{32}, // Arbitrary value. Perhaps a relationship between arch size and header size can be found
        page_min_block_size{16}, // Arbitrary value. Changing page alloc type can change this
        page_max_block_size{page_size / 4}, // Also arbitrary, although it may be reasonable

        cluster_page_count{page_size / page_meta_size - 1},
        cluster_page_block_size(cluster_page_count * page_size),
        cluster_size{(cluster_page_count + 1) * page_size},
        bin_count{count_powers_of_two(page_min_block_size, page_max_block_size) + 1},
        bin_offset{bin_offset_count(page_min_block_size)} {}

    // TODO: change to a 1.5 or 1.6 growth factor to reduce memory usage
    std::size_t config_t::bin_index(std::size_t const size) const {
        if(size > page_max_block_size) { return NO_BIN; }
        return std::max<int>(bin_offset_count(size) - bin_offset, 0);
    }

    std::size_t config_t::bin_index_max_size(std::size_t const index) const {
        return 1 << (index + bin_offset); // TODO: precalculate and cache this
    }

    std::size_t config_t::bin_object_count(std::size_t const index) const {
        auto const object_size = bin_index_max_size(index);
        return page_size / object_size;
    }

    config_t const &config() {
        static auto const inst = config_t{static_cast<std::uint32_t>(system_page_size())};
        return inst;
    }

}
