#include <benchmark/benchmark.h>
#include <cinttypes>
#include <algorithm>
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>

namespace proto {

	using render_command = uint64_t;

	struct reference_radix {

		static constexpr auto radix_count = sizeof(render_command);

		std::vector < render_command >		swap;
		std::array < uint32_t, 256 >	histograph{};

		void reserve(size_t capacity) {
			swap.resize(capacity);
		}

		void sort(std::vector < render_command > & items) {

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


	struct shortcut_radix {
		static constexpr auto radix_count = sizeof(render_command);

		std::vector < render_command >		swap;
		std::array < int, 256 >	histograph{};

		void reserve(size_t capacity) {
			swap.resize(capacity);
		}

		void sort_radix (std::vector < render_command > & items) {

			// clear
			for (int radix = 0; radix < radix_count; ++radix) {
				histograph = {};

				// histograph hit count
				for (auto & item : items) {
					auto ptr = *(reinterpret_cast <uint8_t *>(&item) + radix);

					++histograph[ptr];
				}

				// histograph offsets
				int h_offset = 0;

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

		void sort_insert (std::vector < render_command > & items) {

		    int len = items.size();

		    if (len < 2)
		        return;

		    for (int i = 1; i < len; ++i) {
		        for (int j = i; j > 0 && items[j - 1] > items[j]; --j) {
		            std::swap (items[j - 1], items[j]);
		        }
		    }

		}

		void sort (std::vector < render_command > & items) {
		    if (items.size() < 64)
		        sort_insert (items);
		    else
		        sort_radix (items);
		}

	};


    struct precalc_shortcut_radix {
        static constexpr auto radix_count = sizeof(render_command);

        std::vector < render_command >		swap;
        std::array < std::array < int, 256 >, radix_count >	histograph {};

        void reserve(size_t capacity) {
            swap.resize(capacity);
        }

        void sort_radix (std::vector < render_command > & items) {
            histograph = {};

            // precalc histograph
            for (auto const & item : items) {
                auto * ptr = reinterpret_cast <uint8_t const * const>(&item);

                for (int radix = 0; radix < radix_count; ++radix) {
                    ++histograph[radix][*(ptr + radix)];
                }
            }

            // convert histograph to indexes
            for (int radix = 0; radix < radix_count; ++radix) {
                int index = 0;

                for (uint32_t j = 0; j < 256; ++j) {
                    int n = histograph[radix][j];
                    histograph[radix][j] = index;
                    index += n;
                }
            }

            // clear
            for (int radix = 0; radix < radix_count; ++radix) {
                // reorder values
                for (auto const & item : items) {
                    // get value
                    auto v = *(reinterpret_cast <uint8_t const * const>(&item) + radix);

                    // get and increment offset
                    auto & h_offset = histograph[radix][v];

                    auto index = h_offset;
                    ++h_offset;

                    // set value to swap
                    swap[index] = item;
                }

                // swap vectors
                std::swap(swap, items);
            }

        }

        void static sort_insert (std::vector < render_command > & items) {

            int len = items.size();

            if (len < 2)
                return;

            for (int i = 1; i < len; ++i) {
                for (int j = i; j > 0 && items[j - 1] > items[j]; --j) {
                    std::swap (items[j - 1], items[j]);
                }
            }

        }

        void sort (std::vector < render_command > & items) {
            if (items.size() < 64)
                sort_insert (items);
            else
                sort_radix (items);
        }

    };

}

std::vector < proto::render_command > generate_data(uint32_t size) {
	std::vector < proto::render_command > data (size);

	std::srand(size);

	for (uint32_t i = 0; i < size; ++i) {
		data[i] = std::rand();
	}

	return data;
}

void evaluate(std::vector < proto::render_command > const & data) {
	uint32_t v = 0;

	for (auto & i : data) {
		if (i < v) {
			throw std::runtime_error("invalid algorithm result");
		}

		v = i;
	}
}

static void BM_Baseline(benchmark::State& state) {

	std::vector < proto::render_command > data;

	for (auto _ : state) {
		state.PauseTiming();
		data = generate_data(state.range(0));
		state.ResumeTiming();

		std::sort(data.begin(), data.end());

		state.PauseTiming();
		evaluate(data);
		state.ResumeTiming();
	}

	benchmark::DoNotOptimize(data);
}

static void BM_NaiveRadix (benchmark::State& state) {

	std::vector < proto::render_command > data;
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

	benchmark::DoNotOptimize(data);
}

static void BM_ShortcutRadix(benchmark::State& state) {

	std::vector < proto::render_command > data;
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

	benchmark::DoNotOptimize(data);
}

static void BM_PrecalcShortcutRadix(benchmark::State& state) {

    std::vector < proto::render_command > data;
    proto::precalc_shortcut_radix radix;

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

	benchmark::DoNotOptimize(data);
}

BENCHMARK(BM_Baseline)
->Range(8 << 14, 8 << 20)
->Unit(benchmark::kMillisecond);

BENCHMARK(BM_NaiveRadix)
	->Range(8, 8 << 10);

BENCHMARK(BM_ShortcutRadix)
->Range(8 << 14, 8 << 20)
->Unit(benchmark::kMillisecond);

BENCHMARK(BM_PrecalcShortcutRadix)
->Range(8 << 14, 8 << 20)
->Unit(benchmark::kMillisecond);


BENCHMARK_MAIN();
