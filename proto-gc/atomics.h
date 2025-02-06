#pragma once
#ifndef PROTO_GC_ATOMICS_H
#define PROTO_GC_ATOMICS_H

#include <atomic>
#include <emmintrin.h>

namespace proto_gc::atomics {

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

#endif
