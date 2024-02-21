#include "ptbench.h"
#include "ptsystem.h"

#include <algorithm>
#include <atomic>
#include <random>
#include <thread>

namespace ptbench {
    struct rnd_generator {
    public:
        uint_fast32_t operator() (uint_fast32_t const DIST) {
            return _gen() % DIST;
        }

    private:
        std::mt19937                            _gen {std::random_device {} ()};
    };

    uint_fast32_t uniform (uint_fast32_t distribution) {
        static rnd_generator rnd_gen;
        return rnd_gen (distribution);
    }

    void exec_thread (std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS) {
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

    void exec_thread_affinity (std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS, cpu_id_t core_id) {
        set_this_thread_afinity (core_id);
        exec_thread (ACTIONS, DISTR, ITERATIONS);
    }

    void exec_actions (std::vector < action > const & ACTIONS, uint_fast32_t const DISTR, std::size_t const ITERATIONS) {
        auto const CORES = std::thread::hardware_concurrency ();
        std::vector < std::thread > threads;

        // for each core (minus "this one")
        for (std::size_t i = 1; i < CORES; ++i) {
            threads.emplace_back (
                exec_thread,
                ACTIONS,
                DISTR,
                ITERATIONS).detach ();
        }

        // execute on current thread
        exec_thread (ACTIONS, DISTR, ITERATIONS);

        // sync threads
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

        // for each core (minus "this one")
        for (std::size_t i = 1; i < CORES.size(); ++i) {
            threads.emplace_back (
                exec_thread_affinity,
                ACTIONS,
                DISTR,
                ITERATIONS,
                CORES[i]).detach ();
        }

        // execute on current thread
        exec_thread_affinity (ACTIONS, DISTR, ITERATIONS, CORES[0]);

        // sync threads
        for (auto & th : threads) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    void exec (std::vector < action > const & actions, std::size_t const ITERATIONS, exec_policy const POLICY) {
        // calculate distribution range
        uint_fast32_t dist_range = 0;

        for (auto const & [ _, probability ] : actions) {
            dist_range += probability;
        }

        // generate thread pool according to policy
        std::vector < std::thread > threads;

        switch (POLICY) {
            case exec_policy::per_virtual_core_default: {
                exec_actions (actions, dist_range, ITERATIONS);
                break;
            }
            case exec_policy::per_virtual_core_with_affinity: {
                auto const CORE_COUNT = std::thread::hardware_concurrency ();

                std::vector < cpu_id_t > cores { CORE_COUNT };
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
