#include <benchmark/benchmark.h>
#include <functional>
#include <iostream>
#include <atomic>
#include <mutex>
#include <memory>
#include <thread>

#define MIN_ITERATION_RANGE (1U << 8U)
#define MAX_ITERATION_RANGE (1U << 16U)

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

namespace common {

    struct mat4x4 {
        union {
            struct {
                float
                        m00, m01, m02, m03,
                        m04, m05, m06, m07,
                        m08, m09, m10, m11,
                        m12, m13, m14, m15;
            };

            float data[16];
        };
    };

    inline mat4x4 operator*(mat4x4 const &lhv, mat4x4 const &rhv) {
        mat4x4 result {};

        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.data[i * 4 + j] = 0.0f;

                for (int k = 0; k < 4; ++k) {
                    result.data[i * 4 + j] += lhv.data[i * 4 + k] * rhv.data[k * 4 + j];
                }
            }
        }

        return result;
    }

    class mass_calculator {
    public:

        inline void generate_values(std::size_t count) {

            matrixes.clear();
            matrixes.reserve(count);

            for (std::size_t i = 0; i < count; ++i) {
                auto rn01 = xorshift32();
                auto rn02 = xorshift32();
                auto rn03 = xorshift32();
                auto rn04 = xorshift32();

                matrixes.emplace_back(mat4x4 {
                        rn01, rn02, rn03, rn04,
                        rn02, rn03, rn04, rn01,
                        rn03, rn04, rn01, rn02,
                        rn04, rn01, rn02, rn03
                });
            }
        }

        inline void mass_multiply (mat4x4 const & value) {
            for (auto & m : matrixes) {
                m = m * value;
            }
        }

        inline static float xorshift32()
        {
            /* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
            xorshift32_state ^= xorshift32_state << 13;
            xorshift32_state ^= xorshift32_state << 17;
            xorshift32_state ^= xorshift32_state << 5;

            return float (xorshift32_state) / float (UINT32_MAX) / 100.0F;
        }

    private:
        std::vector < mat4x4 > matrixes;
        static uint32_t xorshift32_state;
    };

    uint32_t mass_calculator::xorshift32_state = 0xdeadbeef;
}

namespace bench {

    class tlocal_calculator : public common::mass_calculator{
    public:
        thread_local static tlocal_calculator instance;
    };

    thread_local tlocal_calculator tlocal_calculator::instance;

    class map_calculator : public common::mass_calculator{
    public:
        inline static map_calculator & instance () {
            static std::map < std::thread::id, map_calculator > instances;
            static std::mutex instances_mutex;

            map_calculator * instance = nullptr;

            {
                auto lock = std::unique_lock (instances_mutex);
                instance = &instances[std::this_thread::get_id()];
            }

            return *instance;
        }
    };
}

static void bm_tlocal (benchmark::State& state) {
    bench::tlocal_calculator::instance.generate_values(state.range(0));

	for (auto _ : state) {
        auto rn01 = common::mass_calculator::xorshift32();
        auto rn02 = common::mass_calculator::xorshift32();
        auto rn03 = common::mass_calculator::xorshift32();
        auto rn04 = common::mass_calculator::xorshift32();

        common::mat4x4 other {
                rn01, rn02, rn03, rn04,
                rn02, rn03, rn04, rn01,
                rn03, rn04, rn01, rn02,
                rn04, rn01, rn02, rn03
        };

		bench::tlocal_calculator::instance.mass_multiply(other);
	}
}

static void bm_map (benchmark::State& state) {
    bench::map_calculator::instance().generate_values(state.range(0));

    for (auto _ : state) {
        auto rn01 = common::mass_calculator::xorshift32();
        auto rn02 = common::mass_calculator::xorshift32();
        auto rn03 = common::mass_calculator::xorshift32();
        auto rn04 = common::mass_calculator::xorshift32();

        common::mat4x4 other {
                rn01, rn02, rn03, rn04,
                rn02, rn03, rn04, rn01,
                rn03, rn04, rn01, rn02,
                rn04, rn01, rn02, rn03
        };

        bench::map_calculator::instance().mass_multiply(other);
    }
}

BENCHMARK(bm_tlocal)
	->RANGE
	->Unit(benchmark::TimeUnit::kMillisecond)
	->Threads(4);

BENCHMARK(bm_map)
	->RANGE
	->Unit(benchmark::TimeUnit::kMillisecond)
	->Threads(4);