#include <benchmark/benchmark.h>

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

#define MIN_ITERATION_RANGE 1 << 20
#define MAX_ITERATION_RANGE 1 << 22

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
	vec3 position;
	quat orientation;
	vec3 scale;

	inline void update_matrix () {
		matrix = glm::scale(
			translate(
				mat4_cast(orientation), 
				position), 
			scale);
	}

	mat4 matrix;
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
namespace proto {

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

	};

	using spin_lock = std::unique_lock < spin_mutex >;

	using callable_type = std::function < void(void) >;

	struct task {
	public:

		void run() {
			if (callback) {
				callback();
				executed = true;

				if (parent)
					--(parent->dependencies);
			}
		}

		bool is_complete() const {
			return dependencies == 0 || executed;
		}

		task () = default;

		task (callable_type && m_callback, task * p_parent) :
			callback { std::move (m_callback) },
			parent {p_parent}
		{}

		callable_type		callback {};

		task *				parent { nullptr };
		std::atomic_uint 	dependencies { 0 };

		bool				executed { false };
	};

	struct task_lane {
	public:

		static constexpr std::size_t mask { 4096 - 1 };

		void push (task * tast_inst) {
			auto b = bottom.load ();
			tasks [b & mask] = tast_inst;
			++bottom;
		}

		task * pop () {
			--bottom;
			auto b = bottom.load();
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

		bool is_empty() const {
			return top == bottom;
		}

		spin_mutex	mutex;

	private:

		std::array  < task*, 4096 >
			tasks;

		std::atomic_size_t top{0};
		std::atomic_size_t bottom {0};
	};

	template < typename _t, std::size_t capacity >
	struct allocator {
	public:

		inline void clear () {
			_index = 0;
		}

		template < typename ... _args_tv >
		inline task * alloc (_args_tv && ... args) {
			if (_index == capacity)
				throw std::runtime_error ("Allocator capacity exceeded");

			task * t = &_data [_index];
			new (t) _t (std::forward < _args_tv > (args)...);

			++_index;
			return t;
		}

	private:
		std::array < _t, capacity > _data;
		std::size_t 				_index {0};

	};

	struct executor {
	public:

		executor(std::size_t worker_count) :
			_workers(worker_count)
		{}

		void clear_alloc () {
			my_alloc.clear();
		}

		void wait_for (task * t) {
			while (!t->is_complete()) {
				run_lane();
			}
		}

		void run_lane () {
			auto* t = next();

			if (t) {
				t->run();
			} else {
				t = steal();

				if (t)
					t->run();
				else
					std::this_thread::yield();
			}
		}

		void run() {
			// setup workers
			for (int i = 0; i < _workers.size(); ++i) {
				_workers[i] = std::thread(
					[](executor * inst) {
						while (inst->_running)
							inst->run_lane();
					}, 
					this
				);
			}
		}

		void stop() {
			_running = false;

			for (auto& worker : _workers) {
				if (worker.joinable())
					worker.join();
			}
		}

		task* next() {
			auto& lane = get_thread_local_lane();
			//spin_lock lane_lock { lane.mutex };

			if (lane.is_empty())
				return nullptr;

			return lane.pop();
		}

		task* steal() {
			/*
			auto it = _thread_lanes.find(std::this_thread::get_id());
			
			if (it == _thread_lanes.end())
				return nullptr;

			auto my_thread_it = it;

			task * t{ nullptr };

			do {
				{
					spin_lock lane_lock { it->second.mutex, std::try_to_lock };

					if (lane_lock && !it->second.tasks.empty()) {
						t = it->second.tasks.front();
						it->second.tasks.pop_front();

						break;
					}
				}

				++it;

				if (it == _thread_lanes.end())
					it = _thread_lanes.begin();

			} while (it != my_thread_it);

			return t;
			*/

			auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (_thread_lanes.size());

			auto it = std::next(_thread_lanes.begin(), rand_index);

			if (it->first == std::this_thread::get_id()) {
				return nullptr;
			}

			return it->second.steal();
		}

		task_lane& get_thread_local_lane() {
			spin_lock lock(_lane_mutex);
			return _thread_lanes[std::this_thread::get_id()];
		}

		template < typename _callback_t, typename _t >
		task* push_stream_task(_callback_t&& callback, _t* data, std::size_t len) {
			auto * t = new task ();

			push_stream (std::forward < _callback_t > (callback), data, data + len, t);
			return t;
		}

		template < typename _callback_t, typename _t >
		inline void parallel_for (_callback_t && callback, _t * data, std::size_t length) {
			auto * t = push_stream_task (std::forward < _callback_t > (callback), data, length);
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

		inline static int gcd (int a, int b) noexcept {
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
		template < typename _callback_t, typename _t >
		void push_stream(_callback_t&& callback, _t* begin, _t* end, task * parent) {
			auto constexpr type_size { sizeof(_t) };
			auto const block_size { gcd (type_size, cache_size) };

			auto len = end - begin;

			auto blocks = len / block_size;
			auto overflow = len % block_size;

			auto worker_count = _workers.size() + 1; // +1 = main thread
			auto blocks_per_thread = blocks / worker_count;

			overflow += (blocks_per_thread % worker_count) * block_size;

			// -------------------------------------
			// create tasks
			// -------------------------------------
			auto & lane = get_thread_local_lane ();
			//spin_lock lane_lock { lane.mutex };

			if (blocks_per_thread > 0) {
				parent->dependencies = worker_count;

				auto * cursor = begin;

				for (int i = 0; i < worker_count; ++i) {
					int items = blocks_per_thread * block_size;

					if (overflow >= cache_size) {
						items += cache_size;
						overflow -= cache_size;
					} else if (i == worker_count - 1) {
						items += overflow;
					}

					auto cursor_end = cursor + items;

					// add task
					auto * t = my_alloc.alloc ();

					t->callback = [=]() {
						callback(cursor, cursor_end);
					};

					t->parent = parent;

					lane.push(t);

					cursor = cursor_end;
				}
			} else {
				parent->dependencies = 1;
				// add task
				auto * t = my_alloc.alloc ([=]() {
					callback (begin, end);
				}, parent);

				lane.push (t);
			}
		}

		allocator < task, 4096 > my_alloc;
		std::vector < std::thread > 
							_workers;
		std::atomic_bool	_running;

		spin_mutex			_lane_mutex;
		std::map < std::thread::id, task_lane > 
							_thread_lanes;
	};
}

void SequentialTransforms (benchmark::State& state) {
	for (auto _ : state) {
		auto range = state.range(0);

		for (int i = 0; i < range; ++i) {
			test_data [i].update_matrix ();
		}
	}
}

void ParallelTransforms(benchmark::State& state) {

	proto::executor exe (7);
	exe.run ();

	for (auto _ : state) {

		auto range = state.range(0);
		exe.clear_alloc();

		exe.parallel_for (
			[=](transformer * begin, transformer * end) {
				auto * it = begin;
				for (; it != end; ++it) {
					it->update_matrix();
				}
			}, 
			test_data.data (), range);
	}

	exe.stop();

}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(SequentialTransforms)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
BENCHMARK(ParallelTransforms)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();