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
//		auto caller_guard = pop_guard (this->_pop_concurrent_callers);
//
//		auto * dead_head = _head.exchange (nullptr, std::memory_order_relaxed);
//
//		while (dead_head) {
//			auto * next = dead_head->next;
//			delete dead_head;
//			dead_head = next;
//		}
		// while (!this->empty()) {
		// 	this->pop_back ();
		// }
	}

	void push_back (value_type const & value) {
		auto * new_node = new node {
			_head.load(),
			value
		};

		hook (_head, new_node);
	}

	value_type pop_back () {
		// auto caller_guard = pop_guard (this->_pop_concurrent_callers);
		++_pop_concurrent_callers;

		value_type value = {};
		auto * unhocked = unhook (_head);

		if (unhocked != nullptr) {
			value = unhocked->value;
			try_release (_pop_concurrent_callers, _death_row, unhocked);
		}

		--_pop_concurrent_callers;
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

	static void try_release (std::atomic_int const & pop_callers, std::atomic < node * > & death_row, node * dead_node) {
		hook (death_row, dead_node);

		if (pop_callers.load (std::memory_order_relaxed) == 1) {
			node * to_delete = dead_node;

			if (death_row.compare_exchange_weak (to_delete, nullptr, std::memory_order_acquire, std::memory_order_relaxed)) {
				while (to_delete) {
					auto * next = to_delete->next;
					delete to_delete;
					to_delete = next;
				}
			}
		}
	}

	std::atomic < node * >	_head { nullptr };
	std::atomic_int 		_pop_concurrent_callers { 0 };
	std::atomic < node * >	_death_row { nullptr };
};

#endif
