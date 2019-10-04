#include <benchmark/benchmark.h>
#include <iostream>
#include <atomic>
#include <mutex>
#include <memory>

#define MIN_ITERATION_RANGE (1U << 14U)
#define MAX_ITERATION_RANGE (1U << 22U)

#define RANGE Range(MIN_ITERATION_RANGE, MAX_ITERATION_RANGE)

struct spin_mutex {
public:
    inline void lock() {
        while (_lockless_flag.test_and_set(std::memory_order_acquire)) {}
    }

    inline void unlock() {
        _lockless_flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag _lockless_flag { ATOMIC_FLAG_INIT };
};

using spin_guard = std::lock_guard<spin_mutex>;

template < typename _t, template < typename > typename _list_t >
inline void fill_list (_list_t < _t > & list, std::size_t count) {
	for (std::size_t i = 0; i < count; ++i) {
		list.push_front (i);
	}
}

namespace baseline {

    template < typename _t >
    struct linked_list {
    public:

    	inline void clear () {
			// release all nodes
			while (_head) {
				auto * prev = _head;
				_head = _head->next;
				delete prev;
			}
    	}

    	inline std::size_t size() const noexcept {
    		auto * cursor = _head;
    		std::size_t count {0};

    		while (cursor) {
    			cursor = cursor->next;
    			++count;
    		}

    		return count;
    	}

    	~linked_list() {
    		clear ();
    	}

        inline void push_front (_t && item) noexcept {
            auto lock = std::unique_lock(_head_mutex);

			auto * new_node = new node { item, _head };
			_head = new_node;
        }

        inline _t pop_front () {
			auto lock = std::unique_lock(_head_mutex);

			if (!_head)
				throw std::runtime_error ("FAAAAIL");

			std::unique_ptr < node > n { _head };
			_head = _head->next;

			return n->data;
    	}

    private:

        struct node {
        public:
    		_t 		data;
            node *  next { nullptr };
        };

        node *      _head { nullptr };
        spin_mutex  _head_mutex;

    };

}

baseline::linked_list < int > baseline_linked_list;

static void bm_baseline_push (benchmark::State& state) {
	if (state.thread_index == 0) {
		baseline_linked_list.clear ();
	}

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			baseline_linked_list.push_front (i);
		}
	}

	if (state.thread_index == 0) {
		if (baseline_linked_list.size() != state.range(0) * state.threads * state.iterations())
			state.SkipWithError("Wrong number of items detected");
	}
}

static void bm_baseline_pop (benchmark::State& state) {
	if (state.thread_index == 0) {
		baseline_linked_list.clear ();
		fill_list(baseline_linked_list, state.range (0) * state.threads * 200);
	}

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			baseline_linked_list.pop_front();
		}
	}
}

namespace lockless {

	template < typename _t >
	struct linked_list {
	public:

		inline void clear () {
			// release all nodes
			while (_head) {
				auto * prev = _head.load();
				_head = _head.load ()->next;
				delete prev;
			}
		}

		inline std::size_t size() const noexcept {
			auto * cursor = _head.load();
			std::size_t count {0};

			while (cursor) {
				cursor = cursor->next;
				++count;
			}

			return count;
		}

		~linked_list() {
			clear ();
		}

		inline void push_front (_t && item) noexcept {
			auto * new_node = new node { item, _head.load (std::memory_order_relaxed) };

			while (!_head.compare_exchange_weak(
				new_node->next,
				new_node,
				std::memory_order_release,
				std::memory_order_relaxed));
		}

	private:

		struct node {
		public:
			_t 		data;
			node *  next { nullptr };
		};

		std::atomic < node * >
					_head { nullptr };

	};

}

lockless::linked_list < int > lockless_linked_list;

static void bm_lockless_push (benchmark::State& state) {
	if (state.thread_index == 0) {
		lockless_linked_list.clear ();
	}

	for (auto _ : state) {
		for (std::size_t i = 0; i < state.range(0); ++i) {
			lockless_linked_list.push_front (i);
		}
	}

	if (state.thread_index == 0) {
		// evaluate
		if (lockless_linked_list.size() != state.range(0) * state.threads * state.iterations())
			state.SkipWithError("Wrong number of items detected");
	}
}

/*
BENCHMARK(bm_baseline_push)
	->RANGE
	->Unit(benchmark::TimeUnit::kMillisecond)
	->Threads(4);
 */

BENCHMARK(bm_baseline_pop)
	->RANGE
	->Unit(benchmark::TimeUnit::kMillisecond)
	;//->Threads(4);

/*
BENCHMARK(bm_lockless_push)
	->RANGE
	->Unit(benchmark::TimeUnit::kMillisecond)
	->Threads(4);
 */