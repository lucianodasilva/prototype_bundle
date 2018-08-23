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

		static inline constexpr uint8_t get_byte (uint32_t v, int radix) {
			return (v >> (radix * 8)) & 0xFF;
		}

		static constexpr auto radix_count = sizeof(uint32_t);

		std::vector < uint32_t >		swap;

		void reserve(size_t capacity) {
			swap.resize(capacity);
		}

		void sort(std::vector < uint32_t > & items) {

			// clear
			std::array < uint32_t, 256 * radix_count >	histograph{};
			std::array < uint32_t, 256 * radix_count >	indexes{};

			// calculate histograph positions
			for (auto const & item : items) {
				for (int radix = 0; radix < radix_count; ++radix) {
					++histograph[(radix * 256) + get_byte(item, radix)];
				}
			}

			// histograph offsets
			int offsets [radix_count] = {0};

			for (int i = 0; i < 256; ++i) {
				for (int radix = 0; radix < radix_count; ++radix) {
					auto t = histograph [radix * 256 + i];
					histograph [radix * 256 + i] = offsets[radix];
					offsets [radix] += t;
				}
			}

			for (uint32_t radix = 0; radix < radix_count; ++radix) {
				auto h_offset = 256 * radix;

				// swap values
				for (auto const & item : items) {
					// get value
					auto key = get_byte(item, radix);

					// get and increment offset
					auto & item_offset = histograph[h_offset + key];

					auto index = item_offset;
					++item_offset;

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
