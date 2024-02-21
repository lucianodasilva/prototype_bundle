#pragma once
#ifndef PTBENCH_H
#define PTBENCH_H

#include <functional>
#include <cinttypes>

namespace ptbench {

    using callable_t = std::function<void()>;

    struct action {
        callable_t      callable = nullptr;
        uint_fast32_t   probability = 0;
    };

    enum struct exec_policy {
        per_virtual_core_default,
        per_virtual_core_with_affinity,
        per_physical_core_affinity
    };

    uint_fast32_t uniform(uint_fast32_t dist);

    void exec (std::vector < action > const & actions, std::size_t iterations = 1000000, exec_policy policy = exec_policy::per_virtual_core_default);

} // namespace ptbench

#endif //PTBENCH_H