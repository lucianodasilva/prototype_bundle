#include <benchmark/benchmark.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>

#include <vector>

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

	inline void to_matrix(mat4 * dest) const {
		*dest = glm::scale(
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
			// callable
			task * parent;

			virtual void execute() = 0;
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


}

void SequentialTransforms (benchmark::State& state) {

}

void ParallelTransforms(benchmark::State& state) {

}

#define MIN_ITERATION_RANGE 1 << 8
#define MAX_ITERATION_RANGE 1 << 16

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(SequentialTransforms)->RANGE;
BENCHMARK(ParallelTransforms)->RANGE;

BENCHMARK_MAIN();