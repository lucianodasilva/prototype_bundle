#include <benchmark/benchmark.h>
#include <future>

#define MIN_ITERATION_RANGE 1 << 20
#define MAX_ITERATION_RANGE 1 << 30

char* buffer_a = (char *) _aligned_malloc(MAX_ITERATION_RANGE, 16);
char * buffer_b = (char*)_aligned_malloc(MAX_ITERATION_RANGE, 16);

void with_memcpy (benchmark::State& state) {
	for (auto _ : state) {
		memcpy(buffer_b, buffer_a, state.range(0));
	}
}

void with_std_copy (benchmark::State& state) {
	for (auto _ : state) {
		std::copy(buffer_a, buffer_a + state.range(0), buffer_b);
	}
}

void with_async_memcpy(benchmark::State& state) {

	auto const cpu_count = std::thread::hardware_concurrency();
	std::vector < std::future < void > > sync (cpu_count);
	
	for (auto _ : state) {

		auto len = state.range(0);
		auto stride = len / cpu_count;
		auto rem = len % cpu_count;

		for (unsigned i = 0; i < cpu_count; ++i) {
			auto offset = i * stride;
			
			if (i == cpu_count - 1)
				stride = rem;

			sync[i] = std::async(std::launch::async, [=]() {
				memcpy(buffer_b + offset, buffer_a + offset, stride);
			});
		}

		for (unsigned i = 0; i < cpu_count; ++i)
			sync[i].get();
	}
}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(with_memcpy)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
BENCHMARK(with_std_copy)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
BENCHMARK(with_async_memcpy)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();