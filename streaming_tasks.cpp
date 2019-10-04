#include <benchmark/benchmark.h>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <atomic>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <new>

#define MIN_ITERATION_RANGE (1U << 14U)
#define MAX_ITERATION_RANGE (1U << 22U)

#define THREAD_COUNT 8

using namespace glm;

struct transformer {
	glm::mat4 matrix;

	glm::vec3 pos;
	glm::vec3 rotation;
	glm::vec3 scale;

	inline void update_matrix() {
		benchmark::DoNotOptimize(matrix);
		matrix = glm::mat4{1.0f};

		matrix = glm::scale(matrix, scale);
		matrix = glm::rotate(matrix, 1.0f, rotation);
		matrix = glm::translate(matrix, pos);

		matrix = glm::inverse(matrix);
		matrix = glm::transpose(matrix);
	}
};

std::vector<transformer> test_data(MAX_ITERATION_RANGE);

namespace proto_d {

	using task_callback = std::function<void()>;

	struct alignas(64) task {
	public:

		task_callback callback;
		task *parent;

		std::atomic_size_t
			unresolved_children;
	};

	struct alignas(64) lane {
	public:

		void push(task *t) noexcept {
			auto b = back.load(std::memory_order_acquire);
			tasks[b & mask] = t;

			back.store(b + 1, std::memory_order_release);
		}

		task *pop() noexcept {
			auto b = --back;
			auto f = front.load(std::memory_order_acquire);

			if (f <= b) {
				task *t = tasks[b & mask];

				if (f != b)
					return t;

				auto f_plus{f + 1};
				if (!front.compare_exchange_strong(f, f_plus, std::memory_order_release, std::memory_order_acquire))
					t = nullptr;

				back.store(f_plus, std::memory_order_release);
				return t;
			} else {
				back.store(f, std::memory_order_release);
				return nullptr;
			}
		}

		task *steal() noexcept {
			auto f = front.load(std::memory_order_relaxed);
			auto b = back.load(std::memory_order_acquire);

			std::atomic_thread_fence(std::memory_order_acq_rel);

			if (f < b) {
				auto *t = tasks[f & mask];

				if (front.compare_exchange_strong(f, f + 1, std::memory_order_release))
					return t;
			}

			return nullptr;
		}

		inline task *alloc() noexcept {
			++task_buffer_index;
			return task_buffer.get() + task_buffer_index;
		}

		inline void alloc_free() noexcept {
			task_buffer_index = 0;
		}

		std::atomic_int64_t
			front{0},
			back{0};

		// off critical path ( good ? / bad ? )
		std::unique_ptr<task *[]> tasks{new task *[8192]};
		std::unique_ptr<task[]> task_buffer{new task[8192]};

		std::size_t task_buffer_index{0};

		bool
			running{false};

		static constexpr std::size_t mask{8192 - 1};
	};

	struct executor {
	public:

		explicit executor(unsigned t_count) :
			lanes(t_count),
			workers(t_count),
			thread_count(t_count) {}

		~executor() {
			stop();
		}

		inline void stop() {
			for (auto &lane : lanes) {
				lane.running = false;
			}

			for (int i = 1; i < thread_count; ++i) {
				if (workers[i].joinable())
					workers[i].join();
			}
		}

		inline static void run_lane(std::vector<lane> &lanes, lane &l, unsigned i) {
			auto *t = l.pop();

			if (!t) {
				auto rand_index = std::chrono::high_resolution_clock::now().time_since_epoch().count() % (lanes.size());

				if (rand_index != i)
					t = lanes[rand_index].steal();
			}

			if (t) {
				t->callback();
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
				workers[i] = std::thread([this, i]() {
					auto &lane = lanes[i];

					lane.running = true;

					while (lane.running)
						run_lane(lanes, lane, i);
				});
			}
		}

		void wait_for(task *t) {
			auto &lane = get_this_lane();

			while (t->unresolved_children.load(std::memory_order_relaxed) != 0) {
				run_lane(lanes, lane, 0);
			}
		}

		template<typename _callback_t, typename _data_t>
		inline void run_parallel(_callback_t callback, _data_t *data, std::size_t length) {
			auto &lane = get_this_lane();

			//unsigned job_div = std::sqrt(length);
			unsigned job_div = THREAD_COUNT;

			task *parent = lane.alloc();
			parent->unresolved_children = job_div;

			auto stride = length / job_div;
			auto rem = length % job_div;

			for (unsigned i = 0; i < job_div; ++i) {
				auto offset = i * stride;

				if (i == job_div - 1)
					stride = stride + rem;

				task *t = lane.alloc();

				t->callback = [task_data{data + offset}, stride, callback, parent]() {
					for (unsigned j = 0; j < stride; ++j)
						callback(task_data[j]);

					--(parent->unresolved_children);
				};

				t->parent = parent;

				lane.push(t);
			}

			wait_for(parent);

			for (auto &flane : lanes)
				flane.alloc_free();
		}

		inline lane &get_this_lane() noexcept {
			auto id = std::this_thread::get_id();

			for (int i = 1; i < workers.size(); ++i) {
				if (workers[i].get_id() == id)
					return lanes[i];
			}

			return lanes[0];
		}

		std::vector<lane> lanes;
		std::vector<std::thread> workers;
		unsigned const thread_count;
	};
}

proto_d::executor exe_d(THREAD_COUNT);

void SEQ_BASELINE(benchmark::State &state) {
	for (auto _ : state) {

		auto range = state.range(0);

		for (unsigned i = 0; i < range; ++i) {
			test_data[i].update_matrix();
		}
	}
}

void PROTO_D(benchmark::State &state) {

	exe_d.run();

	for (auto _ : state) {

		auto range = state.range(0);

		exe_d.run_parallel([](auto &item) {
							   item.update_matrix();
						   },
						   test_data.data(),
						   range);
	}
}

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK(SEQ_BASELINE)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK(PROTO_D)
	->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK_MAIN();