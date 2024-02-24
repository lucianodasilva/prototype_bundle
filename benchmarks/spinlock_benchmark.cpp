#include <benchmark/benchmark.h>
#include <ptbench.h>
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

// test with "real work use cases" for a more accurate comparison
template < typename mutex_t >
void run_push (std::vector < uint_fast32_t > & data, mutex_t & mtx) {
    std::lock_guard < mutex_t > const LOCK { mtx };
    data.push_back (ptbench::uniform (1000));
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

template < typename mutex_t >
void run_benchmark (benchmark::State& state) {
    std::vector < uint_fast32_t > data;
    mutex_t mtx;

    while (state.KeepRunning()) {
        data.clear ();

        ptbench::exec (
            {
                { [&]{ run_push (data, mtx); }, 25 },
                { [&]{ run_pop (data, mtx); }, 25 },
                { [&]{ run_browse (data, mtx); }, 75 }
            },
            state.range (0));
    }
}

#define MIN_ITERATION_RANGE 1 << 8U
#define MAX_ITERATION_RANGE 1 << 10U

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK (run_benchmark < std::mutex >)
    ->Name ("mutex - baseline")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

//BENCHMARK (run_benchmark < demo_a::spin_mutex >)
//    ->Name ("naive spin lock")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK (run_benchmark < demo_b::spin_mutex >)
    ->Name ("slock - relaxed memory order")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK (run_benchmark < demo_c::spin_mutex >)
    ->Name ("slock - expert")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);