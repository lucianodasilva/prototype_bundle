#pragma once
#ifndef BINALLOC_UTILS_H
#define BINALLOC_UTILS_H

#include <atomic>
#include <bit>
#include <cstdint>
#include <type_traits>
#include <xmmintrin.h>

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

    struct addressable {

        [[nodiscard]] constexpr std::byte *address() const {
            return std::bit_cast<std::byte *>(this);
        }
    };

    /// Check if value is a power-of-two
    /// \tparam value_t integer type
    template <typename value_t>
    constexpr bool is_pow_2(value_t num) {
        static_assert(std::is_unsigned_v<value_t>, "is_pow_2 does not support signed data types!");
        return (num & (num - 1)) == 0;
    }

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

    /// Aligns an address to the nearest multiple of the alignment that precedes it
    /// \details Returns the value of address if it is already aligned
    /// \param address the address to align down
    /// \param alignment the alignment to align down to
    /// \return the aligned address
    /// \note This function does not check if the alignment is a power of two, so
    ///       it is the caller's responsibility to ensure that the alignment is a power of two
    inline std::byte *align_down(std::byte *address, uintptr_t alignment) {
        auto const alignment_mask = ~(alignment - 1);

        return std::bit_cast<std::byte *>(std::bit_cast<std::uintptr_t>(address) & alignment_mask);
    }

    /// Aligns an address to the nearest multiple of the alignment that follows it
    /// \details Returns the value of address if it is already aligned
    /// \param address the address to align up
    /// \param alignment the alignment to align up to
    /// \return the aligned address
    /// \note This function does not check if the alignment is a power of two, so
    ///       it is the caller's responsibility to ensure that the alignment is a power of two
    inline std::byte *align_up(std::byte *address, uintptr_t alignment) {
        auto const alignment_mask = ~(alignment - 1);

        return std::bit_cast<std::byte *>(
                (std::bit_cast<std::uintptr_t>(address) + alignment - 1) & alignment_mask);
    }

    /// Aligns an address to the nearest multiple of the alignment that follows it, excluding the address itself
    /// \param address the address to align up
    /// \param alignment the alignment to align up to
    /// \return the aligned address
    /// \note This function does not check if the alignment is a power of two, so
    ///         it is the caller's responsibility to ensure that the alignment is a power of two
    inline std::byte *align_up_exclusive(std::byte *address, uintptr_t alignment) {
        auto const alignment_mask = ~(alignment - 1);

        return std::bit_cast<std::byte *>(
                (std::bit_cast<std::uintptr_t>(address) + alignment) & alignment_mask);
    }

    /// get the address as a pointer to std::byte
    inline std::byte *as_ptr(std::uintptr_t address) noexcept {
        return std::bit_cast<std::byte *>(address);
    }

    /// get the address as an integer
    inline std::uintptr_t as_int(std::byte *address) noexcept {
        return std::bit_cast<std::uintptr_t>(address);
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
