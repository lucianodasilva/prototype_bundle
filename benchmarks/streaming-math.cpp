#include <chrono>
#include <benchmark/benchmark.h>
#include <vector>
#include <immintrin.h>
#include <concepts>

#define GLM_FORCE_AVX2
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>

struct fast_rnd {
    uint32_t s[4];

    static uint32_t rotl(uint32_t x, int k) {
        return (x << k) | (x >> (32 - k));
    }

    static uint64_t splitmix64(uint64_t & x) {
        x          += 0x9e3779b97f4a7c15ULL;
        uint64_t z = x;
        z          = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z          = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    explicit fast_rnd(uint64_t seed = 0x12345678abcdefULL) {
        // Seed 128‑bit state via SplitMix64
        for(int i = 0; i < 4; ++i) {
            s[i] = static_cast<uint32_t>(splitmix64(seed));
        }
        if((s[0] | s[1] | s[2] | s[3]) == 0) s[0] = 1; // avoid all‑zero state
    }

    uint32_t next_u32() {
        // xoroshiro128++
        uint32_t result = rotl(s[0] + s[3], 7) + s[0];
        uint32_t t      = s[1] << 9;

        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];

        s[2] ^= t;
        s[3] = rotl(s[3], 11);

        return result;
    }

    float next_f32() {
        // Use top 24 bits -> uniform in [0,1)
        constexpr float INV_24 = 1.0f / 16777216.0f; // 2^‑24
        return static_cast<float>(next_u32() >> 8) * INV_24;
    }

    float next_f32_range(float lo, float hi) {
        return lo + (hi - lo) * next_f32();
    }

    void fill(auto it, auto end) {
        constexpr float INV_24 = 1.0f / 16777216.0f;

        while(it < end) {
            *it = static_cast<float>(next_u32() >> 8) * INV_24;
            ++it;
        }
    }
};

namespace demo_baseline {
    template <typename op_type, typename value_type>
    concept binary_op = requires(op_type op, value_type v) {
        { op(v, v) } -> std::same_as<value_type>;
    };


    struct vec3 {
        union {
            struct {
                float x, y, z;
            };

            float data[3];
        };

        static constexpr std::size_t size = 3;
    };

    vec3 operator +(vec3 const & lhv, vec3 const & rhv) {
        return {
            lhv.x + rhv.x,
            lhv.y + rhv.y,
            lhv.z + rhv.z
        };
    }

    vec3 operator -(vec3 const & lhv, vec3 const & rhv) {
        return {
            lhv.x - rhv.x,
            lhv.y - rhv.y,
            lhv.z - rhv.z
        };
    }


    vec3 operator *(vec3 const & lhv, float const m) {
        return {
            lhv.x * m,
            lhv.y * m,
            lhv.z * m
        };
    }


    vec3 operator /(vec3 const & lhv, float const m) {
        return {
            lhv.x / m,
            lhv.y / m,
            lhv.z / m
        };
    }

    struct mat4 {
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

        static constexpr std::size_t size = 16;
    };

    inline mat4 operator*(mat4 const & m, float const scalar) {
        mat4 result;

        for(int i = 0; i < 16; ++i) {
            result.data[i] = m.data[i] * scalar;
        }

        return result;
    }

    mat4 operator * (mat4 const & lhv, mat4 const & rhv) {
        mat4 result{};

        for(int i = 0; i < 4; ++i) {
            for(int j = 0; j < 4; ++j) {
                result.data[i * 4 + j] = 0.0f;

                for(int k = 0; k < 4; ++k) {
                    result.data[i * 4 + j] += lhv.data[i * 4 + k] * rhv.data[k * 4 + j];
                }
            }
        }

        return result;
    }

    inline vec3 operator*(mat4 const & lhv, vec3 const & rhv) {
        vec3 result{};

        for(int i = 0; i < 3; ++i) {
            result.data[i] = 0.0f;

            for(int j = 0; j < 3; ++j) {
                result.data[i] += lhv.data[i * 4 + j] * rhv.data[j];
            }
        }

        return result;
    }
}

namespace demo_vectorized {

    struct alignas(16) mat4 {
        __m128 data [4];
    };

    mat4 operator*(mat4 const & lhv, mat4 const & rhv) {
        mat4 result;

        for (int i = 0; i < 4; ++i) {
            auto const e0 = _mm_shuffle_ps(rhv.data[i], rhv.data[i], _MM_SHUFFLE(0, 0, 0, 0));
            auto const e1 = _mm_shuffle_ps(rhv.data[i], rhv.data[i], _MM_SHUFFLE(1, 1, 1, 1));
            auto const e2 = _mm_shuffle_ps(rhv.data[i], rhv.data[i], _MM_SHUFFLE(2, 2, 2, 2));
            auto const e3 = _mm_shuffle_ps(rhv.data[i], rhv.data[i], _MM_SHUFFLE(3, 3, 3, 3));

            auto const m0 = _mm_mul_ps(lhv.data[0], e0);
            auto const m1 = _mm_fmadd_ps(lhv.data[1], e1, m0);
            auto const m2 = _mm_fmadd_ps(lhv.data[2], e2, m1);

            result.data[i] = _mm_fmadd_ps(lhv.data[3], e3, m2);
        }

        return result;
    }

}

#define MIN_ITERATION_RANGE (1U << 14U)
#define MAX_ITERATION_RANGE (1U << 22U)

template <typename vec_t, std::size_t unit_size>
std::vector<vec_t> make_test_data(std::size_t range) {
    fast_rnd           rnd(std::chrono::system_clock::now().time_since_epoch().count());
    std::vector<vec_t> res(range);

    auto *it  = reinterpret_cast<float *>(res.data());
    auto *end = it + range * unit_size;

    constexpr float INV_24 = 1.0f / 16777216.0f;

    while(it < end) {
        *it = static_cast<float>(rnd.next_u32() >> 8) * INV_24;
        ++it;
    }

    return res;
}

auto test_mat4_data   = make_test_data<demo_baseline::mat4, 16>(MAX_ITERATION_RANGE);
auto test_mat4_data_b = make_test_data<demo_baseline::mat4, 16>(MAX_ITERATION_RANGE);

auto test_glm_mat4_data   = make_test_data<glm::mat4, 16>(MAX_ITERATION_RANGE);
auto test_glm_mat4_data_b = make_test_data<glm::mat4, 16>(MAX_ITERATION_RANGE);

auto test_fma_mat4_data   = make_test_data<demo_vectorized::mat4, 16>(MAX_ITERATION_RANGE);
auto test_fma_mat4_data_b = make_test_data<demo_vectorized::mat4, 16>(MAX_ITERATION_RANGE);

inline void PROTO_BASE_MULT_MAT(benchmark::State & state) {
    for(auto _: state) {
        auto const range = state.range(0);

        for(int i = 0; i < range; ++i) {
            test_mat4_data_b[i] = test_mat4_data[i] * test_mat4_data_b[i];
        }
    }
}

inline void PROTO_GLM_MULT_MAT(benchmark::State & state) {
    for(auto _: state) {
        auto const range = state.range(0);

        for(int i = 0; i < range; ++i) {
            test_glm_mat4_data_b[i] = test_glm_mat4_data[i] * test_glm_mat4_data_b[i];
        }
    }
}


inline void PROTO_AVX2_FMA_MULT_MAT(benchmark::State & state) {
    for(auto _: state) {
        auto const range = state.range(0);

        for(int i = 0; i < range; ++i) {
            test_fma_mat4_data_b[i] = test_fma_mat4_data[i] * test_fma_mat4_data_b[i];
        }
    }
}


//inline void PROTO_BASE_MULT_VEC (benchmark::State & state) {
//    for (auto _ : state) {
//        auto range = state.range(0);
//
//        for (int i = 0; i < range; ++i) {
//            test_vec3_data[i] = test_mat4x4_data[i] * test_vec3_data[i];
//        }
//    }
//}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(PROTO_GLM_MULT_MAT)
        ->RANGE
        ->Unit(benchmark::kMillisecond);

BENCHMARK(PROTO_BASE_MULT_MAT)
    ->RANGE
    ->Unit(benchmark::kMillisecond);

BENCHMARK(PROTO_AVX2_FMA_MULT_MAT)
        ->RANGE
        ->Unit(benchmark::kMillisecond);




BENCHMARK_MAIN();
