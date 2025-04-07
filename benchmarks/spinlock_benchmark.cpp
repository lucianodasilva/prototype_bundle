#include <benchmark/benchmark.h>
#include <las/las.h>
#include <las/test/concurrent_stress_tester.hpp>
#include <mutex>
#include <immintrin.h>

namespace demo_a {
    struct spin_mutex {
    public:

        void lock() {
            while (_flag.test_and_set()) {}
        }

        void unlock() {
            _flag.clear();
        }

    private:
        std::atomic_flag _flag = ATOMIC_FLAG_INIT;
    };
}


namespace demo_b {
    struct spin_mutex {
    public:

        void lock() {
            // optimistically assume the lock is free on the first try
            if (try_lock ()) {
                return;
            }

            // wait for lock to be released without generating cache misses
            while (_flag.test_and_set(std::memory_order_relaxed)) {
                _mm_pause();
            }
        }

        bool try_lock() {
            return !_flag.test_and_set (std::memory_order_acquire);
        }

        void unlock() {
            _flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag _flag = ATOMIC_FLAG_INIT;
    };


    struct shared_spin_mutex {
    public:

        void lock() {
            for (;;) {
                // Optimistically assume the lock is free on the first try
                if (intptr_t expected = 0; !_flag.compare_exchange_weak(expected, -1, std::memory_order_acquire)) {
                    return;
                }

                // Wait for lock to be released without generating cache misses
                while (_flag.load(std::memory_order_relaxed) != 0) {
                    _mm_pause();
                }
            }
        }

        /// tries to lock the mutex
        /// \return true if the lock was acquired, false otherwise
        [[nodiscard]] bool try_lock() noexcept {
            intptr_t expected = 0;
            return _flag.compare_exchange_weak(expected, -1, std::memory_order_acquire);
        }

        /// unlocks the mutex
        void unlock() noexcept {
            intptr_t expected = -1;
            _flag.compare_exchange_weak(expected, 0, std::memory_order_release);
        }

        void shared_lock () {
            for (;;) {
                intptr_t expected = _flag.load (std::memory_order_relaxed);

                if (expected >= 0 && _flag.compare_exchange_weak (expected, expected + 1, std::memory_order_acquire)) {
                    return;
                }

                // Wait for lock to be released without generating cache misses
                while (_flag.load(std::memory_order_relaxed) < 0) {
                    _mm_pause();
                }
            }
        }

        void shared_unlock () {
            for (;;) {
                intptr_t expected = _flag.load (std::memory_order_relaxed);

                if (expected <= 0) {
                    return;
                }

                if (expected > 0 && _flag.compare_exchange_weak (expected, expected - 1, std::memory_order_acquire)) {
                    return;
                }
            }
        }

    private:
        std::atomic_intptr_t _flag {0};
    };
}

namespace demo_c {

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

}

namespace demo_futex {

    struct futex {
        static_assert (std::is_standard_layout_v< std::atomic_int32_t >, "std::atomic_int must be standard layout");

        void lock() noexcept {
            for (;;) {
                // lets try the optimistic trick
                if (_flag.atomic.exchange(1, std::memory_order_acquire) == 0) {
                    return;
                }

                // try and relock on futex awake
                while (_flag.atomic.load(std::memory_order_relaxed) == 1) {
                    las::futex_wait(&_flag.integer, 1);
                }
            }
        }

        bool try_lock() noexcept {
            return _flag.atomic.exchange(1, std::memory_order_acquire) == 0;
        }

        void unlock() noexcept {
            _flag.atomic.store(0, std::memory_order_release);
            las::futex_wake_one(&_flag.integer);
        }

    private:
        union {
            std::atomic_int32_t atomic;
            int32_t             integer;
        } _flag {0};
        
    };
}

// test with "real work use cases" for a more accurate comparison
template < typename mutex_t >
void run_push (std::vector < uint_fast32_t > & data, mutex_t & mtx) {
    std::lock_guard < mutex_t > const LOCK { mtx };
    data.push_back (las::test::uniform (1000));
}

template < typename mutex_t >
void run_pop (std::vector < uint_fast32_t > & data, mutex_t & mtx) {
    std::lock_guard < mutex_t > const LOCK { mtx };

    if (!data.empty ()) {
        data.pop_back ();
    }
}

template < typename mutex_t >
void run_browse (std::vector < uint_fast32_t > & data, mutex_t & mtx) {
    std::lock_guard < mutex_t > const LOCK { mtx };

    if (!data.empty ()) {
        for (auto & item : data) {
            benchmark::DoNotOptimize (item);
        }
    }
}

las::test::concurrent_stress_tester stresser {};

template < typename mutex_t >
void run_benchmark (benchmark::State& state) {
    std::vector < uint_fast32_t > data;
    mutex_t mtx;

    while (state.KeepRunning()) {
        data.clear ();

        stresser.dispatch (
            {
                { [&]{ run_push (data, mtx); }, 25 },
                { [&]{ run_pop (data, mtx); }, 25 },
                { [&]{ run_browse (data, mtx); }, 75 }
            },
            state.range (0));
    }
}

#define MIN_ITERATION_RANGE 1 << 16U
#define MAX_ITERATION_RANGE 1 << 20U

#define MY_BENCHMARK(func, name) BENCHMARK((func))->Range (MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)->Name(name)->Unit(benchmark::TimeUnit::kMillisecond)

MY_BENCHMARK(run_benchmark < std::mutex >, "mutex - baseline");
MY_BENCHMARK(run_benchmark < demo_b::spin_mutex >, "spin (expert)");
MY_BENCHMARK(run_benchmark < demo_futex::futex >, "futex");