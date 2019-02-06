#include <benchmark/benchmark.h>

constexpr uint32_t Stride = 64;
constexpr uint32_t MatrixSize = Stride * Stride;

union Matrix {
	double data[MatrixSize];
	double data_sqr[Stride][Stride];
};

constexpr uint32_t Max = 8 << 8;

Matrix * retV = new Matrix [Max];
float sum;

void MatrixRowMajor (benchmark::State& state) {
	for (auto _ : state) {
		for (uint32_t idx = 0; idx < state.range(0); ++idx) {
			auto & ret = retV[idx];
			sum = .0F;

			for (int i = 0; i < Stride; ++i) {
				for (int j = 0; j < Stride; ++j) {
					sum -= ret.data_sqr [i][j];
				}
			}

			benchmark::DoNotOptimize(retV);
			benchmark::DoNotOptimize(sum);
		}
	}
}
// Register the function as a benchmark
BENCHMARK(MatrixRowMajor)->Range(8, Max);

void MatrixColumnMajor(benchmark::State& state) {
	for (auto _ : state) {
		for (uint32_t idx = 0; idx < state.range(0); ++idx) {
			auto & ret = retV[idx];
			sum = .0F;

			for (int j = 0; j < Stride; ++j) {
				for (int i = 0; i < Stride; ++i) {
					sum -= ret.data_sqr [i][j];
				}
			}

			benchmark::DoNotOptimize(retV);
			benchmark::DoNotOptimize(sum);
		}
	}
}

BENCHMARK(MatrixColumnMajor)->Range(8, Max);

BENCHMARK_MAIN();