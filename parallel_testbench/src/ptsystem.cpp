#include "ptsystem.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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
#else // PT_OS_WINDOWS
    std::vector < core_id_t > physical_cpu_cores() {
        std::vector < core_id_t > cores;

        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = nullptr;
        DWORD length_in_bytes = 0;

        // get size of buffer;
        if (FAILED(GetLogicalProcessorInformation(
            nullptr,
            &length_in_bytes)))
        {
            std::cerr << "get cpu physical cores failed" << std::endl;
            return {};
        }

        buffer = reinterpret_cast <PSYSTEM_LOGICAL_PROCESSOR_INFORMATION> (malloc(length_in_bytes));

        if (!buffer) {
            std::cerr << "get cpu physical cores allocation failed" << std::endl;
            return {};
        }

        if (FAILED(GetLogicalProcessorInformation(
            buffer,
            &length_in_bytes)))
        {
            std::cerr << "get cpu physical cores failed" << std::endl;
            return {};
        }

        auto* it = buffer;
        auto* end = it + length_in_bytes / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

        while (it < end) {
            if (it->Relationship == RelationProcessorCore) {
                unsigned long id;

                if (BitScanForward64(&id, it->ProcessorMask)) {
                    cores.push_back(id);
                }
            }

            ++it;
        }

        free(buffer);

        return cores;
	}

    void set_native_thread_afinity(std::thread::native_handle_type thread_handle, core_id_t core_id) {
		SetThreadAffinityMask (thread_handle, DWORD_PTR(1 << core_id));
	}

    std::thread::native_handle_type this_thread_native_handle() {
		return GetCurrentThread ();
	}
#endif

    void set_thread_afinity (std::thread & thread, core_id_t core_id) {
        set_native_thread_afinity (thread.native_handle (), core_id);
    }

    void set_this_thread_afinity (core_id_t core_id) {
        set_native_thread_afinity (this_thread_native_handle(), core_id);
    }
}