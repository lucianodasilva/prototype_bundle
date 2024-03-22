#pragma once
#ifndef PTBENCH_H
#define PTBENCH_H

#include <cinttypes>
#include <emmintrin.h>
#include <functional>
#include <random>
#include <stdexcept>
#include <thread>

#include "ptsystem.h"

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

    struct barrier {
        static_assert (std::is_standard_layout_v< std::atomic_int32_t >, "std::atomic_int must be standard layout");

        explicit barrier (int const COUNT) :
            COUNTER_RESET_VAL (COUNT),
            _counter (COUNT)
        {}

        barrier (barrier const & ) = delete;
        barrier & operator = (barrier const & ) = delete;

        void arrive_and_wait () {
            auto const WAIT_PHASE = _phase.load ();
            auto wait_counter = (--_counter);

            if (wait_counter > 0) { // if we are waiters... wait
                while (WAIT_PHASE == _phase.load () && wait_counter > 0) { // NOLINT
                    futex_wait (reinterpret_cast < int32_t * > (&_counter), wait_counter);
                    wait_counter = _counter.load();
                }
            } else if (wait_counter == 0) {
                // complete barrier phase
                ++_phase;
                _counter.store(COUNTER_RESET_VAL);

                futex_wake_all (reinterpret_cast < int32_t * > (&_counter));
            } else {
                throw std::runtime_error ("Barrier is in invalid state");
            }
        }

    private:
        int32_t const                   COUNTER_RESET_VAL;
        std::atomic_int32_t             _counter {0};
        std::atomic_int32_t             _phase {0};
    };

    struct rnd_generator {
        uint_fast32_t operator() (uint_fast32_t const DIST) const {
            return _gen() % DIST;
        }
    private:
        mutable std::mt19937 _gen {std::random_device {} ()};
    };

    uint_fast32_t uniform(uint_fast32_t dist);


    using task_callback_t = std::function < void (void) >;

    struct task {
        task_callback_t callback;
        int             probability;
    };

    enum struct exec_policy {
        default_threading,          ///< no affinity set, create threads as set by the thread_count parameter
        affinity_threading,         ///< one thread as set by the thread_count parameter with affinity automatically set
        per_virtual_core_affinity,  ///< one thread per virtual core with affinity set per thread to a virtual core
        per_physical_core_affinity, ///< one thread per physical core with affinity set per thread to a physical core
    };

    struct random_iterative_task {

        explicit random_iterative_task (std::vector < task > tasks, std::size_t const ITERATIONS) :
            TASKS (std::move(tasks)),
            DISTRIBUTION { std::accumulate (TASKS.begin(), TASKS.end(), uint_fast32_t {}, [](auto const & acc, auto const & task) {
                return acc + task.probability;
            })},
            ITERATIONS(ITERATIONS)
        {}

        void operator ()() const {
            for (std::size_t i = 0; i < ITERATIONS; ++i) {
                auto const IDX = GEN(DISTRIBUTION);
                auto dist_sum = 0;

                for (auto const & [callback, probability] : TASKS) {
                    dist_sum += probability;

                    if (IDX < dist_sum) {
                        callback();
                        break;
                    }
                }
            }
        }

    private:
        std::vector < task > const  TASKS;
        rnd_generator const         GEN;
        uint_fast32_t const         DISTRIBUTION;
        std::size_t const           ITERATIONS;
    };

    struct executor {
        explicit executor (exec_policy POLICY = exec_policy::default_threading, std::size_t THREAD_COUNT = std::thread::hardware_concurrency());
        ~executor();

        void dispatch(std::vector < task > const & tasks, std::size_t ITERATIONS = 1000000, std::function < void () > const & custom_main_thread_task = nullptr);

        [[nodiscard]] std::size_t thread_count () const { return THREAD_COUNT; }

        [[nodiscard]] static std::vector < core_id_t > core_affinity (exec_policy POLICY, std::size_t THREAD_COUNT);
    private:

        static void lane_thread (
            executor const & this_,
            barrier * start_sync,
            barrier * end_sync,
            std::atomic_bool
            const * RUN_TOKEN,
            core_id_t CPU_ID);

        static void lane_phase (executor const & this_);

        std::vector < core_id_t > const             AFFINITY_LIST;
        exec_policy const                           POLICY;
        std::size_t const                           THREAD_COUNT;

        std::unique_ptr < barrier >                 _start_sync = std::make_unique < barrier > (THREAD_COUNT + 1);
        std::unique_ptr < barrier >                 _end_sync = std::make_unique < barrier > (THREAD_COUNT + 1);
        std::unique_ptr < std::atomic_bool >        _run_token = std::make_unique < std::atomic_bool > (true);
        std::unique_ptr < random_iterative_task >   _iterative_task;
        std::vector < std::thread >                 _lanes;
    };
} // namespace ptbench

#endif //PTBENCH_H