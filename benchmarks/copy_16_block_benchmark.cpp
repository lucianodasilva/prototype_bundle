#include <benchmark/benchmark.h>
#include <future>
#include <cstring>
#include <emmintrin.h>
#include <las/test/random.hpp>

#define MIN_ITERATION_RANGE 16 << 10
#define MAX_ITERATION_RANGE 16 << 20

auto buffer_a_ = std::make_unique < uint8_t [] > (MAX_ITERATION_RANGE);
auto buffer_a = std::unique_ptr < uint8_t []> (static_cast < uint8_t * > (std::aligned_alloc (16, MAX_ITERATION_RANGE)));
auto buffer_b = std::unique_ptr < uint8_t []> (static_cast < uint8_t * > (std::aligned_alloc (16, MAX_ITERATION_RANGE)));

void with_aligned_std_copy (benchmark::State& state) {
	for (auto _ : state) {
		memcpy(buffer_b.get(), buffer_a.get(), state.range(0));
	}
}

template < typename t >
t next_pow_of (t value, t pow) {
	return value + (pow - value % pow) % pow;
}

void fast_aligned_copy_n (uint8_t * __restrict__ src, std::size_t n, uint8_t * __restrict__ dst) {

	uint8_t * src_aligned = (uint8_t *)__builtin_assume_aligned (src, 16);
	uint8_t * dst_aligned = (uint8_t *)__builtin_assume_aligned (dst, 16);

	// get nearest aligned address
	for (int i = 0; i < n; ++i) {
	    dst_aligned [i] = src_aligned [i];
    }
}

void with_fast_aligned_copy_n (benchmark::State& state) {
	for (auto _ : state) {
		fast_aligned_copy_n (buffer_a.get(), state.range(0), buffer_b.get());
	}
}


#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(with_aligned_std_copy)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
BENCHMARK(with_fast_aligned_copy_n)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();