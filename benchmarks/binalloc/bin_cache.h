#pragma once
#ifndef BINALLOC_BIN_H
#define BINALLOC_BIN_H

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

#include "page.h"

namespace sgc2 {
    void * bin_alloc (std::size_t size);
    void   bin_free  (void * ptr);
}

#endif
