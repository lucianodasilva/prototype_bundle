#include <strings.h>
#include <benchmark/benchmark.h>
#include <bits/atomic_base.h>

#include "bin.h"
#include "stack.h"

#include <las/test/concurrent_stress_tester.hpp>

// #define MULTITHREADING 1

/// random number generator
/// \note this is a simple random number generator that wraps std::mt19937 and provides a uniform distribution
struct uniform_generator {
    uint_fast64_t operator()(uint_fast64_t const DIST) const {
        return _gen() % DIST;
    }

private:
    mutable std::mt19937_64 _gen{std::random_device{}()};
};

/// generate a random number using a uniform distribution within the range
/// \param dist distribution
/// \return a random number from zero to dist
uint_fast64_t uniform(uint_fast64_t dist) {
    return uniform_generator()(dist);
}

/// generate a random number using a uniform distribution from low to high
/// \param low lower bound for the uniform distribution
/// \param high higher bound for the uniform distribution
/// \return a random number from low to high
inline uint_fast64_t uniform(uint_fast64_t low, uint_fast64_t high) {
    if(high < low) {
        std::swap(low, high);
    }

    return low + uniform(high - low);
}

template <typename type>
concept allocator = requires(std::size_t size, void *ptr) {
    { type::alloc(size) } -> std::same_as<void *>;
    { type::free(ptr) };
};

struct sgc2_alloc {
    static void *alloc(std::size_t size) {
        return sgc2::bin_store::this_thread_store().alloc(size);
    }

    static void free(void *ptr) {
        sgc2::bin_store::this_thread_store().free(ptr);
    }
};

struct system_alloc {
    static void *alloc(std::size_t size) {
        return std::malloc(size);
    }

    static void free(void *ptr) {
        return std::free(ptr);
    }
};

las::test::concurrent_stress_tester stresser{};

template <allocator alloc_t>
void mixed_benchmark(benchmark::State &state) {
    auto objects = std::make_unique<std::atomic<std::byte *>[]>(state.range(0));
    std::fill_n(objects.get(), state.range(0), nullptr);

    auto do_replace = [&]() {
        auto const index = uniform(state.range(0));
        auto       size  = uniform(8, 1024);

        auto *ptr = alloc_t::alloc(size);

        if(auto *other = objects[index].exchange(std::bit_cast<std::byte *>(ptr))) {
            alloc_t::free(other);
        }
    };

    while(state.KeepRunning()) {
        #ifdef MULTITHREADING
        stresser.dispatch(
                {{[&] { do_replace(); }, 100}},
                state.range(0));
        #else
        for (std::size_t i = 0; i < state.range(0); ++i) {
            do_replace();
        }
        #endif
    }

    // clean stuff up
    for(int i = 0; i < state.range(0); i++) {
        if(auto *ptr = objects[i].load(std::memory_order::relaxed)) {
            alloc_t::free(ptr);
        }
    }
}

template <allocator alloc_t>
void alloc_benchmark(benchmark::State &state) {
    for(auto _: state) {
        auto objects = std::make_unique<std::byte *[]>(state.range(0));
        std::fill_n(objects.get(), state.range(0), nullptr);

        std::atomic_uintmax_t index = 0;

        auto test = [&] {
            auto i     = index.fetch_add(1, std::memory_order::relaxed);
            auto size = las::test::uniform(8, 1024);
            objects[i] = std::bit_cast<std::byte *>(alloc_t::alloc(size));
        };

        state.ResumeTiming();

        #if defined (MULTITHREADING)
        stresser.dispatch(
                {{test, 100}},
                state.range(0));
        #else
        while (index < state.range(0)) {
            test ();
        }
        #endif

        state.PauseTiming();

        /// cleanup
        for(int i = 0; i < state.range(0); i++) {
            if(auto *ptr = objects[i]) {
                alloc_t::free(ptr);
            }
        }
    }
}

template <allocator alloc_t>
void free_benchmark(benchmark::State &state) {
    for(auto _: state) {
        auto objects = std::make_unique<std::byte *[]>(state.range(0));

        for(std::size_t i = 0; i < state.range(0); i++) {
            objects[i] = std::bit_cast<std::byte *>(alloc_t::alloc(las::test::uniform(8, 1024)));
        }

        std::atomic_uintmax_t index = 0;
        auto test = [&] {
            auto i     = index.fetch_add(1, std::memory_order::relaxed);
            alloc_t::free(objects[i]);
        };

        state.ResumeTiming();

        #if defined (MULTITHREADING)
        stresser.dispatch(
                {{test, 100}},
                state.range(0));
        #else
        while (index < state.range(0)) {
            test ();
        }
        #endif


        state.PauseTiming();
    }
}

#define MIN_ITERATION_RANGE (1U << 14U)
#define MAX_ITERATION_RANGE (1U << 16U)

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)
#define MY_BENCHMARK(func, name) BENCHMARK((func))->Range (MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)->Name(name)->Unit(benchmark::TimeUnit::kMillisecond)

//MY_BENCHMARK(mixed_benchmark< system_alloc >, "malloc - mixed baseline");
//MY_BENCHMARK(mixed_benchmark< sgc2_alloc >, "sgc2 - mixed");

//MY_BENCHMARK(alloc_benchmark< system_alloc >, "malloc - alloc baseline");
MY_BENCHMARK(alloc_benchmark< sgc2_alloc >, "sgc2 - alloc");

//MY_BENCHMARK(free_benchmark< system_alloc >, "malloc - free baseline");
//MY_BENCHMARK(free_benchmark< sgc2_alloc >, "sgc2 - free");

BENCHMARK_MAIN();
