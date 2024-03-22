#include "ptsystem.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <string>
#include <thread>

namespace ptbench {

#if defined (PT_OS_GNU_LINUX)
    std::vector < core_id_t > physical_cpu_cores () {
        std::vector < core_id_t > cores;

        auto const CORE_COUNT = std::thread::hardware_concurrency();

        for (std::size_t i = 0; i < CORE_COUNT; ++i) {
            auto const CORE_PATH =
                std::filesystem::path ("/sys/devices/system/cpu/cpu" + std::to_string (i) + "/topology/thread_siblings_list");

            if (exists(CORE_PATH)) {
                std::ifstream filestream (CORE_PATH, std::ios::binary);

                if (!filestream) {
                    continue;
                }

                std::string content (std::istreambuf_iterator {filestream}, {});

                if (content.empty ()) {
                    continue;
                }

                auto it = std::find (content.begin(), content.end (), ',');
                auto const ID = std::stoul(std::string (content.begin (), it));

                if (i == ID) {
                    cores.push_back (i);
                }
            }
        }

        return cores;
    }

    void set_native_thread_afinity (std::thread::native_handle_type thread_handle, core_id_t core_id) {
        cpu_set_t cpuset;

        CPU_ZERO (&cpuset);
        CPU_SET (core_id, &cpuset);

        pthread_setaffinity_np (thread_handle, sizeof (cpu_set_t), &cpuset);
    }

    std::thread::native_handle_type this_thread_native_handle () {
        return pthread_self ();
    }

#endif

    void set_thread_afinity (std::thread & thread, core_id_t core_id) {
        set_native_thread_afinity (thread.native_handle (), core_id);
    }

    void set_this_thread_afinity (core_id_t core_id) {
        set_native_thread_afinity (this_thread_native_handle(), core_id);
    }
}