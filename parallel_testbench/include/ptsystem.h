#pragma once
#ifndef PTSYSTEM_H
#define PTSYSTEM_H

#include "ptconfig.h"

#include <atomic>
#include <thread>
#include <vector>

#if defined (PT_OS_GNU_LINUX)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ptbench {

#if defined (PT_OS_GNU_LINUX)
    using futex_t = uint32_t;

    inline bool futex_wait (futex_t * address) {
        return syscall (SYS_futex, address, FUTEX_WAIT_PRIVATE, *address, nullptr, nullptr, 0) == 0;
    }

    inline void futex_wake_all (futex_t * address, std::size_t waiters = std::numeric_limits<int>::max()) {
        syscall (SYS_futex, address, FUTEX_WAKE_PRIVATE, waiters, nullptr, nullptr, 0);
    }
#endif

    using cpu_id_t = std::size_t;

    std::vector < cpu_id_t > physical_cpu_cores ();

    void set_thread_afinity (std::thread & thread, cpu_id_t core_id);
    void set_this_thread_afinity (cpu_id_t core_id);
}

#endif //PTSYSTEM_H
