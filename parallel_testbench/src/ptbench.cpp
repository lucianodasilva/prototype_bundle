#include "ptbench.h"
#include "ptsystem.h"

#include <algorithm>
#include <atomic>
#include <random>
#include <condition_variable>
#include <emmintrin.h>
#include <thread>

namespace ptbench {

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

    struct latch {
    public:
        latch () = default;
        explicit latch (int const COUNT) :
            _counter (COUNT)
        {}

        void count_down() {
            if (--_counter != 0) {
                return;
            }

            futex_wake_all (reinterpret_cast < futex_t * > (&_counter));
        }

        bool try_wait () {
            if (_counter.load () == 0) {
                return true;
            }

            futex_wait (reinterpret_cast< futex_t * > (&_counter));

            return _counter.load () == 0;
        }

        void wait () {
            while (try_wait () == false) {
                _mm_pause();
            }
        }

        void arrive_and_wait () {
            this->count_down ();
            this->wait ();
        }

    private:
        std::atomic_uint32_t _counter;
    };

    struct rnd_generator {
    public:
        uint_fast32_t operator() (uint_fast32_t const DIST) {
            return _gen() % DIST;
        }

    private:
        std::mt19937 _gen {std::random_device {} ()};
    };

    uint_fast32_t uniform (uint_fast32_t distribution) {
        static rnd_generator rnd_gen;
        return rnd_gen (distribution);
    }

    void exec_thread (latch * sync, std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS) {

        if (sync == nullptr) {
            return;
        }

        sync->arrive_and_wait ();

        for (std::size_t i = 0; i < ITERATIONS; ++i) {
            auto const index = uniform (DISTR);

            // search for next action
            uint_fast32_t accum = 0;
            for (auto const & [ callable, probability ] : ACTIONS) {
                accum += probability;

                if (index < accum) {
                    callable ();
                    break;
                }
            }
        }
    }

    void exec_thread_affinity (latch * sync, std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS, cpu_id_t core_id) {
        set_this_thread_afinity (core_id);
        exec_thread (sync, ACTIONS, DISTR, ITERATIONS);
    }

    void exec_actions (std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS, std::size_t THREAD_COUNT) {

        std::vector < std::thread > threads;

        auto sync = std::make_unique < latch > (static_cast < int >(THREAD_COUNT));

        // for each core (minus "this one")
        for (std::size_t i = 1; i < THREAD_COUNT; ++i) {
            threads.emplace_back (
                exec_thread,
                sync.get(),
                ACTIONS,
                DISTR,
                ITERATIONS);
        }

        // execute on current thread
        exec_thread (sync.get(), ACTIONS, DISTR, ITERATIONS);

        // join threads
        for (auto & th : threads) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    void exec_actions_with_affinity (std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS, std::vector < cpu_id_t > const & CORES) {
        std::vector < std::thread > threads;

        if (CORES.empty ()) {
            return;
        }

        auto sync = std::make_unique < latch > (static_cast < int > (CORES.size()));

        // for each core (minus "this one")
        for (std::size_t i = 1; i < CORES.size(); ++i) {
            threads.emplace_back (
                exec_thread_affinity,
                sync.get(),
                ACTIONS,
                DISTR,
                ITERATIONS,
                CORES[i]);
        }

        // execute on current thread
        exec_thread_affinity (sync.get(), ACTIONS, DISTR, ITERATIONS, CORES[0]);

        // join threads
        for (auto & th : threads) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    void exec (std::vector < action > const & actions, std::size_t const ITERATIONS, exec_policy const POLICY, std::size_t const THREAD_COUNT) {
        // calculate distribution range
        uint_fast32_t dist_range = 0;

        for (auto const & [ _, probability ] : actions) {
            dist_range += probability;
        }

        // generate thread pool according to policy
        std::vector < std::thread > threads;

        switch (POLICY) {
            case exec_policy::default_threading: {
                exec_actions (actions, dist_range, ITERATIONS, THREAD_COUNT);
                break;
            }
            case exec_policy::affinity_threading: {
                auto CORE_COUNT = std::thread::hardware_concurrency ();

                auto cores = std::vector < cpu_id_t > ( THREAD_COUNT, 0 );

                std::generate (cores.begin(), cores.end(), [CORE_COUNT, n=0]() mutable {
                    if (n == CORE_COUNT) {
                        n = 0;
                    }

                    return n++;
                });

                exec_actions_with_affinity (actions, dist_range, ITERATIONS, cores);
                break;
            }
            case exec_policy::per_virtual_core_affinity: {
                auto const CORE_COUNT = std::thread::hardware_concurrency ();

                auto cores = std::vector < cpu_id_t > ( CORE_COUNT, 0 );
                std::generate (cores.begin(), cores.end(), [n = 0]() mutable { return n++; });

                exec_actions_with_affinity (actions, dist_range, ITERATIONS, cores);
                break;
            }
            case exec_policy::per_physical_core_affinity: {
                auto const CORES = physical_cpu_cores ();
                exec_actions_with_affinity (actions, dist_range, ITERATIONS, CORES);
                break;
            }
        }
    }

} // namespace ptbench
