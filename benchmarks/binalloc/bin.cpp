#include "bin.h"

#include <las/bits.hpp>

#include "config.h"
#include "slab.h"
#include "stack.h"

namespace sgc2 {

    bin_store::bin_store() :
        _bins(std::make_unique<page::header *[]>(config().bin_count)) {
        std::fill_n(_bins.get(), config().bin_count, nullptr);
    }

    void *bin_store::alloc(size_t size) {
        auto const index = bin_index(size);

        if(index == NO_BIN_INDEX) [[unlikely]] {
            // size is too large, use the system allocator
            return nullptr;
        }

        auto *header  = _bins[index];
        auto *address = header ? header->alloc() : nullptr;

        while(!address) [[unlikely]] {
            // remove existing head since it is full (nothing happens if it's already empty)
            stack::pop(_bins[index]);

            header = slab_stack::alloc(index);

            if(!header) [[unlikely]] {
                return nullptr; // slab page allocation failed, nothing to do
            }

            stack::push(_bins[index], header);
            address = header->alloc();
        }

        return address;
    }

    void bin_store::free(void *address) {
        auto [slab, page, header]
                = slab_address_table::from(std::bit_cast<std::byte *>(address));

        bool const page_was_full = header->free_stack.load(std::memory_order::relaxed) == nullptr;

        header->free(address);

        if(page_was_full) [[unlikely]] {
            // if the page was full, we need to add it back to the bin stack
            stack::push(
                _bins[header->block_bin],
                header);
        }
    }

    bin_store &bin_store::this_thread_store() {
        thread_local bin_store store;
        return store;
    }

    std::size_t bin_store::bin_index(std::size_t size) {
        if(size == 0 || size > config().page_max_block_size) [[unlikely]] {
            return NO_BIN_INDEX; // overflow, signal that size is too large
        }

        if(size < config().page_min_block_size) [[unlikely]] {
            return 0; // underflow, return the first bin
        }

        auto bit_index = _bit_scan_reverse(size);

        if (!is_pow_2(size)) {
            ++bit_index;
        }

        return bit_index - 3; // TODO: precalculate and cache this
    }

    std::size_t bin_store::bin_index_max_size(std::size_t index) {
        return 1 << (index + 3); // TODO: precalculate and cache this
    }

} // namespace sgc2
