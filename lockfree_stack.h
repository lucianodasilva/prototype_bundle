#pragma once
#ifndef LOCKFREE_STACK_H
#define LOCKFREE_STACK_H

#include <atomic>
#include <functional>
#include <immintrin.h>
#include <memory>
#include <optional>

namespace lf {
	namespace atomics {

		/// compare and swap with acquire and relaxed memory order
		/// \tparam t the type of the atomic variable
		/// \param target the atomic variable to compare and swap
		/// \param expected the expected value
		/// \param desired the desired value
		/// \return true if the compare and swap was successful, false otherwise
		template < typename t >
		bool compare_and_swap (std::atomic < t > & target, t & expected, t desired) {
			return target.compare_exchange_weak (expected, desired, std::memory_order_acquire, std::memory_order_relaxed);
		}

		/// hooks a node to the head of a linked list
		/// \tparam node_t the type of the node
		/// \param head the head of the linked list
		/// \param node_ptr the node to hook
		template < typename node_t >
		void push (std::atomic < node_t * > & head, node_t * node_ptr) {
			node_ptr->next = head.load (std::memory_order_relaxed);

			// lets go for the optimistic approach
			if (compare_and_swap (head, node_ptr->next, node_ptr)) {
				return;
			}

			// lets go for the pessimistic approach
			while (!compare_and_swap (head, node_ptr->next, node_ptr)) {
				_mm_pause();
			}
		}

		/// hooks a sequence of nodes to the head of a linked list
		/// \tparam node_t the type of the node
		/// \param head the head of the linked list
		/// \param first the first node of the sequence
		/// \param last the last node of the sequence
		template < typename node_t >
		void push (std::atomic < node_t * > & head, node_t * first, node_t * last) {
			last->next = head.load (std::memory_order_relaxed);

			// lets go for the optimistic approach
			if (compare_and_swap (head, last->next, first)) {
				return;
			}

			// lets go for the pessimistic approach
			while (!compare_and_swap (head, last->next, first)) {
				_mm_pause();
			}
		}

		/// unhooks the top node of a linked list
		/// \tparam node_t the type of the node
		/// \param head the head of the linked list
		/// \return the unhooked node
		template < typename node_t >
		node_t * pop (std::atomic < node_t * > & head) {
			auto * old_head = head.load (std::memory_order_relaxed);

			if (old_head) {
				// lets go for the optimistic approach
				if (compare_and_swap (head, old_head, old_head->next)) {
					return old_head;
				}

				// lets go for the pessimistic approach
				while(old_head && !compare_and_swap (head, old_head, old_head->next)) {
					_mm_pause();
				}
			}

			return old_head;
		}

		/// unhooks the full linked list from the head
		/// \tparam node_t the type of the node
		/// \param head the head of the linked list
		/// \return the full linked list now derreferenced from the head
		template < typename node_t >
		node_t * detach (std::atomic < node_t * > & head) {
			return head.exchange (nullptr, std::memory_order_relaxed);
		}

		/// find the tail of a chain
		/// \tparam node_t the type of the node
		/// \param head the head of the chain
		/// \return the tail of the chain
		/// \note this function is NOT thread safe
		template < typename node_t >
		node_t * find_tail (node_t * head) {
			node_t * tail = nullptr;

			while (head) {
				tail = head;
				head = head->next;
			}

			return tail;
		}

	}

	/// a lock free stack implementation
	/// \tparam t the value type
	/// \tparam allocator_t the allocator type
	template < typename t, typename allocator_t = std::allocator <t> >
	struct stack {
	private:
		using alloc_traits = std::allocator_traits < allocator_t >;
	public:

		using value_type		= typename alloc_traits::value_type;
		using allocator_type	= std::remove_cv_t<std::remove_reference_t <allocator_t>>;
		using size_type			= std::size_t;
		using reference			= value_type &;
		using const_reference	= value_type const &;
		using pointer			= typename alloc_traits::pointer;
		using const_pointer		= typename alloc_traits::const_pointer;

		stack() = default;
		//stack (stack const & other);
		//stack (stack && other) noexcept;
		//template < typename input_iterator_t >
		//stack (input_iterator_t first, input_iterator_t last);

		explicit stack (allocator_type const & alloc) :
			_impl (alloc),
			_allocator (alloc) {}
		//stack (stack const & other, allocator_type const & alloc);
		//stack (stack && other, allocator_type const & alloc);

		//template < typename input_iterator_t >
		//stack (input_iterator_t first, input_iterator_t last, allocator_type const & alloc);

		~stack () {
			this->clear ();
		}

		//stack & operator = (stack const & other);
		//stack & operator = (stack && other);

		/// check if the stack is empty
		/// \return true if the stack is empty, false otherwise
		[[nodiscard]] bool empty() const {
			return _impl.empty ();
		}

		/// get the size of the stack
		[[nodiscard]] size_type size() const { return _impl.size (); }

		/// clear the stack
		void clear () {
			auto const GUARD = _collector.guard ();

			// highjack the active chain links and if there is a chain
			// push it to the collector
			if (auto * detached_list = _impl.detach ()) {
				_collector.push (detached_list, atomics::find_tail (detached_list));
			}

			// we try to collect any way
			_collector.try_collect ();
		}

		/// push a new value to the stack
		/// \param value the value to push
		void push (value_type const & value) {
			// get a new node
			auto * new_node = _impl.allocate();

			// copy the value into its place
			_allocator.construct (new_node->data (), value);

			// push the new node to the stack
			_impl.push (new_node);
		}

		/// move a new value to the stack
		/// \param value the value to move
		void push (value_type && value) {
			// get a new node
			auto * new_node = _impl.allocate ();

			// move the value to its place
			_allocator.construct (new_node->data (), std::forward < value_type > (value));

			// push the new node to the stack
			_impl.push (new_node);
		}

		/// emplace a new value in place
		/// \tparam args_tv the type of the arguments
		/// \param args the arguments to forward to the value constructor
		/// \return a reference to the newly created value
		/// \note although construction and push is thread safe, the reference returned may NOT be present
		template < typename ... args_tv >
		reference emplace (args_tv && ... args) {
			// get a new node
			auto * new_node = _impl.allocate();

			// construct the value in place
			_allocator.construct (new_node->data (), std::forward < args_tv > (args)...);

			// push the new node to the stack
			_impl.push (new_node);

			return *new_node->data ();
		}

		/// pop a value from the stack
		/// \return an optional value containing the popped value if the stack is not empty
		[[nodiscard]] std::optional < value_type > pop () {
			// declare ourselfs as a critical client
			auto const GUARD = _collector.guard ();

			std::optional < value_type > result {};

			// unhook the top node
			auto * unhocked = _impl.pop();

			// if we unhooked a node, we can push it to the collector and try to collect
			if (unhocked != nullptr) {
				// move the value out
				result = std::move (*unhocked->data ());

				// destroy the remaining value in the node
				_allocator.destroy (unhocked->data ());

				// push the node to the collector
				_collector.push (unhocked);
			}

			// critical clients should always try to collect
			_collector.try_collect ();

			return result;
		}

	private:
		/// storage chain link and value container
		struct node {
			pointer data () {
				return reinterpret_cast < pointer > (storage);
			}

			const_pointer data () const {
				return reinterpret_cast < const_pointer > (storage);
			}

			alignas (value_type) std::byte storage [sizeof (value_type)];
			node * next = nullptr;
		};

		using node_pointer = node *;
		using const_node_pointer = node const *;
		using atomic_node_ptr = std::atomic < node_pointer >;

		using alloc_rebind_type = typename alloc_traits::template rebind_alloc < node >;

		// allocate and manage the internal linked list
		struct stack_impl : alloc_rebind_type {
			using alloc_rebind_type::alloc_rebind_type;

			stack_impl () = default;
			explicit stack_impl (alloc_rebind_type const & other) : alloc_rebind_type (other) {}
			explicit stack_impl (alloc_rebind_type && other) : alloc_rebind_type (other) {}

			[[nodiscard]] bool empty () const { return _head.load (std::memory_order_relaxed) == nullptr; }
			[[nodiscard]] std::size_t size () const { return _size.load (std::memory_order_relaxed); }

			node_pointer allocate () {
				return alloc_rebind_type::allocate (1);
			}

			void push (node_pointer new_node) {
				atomics::push (_head, new_node);
				++_size;
			}

			node_pointer pop () {
				--_size;
				return atomics::pop (_head);
			}

			node_pointer detach () {
				return atomics::detach (_head);
			}

		private:
			atomic_node_ptr		_head { nullptr };
			std::atomic_size_t	_size { 0 };
		};

		/// track and manage the number of concurrent critical clients
		struct client_guard {
			explicit client_guard (std::atomic_int & counter) : _counter (counter) { ++_counter; }

			explicit client_guard (client_guard const &) = delete;
			explicit client_guard (client_guard &&) = delete;

			~client_guard () { --_counter; }
		private:
			std::atomic_int & _counter;
		};

		/// manage the proper collection of chain links and critical clients
		struct collector {

			client_guard guard () { return client_guard (_critical_clients); }

			void push (node_pointer rec_node) {
				atomics::push (_collection_head, rec_node);
			}

			void push (node_pointer rec_head, node_pointer rec_tail) {
				atomics::push (_collection_head, rec_head, rec_tail);
			}

			void try_collect () {
				auto * collectable = _collection_head.load (std::memory_order_relaxed);

				// if we are the last critical client, try to release the death row
				if (_critical_clients.load (std::memory_order_relaxed) == 1) {
					// try to detach the currently existing collectable node chain
					if (atomics::compare_and_swap (_collection_head, collectable, static_cast < node_pointer > (nullptr))) {
						// iterate through the collectable nodes and delete them
						while (collectable) {
							auto * next = collectable->next;
							delete collectable;
							collectable = next;
						}
					}
				}
			}

		private:
			atomic_node_ptr _collection_head { nullptr };
			std::atomic_int _critical_clients { 0 };
		};

		stack_impl		_impl;
		collector		_collector;
		allocator_type	_allocator;
	};
}

#endif
