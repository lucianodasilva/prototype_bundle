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

	inline void to_matrix(mat4 & dest) const {
		dest = glm::scale(
			translate(
				mat4_cast(orientation), 
				position), 
			scale);
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

		std::unique_lock < std::mutex > lock() {
			return std::unique_lock < std::mutex > { mutex };
		}

		std::unique_lock < std::mutex > try_lock() {
			return std::unique_lock < std::mutex > (mutex, std::try_to_lock);
		}

		static_ring_buffer  < task *, 4098 > 
					tasks;
		std::mutex	mutex;

	};

	struct executor {
	public:

		executor(std::size_t worker_count) :
			_workers(worker_count)
		{}

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
			auto lock{ lane.lock() };

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

			task* task{ nullptr };

			do {
				if (it == _thread_lanes.end())
					it = _thread_lanes.begin();

				{ 
					auto lock = it->second.try_lock(); 

					if (lock) {
						task = it->second.tasks.front();
						it->second.tasks.pop_front();

						break;
					}
				}

				++it;
			} while (it != my_thread_it);

			return task;
		}

		task_lane& get_thread_local_lane() {
			std::unique_lock < std::mutex > lock(_lane_mutex);
			return _thread_lanes[std::this_thread::get_id()];
		}

		template < typename _callback_t, typename _t >
		task* push_stream_task(_callback_t&& callback, _t* data, std::size_t len) {


		}

	private:

		template < typename _callback_t, typename _t >
		void push_stream(_callback_t&& callback, _t* begin, _t* end) {
			auto len = begin - end;

			if (len * sizeof(_t) > 32) {
				auto* mid = begin + (len / 2);
				push_stream(std::forward(callback), begin, mid);
				push_stream(std::forward(callback), mid, end);
			}
			else {
				task * task = new task (par)
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
	auto range = state.range(0);

	state.PauseTiming();
	auto data = generate_data(range);
	auto result = std::vector < glm::mat4 > (range);
	state.ResumeTiming();

	for (auto _ : state) {
		for (int i = 0; i < range; ++i) {
			data[i].to_matrix(result [i]);
		}
	}
}

// void ParallelTransforms(benchmark::State& state) {}

#define MIN_ITERATION_RANGE 1 << 16
#define MAX_ITERATION_RANGE 1 << 24

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(SequentialTransforms)->RANGE->Unit(benchmark::TimeUnit::kMillisecond);
//BENCHMARK(ParallelTransforms)->RANGE;

BENCHMARK_MAIN();