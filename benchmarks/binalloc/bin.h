#pragma once
#ifndef BINALLOC_BIN_H
#define BINALLOC_BIN_H

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "page.h"

namespace sgc2 {

    struct bin_store {

        bin_store();

        ~bin_store() = default; // TODO: clean up remaining bins on thread exit

        void *alloc(size_t size);

        void free(void *address);

        static bin_store &this_thread_store();

    private:
        constexpr static std::size_t bin_index(std::size_t size);

        static constexpr std::size_t      NO_BIN_INDEX = std::numeric_limits<std::size_t>::max();
        std::unique_ptr<page::header *[]> _bins;
    };

}

#endif
