#include "page.h"

#include <stdexcept>

#include "slab.h"
#include "stack.h"

namespace sgc2 {

    page::header::header(std::size_t const block_bin, block *free_stack_head) :
        free_stack(free_stack_head),
        block_bin(block_bin) {}

    page *page::header::page() {
        return slab_address_table::from(this).page_ptr;
    }

    void *page::header::alloc() {
        return stack::atomic_pop(free_stack);
    }

    void page::header::free(void *address) {
        stack::atomic_push(free_stack, std::bit_cast<block *>(address));
    }


}
