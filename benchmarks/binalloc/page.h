#pragma once
#ifndef BINALLOC_PAGE_H
#define BINALLOC_PAGE_H

#include <span>

#include "config.h"
#include "utils.h"

namespace sgc2 {

    struct page_meta {
        [[nodiscard]] constexpr bool is_unused() const noexcept {
            return used == 0;
        }

        unique_spin_lock lock();

        void transfer_to(link *&head);

        void free(address_t address);

        static page_meta *make(address_t address, uint8_t bin_index);

        static page_meta *owning(address_t address);

        address_t page() const;

        page_meta *   next{nullptr};
        page_meta *   prev{nullptr};
        link *        free_list{nullptr};
        uint16_t      used{0};
        uint8_t const bin_index{0};
        spin_mutex    mutex{};
        bool          is_tethered{true};
    };

}

#endif
