#include "config.h"
#include "utils.h"
#include "page.h"

namespace sgc2 {

    std::size_t const slab_page_count = page_size / page_header_size - 1;
    std::size_t const slab_size       = (slab_page_count + 1 /* take into account the header map */) * page_size;

    std::size_t const page_max_block_size = page_size / 4;
    std::size_t const bin_count           = (page_max_block_size - page_min_block_size) / sizeof(uintptr_t);

}
