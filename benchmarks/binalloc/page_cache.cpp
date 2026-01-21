#include "page_cache.h"

#include <memory>

#include "cluster.h"
#include "cluster_cache.h"
#include "page.h"

namespace sgc2 {

    struct page_cache_bin {

        unique_spin_lock lock() { return unique_spin_lock(mutex); }

        void detach(page_meta const *const pmeta) {
            auto lock_guard = lock();

            pmeta->prev->next = pmeta->next;
            pmeta->next->prev = pmeta->prev;
        }

        void push(page_meta *pmeta) {
            auto lock_guard = lock();

            // squeeze into the list
            pmeta->next = head.next;
            pmeta->prev = &head;

            pmeta->next->prev = pmeta;
            pmeta->prev->next = pmeta;
        }

        page_meta *pop() {
            auto lock_guard = lock();

            auto *const pmeta = head.next;

            if(pmeta == &tail) {
                return nullptr; // empty list
            }

            head.next         = pmeta->next;
            pmeta->next->prev = &head;

            return pmeta;
        }

        // doubly linked list with sentinel nodes
        page_meta head{.next = &tail, .prev = nullptr};
        page_meta tail{.next = nullptr, .prev = &head};

        spin_mutex mutex{};
    };

    struct page_cache {
        unique_spin_lock lock() { return unique_spin_lock(mutex); }

        page_cache_bin &operator[](uint8_t const index) { return bins[index]; }

        static page_cache &get() {
            static page_cache inst = {};
            return inst;
        }

        link *                            free_list;
        std::unique_ptr<page_cache_bin[]> bins{
                std::make_unique<page_cache_bin[]>(config().bin_count)};

        spin_mutex mutex;
    };

    page_meta *page_alloc_new_page(page_cache &cache, uint8_t const bin_index) {
        auto  lock_guard = cache.lock();
        auto *head       = cache.free_list;

        // if we have no free elements available, go get some more
        if(!head) [[unlikely]] {
            auto *cmeta = cluster_cache_fetch();

            if(!cmeta) { return nullptr; }

            cmeta->transfer(cache.free_list);
            head = cache.free_list;
        }

        // pop element and format as page
        cache.free_list = head->next;
        return page_meta::make(as_address(head), bin_index);
    }

    page_meta *page_cache_fetch(uint8_t const bin_index) {
        auto &cache = page_cache::get();

        // get page from cache bin
        auto *pmeta = cache[bin_index].pop();

        if(!pmeta) [[unlikely]] {
            return page_alloc_new_page(cache, bin_index);
        }

        return pmeta;
    }

    void page_cache_return(page_meta *pmeta) {
        auto &cache = page_cache::get();
        cache[pmeta->bin_index].push(pmeta);
    }

    void page_cache_release(page_meta *pmeta) {
        auto &cache = page_cache::get();
        cache[pmeta->bin_index].detach(pmeta);

        auto *cluster_ptr = cluster_meta::owning(as_address(pmeta));
        cluster_ptr->free(pmeta);
    }

}
