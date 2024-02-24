#include <benchmark/benchmark.h>
#include <ptbench.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <xmmintrin.h>
#include <stack>
#include <list>

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

namespace demo_a {

	template < typename t >
	struct stack {
	public:

		using value_type = t;

		stack() = default;

		~stack () {
			while(!empty()) {
				pop_back();
			}
		}

		[[nodiscard]] bool empty() const {
			return _head.load () == nullptr;
		}

		void push_back (value_type const & value) {
			auto * new_node = new node {
				_head.load(),
				value };

			push (_head, new_node);
		}

		value_type pop_back () {
			++_poppers;

			auto * old_head = _head.load ();

			while(old_head && !_head.compare_exchange_weak (old_head, old_head->next)) {}

			value_type value = {};

			if (old_head) {
				value = old_head->value;
				push(_to_be_deleted, old_head);
			}

			try_collect ();

			return value;
		}

	private:

		struct node {
		public:
			node * next;
			value_type value;
		};

		std::atomic < node * >	_head { nullptr };
		std::atomic_int 		_poppers { 0 };
		std::atomic < node * >	_to_be_deleted { nullptr };

		static void push (std::atomic < node * > & head, node * new_node) {
			for (;;) {
				new_node->next = head.load ();

				if (head.compare_exchange_weak (
					new_node->next,
					new_node))
					//std::memory_order_release,
					//std::memory_order_relaxed))
				{
					return;
				}

				_mm_pause();
			}
		}

		void try_collect () {
			if (_poppers.load (std::memory_order_relaxed) == 1) {
				auto * to_be_deleted = _to_be_deleted.exchange (nullptr);//, std::memory_order_acquire);

				while(to_be_deleted) {
					auto * next = to_be_deleted->next;
					delete to_be_deleted;
					to_be_deleted = next;
				}
			}

			--_poppers;
		}
	};

}

namespace demo_b {

	template < typename t >
	struct stack {
	public:

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
		public:
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

template < typename list_t, typename mutex_t, ptbench::exec_policy policy >
void run_benchmark_mutex (benchmark::State & state) {
	list_t list;
	mutex_t mtx;

	for (auto _ : state) {
		// clear list
		while(!list.empty ()) {
			list.pop_back();
		}

		ptbench::exec ({
			{ [&]{ run_push_mutex (list, mtx); }, 50 },
			{ [&]{ run_pop_mutex (list, mtx); }, 50 }
		},
		state.range(0), policy);
	}
}

template < typename list_t, ptbench::exec_policy policy >
void run_benchmark (benchmark::State & state) {
	list_t list;

	for (auto _ : state) {
		// clear list
		while(!list.empty ()) {
			list.pop_back();
		}

		ptbench::exec ({
			{ [&]{ run_push (list); }, 50 },
			{ [&]{ run_pop (list); }, 50 }
		},
		state.range(0), policy);
	}
}

#define MIN_ITERATION_RANGE 1 << 14U
#define MAX_ITERATION_RANGE 1 << 16U

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

BENCHMARK (run_benchmark_mutex < std::list < int >, std::mutex, ptbench::exec_policy::per_physical_core_affinity >)
	->Name ("mutex - std::list")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK (run_benchmark_mutex < std::list < int >, spin_mutex, ptbench::exec_policy::per_physical_core_affinity >)
	->Name ("spin - std::list")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK (run_benchmark < demo_a::stack < int >, ptbench::exec_policy::per_physical_core_affinity>)
	->Name ("lockfree queue")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);

BENCHMARK (run_benchmark < demo_b::stack < int >, ptbench::exec_policy::per_physical_core_affinity>)
	->Name ("embeded spin queue")->RANGE->Unit(benchmark::TimeUnit::kMillisecond);