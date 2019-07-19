#include <benchmark/benchmark.h>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>

#include <array>
#include <atomic>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>

#if defined (_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#include <functional>

#endif

#include "static_ring_buffer.hpp"

#define MIN_ITERATION_RANGE 1 << 18
#define MAX_ITERATION_RANGE 1 << 18

using namespace glm;

// data and tasks 
struct camera {
	vec3 position;
	vec3 target;
	vec3 up;

	mat4 to_matrix() const {
		return lookAt(position, target, up);
	}
};

struct transformer {
	mat4 matrix;

	vec3 position;
	quat orientation;
	vec3 scale;

	inline void update_matrix () {
		matrix = glm::translate (matrix, position);
		//matrix = glm::mat4_cast (orientation) * matrix;
		//glm::scale (matrix, scale);
	}
};

std::vector < transformer > generate_data(std::size_t count) {
	std::vector < transformer > data(count);

	for (std::size_t i = 0; i < count; ++i) {
		data[i].position =		glm::linearRand(vec3(-1.0, -1.0, -1.0), vec3(1.0, 1.0, 1.0));
		data[i].orientation =	glm::linearRand(vec3(-1.0, -1.0, -1.0), vec3(1.0, 1.0, 1.0));
		data[i].scale = 		glm::linearRand(vec3(-1.0, -1.0, -1.0), vec3(1.0, 1.0, 1.0));
	}

	return data;
}

auto test_data{ generate_data(MAX_ITERATION_RANGE) };

// prototype implementation
namespace proto_a {
	/*
	struct spin_mutex {
	public:

		inline bool try_lock() {
			return !_lockless_flag.test_and_set(std::memory_order_acquire);
		}

		inline void lock() {
			while (_lockless_flag.test_and_set(std::memory_order_acquire)) {}
		}

		inline void unlock() {
			_lockless_flag.clear(std::memory_order_release);
		}

	private:

		std::atomic_flag _lockless_flag = ATOMIC_FLAG_INIT;

	};*/

	//using spin_lock = std::unique_lock < spin_mutex >;

	using callable_type = void(*)(void * begin, std::size_t length);

	struct task {
	public:

		void run() {
			if (callback)
				callback(data, data_length);

			if (parent)
				--parent->unfinished_tasks;
		}

		inline bool is_complete() const noexcept {
			return unfinished_tasks == 0;
		}

		task () = default;

		task (callable_type m_callback, task * p_parent) :
			callback { m_callback },
			parent {p_parent}
		{}

		callable_type		callback{};
		std::atomic_size_t	unfinished_tasks { 0 };
		task *				parent { nullptr };
		void*				data{ nullptr };
		std::size_t			data_length{ 0 };
		std::uint8_t 		_pad_[24]{};
	};

	template < typename _t >
	struct allocator {
	public:

		inline void clear() {
			_index = 0;
		}

		template < typename ... _args_tv >
		inline task* alloc(_args_tv&& ... args) {
			if (_index == _data.size())
				throw std::runtime_error("Allocator capacity exceeded");

			task* t = &_data[_index];
			new (t) _t(std::forward < _args_tv >(args)...);

			++_index;
			return t;
		}

	private:
		std::vector < _t >	_data{ 4096 };
		std::size_t 		_index{ 0 };

	};

	struct executor;

	struct task_lane {
	public:

		static constexpr std::size_t mask { 4096 - 1 };

		void push (task * tast_inst) {
			auto b = bottom.load ();
			tasks [b & mask] = tast_inst;
			++bottom;
		}

		task * pop () {
			auto b = bottom.fetch_sub(1) - 1;
			auto t = top.load ();

			if (t <= b) {
				task * task_instance = tasks [b & mask];

				if (t != b)
					return task_instance;

				auto tmp_t{ t };
				if (!top.compare_exchange_weak (tmp_t, t + 1))
					task_instance = nullptr;

				bottom.store (t + 1);
				return task_instance;
			} else {
				bottom.store(t);
				return nullptr;
			}
		}

		task * steal () {
			auto t = top.load ();
			auto b = bottom.load ();

			if (t < b)  {
				auto * task_inst = tasks [t & mask];

				if (!top.compare_exchange_weak(t, t + 1))
					return nullptr;

				return task_inst;
			} else {
				return nullptr;
			}
		}

		bool is_empty() const noexcept {
			return top == bottom;
		}

		void run_lane();

		std::atomic_size_t		top{ 0 };
		std::atomic_size_t		bottom{ 0 };

		executor*				exec;
		std::thread::id			id;

		allocator < task >		local_allocator;

		std::vector < task* >	tasks{ 4096 };

		std::thread				worker;

		
	};

	struct executor {
	public:

		executor(std::size_t worker_count) :
			_lanes(worker_count + 1)
		{}

		~executor () {
			stop();
		}

		inline bool is_running () const noexcept {
			return _running;
		}

		void clear_alloc () {
			for (auto& lane : _lanes)
				lane.local_allocator.clear();
		}

		void wait_for (task * t) {
			auto& lane = this->get_thread_local_lane();

			while (!t->is_complete())
				lane.run_lane();
		}

		void run() {
			// setup workers
			_running = true;

			_lanes[0].id = std::this_thread::get_id();
			_lanes[0].exec = this;

			for (int i = 1; i < _lanes.size(); ++i) {
				_lanes[i].worker = std::thread(
					[](executor * exec, task_lane * lane) {
						lane->exec = exec;
						lane->id = std::this_thread::get_id();

						while (exec->_running)
							lane->run_lane();
					},
					this,
					& _lanes[i]
				);
			}
		}

		void stop() {
			_running = false;

			for (auto& lane : _lanes) {
				if (lane.worker.joinable())
					lane.worker.join();
			}
		}

		task* steal() {
			auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (_lanes.size());
			return _lanes[rand_index].steal();
		}

		task_lane & get_thread_local_lane() {
			auto id = std::this_thread::get_id();

			for (auto & lane : _lanes) {
				if (lane.id == id)
					return lane;
			}

			return _lanes[0];
		}

		template < typename _callback_t, typename _t >
		inline void parallel_for (_callback_t callback, _t * data, std::size_t length) {
			
			auto * t = get_thread_local_lane().local_allocator.alloc();
			push_stream(callback, data, length, t);
			
			wait_for (t);
		}

	private:
#if defined (_WIN32)
		long const cache_size{
			[]() -> long {
				long line_size = 0;
				DWORD buffer_size = 0;

				GetLogicalProcessorInformationEx(RelationCache, nullptr, &buffer_size);
				if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
					return -1; // TODO: perhaps log

				auto * buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)malloc(buffer_size);

				GetLogicalProcessorInformationEx(RelationCache, buffer, &buffer_size);

				for (int i = 0; i != buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX); ++i) {
					if (buffer[i].Cache.Level == 1) {
						line_size = buffer[i].Cache.LineSize;
						break;
					}
				}

				free(buffer);
				return line_size;

			}()
		};
#else
		long const cache_size { sysconf (_SC_LEVEL1_DCACHE_LINESIZE) };
#endif

		inline static std::size_t gcd (std::size_t a, std::size_t b) noexcept {
			//return a == 0 ? b : gcd (b % a, a);

			for (;;) {
				a %= b;
				if (a == 0)
					return b;
				b %= a;
				if (b == 0)
					return a;
			}
		}

		// Calculate workload and create tasks
		template < typename _t >
		void push_stream(callable_type callback, _t* begin, std::size_t len, task * parent) {
/*
			// divide per cache line multiple --------------------------------
			auto constexpr type_size { sizeof(_t) };
			auto const block_size { gcd (type_size, cache_size) * 4 };

			auto blocks = len / block_size;
			auto rem = len % block_size;

			decltype (len) offset { 0 };

			// -------------------------------------
			// create tasks
			// -------------------------------------
			auto & lane = get_thread_local_lane ();
			parent->dependencies = blocks + (rem > 0 ? 1 : 0);

			while (offset < len) {
				// add task
				auto * t = lane.local_allocator.alloc (callback, parent);

				t->data 		= begin + offset;
				t->data_length 	= block_size;

				offset 			+= block_size;

				if (offset < len)
					t->data_length = block_size;
				else
					t->data_length = rem;

				lane.push(t);
			}
*/

			// divide per worker --------------------------------

			auto constexpr type_size { sizeof(_t) };
			auto const block_size { gcd (type_size, cache_size) };

			auto blocks = len / block_size;
			auto overflow = len % block_size;

			auto worker_count = _lanes.size();
			auto blocks_per_thread = blocks / worker_count;

			overflow += (blocks_per_thread % worker_count) * block_size;

			// -------------------------------------
			// create tasks
			// -------------------------------------
			auto & lane = get_thread_local_lane ();

			if (blocks_per_thread > 0) {
				parent->unfinished_tasks.store(worker_count, std::memory_order_relaxed);

				auto * cursor = begin;

				for (int i = 0; i < worker_count; ++i) {
					auto items = blocks_per_thread * block_size;

					if (overflow >= cache_size) {
						items += cache_size;
						overflow -= cache_size;
					} else if (i == worker_count - 1) {
						items += overflow;
					}

					auto cursor_end = cursor + items;

					// add task
					auto * t = lane.local_allocator.alloc (callback, parent);
					t->data = cursor;
					t->data_length = items;

					lane.push(t);

					cursor = cursor_end;
				}
			} else {
				parent->unfinished_tasks.store(1, std::memory_order_relaxed);
				// add task
				auto * t	= lane.local_allocator.alloc (callback, parent);
				t->data	= begin;
				t->data_length = len;

				lane.push (t);
			}

			// linear

			/*

			auto& lane = get_thread_local_lane();

			auto len = end - begin;
			auto lane_count = _lanes.size();
			auto task_size = len / lane_count;

			parent->dependencies = lane_count;

			for (int i = 0; i < lane_count; ++i) {
				auto* t = lane.local_allocator.alloc(callback, parent);
				t->begin = begin;
				begin += task_size;
				t->end = begin;

				lane.push(t);
			}

			*/
		}

		std::vector < task_lane > 
							_lanes;
		std::atomic_bool	_running;

	};

	void task_lane::run_lane() {
		task* t = pop();

		if (!t)
			t = exec->steal();

		if (t)
			t->run();

		std::this_thread::yield();
	}

}

void SequentialTransforms (benchmark::State& state) {
	for (auto _ : state) {
		auto range = state.range(0);

		for (int64_t i = 0; i < range; ++i)
			test_data[i].update_matrix();
	}
}

proto_a::executor exe_a (3);

void parallel_for_runner (void * begin, std::size_t length) {
	auto * it = static_cast < transformer * > (begin);

	for (std::size_t i = 0; i < length; ++i)
		it[i].update_matrix();
}

void ParallelTransforms(benchmark::State& state) {

	if (!exe_a.is_running())
		exe_a.run();

	for (auto _ : state) {

		auto range = state.range(0);
		exe_a.clear_alloc();

		exe_a.parallel_for (
			parallel_for_runner,
			test_data.data (), range);
	}
}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(SequentialTransforms)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK(ParallelTransforms)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();