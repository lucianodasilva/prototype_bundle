#include <benchmark/benchmark.h>
#include <cinttypes>

constexpr uint64_t max_items{ 1 << 18 };

uint8_t const* index_set = [] {
	auto* buffer = new uint8_t[max_items];

	for (uint64_t i = 0; i < max_items; ++i)
		buffer[i] = std::rand() % 256;

	return buffer;
}();

// naive bitset
struct naive_bitset {
	uint64_t data[256 / sizeof(uint64_t)];

	inline bool test(uint8_t index) const noexcept {
		auto block = index / sizeof(uint64_t);
		auto bit = index % sizeof(uint64_t);

		auto v = data[block];
		auto mask = uint64_t{ 1 } << bit;

		return !!(v & mask);
	}

	inline void set(uint8_t index) noexcept {
		auto block = index / sizeof(uint64_t);
		auto bit = index % sizeof(uint64_t);

		auto mask = uint64_t{ 1 } << bit;
		data [block] |= mask;
	}
};

// intrinsics bitset 
#ifdef __linux__
#	include <bitset>
#else
#	include <intrin.h>
#endif

struct int_bitset {
	uint64_t data[256 / sizeof(uint64_t)];

	inline bool test(uint8_t index) const noexcept {
		auto block = index / sizeof(uint64_t);
		auto bit = index % sizeof(uint64_t);

		auto v = data[block];
#ifdef __linux__
		
#else
		return _bittest64(reinterpret_cast < const long long * > (data + block), bit);
#endif

	}

	inline void set (uint8_t index) noexcept {
		auto block = index / sizeof(uint64_t);
		auto bit = index % sizeof(uint64_t);

		auto v = data[block];
#ifdef __linux__

#else
		_bittestandreset64(reinterpret_cast <long long*> (data + block), bit);
#endif

	}
};

// ----------------------------------------------------------------------------------------------------------

void naive_bit_test (benchmark::State &state) {
	naive_bitset bitset;

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto bit_value = bitset.test(index_set[i]);
			benchmark::DoNotOptimize(bit_value);
		}
	}
}

BENCHMARK(naive_bit_test)->Range(1 << 8, max_items);

void intrinsics_bit_test(benchmark::State& state) {
	int_bitset bitset;

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto bit_value = bitset.test(index_set[i]);
			benchmark::DoNotOptimize(bit_value);
		}
	}
}

BENCHMARK(intrinsics_bit_test)->Range(1 << 8, max_items);

void naive_bit_set(benchmark::State& state) {
	naive_bitset bitset;

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			bitset.set(index_set[i]);
			benchmark::DoNotOptimize(bitset);
		}
	}
}

BENCHMARK(naive_bit_set)->Range(1 << 8, max_items);

void intrinsics_bit_set(benchmark::State& state) {
	int_bitset bitset;

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			bitset.set(index_set[i]);
			benchmark::DoNotOptimize(bitset);
		}
	}
}

BENCHMARK(intrinsics_bit_set)->Range(1 << 8, max_items);

BENCHMARK_MAIN();
