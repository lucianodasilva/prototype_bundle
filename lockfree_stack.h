#pragma once
#ifndef LOCKFREE_STACK_H
#define LOCKFREE_STACK_H

#include <atomic>
#include <immintrin.h>

template < typename t >
struct lockfree_stack {
	using value_type = t;

	lockfree_stack() = default;

	~lockfree_stack () {
		this->clear ();
	}

	[[nodiscard]] bool empty() const {
		return _head.load (std::memory_order_relaxed) == nullptr;
	}

	void clear () {
		auto caller_guard = pop_guard (this->_pop_concurrent_callers);

		// highjack live nodes
		auto * live_list = _head.exchange (nullptr, std::memory_order_relaxed);

		if (live_list == nullptr) {
            return;
        }

		auto * live_tail = tail (live_list);
		auto * death_row = _death_row.load (std::memory_order_relaxed);

		// append death row to live list (optimistic)
		live_tail->next = death_row;

		if (!_death_row.compare_exchange_weak (death_row, live_list, std::memory_order_acquire, std::memory_order_relaxed)) {
            // append death row to live list (pessimistic)
			do {
				live_tail->next = death_row;
			} while (!_death_row.compare_exchange_weak (death_row, live_list, std::memory_order_acquire, std::memory_order_relaxed));
        }

		try_release (_pop_concurrent_callers, _death_row);
	}

	void push_back (value_type const & value) {
		auto * new_node = new node {
			_head.load(),
			value
		};

		hook (_head, new_node);
	}

	value_type pop_back () {
		auto caller_guard = pop_guard (this->_pop_concurrent_callers);
		//++_pop_concurrent_callers;

		value_type value = {};
		auto * unhocked = unhook (_head);

		if (unhocked != nullptr) {
			value = unhocked->value;
			hook (_death_row, unhocked); // append to death row
			try_release (_pop_concurrent_callers, _death_row);
		}

		//--_pop_concurrent_callers;
		return value;
	}

private:

	struct pop_guard {
		explicit pop_guard (std::atomic < int > & pop_callers) : _pop_callers (pop_callers) { ++pop_callers; }
		~pop_guard () { --_pop_callers; }
	private:
		std::atomic < int > & _pop_callers;
	};

	struct node {
		node * next;
		value_type value;
	};

	static void hook (std::atomic < node * > & head, node * new_node) {
		new_node->next = head.load ();

		// lets go for the optimistic approach
		if (head.compare_exchange_weak (
			new_node->next,
			new_node,
			std::memory_order::memory_order_acquire,
			std::memory_order::memory_order_relaxed)
		) {
			return;
		}

		// lets go for the pessimistic approach
		while (!head.compare_exchange_weak (
				new_node->next,
				new_node,
				std::memory_order_acquire,
				std::memory_order_relaxed))
		{
			_mm_pause();
		}
	}

	static node * unhook (std::atomic < node * > & head) {
		auto * old_head = head.load (std::memory_order_relaxed);

		if (old_head) {
			// lets go for the optimistic approach
			if (head.compare_exchange_weak (
				old_head,
				old_head->next,
				std::memory_order::memory_order_acquire,
				std::memory_order::memory_order_relaxed)
			) {
				return old_head;
			}

			// lets go for the pessimistic approach
			while(old_head && !head.compare_exchange_weak (old_head, old_head->next, std::memory_order_acquire, std::memory_order_relaxed)) {
				_mm_pause();
			}
		}

		return old_head;
	}

	static void try_release (std::atomic_int const & pop_callers, std::atomic < node * > & death_row) {
		node * to_delete = death_row.load (std::memory_order_relaxed);

		if (pop_callers.load (std::memory_order_relaxed) == 1) {
			if (death_row.compare_exchange_weak (to_delete, nullptr, std::memory_order_acquire, std::memory_order_relaxed)) {
				while (to_delete) {
					auto * next = to_delete->next;
					delete to_delete;
					to_delete = next;
				}
			}
		}
	}

	static node * tail (node * head) {
		node * tail = nullptr;

		while (head) {
			tail = head;
			head = head->next;
		}

		return tail;
	}

	std::atomic < node * >	_head { nullptr };
	std::atomic_int 		_pop_concurrent_callers { 0 };
	std::atomic < node * >	_death_row { nullptr };
};

#endif
