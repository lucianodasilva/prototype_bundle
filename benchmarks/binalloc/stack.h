#pragma once
#ifndef BINALLOC_STACK_H
#define BINALLOC_STACK_H

#include <atomic>
#include <immintrin.h>
#include <span>
#include <type_traits>

namespace sgc2::stack {

    template <typename, typename = void>
    struct is_stack_node : std::false_type {};

    template <typename type>
    struct is_stack_node<type, std::void_t<decltype (std::declval<type>().next)>> {
        static constexpr bool value = std::is_same_v<decltype(std::declval<type>().next), type *>;
    };

    /// checks if a type fulfills the stack node concept
    /// \tparam type the type to check
    /// \note a stack node must have a member with the signature <c>type * type::next</c>
    template <typename type>
    constexpr bool is_stack_node_v = is_stack_node<type>::value;

    /// compare and swap with acquire and relaxed memory order
    /// \tparam type the type of the atomic variable
    /// \param target the atomic variable to compare and swap
    /// \param expected the expected value
    /// \param desired the desired value
    /// \return true if the compare and swap was successful, false otherwise
    template <typename type>
    bool compare_and_swap(std::atomic<type> &target, type &expected, type desired) {
        return target.compare_exchange_weak(expected, desired, std::memory_order_acquire, std::memory_order_relaxed);
    }

    /// compare and swap string with acquire and relaxed memory order
    /// \tparam type the type of the atomic variable
    /// \param target the atomic variable to compare and swap
    /// \param expected the expected value
    /// \param desired the desired value
    /// \return true if the compare and swap was successful, false otherwise
    template <typename type>
    bool compare_and_swap_strong(std::atomic<type> &target, type &expected, type desired) {
        return target.compare_exchange_strong(expected, desired, std::memory_order_acquire, std::memory_order_relaxed);
    }

    /// hooks a node to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    /// \param node_ptr the node to hook
    template <typename node_type>
    void push(node_type * node_type::*next_field, node_type *&head, node_type *node_ptr) {
        node_ptr->*next_field = head;
        head                  = node_ptr;
    }

    /// hooks a node to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    /// \param node_ptr the node to hook
    template <typename node_type>
    void push(node_type *&head, node_type *node_ptr) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        push(&node_type::next, head, node_ptr);
    }

    /// hooks a node to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    /// \param node_ptr the node to hook
    template <typename node_type>
    void atomic_push(node_type * node_type::*next_field, std::atomic<node_type *> &head, node_type *node_ptr) {
        node_ptr->*next_field = head.load(std::memory_order_relaxed);

        // let's go for the optimistic approach
        if(compare_and_swap(head, node_ptr->*next_field, node_ptr)) {
            return;
        }

        // let's go for the pessimistic approach
        while(!compare_and_swap(head, node_ptr->*next_field, node_ptr)) {
            _mm_pause();
        }
    }

    /// hooks a node to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    /// \param node_ptr the node to hook
    template <typename node_type>
    void atomic_push(std::atomic<node_type *> &head, node_type *node_ptr) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        atomic_push(&node_type::next, head, node_ptr);
    }

    /// hooks a sequence of nodes to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    /// \param first the first node of the sequence
    /// \param last the last node of the sequence
    template <typename node_type>
    void insert_at_head(node_type * node_type::*next_field, node_type *&head, node_type *first, node_type *last) {
        last->*next_field = head;
        head              = first;
    }

    /// hooks a sequence of nodes to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    /// \param first the first node of the sequence
    /// \param last the last node of the sequence
    template <typename node_type>
    void insert_at_head(node_type *&head, node_type *first, node_type *last) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        insert_at_head(&node_type::next, head, first, last);
    }

    /// hooks a sequence of nodes to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    /// \param first the first node of the sequence
    /// \param last the last node of the sequence
    template <typename node_type>
    void atomic_insert_at_head(
            node_type * node_type::*  next_field,
            std::atomic<node_type *> &head,
            node_type *               first,
            node_type *               last) {
        last->*next_field = head.load(std::memory_order_relaxed);

        // lets go for the optimistic approach
        if(compare_and_swap(head, last->*next_field, first)) {
            return;
        }

        // lets go for the pessimistic approach
        while(!compare_and_swap(head, last->*next_field, first)) {
            _mm_pause();
        }
    }

    /// hooks a sequence of nodes to the head of a linked list
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    /// \param first the first node of the sequence
    /// \param last the last node of the sequence
    template <typename node_type>
    void atomic_insert_at_head(std::atomic<node_type *> &head, node_type *first, node_type *last) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        atomic_insert_at_head(&node_type::next, head, first, last);
    }

    /// unhooks the top node of a linked list
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    template <typename node_type>
    node_type *pop(node_type * node_type::*next_field, node_type *&head) {
        auto *old_head = head;

        if(old_head) {
            head = old_head->*next_field;
        }

        return old_head;
    }

    /// unhooks the top node of a linked list
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    template <typename node_type>
    node_type *pop(node_type *&head) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        return pop(&node_type::next, head);
    }

    /// unhooks the top node of a linked list
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    /// \return the unhooked node
    template <typename node_type>
    node_type *atomic_pop(node_type * node_type::*next_field, std::atomic<node_type *> &head) {
        auto *old_head = head.load(std::memory_order_relaxed);

        if(old_head) {
            // lets go for the optimistic approach
            if(compare_and_swap(head, old_head, old_head->*next_field)) {
                return old_head;
            }

            // lets go for the pessimistic approach
            while(old_head && !compare_and_swap(head, old_head, old_head->*next_field)) {
                _mm_pause();
            }
        }

        return old_head;
    }

    /// unhooks the top node of a linked list
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    /// \return the unhooked node
    template <typename node_type>
    node_type *atomic_pop(std::atomic<node_type *> &head) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        return atomic_pop(&node_type::next, head);
    }

    /// unhooks the top node of a linked list and returns it if it is the expected node
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the linked list
    /// \param expected the expected node to pop
    /// \return the new head or nullptr if the expected node was not the head
    template <typename node_type>
    bool atomic_pop_expected (node_type * node_type::*next_field, std::atomic<node_type *> &head, node_type * expected) {
        if (expected == nullptr) {
            return false;
        }

        return compare_and_swap_strong(head, expected, expected->*next_field);
    }

    template <typename node_type>
    bool atomic_pop_expected (std::atomic<node_type *> &head, node_type * expected) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        return atomic_pop_expected(&node_type::next, head, expected);
    }

    /// unhooks the full linked list from the head
    /// \tparam node_type the type of the node
    /// \param head the head of the linked list
    /// \return the full linked list now derreferenced from the head
    template <typename node_type>
    node_type *atomic_detach(std::atomic<node_type *> &head) {
        return head.exchange(nullptr, std::memory_order_relaxed);
    }

    /// find the tail of a chain
    /// \tparam node_type the type of the node
    /// \param next_field address offset for the next field
    /// \param head the head of the chain
    /// \return the tail of the chain
    /// \note this function is NOT thread safe
    template <typename node_type>
    node_type *find_tail(node_type * node_type::*next_field, node_type *head) {
        node_type *tail = nullptr;

        while(head) {
            tail = head;
            head = head->*next_field;
        }

        return tail;
    }

    /// find the tail of a chain
    /// \tparam node_type the type of the node
    /// \param head the head of the chain
    /// \return the tail of the chain
    /// \note this function is NOT thread safe
    template <typename node_type>
    node_type *find_tail(node_type *head) {
        static_assert(is_stack_node_v<node_type>, "Node type does not match node requirements");
        return find_tail(&node_type::next, head);
    }

    /// Formats a buffer as a linked list of nodes
    /// \tparam node_type the type of the node
    /// \param buffer the buffer to format
    /// \param stride the stride of the node in bytes
    /// \return the head of the linked list, or nullptr if the buffer is too small or empty
    template <typename node_type>
    node_type *format_stack(std::span<std::byte> buffer, std::size_t const stride) {

        if(buffer.size() < stride) {
            return nullptr; // no buffer provided
        }

        auto *cursor = buffer.data();
        auto *last   = cursor + stride * (buffer.size() / stride - 1);

        while(cursor < last) {
            auto *node = std::bit_cast<node_type *>(cursor);
            node->next = std::bit_cast<node_type *>(cursor + stride);

            cursor += stride;
        }

        std::bit_cast<node_type *>(last)->next = nullptr; // last node points to nullptr

        return std::bit_cast<node_type *>(buffer.data());
    }

}

#endif
