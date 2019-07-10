#include <benchmark/benchmark.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>

#include <atomic>
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

	namespace details {

		class task {
		public:

			task * parent;
			std::atomic < int > dependencies;

			bool is_done();

			bool can_run () {
				return (!parent || parent->is_done());
			}

		private:
		};

		struct task_set {
		public:

			void push(task * t) {
				_tasks.push_back(t);
			}

			task * pop() {
				if (_tasks.empty())
					return nullptr;

				task * t = _tasks.front();
				_tasks.pop_front();

				return t;
			}

			task* steal() {

			}
			
		private:
			static_ring_buffer < task *, 4096 > _tasks;
		};

	}

	struct executor {
	public:

		executor(std::size_t worker_count = std::thread::hardware_concurrency() - 1) :
			_workers(worker_count)
		{}

		void run() {

			// setup workers
			_workers.emplace_back ([this] {
				for (;;) {
					{
						std::unique_lock < typename base_t::mutex_t > lock(_task_mutex);

						if (!this->_run && this->_tasks.empty())
							return;

						_task_condition.wait(
							lock,
							[this] { return !(this->_tasks.empty() && this->_run); }
						);

						std::swap(this->_tasks, this->_executing_tasks);
						this->_tasks.resize(0);
					}

					for (auto& task : this->_tasks)
						task();
				}
				});
		}

	private:
		std::vector < std::thread > _workers;
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