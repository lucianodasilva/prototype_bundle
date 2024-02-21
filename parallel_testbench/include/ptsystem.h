#pragma once
#ifndef PTSYSTEM_H
#define PTSYSTEM_H

#include <thread>
#include <vector>

namespace ptbench {

    using cpu_id_t = std::size_t;

    std::vector < cpu_id_t > physical_cpu_cores ();

    void set_thread_afinity (std::thread & thread, cpu_id_t core_id);
    void set_this_thread_afinity (cpu_id_t core_id);
}

#endif //PTSYSTEM_H
