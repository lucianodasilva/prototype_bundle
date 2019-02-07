#include <benchmark/benchmark.h>
#include <cinttypes>
#include <algorithm>
#include <array>
#include <vector>
#include <cstring>

namespace proto {

	struct engine {

		virtual void do_stuffs () = 0;
		virtual void do_other_stuffs () = 0;

	};

	struct engine_spec : public engine {

		void do_stuffs () override {
			benchmark::DoNotOptimize(data);
		}
	};

	namespace function_ptrs {

		using command_t = void (*)(engine_& e) {}

	}

	union render_command {
		struct {
			uint8_t target : 8;
			bool is_transparent: 1;
			uint8_t layer : 7;
			uint16_t tag : 16;
		};

		uint32_t full_data;
		uint8_t  data[4];
	};



}

std::vector < uint32_t > generate_data(uint32_t size) {
	std::vector < uint32_t > data (size);

	std::srand(size);

	for (uint32_t i = 0; i < size; ++i) {
		data[i] = std::rand();
	}

	return data;
}

void evaluate(std::vector < uint32_t > const & data) {
	uint32_t v = 0;

	for (auto & i : data) {
		if (i < v) {
			throw std::runtime_error("invalid algorithm result");
		}

		v = i;
	}
}

static void BM_Baseline(benchmark::State& state) {

	std::vector < uint32_t > data;

	for (auto _ : state) {
		state.PauseTiming();
		data = generate_data(state.range(0));
		state.ResumeTiming();

		std::sort(data.begin(), data.end());

		state.PauseTiming();
		evaluate(data);
		state.ResumeTiming();
	}
}

BENCHMARK(BM_Baseline)
->Range(8, 8 << 10);

static void BM_NaiveRadix (benchmark::State& state) {

	std::vector < uint32_t > data;
	proto::reference_radix radix;

	for (auto _ : state) {
		state.PauseTiming();
		data = generate_data (state.range(0));
		radix.reserve(state.range(0));
		state.ResumeTiming();

		radix.sort(data);

		state.PauseTiming();
		evaluate(data);
		state.ResumeTiming();
	}
}

BENCHMARK(BM_NaiveRadix)
	->Range(8, 8 << 10);

static void BM_ShortcutRadix(benchmark::State& state) {

	std::vector < uint32_t > data;
	proto::shortcut_radix radix;

	for (auto _ : state) {
		state.PauseTiming();
		data = generate_data(state.range(0));
		radix.reserve(state.range(0));
		state.ResumeTiming();

		radix.sort(data);

		state.PauseTiming();
		evaluate(data);
		state.ResumeTiming();
	}
}

BENCHMARK(BM_ShortcutRadix)
	->Range(8, 8 << 10);

BENCHMARK_MAIN();
