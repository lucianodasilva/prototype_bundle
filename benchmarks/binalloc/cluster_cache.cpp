#include "cluster_cache.h"
#include "cluster.h"
#include "utils.h"

namespace sgc2 {

    struct cluster_cache {
        unique_spin_lock lock() { return unique_spin_lock(mutex); }

        void push(cluster_meta *cmeta) {
            auto lock_guard = lock();

            cmeta->next = free_list;
            free_list   = cmeta;
        }

        cluster_meta * pop() {
            auto lock_guard = lock();

            auto *cmeta = free_list;

            if(cmeta) {
                free_list = cmeta->next;
            }

            return cmeta;
        }

        static cluster_cache & get() {
            static cluster_cache inst = {};
            return inst;
        }

        cluster_meta *free_list{nullptr};
        spin_mutex    mutex{};
    };

    cluster_meta *cluster_alloc() {
        auto &cfg = config();

        auto *ptr = reserve(cfg.cluster_size, cfg.cluster_size);

        // check for alloc failure
        if(!ptr) [[unlikely]] { return nullptr; }

        // make cluster memory available
        commit(ptr, cfg.cluster_size);

        return cluster_meta::make(as_address(ptr));
    }

    cluster_meta *cluster_cache_fetch() {
        auto &cache = cluster_cache::get();

        cluster_meta *cmeta = cache.pop();

        if(!cmeta) [[unlikely]] {
            cmeta = cluster_alloc();
            // recheck for failed allocation
            if(!cmeta) { return nullptr; }
        }

        if(cmeta->state == cluster_state::unused) [[unlikely]] {
            cmeta->commit();
        }

        return cmeta;
    }

    void cluster_cache_return (cluster_meta *cmeta) {
        auto &cache = cluster_cache::get();
        cache.push(cmeta);
    }

    void cluster_cache_release (cluster_meta *cmeta) {
        cmeta->decommit();
    }

}
