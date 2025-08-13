#include "slab.h"

#include "bin.h"
#include "config.h"
#include "page.h"
#include "stack.h"
#include "utils.h"

namespace sgc2 {

    slab_address_table slab_address_table::from(std::byte *address) {
        auto *const slab_ptr = align_down(address, config().slab_size);

        auto const page_index = (address - slab_ptr) / config().page_size;

        auto *const header_ptr = slab_ptr + config().page_header_size * page_index;
        auto *const page_ptr   = slab_ptr + (config().page_size * page_index);

        return {
                std::bit_cast<slab *>(slab_ptr),
                std::bit_cast<page *>(page_ptr),
                std::bit_cast<page::header *>(header_ptr)
        };
    }

    slab_address_table slab_address_table::from(page *page_ptr) {
        return from(page_ptr->address());
    }

    slab_address_table slab_address_table::from(page::header *header_ptr) {
        auto *const slab_ptr   = align_down(header_ptr->address(), config().slab_size);
        auto const  page_index = (header_ptr->address() - slab_ptr) / config().page_header_size;
        auto *const page_ptr   = slab_ptr + (config().page_size * page_index);

        return {
                std::bit_cast<slab *>(slab_ptr),
                std::bit_cast<page *>(page_ptr),
                header_ptr
        };
    }

    slab *slab::reserve() {
        auto *address = sgc2::reserve(config().slab_size, config().slab_size);

        if(!address) [[unlikely]] {
            return nullptr; // allocation failed
        }

        // commit the first page to the slab for the page header data
        if(!commit(address, config().slab_size)) [[unlikely]] {
            return nullptr;
        }

        return new(address) slab; // initialize the slab
    }

    page::header *slab::alloc(std::size_t const bin_index) {
        auto *header = stack::atomic_pop(free_stack);

        if(!header) [[unlikely]] {
            return nullptr;
        }

        auto *page = header->page();

        // really allocate the page
        //if(!commit(page->address(), config().page_size)) [[unlikely]] {
        //    // don't know what good it will do after failing to commit, but at least tries to keep this thing stable
        //    stack::atomic_push(free_stack, header);
        //    return nullptr;
        //}

        // initialize blocks and return
        return new(header) page::header(
                bin_index,
                stack::format_stack<page::block>(
                        {page->address(), config().page_size },
                        bin_store::bin_index_max_size(bin_index)));
    }

    void slab::free(page::header *header) {
        // TODO: identify "empty" page and decommit
        stack::atomic_push(free_stack, header);
    }

    slab::slab() :
        free_stack{
                stack::format_stack<page::header>(
                        {
                                this->address() + config().page_header_size,
                                // first "header" is reserved for the slab itself
                                (config().slab_page_count - 1) * config().page_header_size
                        },
                        config().page_header_size)} // header data space is equal to the size of a page
    {}

    page::header *slab_stack::alloc(std::size_t const bin_index) {
        auto *slab_ptr = free_stack.load(std::memory_order_relaxed);
        auto *header   = slab_ptr ? slab_ptr->alloc(bin_index) : nullptr;

        while(!header)[[unlikely]] {
            // probably the slab is full, try to pop it from the stack
            stack::atomic_pop_expected(free_stack, slab_ptr);

            slab_ptr = slab::reserve();

            if(!slab_ptr) [[unlikely]] {
                return nullptr; // allocation failed
            }

            header = slab_ptr->alloc(bin_index);
            stack::atomic_push(free_stack, slab_ptr);
        }

        return header;
    }

    void slab_stack::free(page::header *header) {
        auto  [slab_ptr , page_ptr, header_ptr] = slab_address_table::from(header);
        auto *free_stack_head                   = slab_ptr->free_stack.load(std::memory_order_relaxed);

        if(free_stack_head == nullptr) [[unlikely]] {
            // slab is full, after freeing the header we need to add it back to the slab stack
            // try to free "by hand" so we can control atomicity
            if(stack::compare_and_swap_strong(
                    slab_ptr->free_stack,
                    free_stack_head,
                    header)) {
                stack::atomic_push(free_stack, slab_ptr);
                return;
            }
        }

        slab_ptr->free(header);
    }

}
