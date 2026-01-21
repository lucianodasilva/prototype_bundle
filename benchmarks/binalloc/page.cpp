#include "page.h"

#include <mutex>
#include <stdexcept>

#include "cluster.h"
#include "page_cache.h"
#include "stack.h"

namespace sgc2 {

    unique_spin_lock page_meta::lock() {
        return unique_spin_lock(mutex);
    }

    void page_meta::transfer_to(link *&head) {
        auto lock_guard = lock();

        // transfer available objets to external cache
        head      = free_list;
        free_list = nullptr;

        // set page state to untethered
        used        = config().bin_object_count(bin_index);
        is_tethered = false;
    }

    void page_meta::free(address_t const address) {
        auto        lock_guard = lock();
        auto *const item       = std::bit_cast<link *>(address);

        // push to free list
        item->next = free_list;
        free_list  = item;

        // handle page integrity
         --used;

        if(!is_tethered) [[unlikely]] {
            page_cache_return(this);
            is_tethered = true;
        }

        if(used == 0) [[unlikely]] {
            page_cache_release(this);
        }
    }

    page_meta *page_meta::make(address_t const address, uint8_t const bin_index) {
        auto const &cfg       = config();
        auto const  addr_meta = address_meta(address);

        if(bin_index >= cfg.bin_count) {
            throw std::invalid_argument("Invalid bin index");
        }

        auto const page_data = std::span(
                as_ptr (addr_meta.page),
                cfg.page_size);

        return new(std::bit_cast<void *>(address)) page_meta{
                .free_list = stack::format_stack<link>(
                        page_data,
                        cfg.bin_index_max_size(bin_index)),
                .bin_index = bin_index
        };
    }

    page_meta *page_meta::owning(address_t const address) {
        return std::bit_cast<page_meta *>(address_meta(address).page_meta);
    }

    address_t page_meta::page() const {
        return address_meta(this).page;
    }
}
