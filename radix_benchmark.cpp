#include <benchmark/benchmark.h>
#include <cinttypes>
#include <algorithm>
#include <array>
#include <vector>

namespace proto {

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

	struct reference_radix {

		static constexpr auto radix_count = sizeof(uint32_t);

		std::vector < uint32_t >		swap;
		std::array < uint32_t, 256 >	histograph{};

		void reserve(size_t capacity) {
			swap.resize(capacity);
		}

		void sort(std::vector < uint32_t > & items) {

			// clear
			for (uint32_t radix = 0; radix < radix_count; ++radix) {
				histograph = {};

				// histograph hit count
				for (auto & item : items) {
					auto ptr = *(reinterpret_cast <uint8_t *>(&item) + radix);

					++histograph[ptr];
				}

				// histograph offsets
				uint32_t h_offset = 0;

				for (auto & h : histograph) {
					auto count = h;
					h = h_offset;
					h_offset += count;
				}

				// swap values
				for (auto & item : items) {
					// get value
					auto ptr = *(reinterpret_cast <uint8_t *>(&item) + radix);

					// get and increment offset
					auto & h_offset = histograph[ptr];

					auto index = h_offset;
					++h_offset;

					// set value to swap
					swap[index] = item;
				}

				// swap vectors
				std::swap(swap, items);
			}

		}

	};


	struct preload_histograph_radix {

		static inline constexpr int get_byte (uint32_t v, int radix) {
			return (v >> (radix * 8)) & 0xFF;
		}

		static constexpr auto radix_count = sizeof(uint32_t);

		std::vector < uint32_t >		swap;

		void reserve(size_t capacity) {
			swap.resize(capacity);
		}

		void sort(std::vector < uint32_t > & items) {

			// clear
			std::array < int, radix_count * 0x100 > histograph { 0 };
			std::array < int, radix_count >			offset_sum { 0 };

			// calculate histograph positions
			for (auto const & item : items) {
				for (int radix = 0; radix < radix_count; ++radix) {
					int key = (item >> (radix * 8)) & 0xFF;
					++histograph[radix * 0x100 + key];
				}
			}

			// histograph offsets
			for (int i = 0; i < 0x100; ++i) {
				for (int radix = 0; radix < radix_count; ++radix) {

					int & h = histograph [radix * 0x100 + i];

					if (h != 0) {
						int t = h;
						h = offset_sum[radix];
						offset_sum[radix] += t;
					}
				}
			}

			for (int radix = 0; radix < radix_count; ++radix) {

				// swap values
				for (auto const & item : items) {
					// get value
					auto key = (item >> (radix * 8)) & 0xFF;
					// get and increment offset
					auto index = histograph [radix * 0x100 + key]++;
					// set value to swap
					swap[index] = item;
				}

				// swap vectors
				std::swap(swap, items);
			}

		}

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

//BENCHMARK(BM_Baseline)
//->Range(8, 8 << 10);

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

//BENCHMARK(BM_NaiveRadix)
//	->Range(8, 8 << 10);

static void BM_PreloadedRadix(benchmark::State& state) {

	std::vector < uint32_t > data;
	proto::preload_histograph_radix radix;

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

BENCHMARK(BM_PreloadedRadix)
	->Range(8, 8 << 10);

BENCHMARK_MAIN();
