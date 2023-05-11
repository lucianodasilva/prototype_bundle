//#include <benchmark/benchmark.h>
//#include <immintrin.h>
//#include <vector>
//#include <x86intrin.h>
//
//namespace demo_baseline {
//
//    struct vec3 {
//        union {
//            struct {
//                float x, y, z, w;
//            };
//            float data[4];
//            __m128 simd_data;
//        };
//    };
//
//    struct mat4x4 {
//        union {
//            struct {
//                float
//                        m00, m01, m02, m03,
//                        m04, m05, m06, m07,
//                        m08, m09, m10, m11,
//                        m12, m13, m14, m15;
//            };
//
//            float data[16];
//            __m512 simd_data;
//        };
//    };
//
//    inline mat4x4 operator*(mat4x4 const &m, float const scalar) {
//        mat4x4 result;
//
//        for (int i = 0; i < 16; ++i) {
//            result.data[i] = m.data[i] * scalar;
//        }
//
//        return result;
//    }
//
//    inline mat4x4 operator*(mat4x4 const &lhv, mat4x4 const &rhv) {
//        mat4x4 result;
//
//        for (int i = 0; i < 4; ++i) {
//            for (int j = 0; j < 4; ++j) {
//                result.data[i * 4 + j] = 0.0f;
//
//                for (int k = 0; k < 4; ++k) {
//                    result.data[i * 4 + j] += lhv.data[i * 4 + k] * rhv.data[k * 4 + j];
//                }
//            }
//        }
//
//        return result;
//    }
//
//    inline vec3 operator*(mat4x4 const &lhv, vec3 const &rhv) {
//        vec3 result;
//
//        for (int i = 0; i < 4; ++i) {
//            result.data[i] = 0.0f;
//
//            for (int j = 0; j < 4; ++j) {
//                result.data[i] += lhv.data[i * 4 + j] * rhv.data[j];
//            }
//        }
//
//        return result;
//    }
//
//}
//
//namespace demo_avx512 {
//
//    using vec3 = demo_baseline::vec3;
//    using mat4x4 = demo_baseline::mat4x4;
//
//    inline mat4x4 mult_scalar(mat4x4 const &m, float const scalar) {
//        mat4x4 res;
//        __m512 const scalar_v = _mm512_set1_ps(scalar);
//        res.simd_data = _mm512_mul_ps(m.simd_data, scalar_v);
//
//        return res;
//    }
//
//    inline vec3 mult_vec (mat4x4 const & m, vec3 const & v) {
//        return {};
//    }
//
//    inline mat4x4 mult_mat4 (mat4x4 const & lhv, mat4x4 const & rhv) {
//        /*
//        vmovups zmm1, ZMMWORD PTR [rdx]
//        vmovups zmm0, ZMMWORD PTR [rsi]
//        vshuff32x4      zmm3, zmm1, zmm1, 0
//        vshuff32x4      zmm6, zmm1, zmm1, 255
//        vpermilps       zmm5, zmm0, 0
//        vshuff32x4      zmm2, zmm1, zmm1, 85
//        vpermilps       zmm7, zmm0, 85
//        vpermilps       zmm4, zmm0, 170
//        vshuff32x4      zmm1, zmm1, zmm1, 170
//        vpermilps       zmm0, zmm0, 255
//        vmulps  zmm1, zmm4, zmm1
//        vmulps  zmm0, zmm0, zmm6
//        mov     rax, rdi
//        vfmadd231ps     zmm1, zmm2, zmm7
//        vfmadd231ps     zmm0, zmm3, zmm5
//        vaddps  zmm0, zmm1, zmm0
//        vmovups ZMMWORD PTR [rdi], zmm0
//        vzeroupper
//         */
//
//        mat4x4 result;
//
//        __m512 shuffle_00 = _mm512_shuffle_f32x4(lhv.simd_data, lhv.simd_data, 0x00);
//        __m512 shuffle_ff = _mm512_shuffle_f32x4(lhv.simd_data, lhv.simd_data, 0xFF);
//
//        __m512 perm_00 = _mm512_permute_ps(rhv.simd_data, 0x00);
//
//        __m512 shuffle_55 = _mm512_shuffle_f32x4(lhv.simd_data, lhv.simd_data, 0x55);
//        __m512 perm_55 = _mm512_permute_ps(rhv.simd_data, 0x55);
//
//        __m512 perm_aa = _mm512_permute_ps(rhv.simd_data, 0xAA);
//        __m512 shuffle_aa = _mm512_shuffle_f32x4(lhv.simd_data, lhv.simd_data, 0xAA);
//
//        __m512 perm_ff = _mm512_permute_ps(rhv.simd_data, 0xFF);
//
//        __m512 sum_01 = _mm512_fmadd_ps(_mm512_mul_ps(perm_aa, lhv.simd_data), shuffle_55, perm_55);
//        __m512 sum_02 = _mm512_fmadd_ps(_mm512_mul_ps(rhv.simd_data, shuffle_ff), shuffle_00, perm_00);
//
//        result.simd_data = _mm512_add_ps(sum_01, sum_02);
//
//        return result;
//    }
//
//}
//
//#define MIN_ITERATION_RANGE (1U << 14U)
//#define MAX_ITERATION_RANGE (1U << 22U)
//
//std::vector < demo_baseline::vec3 >     test_vec3_data(MAX_ITERATION_RANGE);
//std::vector < demo_baseline::mat4x4 >   test_mat4x4_data(MAX_ITERATION_RANGE);
//std::vector < demo_baseline::mat4x4 >   test_mat4x4_data_b(MAX_ITERATION_RANGE);
//
//inline void PROTO_BASE_MULT_MAT (benchmark::State & state) {
//    for (auto _ : state) {
//        auto range = state.range(0);
//
//        for (int i = 0; i < range; ++i) {
//            test_mat4x4_data_b[i] = test_mat4x4_data[i] * test_mat4x4_data_b[i];
//        }
//    }
//}
//
//inline void PROTO_AVX512_MULT_MAT (benchmark::State & state) {
//    for (auto _ : state) {
//        auto range = state.range(0);
//
//        for (int i = 0; i < range; ++i) {
//            test_mat4x4_data_b[i] = demo_avx512::mult_mat4(test_mat4x4_data[i], test_mat4x4_data_b[i]);
//        }
//    }
//}
//
//inline void PROTO_BASE_MULT_VEC (benchmark::State & state) {
//    for (auto _ : state) {
//        auto range = state.range(0);
//
//        for (int i = 0; i < range; ++i) {
//            test_vec3_data[i] = test_mat4x4_data[i] * test_vec3_data[i];
//        }
//    }
//}
//
//#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)
//
//BENCHMARK(PROTO_BASE_MULT_MAT)
//    ->RANGE
//    ->Unit(benchmark::kMillisecond);
//
//
//BENCHMARK(PROTO_AVX512_MULT_MAT)
//        ->RANGE
//        ->Unit(benchmark::kMillisecond);
//
//BENCHMARK(PROTO_BASE_MULT_VEC)
//        ->RANGE
//        ->Unit(benchmark::kMillisecond);
//
//
//BENCHMARK_MAIN();