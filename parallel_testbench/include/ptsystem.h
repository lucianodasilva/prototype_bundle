#pragma once
#ifndef PTSYSTEM_H
#define PTSYSTEM_H

#include "ptconfig.h"

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

#if defined (PT_OS_GNU_LINUX)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ptbench {

    using futex_t = int32_t;

#if defined (PT_OS_GNU_LINUX)
    inline bool futex_wait (futex_t * address, futex_t expected) {
        return syscall (SYS_futex, address, FUTEX_WAIT_PRIVATE, expected, nullptr, nullptr, 0) == 0;
    }

    inline void futex_wake_all (futex_t * address) {
        futex_wake (address, std::numeric_limits<int>::max());
    }

    inline void futex_wake_one (futex_t * address) {
        futex_wake (address, 1);
    }
#else
    inline void futex_wait(futex_t* address, futex_t expected) {
		WaitOnAddress (address, &expected, sizeof (futex_t), INFINITE);
	}

    inline void futex_wake_all(futex_t* address) {
		WakeByAddressAll(address);
	}

    inline void futex_wake_one(futex_t* address) {
		WakeByAddressSingle(address);
	}
#endif

    using core_id_t = std::size_t;
    constexpr core_id_t UNDEFINED_CORE_ID = std::numeric_limits<core_id_t>::max();

    std::vector < core_id_t > physical_cpu_cores ();

    std::thread::native_handle_type this_thread_native_handle ();

    void set_thread_afinity (std::thread & thread, core_id_t core_id);
    void set_this_thread_afinity (core_id_t core_id);
}

#endif //PTSYSTEM_H
