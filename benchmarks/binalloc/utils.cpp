#include "utils.h"

#ifdef __linux
#include <zconf.h>
#include <sys/mman.h>

namespace sgc2 {

    size_t const page_size = [] {
        static auto const size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        return size * 8; // let's make it a "software" large page of sorts
    }();

    std::byte *reserve(size_t size, size_t alignment) {
        if(!alignment) {
            alignment = page_size;
        }

        size_t padded_size = size + (alignment - page_size);

        auto *address = static_cast<std::byte *>(
            mmap(
                    nullptr,
                    padded_size,
                    PROT_NONE,
                    MAP_ANON | MAP_PRIVATE,
                    -1,
                    0));

        if(!address) {
            return nullptr;
        }

        // align address
        auto *const aligned_address = align_up(address, alignment);

        // check if we need to adjust the allocation to be aligned
        auto const starting_pad = aligned_address - address;

        if(starting_pad != 0) {
            munmap(address, starting_pad);
        }

        auto const ending_pad = padded_size - (starting_pad + size);

        if(ending_pad != 0)
            munmap(aligned_address + size, ending_pad);

        return aligned_address;
    }

    bool release(std::byte *address, size_t size) {
        return munmap(address, size) == 0;
    }

    bool commit(std::byte *address, size_t size) {
        return mprotect(address, size, PROT_WRITE | PROT_READ) == 0;
    }

    bool decommit(std::byte *address, size_t size) {
        return mprotect(address, size, PROT_NONE) == 0;
    }
}

#endif

#ifdef _WIN32

#include "sgc/system.h"
#include "sgc/gc/utils.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace sgc {
    std::size_t page_size() {
        static auto const size =
                [] -> std::size_t {
                    SYSTEM_INFO info;
                    GetSystemInfo(&info);
                    return info.dwPageSize;
                }()

        return size;
    };

    std::byte *reserve(size_t size, size_t alignment) {
        if(alignment == 0)
            alignment = page_size;

        auto aligned_size =
                size + (alignment - page_size);

        std::byte *address{nullptr};
        uint8_t    retry{255};

        do {
            address = reinterpret_cast<std::byte *>(
                // if too many collisions are detected, enabling mem_top_down should reduce collision count
                ::VirtualAlloc(nullptr, aligned_size, MEM_RESERVE /*| MEM_TOP_DOWN*/, PAGE_READWRITE));

            if(!address)
                return nullptr;

            std::byte *aligned_address = reinterpret_cast<std::byte *>(
                (reinterpret_cast<uintptr_t>(address) + (alignment - 1)) & ~(alignment - 1));

            if(aligned_address != address) {
                ::VirtualFree(address, 0, MEM_RELEASE);
                address = reinterpret_cast<std::byte *>(
                    ::VirtualAlloc(aligned_address, size, MEM_RESERVE, PAGE_READWRITE));
            } else {
                address = aligned_address;
            }

            --retry;
        } while(address == nullptr && retry > 0);

        return address;
        //TODO: Throw on bad address
    }

    bool release(std::byte *address, size_t) {
        return ::VirtualFree(address, 0, MEM_RELEASE) == TRUE;
    }

    bool commit(std::byte *address, size_t byte_length) {
        return ::VirtualAlloc(address, byte_length, MEM_COMMIT, PAGE_READWRITE)
                == address;
    }

    bool decommit(std::byte *address, size_t) {
        return ::VirtualFree(address, 0, MEM_DECOMMIT) == TRUE;
    }
}

#endif
