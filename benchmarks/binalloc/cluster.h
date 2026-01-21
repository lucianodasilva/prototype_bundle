#pragma once
#ifndef BINALLOC_CLUSTER_H
#define BINALLOC_CLUSTER_H

#include <cstdint>
#include "utils.h"

namespace sgc2 {

    struct page_meta;

    enum struct cluster_state : uint8_t {
        unused, ///< cluster is not in use and has no allocated pages
        in_use, ///< cluster is in use and has allocated pages
        untethered, ///< cluster is in use and is allocated, but not on the available stack
    };

    struct cluster_meta {
        unique_spin_lock lock() { return unique_spin_lock(mutex); }

        void transfer(link *&head);

        void free(page_meta *page);

        void commit();

        void decommit();

        static cluster_meta *make(address_t address);

        static cluster_meta *owning(address_t address);

        cluster_meta *next{nullptr};
        link *        free_list;
        uint16_t      used{0};
        spin_mutex    mutex{};
        cluster_state state{cluster_state::in_use};
    };

}

#endif
