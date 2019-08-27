#include <benchmark/benchmark.h>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>

#include <array>
#include <atomic>
#include <execution>
#include <vector>
#include <future>
#include <functional>
#include <memory>
#include <thread>
#include <experimental/algorithm>

#if defined (_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#include <functional>

#endif

#include "static_ring_buffer.hpp"

#define MIN_ITERATION_RANGE 1 << 14
#define MAX_ITERATION_RANGE 1 << 20

#define THREAD_COUNT 12

using namespace glm;

struct transformer {
	float data [256];

	inline void update_matrix () {
		for (auto & v : data) {
			v = v * 2;
		}
	}
};

std::vector < transformer > generate_data(std::size_t count) {
	std::vector < transformer > data(count);

	for (std::size_t i = 0; i < count; ++i) {
		for (auto & v : data[i].data) {
			v = (float) (std::rand() % 1000);
		}
	}

	return data;
}

auto test_data{ generate_data(MAX_ITERATION_RANGE) };

// prototype implementation

namespace proto_c {

	struct task;
	struct lane;

	struct exec_context {
		proto_c::lane & lane;
		proto_c::task * task;
	};

	using task_callback = void (*)(exec_context & cxt, void *, std::size_t );

	struct alignas (32) task {
	public:
		
		task_callback	callback;
		task const *	parent;
		transformer*	data;
		std::size_t		length;

		mutable std::atomic_size_t
						unresolved_children;
	};

	struct alignas (32) lane {
	public:

		void push(task* t) noexcept {
			auto b = back.load (std::memory_order_acquire);
			tasks[b & mask] = t;

			back.store (b + 1, std::memory_order_release);
		}

		task* pop() noexcept {
			auto b = --back;
			auto f = front.load(std::memory_order_acquire);

			if (f <= b) {
				task* t = tasks[b & mask];

				if (f != b)
					return t;

				auto f_plus{ f + 1 };
				if (!front.compare_exchange_strong(f, f_plus, std::memory_order_release, std::memory_order_acquire))
					t = nullptr;

				back.store(f_plus, std::memory_order_release);
				return t;
			}
			else {
				back.store(f, std::memory_order_release);
				return nullptr;
			}
		}

		task* steal() noexcept {
			auto f = front.load(std::memory_order_relaxed);
			auto b = back.load(std::memory_order_acquire);

			std::atomic_thread_fence(std::memory_order_acq_rel);

			if (f < b) {
				auto* t = tasks[f & mask];

				if (front.compare_exchange_strong(f, f + 1, std::memory_order_release))
					return t;
			}

			return nullptr;
		}

		inline task* alloc() noexcept {
			++task_buffer_index;
			return task_buffer.get() + task_buffer_index;
		}

		inline void alloc_free() noexcept {
			task_buffer_index = 0;
		}

		std::atomic_int64_t
			front{ 0 },
			back { 0 };

		// off critical path ( good ? / bad ? )
		std::unique_ptr < task* [] > tasks { new task * [4096] };
		std::unique_ptr < task [] >	 task_buffer { new task [4096] };

		std::size_t task_buffer_index{ 0 };

		bool
			running{ false };

		static constexpr std::size_t mask{ 4096 - 1 };
	};

	struct executor {
	public:

		executor(unsigned t_count) :
			lanes(t_count),
			workers (t_count),
			thread_count (t_count)
		{}

		~executor() {
			stop ();
		}

		inline void stop () {
			for (auto& lane : lanes) {
				lane.running = false;
			}

			for (int i = 1; i < thread_count; ++i) {
				if (workers [i].joinable())
					workers[i].join();
			}
		}

		inline static void run_lane(std::vector < lane > & lanes, lane & l, unsigned i) {
			auto* t = l.pop();

			if (!t) {
				auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (lanes.size());

				if (rand_index != i)
					t = lanes[rand_index].steal();
			}

			if (t) {
				exec_context cxt {l, t};
				t->callback(cxt, t->data, t->length);

				if (t->parent) {
					t->parent->unresolved_children.fetch_sub(1);
				}
			} else {
				std::this_thread::yield();
			}
		}

		void run() {
			if (lanes[0].running)
				return;

			lanes[0].running = true;

			// init workers 
			for (unsigned i = 1; i < thread_count; ++i) {
				workers [i] = std::thread ([this, i]() {
					auto & lane = lanes[i];

					lane.running = true;

					while (lane.running)
						run_lane(lanes, lane, i);
				});
			}
		}

		void wait_for(task* t) {
			auto & lane = get_this_lane();

			while (t->unresolved_children.load() > 0) {
				run_lane(lanes, lane, 0);
			}	
		}

		void static divide_and_conquer (exec_context & cxt, void * data, std::size_t length) {
			constexpr std::size_t slice_length { 1024 };
			auto * t_data = reinterpret_cast<transformer *> (data);

			auto * ta = cxt.lane.alloc ();
			auto * tb = cxt.lane.alloc ();

			ta->parent = tb->parent = cxt.task->parent;

			if (length > (cxt.task->parent->length / 48)) {
				ta->callback = tb->callback = &divide_and_conquer;
			} else {
				ta->callback = tb->callback = cxt.task->parent->callback;
			}

			ta->data = t_data;
			ta->length = length / 2;

			tb->data = t_data + ta->length;
			tb->length = length - ta->length;

			cxt.task->parent->unresolved_children.fetch_add(2);

			cxt.lane.push(ta);
			cxt.lane.push(tb);
		}

		void run_parallel(task_callback callback, transformer* data, std::size_t length) {

			auto & lane = get_this_lane();

			auto * parent = lane.alloc();
			parent->callback = callback;
			parent->unresolved_children.store (1);

			parent->data = data;
			parent->length = length;
			parent->parent = nullptr;

			auto * t = lane.alloc ();

			t->data 	= data;
			t->length 	= length;
			t->parent 	= parent;

			t->callback = &divide_and_conquer;

			lane.push (t);

			wait_for (parent);

			for (auto & flane : lanes)
				flane.alloc_free();
		}

		inline lane & get_this_lane () noexcept {
			auto id = std::this_thread::get_id();

			for (int i = 1; i < workers.size(); ++i) {
				if (workers[i].get_id() == id)
					return lanes [i];
			}

			return lanes [0];
		}

		std::vector < lane >		lanes;
		std::vector < std::thread >	workers;
		unsigned const			thread_count;
	};

}

namespace proto_d {

	struct task;
	struct lane;

	struct exec_context {
		proto_d::lane & lane;
		proto_d::task * task;
	};

	using task_callback = std::function < void (exec_context &, void *, std::size_t ) >;

	struct alignas (32) task {
	public:

		task_callback	callback;
		task const *	parent;
		transformer*	data;
		std::size_t		length;

		mutable std::atomic_size_t
			unresolved_children;
	};

	struct alignas (32) lane {
	public:

		void push(task* t) noexcept {
			auto b = back.load (std::memory_order_acquire);
			tasks[b & mask] = t;

			back.store (b + 1, std::memory_order_release);
		}

		task* pop() noexcept {
			auto b = --back;
			auto f = front.load(std::memory_order_acquire);

			if (f <= b) {
				task* t = tasks[b & mask];

				if (f != b)
					return t;

				auto f_plus{ f + 1 };
				if (!front.compare_exchange_strong(f, f_plus, std::memory_order_release, std::memory_order_acquire))
					t = nullptr;

				back.store(f_plus, std::memory_order_release);
				return t;
			}
			else {
				back.store(f, std::memory_order_release);
				return nullptr;
			}
		}

		task* steal() noexcept {
			auto f = front.load(std::memory_order_relaxed);
			auto b = back.load(std::memory_order_acquire);

			std::atomic_thread_fence(std::memory_order_acq_rel);

			if (f < b) {
				auto* t = tasks[f & mask];

				if (front.compare_exchange_strong(f, f + 1, std::memory_order_release))
					return t;
			}

			return nullptr;
		}

		inline task* alloc() noexcept {
			++task_buffer_index;
			return task_buffer.get() + task_buffer_index;
		}

		inline void alloc_free() noexcept {
			task_buffer_index = 0;
		}

		std::atomic_int64_t
			front{ 0 },
			back { 0 };

		// off critical path ( good ? / bad ? )
		std::unique_ptr < task* [] > tasks { new task * [4096] };
		std::unique_ptr < task [] >	 task_buffer { new task [4096] };

		std::size_t task_buffer_index{ 0 };

		bool
			running{ false };

		static constexpr std::size_t mask{ 4096 - 1 };
	};

	struct executor {
	public:

		executor(unsigned t_count) :
			lanes(t_count),
			workers (t_count),
			thread_count (t_count)
		{}

		~executor() {
			stop ();
		}

		inline void stop () {
			for (auto& lane : lanes) {
				lane.running = false;
			}

			for (int i = 1; i < thread_count; ++i) {
				if (workers [i].joinable())
					workers[i].join();
			}
		}

		inline static void run_lane(std::vector < lane > & lanes, lane & l, unsigned i) {
			auto* t = l.pop();

			if (!t) {
				auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (lanes.size());

				if (rand_index != i)
					t = lanes[rand_index].steal();
			}

			if (t) {
				exec_context cxt {l, t};
				t->callback(cxt, t->data, t->length);

				if (t->parent) {
					t->parent->unresolved_children.fetch_sub(1);
				}
			} else {
				std::this_thread::yield();
			}
		}

		void run() {
			if (lanes[0].running)
				return;

			lanes[0].running = true;

			// init workers
			for (unsigned i = 1; i < thread_count; ++i) {
				workers [i] = std::thread ([this, i]() {
					auto & lane = lanes[i];

					lane.running = true;

					while (lane.running)
						run_lane(lanes, lane, i);
				});
			}
		}

		void wait_for(task* t) {
			auto & lane = get_this_lane();

			while (t->unresolved_children.load() > 0) {
				run_lane(lanes, lane, 0);
			}
		}

		void static divide_and_conquer (exec_context & cxt, void * data, std::size_t length) {
			constexpr std::size_t slice_length { 1024 };
			auto * t_data = reinterpret_cast<transformer *> (data);

			auto * ta = cxt.lane.alloc ();
			auto * tb = cxt.lane.alloc ();

			ta->parent = tb->parent = cxt.task->parent;

			if (length > (cxt.task->parent->length / 48)) {
				ta->callback = tb->callback = &divide_and_conquer;
			} else {
				ta->callback = tb->callback = cxt.task->parent->callback;
			}

			ta->data = t_data;
			ta->length = length / 2;

			tb->data = t_data + ta->length;
			tb->length = length - ta->length;

			cxt.task->parent->unresolved_children.fetch_add(2);

			cxt.lane.push(ta);
			cxt.lane.push(tb);
		}

		void run_parallel(task_callback callback, transformer* data, std::size_t length) {

			auto & lane = get_this_lane();

			auto * parent = lane.alloc();
			parent->callback = callback;
			parent->unresolved_children.store (1);

			parent->data = data;
			parent->length = length;
			parent->parent = nullptr;

			auto * t = lane.alloc ();

			t->data 	= data;
			t->length 	= length;
			t->parent 	= parent;

			t->callback = &divide_and_conquer;

			lane.push (t);

			wait_for (parent);

			for (auto & flane : lanes)
				flane.alloc_free();
		}

		inline lane & get_this_lane () noexcept {
			auto id = std::this_thread::get_id();

			for (int i = 1; i < workers.size(); ++i) {
				if (workers[i].get_id() == id)
					return lanes [i];
			}

			return lanes [0];
		}

		std::vector < lane >		lanes;
		std::vector < std::thread >	workers;
		unsigned const			thread_count;
	};

}

void BASELINE(benchmark::State& state) {

/*

	std::vector < std::future < void > > sync(THREAD_COUNT);

	auto * data = test_data.data();

	for (auto _ : state) {

		auto len = state.range(0);
		auto stride = len / THREAD_COUNT;
		auto rem = len % THREAD_COUNT;

		for (unsigned i = 0; i < THREAD_COUNT; ++i) {
			auto offset = i * stride;

			if (i == THREAD_COUNT - 1)
				stride = stride + rem;

			sync[i] = std::async(std::launch::async, [=]() {
				for (unsigned j = offset; j < stride; ++j)
					data[j].update_matrix();
				});
		}

		for (unsigned i = 0; i < THREAD_COUNT; ++i)
			sync[i].get();
	}
 */
	for (auto _ : state) {
		std::for_each (
			std::execution::par,
			std::begin (test_data),
			std::begin (test_data) + state.range(0),
			[] (transformer & v) {
				v.update_matrix();
			}
		);
	}
}

//proto_a::executor exe_a (THREAD_COUNT - 1);
proto_c::executor exe_c(THREAD_COUNT);
proto_d::executor exe_d(THREAD_COUNT);

void PROTO_C(benchmark::State& state) {

	exe_c.run();

	for (auto _ : state) {

		auto range = state.range(0);

		exe_c.run_parallel([](proto_c::exec_context &, void * data, std::size_t length) {
				auto * t_data = reinterpret_cast < transformer * > (data);

				for (std::size_t i = 0; i < length; ++i)
					t_data[i].update_matrix();
			},
			test_data.data(),
			range);
	}

	exe_c.stop();
}

void PROTO_D(benchmark::State& state) {

	exe_d.run();

	for (auto _ : state) {

		auto range = state.range(0);

		exe_d.run_parallel([](proto_d::exec_context &, void * data, std::size_t length) {
							   auto * t_data = reinterpret_cast < transformer * > (data);

							   for (std::size_t i = 0; i < length; ++i)
								   t_data[i].update_matrix();
						   },
						   test_data.data(),
						   range);
	}
}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

/*
BENCHMARK(SequentialTransforms)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
*/
BENCHMARK(BASELINE)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

//BENCHMARK(PROTO_C)
//->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK(PROTO_D)
->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
/*
BENCHMARK(ParallelTransforms_A)
->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
*/

BENCHMARK_MAIN();