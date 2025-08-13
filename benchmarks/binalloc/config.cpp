#include "config.h"
#include "utils.h"
#include "page.h"

namespace sgc2 {

    config_t::config_t(std::uint32_t const page_size) :
        page_size{page_size},
        page_header_size{32},                               // Arbitrary value. Perhaps a relation between arch size and header size can be found
        page_min_block_size{16},                            // Arbitrary value. Changing page alloc type can change this
        page_max_block_size{page_size / 4},                 // Also arbitrary although may be reasonable
        slab_page_count{page_size / page_header_size - 1},
        slab_size{(slab_page_count + 1) * page_size},
        bin_count{ count_powers_of_two(page_min_block_size, page_max_block_size) + 1 } {}

    config_t const &config() {
        static auto const inst = config_t{static_cast<std::uint32_t>(system_page_size())};
        return inst;
    }

}
