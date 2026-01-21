#include "cluster.h"

#include <span>

#include "cluster_cache.h"
#include "stack.h"
#include "utils.h"

namespace sgc2 {

    void cluster_meta::transfer(link *&head) {
        auto lock_guard = lock();

        head      = free_list;
        free_list = nullptr;

        used  = config().cluster_page_count;
        state = cluster_state::untethered;
    }

    void cluster_meta::free(page_meta *page) {
        auto  lock_guard = lock();
        auto *item       = std::bit_cast<link *>(page);

        item->next = free_list;
        free_list  = item;

        --used;

        if(state == cluster_state::untethered) [[unlikely]] {
            cluster_cache_return(this);
        }

        if(used == 0) [[unlikely]] {
            cluster_cache_release(this);
        }
    }

    void cluster_meta::commit() {
        auto const &cfg = config();

        // although we could reduce this to only the cluster's meta info,
        // the whole page would remain commited by the page manager, so why bother?
        sgc2::commit(
                std::bit_cast<std::byte *>(this) + cfg.page_size,
                cfg.cluster_page_block_size);

        state = cluster_state::in_use;
    }

    void cluster_meta::decommit() {
        auto const &cfg = config();

        sgc2::decommit(
                std::bit_cast<std::byte *>(this) + cfg.page_size,
                cfg.cluster_page_block_size);

        state = cluster_state::unused;
    }


    cluster_meta *cluster_meta::make(address_t const address) {
        auto &cfg = config();
        auto *ptr = as_ptr(address);

        auto const cluster_page_data = std::span(
                ptr + cfg.page_meta_size,
                cfg.page_meta_size * cfg.cluster_page_count);

        return new(ptr) cluster_meta{
                .free_list = stack::format_stack<link>(
                        cluster_page_data,
                        cfg.page_meta_size)
        };
    }

    cluster_meta *cluster_meta::owning(address_t const address) {
        return std::bit_cast<cluster_meta *>(address_meta(address).cluster);
    }

}
