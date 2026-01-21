#pragma once
#ifndef BINALLOC_UTILS_H
#define BINALLOC_UTILS_H

#include <atomic>
#include <bit>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <type_traits>
#include <xmmintrin.h>
#include <x86intrin.h>

#include "config.h"

namespace sgc2 {

    struct spin_mutex {
        void lock() noexcept {
            for(;;) {
                // Optimistically assume the lock is free on the first try
                if(!_lock.exchange(true, std::memory_order_acquire)) {
                    return;
                }
                // Wait for lock to be released without generating cache misses
                while(_lock.load(std::memory_order_relaxed)) {
                    // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                    // hyper-threads
                    _mm_pause();
                }
            }
        }

        bool try_lock() noexcept {
            // First do a relaxed load to check if lock is free in order to prevent
            // unnecessary cache misses if someone does while(!try_lock())
            return !_lock.load(std::memory_order_relaxed) &&
                    !_lock.exchange(true, std::memory_order_acquire);
        }

        void unlock() noexcept {
            _lock.store(false, std::memory_order_release);
        }

    private:
        std::atomic_bool _lock{false};
    };

    using unique_spin_lock = std::unique_lock<spin_mutex>;

    /// check if value is a multiple of another value
    /// \tparam value_t integer type
    template <typename value_t>
    constexpr bool is_multiple_of(value_t value, value_t multiple) {
        static_assert(std::is_unsigned_v<value_t>, "is_multiple_of does not support signed data types!");
        return (value % multiple) == 0;
    }

    /// Calculate the next multiple of a value or itself if it is already a multiple
    /// \tparam value_t integer type
    /// \param value the value to round up
    /// \param multiplier the multiple to round up to
    template <typename value_t>
    constexpr value_t next_multiple_of(value_t value, value_t multiplier) {
        static_assert(std::is_unsigned_v<value_t>, "next_multiple_of does not support signed data types!");
        return ((value + multiplier - 1) / multiplier) * multiplier;
    }


    using address_t = std::uintptr_t;
    constexpr address_t const null_address = 0;

    struct link {
        link *next{nullptr}; // pointer to the next item in the linked list
    };

    /// get the address as a pointer to std::byte
    inline std::byte *as_ptr(address_t const address) noexcept {
        return std::bit_cast<std::byte *>(address);
    }

    /// get the address as an integer
    address_t as_address (auto * const ptr) noexcept {
        return std::bit_cast<address_t>(ptr);
    }

    inline address_t align_down(address_t const address, uintptr_t const alignment) {
        auto const alignment_mask = ~(alignment - 1);
        return address & alignment_mask;
    }

    inline std::byte *align_down(std::byte *address, uintptr_t alignment) {
        return std::bit_cast<std::byte *>(
                align_down(std::bit_cast<std::uintptr_t>(address), alignment));
    }

    inline address_t align_up(address_t const address, uintptr_t const alignment) {
        auto const alignment_mask = ~(alignment - 1);
        return (address + alignment - 1) & alignment_mask;
    }

    inline std::byte *align_up(std::byte *address, uintptr_t alignment) {
        return std::bit_cast<std::byte *>(
                align_up(std::bit_cast<std::uintptr_t>(address), alignment));
    }

    inline address_t align_up_exclusive(address_t const address, uintptr_t const alignment) {
        auto const alignment_mask = ~(alignment - 1);
        return (address + alignment) & alignment_mask;
    }

    inline std::byte *align_up_exclusive(std::byte *address, uintptr_t alignment) {
        return std::bit_cast<std::byte *>(
                align_up_exclusive(std::bit_cast<std::uintptr_t>(address), alignment));
    }


    struct address_meta_t {
        address_t   cluster;
        address_t   page;
        address_t   page_meta;
        uint16_t    page_index;

        [[nodiscard]] struct cluster_meta * cluster_meta_as_ptr () const { return std::bit_cast < cluster_meta * >(cluster); }
        [[nodiscard]] struct page_meta * page_meta_as_ptr () const { return std::bit_cast < sgc2::page_meta * > (page_meta); }
    };

    constexpr address_meta_t address_meta (address_t cluster_address, uint16_t page_index) {
        auto const & cfg = config();

        return {
            .cluster = cluster_address,
            .page = cluster_address + cfg.page_size * (page_index + 1),
            .page_meta = cluster_address + cfg.page_meta_size,
            .page_index = page_index,
        };
    }

    constexpr address_meta_t address_meta (address_t const address) {
        auto const & cfg = config();

        auto const cluster_address = align_down(address, cfg.cluster_size);
        auto const page_index = static_cast < uint16_t > ((address - cluster_address) / cfg.page_size);

        return address_meta (cluster_address, page_index);
    }

    constexpr address_meta_t address_meta (struct page_meta const * pmeta) {
        auto const address = as_address(pmeta);
        auto const & cfg = config();

        auto const cluster_address = align_down(address, cfg.cluster_size);
        auto const page_index = static_cast < uint16_t > ((address - cluster_address) / cfg.page_meta_size);

        return address_meta (cluster_address, page_index);
    }

    constexpr int count_powers_of_two(std::uint32_t const lhv, std::uint32_t const rhv) {
        auto const start = std::ceil(std::log2(lhv));
        auto const end = std::floor(std::log2(rhv));
        return (end >= start) ? (end - start + 1) : 0;
    }

    /// page size in bytes
    /// \note This is the size of a memory page on the system, used for memory
    ///       allocation and deallocation. It is typically 4KB on most systems.
    std::size_t system_page_size();

    /// Reserve a memory region of the given size and alignment
    std::byte *reserve(std::size_t size, std::size_t alignment);

    /// Release a memory region that was previously reserved
    bool release(std::byte *address, std::size_t size);

    /// Commit a memory region to make it accessible for use
    bool commit(std::byte *address, std::size_t size);

    /// Decommit a memory region to make it inaccessible for use
    bool decommit(std::byte *address, std::size_t size);

}

#endif
