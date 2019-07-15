#include <benchmark/benchmark.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>

#include <atomic>
#include <functional>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>

#include <unistd.h>

#include "static_ring_buffer.hpp"


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

// prototype implementation
namespace proto {

	using callable_type = std::function < void(void) >;

	struct task {
	public:

		bool is_complete() const {
			return dependencies == 0 || _complete;
		}

		void run() {
			if (_callback) {
				_callback();
				_complete = true;

				if (_parent)
					--(_parent->dependencies);
			}
		}

		task() = default;

		task(callable_type&& callback, task* parent) : _callback{ std::move(callback) }, _parent { parent }
		{}

		std::atomic_uint dependencies{ 0 };

	private:
		callable_type	_callback{};
		task *			_parent{ nullptr };
		bool			_complete{ false };
	};

	struct task_lane {
	public:
		static_ring_buffer  < task *, 4098 > 
					tasks;
		std::mutex	mutex;

	};

	struct executor {
	public:

		executor(std::size_t worker_count) :
			_workers(worker_count)
		{}

		void wait_for (task * t) {
			while (!t->is_complete()) {
				run_lane();
			}
		}

		void run_lane () {
			auto* t = next();

			if (t) {
				t->run();
			}
			else {
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
						while (inst->_running) {
							inst->run_lane();
						}
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
			std::unique_lock < std::mutex > lane_lock { lane.mutex };

			if (lane.tasks.empty())
				return nullptr;

			auto* t = lane.tasks.back();
			lane.tasks.pop_back();
			return t;
		}

		task* steal() {
			auto it = _thread_lanes.find(std::this_thread::get_id());
			
			if (it == _thread_lanes.end())
				return nullptr;

			auto my_thread_it = it;

			task * t{ nullptr };

			do {
				{
					std::unique_lock < std::mutex > lane_lock { it->second.mutex, std::try_to_lock };

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
		}

		task_lane& get_thread_local_lane() {
			std::unique_lock < std::mutex > lock(_lane_mutex);
			return _thread_lanes[std::this_thread::get_id()];
		}

		template < typename _callback_t, typename _t >
		task* push_stream_task(_callback_t&& callback, _t* data, std::size_t len) {
			auto * t = new task ();

			push_stream (std::forward < _callback_t > (callback), data, data + len, t);
			return t;
		}

		template < typename _callback_t, typename _t >
		inline void parallel_for (_callback_t && callback, std::vector < _t > & data) {
			auto * t = push_stream_task (std::forward < _callback_t > (callback), data.data(), data.size());
			wait_for (t);
		}

	private:

		long const cache_size { sysconf (_SC_LEVEL1_DCACHE_LINESIZE) };


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
			std::unique_lock < std::mutex > lane_lock { lane.mutex };

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
					auto * t = new task ([=]() {
						callback (cursor, cursor_end);
						--parent->dependencies;
					}, parent);

					lane.tasks.push_back (t);

					cursor = cursor_end;
				}
			} else {
				parent->dependencies = 1;
				// add task
				auto * t = new task ([=]() {
					callback (begin, end);
					--parent->dependencies;
				}, parent);

				lane.tasks.push_back (t);
			}
		}

		std::vector < std::thread > 
							_workers;
		std::atomic_bool	_running;

		std::mutex			_lane_mutex;
		std::map < std::thread::id, task_lane > 
							_thread_lanes;
	};
}

void SequentialTransforms (benchmark::State& state) {


	for (auto _ : state) {
		auto range = state.range(0);

		state.PauseTiming();
		auto data = generate_data(range);
		state.ResumeTiming();

		for (int i = 0; i < range; ++i) {
			data[i].update_matrix ();
		}

		state.PauseTiming();
		data.clear();
		state.ResumeTiming();
	}
}

void ParallelTransforms(benchmark::State& state) {

	proto::executor exe (7);
	exe.run ();

	for (auto _ : state) {

		auto range = state.range(0);

		state.PauseTiming();
		auto data = generate_data(range);
		state.ResumeTiming();

		exe.parallel_for ([=](transformer * begin, transformer * end) {
			auto * it = begin;
			for (; it != end; ++it) {
				it->update_matrix();
			}
		}, data);

		state.PauseTiming();
		data.clear();
		state.ResumeTiming();
	}

	exe.stop();

}

#define MIN_ITERATION_RANGE 1 << 10
#define MAX_ITERATION_RANGE 1 << 16

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

//BENCHMARK(SequentialTransforms)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
BENCHMARK(ParallelTransforms)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();