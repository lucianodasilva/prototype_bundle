#include <benchmark/benchmark.h>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>

#include <array>
#include <atomic>
#include <vector>
#include <future>
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

#define MIN_ITERATION_RANGE 1 << 14
#define MAX_ITERATION_RANGE 1 << 24
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
		matrix = glm::mat4_cast (orientation) * matrix;
		glm::scale (matrix, scale);
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
		
		void push(task* task_inst) {
			auto index = bottom.load (std::memory_order_acquire);
			tasks[index & mask] = task_inst;

			bottom.store(index + 1, std::memory_order_release);
			++tasks_pushed;
		}

		/*
		task* pop() {
			auto b = bottom.load();
			auto t = top.load();

			if (t < b) {
				auto* task = tasks[b - 1];

				if (bottom.compare_exchange_weak(b, b - 1))
					return task;
			}

			return nullptr;
		}

		
		void push (task * tast_inst) {
			auto b = bottom.load ();
			tasks [b & mask] = tast_inst;

			bottom.store(b + 1);
			++tasks_pushed;
		}*/

		task * pop () {
			auto b = bottom.fetch_sub(1) - 1;
			auto t = top.load ();

			if (t <= b) {
				task * task_instance = tasks [b & mask];

				if (t != b)
					return task_instance;

				auto t_plus { t + 1 };
				if (!top.compare_exchange_strong (t, t_plus))
					task_instance = nullptr;

				bottom.store (t_plus);
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

				if (top.compare_exchange_strong(t, t + 1))
					return task_inst;
			} 

			return nullptr;
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
		std::atomic_bool		running;

		std::size_t				tasks_executed{ 0 };
		std::size_t				tasks_pushed { 0 };
	};

	struct executor {
	public:

		executor(std::size_t worker_count) :
			_lanes(worker_count + 1)
		{}

		~executor () {
			std::cout << "executions: " << executions << std::endl;
			stop();
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
			if (_lanes[0].running)
				return;

			// setup workers
			_lanes[0].id = std::this_thread::get_id();
			_lanes[0].exec = this;
			_lanes[0].running = true;

			for (int i = 1; i < _lanes.size(); ++i) {
				_lanes[i].worker = std::thread(
					[](executor * exec, task_lane * lane) {
						lane->exec = exec;
						lane->id = std::this_thread::get_id();
						lane->running = true;

						while (lane->running)
							lane->run_lane();
					},
					this,
					& _lanes[i]
				);
			}
		}

		void stop() {

			for (auto& lane : _lanes) {
				lane.running = false;

				if (lane.worker.joinable())
					lane.worker.join();

				std::cout << "lane for thread: " << lane.id << " | pushed: " << lane.tasks_pushed << " | executed: " << lane.tasks_executed << std::endl;
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

		std::size_t executions{ 0 };

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

	};

	void task_lane::run_lane() {
		task* t = pop();

		if (!t)
			t = exec->steal();

		if (t) {
			t->run();
			++tasks_executed;
		}

		std::this_thread::yield();
	}

}

namespace proto_b {

	using task_callback = void (*)(transformer*, std::size_t);

	struct task {
	public:
		
		task_callback	callback;
		task*			parent;
		transformer*	data;
		std::size_t		length;

		std::atomic_size_t
						unresolved_children;

		std::uint8_t	_padding_ [24];
	};

	struct lane {
	public:

		void push(task* t) noexcept {
			auto b = back.load(std::memory_order_acquire);
			tasks[b & mask] = t;
			back.store(b + 1, std::memory_order_release);
		}

		task* pop() noexcept {
			auto b = back.fetch_sub(1) - 1;
			auto f = front.load();

			if (f <= b) {
				task* t = tasks[b & mask];

				if (f != b)
					return t;

				auto f_plus{ f + 1 };
				if (!front.compare_exchange_strong(f, f_plus))
					t = nullptr;

				back.store(f_plus);
				return t;
			}
			else {
				back.store(f);
				return nullptr;
			}
		}

		task* steal() noexcept {
			auto f = front.load();
			auto b = back.load();

			if (f < b) {
				auto* t = tasks[f & mask];

				if (front.compare_exchange_strong(f, f + 1))
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

		std::atomic_size_t
			front{ 0 },
			back { 0 };

		std::atomic_bool
			running{ false };

		// off critical path ( good ? / bad ? )
		std::unique_ptr < task* [] > tasks { new task * [4096] };
		std::unique_ptr < task [] >	 task_buffer { new task [4096] };

		std::size_t task_buffer_index{ 0 };

		uint8_t _padding_ [16];

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
			for (auto& lane : lanes)
				lane.running = false;
		}

		inline static void run_lane(std::vector < lane > & lanes, lane & l, unsigned i) {
			auto* t = l.pop();

			if (!t) {
				auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (lanes.size());

				if (rand_index != i)
					t = lanes[rand_index].steal();
			}

			if (t) {
				t->callback(t->data, t->length);

				if (t->parent)
					--(t->parent->unresolved_children);
			}

			std::this_thread::yield();
		}

		void run() {
			if (lanes[0].running)
				return;

			lanes[0].running = true;

			// init workers 
			for (unsigned i = 1; i < thread_count; ++i) {
				workers [i] = std::thread ([this, i]() {
					auto& lane = lanes[i];

					lane.running = true;

					while (lane.running)
						run_lane(lanes, lane, i);
				});
			}
		}

		void wait_for(task* t) {
			while (t->unresolved_children > 0) {
				run_lane(lanes, lanes[0], 0);
			}	
		}

		void run_parallel(task_callback callback, transformer* data, std::size_t length) {

			auto& lane = lanes[0];

			task* parent = lane.alloc();
			parent->unresolved_children = thread_count;

			auto stride = length / thread_count;
			auto rem = length % thread_count;

			for (unsigned i = 0; i < thread_count; ++i) {
				auto offset = i * stride;

				if (i == thread_count - 1)
					stride = stride + rem;

				task* t = lane.alloc();
				
				t->callback = callback;
				t->data = data + offset;
				t->length = stride;
				t->parent = parent;

				lane.push(t);
			}

			wait_for(parent);
			lane.alloc_free();
		}

		std::vector < lane >	lanes;
		std::vector < std::thread >	workers;
		unsigned const			thread_count;
	};

}

void SequentialTransforms (benchmark::State& state) {
	for (auto _ : state) {
		auto range = state.range(0);

		for (int64_t i = 0; i < range; ++i)
			test_data[i].update_matrix();
	}
}

#define THREAD_COUNT 4

void CPPAsyncTransforms(benchmark::State& state) {

	std::vector < std::future < void > > sync(THREAD_COUNT);

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
					test_data[j].update_matrix();
				});
		}

		for (unsigned i = 0; i < THREAD_COUNT; ++i)
			sync[i].get();
	}
}

proto_a::executor exe_a (THREAD_COUNT - 1);
proto_b::executor exe_b(THREAD_COUNT);

void parallel_for_runner (void * begin, std::size_t length) {
	auto * it = static_cast < transformer * > (begin);

	for (std::size_t i = 0; i < length; ++i)
		it[i].update_matrix();
}

void ParallelTransforms_A(benchmark::State& state) {

	exe_a.run();

	for (auto _ : state) {

		++exe_a.executions;

		auto range = state.range(0);
		exe_a.clear_alloc();

		exe_a.parallel_for (
			parallel_for_runner,
			test_data.data (), range);
	}
}

void ParallelTransforms_B(benchmark::State& state) {

	exe_b.run();

	for (auto _ : state) {

		auto range = state.range(0);

		exe_b.run_parallel([](transformer* data, std::size_t length) {
				for (std::size_t i = 0; i < length; ++i)
					data[i].update_matrix();
			},
			test_data.data(),
			range);
	}
}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)
/*
BENCHMARK(SequentialTransforms)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK(CPPAsyncTransforms)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK(ParallelTransforms_A)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
	*/
BENCHMARK(ParallelTransforms_B)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();