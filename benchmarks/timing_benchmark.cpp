#include <benchmark/benchmark.h>
#include <chrono>

#define MIN_ITERATION_RANGE 1 << 16
#define MAX_ITERATION_RANGE 1 << 30

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

void with_std_chrono (benchmark::State& state) {
	std::chrono::high_resolution_clock::time_point time;
	while (state.KeepRunning()) {
		benchmark::DoNotOptimize(time = std::chrono::high_resolution_clock::now());
	}
}

BENCHMARK(with_std_chrono)->RANGE->Unit(benchmark::TimeUnit::kNanosecond);

BENCHMARK_MAIN();