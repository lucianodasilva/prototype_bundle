#include "ptbench.h"
#include "ptsystem.h"

#include <algorithm>
#include <atomic>
#include <random>
#include <condition_variable>
#include <stdexcept>
#include <thread>

namespace ptbench {

    uint_fast32_t uniform (uint_fast32_t distribution) {
        thread_local rnd_generator rnd_gen;
        return rnd_gen (distribution);
    }

    executor::executor (exec_policy const POLICY, std::size_t const THREAD_COUNT) :
        AFFINITY_LIST(core_affinity (POLICY, THREAD_COUNT)),
        POLICY (POLICY),
        THREAD_COUNT (AFFINITY_LIST.size ())
    {
        // initialize thread lanes
        for (auto const & CORE_AFFINITY : AFFINITY_LIST) {
            _lanes.emplace_back (lane_thread, std::reference_wrapper (*this), _start_sync.get(), _end_sync.get(), _run_token.get(), CORE_AFFINITY);
        }
    }

    executor::~executor () {
        // end execution
        this->_run_token->store (false);
        this->_start_sync->arrive_and_wait ();

        // join threads
        for (auto & lane : _lanes) {
            if (lane.joinable ()) {
                lane.join ();
            }
        }
    }

    void executor::dispatch (std::vector < task > const & tasks, std::size_t const ITERATIONS, std::function < void () > const & custom_main_thread_task) {
        this->_iterative_task = std::make_unique < random_iterative_task > (tasks, ITERATIONS / THREAD_COUNT);

        // notify lanes and wait for them to sync
        _start_sync->arrive_and_wait ();

        // if we have something to run on main thread, do it
        if (custom_main_thread_task) {
            custom_main_thread_task ();
        }

        // notify lanes to prepare for next task execution
        _end_sync->arrive_and_wait ();
    }


    std::vector < core_id_t > executor::core_affinity(exec_policy const POLICY, std::size_t const THREAD_COUNT) {
        auto VCORE_COUNT = std::thread::hardware_concurrency ();

        switch (POLICY) {
        case exec_policy::default_threading: {
            auto affinity_vector = std::vector ( THREAD_COUNT, UNDEFINED_CORE_ID );
            return affinity_vector;
        }
        case exec_policy::affinity_threading: {
            auto affinity_vector = std::vector ( THREAD_COUNT, UNDEFINED_CORE_ID );

            std::generate (affinity_vector.begin(), affinity_vector.end(), [VCORE_COUNT, n=0]() mutable {
                if (n == VCORE_COUNT) { n = 0; }
                return n++;
            });

            return affinity_vector;
        }
        case exec_policy::per_virtual_core_affinity: {
            auto affinity_vector = std::vector ( VCORE_COUNT, UNDEFINED_CORE_ID );
            std::generate (affinity_vector.begin(), affinity_vector.end(), [n = 0]() mutable { return n++; });

            return affinity_vector;
        }
        case exec_policy::per_physical_core_affinity: {
            return physical_cpu_cores ();
        }
        default:
            throw std::runtime_error ("Unknown execution policy");
        }
    }

    void executor::lane_thread (
        executor const & this_,
        barrier * start_sync,
        barrier * end_sync,
        std::atomic_bool const * RUN_TOKEN,
        core_id_t const CPU_ID
    ){
        if (CPU_ID != UNDEFINED_CORE_ID) {
            set_this_thread_afinity (CPU_ID);
        }

        while (true) {
            start_sync->arrive_and_wait ();

            if (!RUN_TOKEN->load ()) {
                break;
            }

            lane_phase (this_);

            end_sync->arrive_and_wait ();
        }
    }

    void executor::lane_phase (executor const & this_) {
        if (!this_._iterative_task) {
            return;
        }

        auto local_task = *this_._iterative_task;
        local_task();
    }

} // namespace ptbench
