#ifndef LOCKLESS_TREIBER_STACK_H
#define LOCKLESS_TREIBER_STACK_H

#include "../core/lockless_ebr.h"

#include <atomic>
#include <utility>
#include <optional>

namespace lockless {
    // Implementation more or less based on wikipedia:
    // https://en.wikipedia.org/wiki/Treiber_stack
    template<typename T>
    class TreiberStack final {
        public:
        TreiberStack() = default;

        ~TreiberStack() {
            Node* node = head_.load(std::memory_order_relaxed);
            while(node != nullptr) {
                Node* next = node->next;
                delete node;
                node = next;
            }
        }

        void push(T value) {
            Node* new_head = new Node{std::move(value), nullptr}; 
            Node* old_head = head_.load(std::memory_order_relaxed);
            do {
                new_head->next = old_head;
            } while(!head_.compare_exchange_weak(
                old_head, 
                new_head, 
                std::memory_order_release, 
                std::memory_order_relaxed)
            );
        }

        std::optional<T> pop() {
            // TODO: Advanced Epoch based reclamation for freeing memory
            // https://aturon.github.io/blog/2015/08/27/epoch/
            EbrGuard guard(ebr_);
            Node* old_head = head_.load(std::memory_order_relaxed);
            while(old_head != nullptr) {
                Node* new_head = old_head->next;
                if(head_.compare_exchange_weak(old_head, new_head, std::memory_order_acquire, std::memory_order_acquire)) {
                    T val = std::move(old_head->data);
                    guard.retire(old_head);
                    return val;
                }
            }
            return std::nullopt;
        }

        TreiberStack(const TreiberStack&) = delete;
        TreiberStack&  operator=(const TreiberStack&) = delete;
        TreiberStack(TreiberStack&&) = delete;
        TreiberStack& operator=(TreiberStack&&) = delete;

        private:
        struct Node {
            T data;
            Node* next;
        };
        EbrDomain ebr_;
        std::atomic<Node*> head_{nullptr};
    };

} // namespace lockless

#endif // LOCKLESS_TREIBER_STACK_H