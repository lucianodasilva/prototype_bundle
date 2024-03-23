#include <benchmark/benchmark.h>
#include <ptbench.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <xmmintrin.h>
#include <stack>
#include <list>
#include <lockfree_stack.h>

struct spin_mutex {

	void lock() noexcept {
		for (;;) {
			// Optimistically assume the lock is free on the first try
			if (!_lock.exchange(true, std::memory_order_acquire)) {
				return;
			}
			// Wait for lock to be released without generating cache misses
			while (_lock.load(std::memory_order_relaxed)) {
				// Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
				// hyper-threads

				_mm_pause();
			}
		}
	}

	bool try_lock() noexcept {
		// First do a relaxed load to check if lock is free in order to prevent
		// unnecessary cache misses if someone does while(!try_lock())
		return !_lock.load(std::memory_order_relaxed) &&
			   !_lock.exchange(true, std::memory_order_acquire);
	}

	void unlock() noexcept {
		_lock.store(false, std::memory_order_release);
	}

private:
	std::atomic_bool _lock { false };
};

namespace demo_b {

	template < typename t >
	struct stack {
		using value_type = t;

		stack() = default;

		~stack () {
			while(!empty()) {
				pop_back();
			}
		}

		[[nodiscard]] bool empty() const {
			std::lock_guard const LOCK { _mutex };
			return _head == nullptr;
		}

		void push_back (value_type const & value) {
			auto * new_node = new node {
				nullptr,
				value };

			{ std::lock_guard const LOCK { _mutex };
				new_node->next = _head;
				_head = new_node;
			}
		}

		value_type pop_back () {
			node * old_head;

			{ std::lock_guard const LOCK { _mutex };
				old_head = _head;

				if (!old_head) {
					return {};
				}

				_head = old_head->next;
			}

			auto value = old_head->value;
			delete old_head;

			return value;
		}

	private:

		struct node {
			node * next;
			value_type value;
		};

		node *				_head { nullptr };
		mutable spin_mutex	_mutex;
	};

}

template < typename list_t >
void run_push (list_t & list) {
	list.push_back (ptbench::uniform(1000));
}

template < typename list_t >
void run_pop (list_t & list) {
	if (!list.empty()) {
		list.pop_back();
	}
}

template < typename list_t, typename mutex_t >
void run_push_mutex (list_t & list, mutex_t & mtx) {
	std::unique_lock const LOCK { mtx };
	run_push (list);
}

template < typename list_t, typename mutex_t >
void run_pop_mutex (list_t & list, mutex_t & mtx) {
	std::unique_lock const LOCK { mtx };
	run_pop(list);
}

//ptbench::executor exec { ptbench::exec_policy::per_physical_core_affinity };
ptbench::executor exec {};

template < typename list_t, typename mutex_t >
void run_benchmark_mutex (benchmark::State & state) {
	list_t list;
	mutex_t mtx;

	for (auto _ : state) {
		// clear list
		while(!list.empty ()) {
			list.pop_back();
		}

		exec.dispatch ({
				{ [&]{ run_push_mutex (list, mtx); }, 50 },
				{ [&]{ run_pop_mutex (list, mtx); }, 50 }
			},
			state.range(0));
	}
}

template < typename list_t >
void run_benchmark (benchmark::State & state) {
	list_t list;

	for (auto _ : state) {
		// clear list
		while(!list.empty ()) {
			list.pop_back();
		}

		exec.dispatch ({
				{ [&]{ run_push (list); }, 50 },
				{ [&]{ run_pop (list); }, 50 }
			},
			state.range(0));
	}
}

#define MIN_ITERATION_RANGE 1 << 14U
#define MAX_ITERATION_RANGE 1 << 16U

#define MY_BENCHMARK(func, name) BENCHMARK((func))->Range (MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)->Name(name)->Unit(benchmark::TimeUnit::kMillisecond)

//MY_BENCHMARK ((run_benchmark_mutex <std::list <int>, std::mutex >), "mutex - std::list");
//MY_BENCHMARK ((run_benchmark_mutex <std::list <int>, spin_mutex >), "spin - std::list");
MY_BENCHMARK (run_benchmark < lockfree_stack < int > >, "lockfree stack");
//MY_BENCHMARK (run_benchmark < demo_b::stack < int > >, "embeded spin stack");