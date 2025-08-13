#include <benchmark/benchmark.h>
#include <atomic>
#include <mutex>
#include <x86gprintrin.h>
#include <xmmintrin.h>
#include <las/test/concurrent_stress_tester.hpp>

namespace bitmap {

    struct spin_mutex {

        void lock() noexcept {
            for (;;) {
                // Optimistically assume the lock is free on the first try
                if (!_lock.exchange(true, std::memory_order_acquire)) {
                    return;
                }
                // Wait for lock to be released without generating cache misses
                while (_lock.load(std::memory_order_relaxed)) {
                    // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                    // hyper-threads
                    // __builtin_ia32_pause();

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
        std::atomic_bool _lock { false };
    };

    struct map {
        constexpr static std::size_t NO_BLOCK = std::numeric_limits<std::size_t>::max();
        std::uint64_t data{std::numeric_limits<uint64_t>::max()};
        spin_mutex mutex;

        std::size_t pop() {
            auto lock = std::unique_lock(mutex);

            auto index = _bit_scan_forward(data);

            if (index == 64) [[unlikely]] {
                return NO_BLOCK; // no free blocks
            }

            data &= ~(uint64_t{1} << index); // clear the bit

            return index;
        }
    };

}

namespace stack {

    struct block {
        block *next{nullptr};
    };

    struct stack {
        std::atomic<block *> head;

        block data [64];

        stack () {
            for (int i = 0; i < 63; ++i) {
                data[i].next = &data[i + 1];
            }

            data [63].next = nullptr;

            head.store(data, std::memory_order_relaxed);
        }

        block *pop() {
            block *old_head = head.load(std::memory_order_relaxed);
            while(old_head && !head.compare_exchange_weak(old_head, old_head->next, std::memory_order_acquire, std::memory_order_relaxed)) {
                // do nothing
                _mm_pause();
            }
            return old_head;
        }
    };

}

las::test::concurrent_stress_tester stresser{};

template <typename impl_t>
void run_benchmark(benchmark::State &state) {

    while(state.KeepRunning()) {
        state.PauseTiming();
        std::vector < impl_t > impls(state.range(0));
        std::atomic_uint64_t index = 0;

        auto run_test = [&]() {
            auto idx = index.fetch_add(1, std::memory_order_relaxed);

            for (int i = 0; i < 64; ++i) {
                impls[idx].pop ();
            }
        };

        state.ResumeTiming();

        stresser.dispatch(
                {
                        { run_test, 1},
                },
                state.range(0));
    }
}

#define MIN_ITERATION_RANGE 1 << 12U
#define MAX_ITERATION_RANGE 1 << 16U

#define MY_BENCHMARK(func, name) BENCHMARK((func))->Range (MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)->Name(name)->Unit(benchmark::TimeUnit::kMillisecond)

MY_BENCHMARK(run_benchmark < bitmap::map >, "bitmap");
MY_BENCHMARK(run_benchmark < stack::stack >, "stack");
