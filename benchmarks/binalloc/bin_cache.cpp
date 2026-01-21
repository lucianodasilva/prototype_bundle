#include "bin_cache.h"

#include <mutex>
#include <las/bits.hpp>

#include "config.h"
#include "page_cache.h"
#include "cluster.h"
#include "stack.h"

namespace sgc2 {

    struct bin_cache {
        ~bin_cache(); ///< called on thread exit

        link *&operator[](std::size_t const index) const noexcept {
            return bins[index];
        }

        std::unique_ptr<link *[]> bins{
                std::make_unique<link *[]>(config().bin_count)};
    };

    bin_cache::~bin_cache() {
        // TODO: mass release all cached objects
    }

    void *bin_alloc_fetch_page(bin_cache const &cache, std::size_t const index) {
        // get an available page from the page cache
        auto *const pmeta = page_cache_fetch(index);

        if(!pmeta) [[unlikely]] {
            return nullptr; // allocation failed
        }

        // prefetch bin head
        auto *& bin_head = cache[index];

        // lock and transfer ownership of the contained blocks to the thread local cache
        pmeta->transfer_to(bin_head);

        // pop the head from the stack
        auto *head = bin_head;
        if(head) { bin_head = head->next; }

        return std::bit_cast<void *>(head); // reuse the address
    }

    void *bin_alloc(std::size_t size) {
        // store instance for this thread
        thread_local bin_cache cache = {};

        auto const index = config().bin_index(size);

        if(index == NO_BIN) [[unlikely]] {
            // size is too large, use the system allocator?
            // TODO:    manually allocate using mmap/VirtualAlloc
            //          find way to track these allocations for free
            throw std::invalid_argument("Requested size too large for bin allocator");
        }

        // check the bin for available items ---------------------------------------------------------------------------
        auto *& bin_head = cache[index];

        // worst case scenario -----------------------------------------------------------------------------------------
        if(!bin_head) [[unlikely]] {
            return bin_alloc_fetch_page(cache, index);
        }

        // alloc best case scenario ------------------------------------------------------------------------------------
        bin_head = bin_head->next; // pop the head from the stack
        return std::bit_cast<void *>(bin_head); // reuse the address
    }

    void bin_free(void * const ptr) {
        auto const address = as_address(ptr);
        page_meta::owning(address)->free(address);
    }

} // namespace sgc2
