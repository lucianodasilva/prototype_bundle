#pragma once
#ifndef PTBENCH_H
#define PTBENCH_H

#include <functional>
#include <cinttypes>
#include <thread>

namespace ptbench {

    using callable_t = std::function<void()>;

    struct action {
        callable_t      callable = nullptr;
        uint_fast32_t   probability = 0;
    };

    enum struct exec_policy {
        default_threading,          ///< no affinity set, create threads as set by the thread_count parameter
        affinity_threading,         ///< one thread as set by the thread_count parameter with affinity automatically set
        per_virtual_core_affinity,  ///< one thread per virtual core with affinity set per thread to a virtual core
        per_physical_core_affinity, ///< one thread per physical core with affinity set per thread to a physical core
    };

    uint_fast32_t uniform(uint_fast32_t dist);

    void exec (
        std::vector < action > const & actions,
        std::size_t iterations = 1000000,
        exec_policy policy = exec_policy::default_threading,
        std::size_t thread_count = std::thread::hardware_concurrency());
} // namespace ptbench

#endif //PTBENCH_H