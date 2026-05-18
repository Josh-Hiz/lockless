#ifndef LOCKLESS_CONTAINERS_SPSC_QUEUE_H
#define LOCKLESS_CONTAINERS_SPSC_QUEUE_H

#include "../core/lockless_cache.h"

#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <new>

namespace lockless {
    template <typename T>
    class SPSCQueue final {
        public:
        explicit SPSCQueue(std::size_t capacity) : 
            capacity_(check_cap(capacity)), 
            // capacity = 10000000 = 128
            // 01111111 = 127
            // 10000000 & 01111111 = 0
            // 14 = 00001110 & 01111111 = 00001110 = 14
            bit_mask_(capacity_ - 1), 
            queue(std::make_unique<Node[]>(capacity_)) {}

        ~SPSCQueue() {
            while(pop());
        }

        // Push support both lvalues and rvalues
        bool push(const T& v) {
            return push_queue(v);
        }

        bool push(T&& v) { 
            return push_queue(std::move(v));
        }

        template<typename... Args>
        bool push_queue(Args&&... args) {
            const std::size_t tail = tail_.load(std::memory_order_relaxed);
            // Perform current index % capacity to prevent out of bounds
            const std::size_t next_tail = (tail + 1) & bit_mask_;
            // If the next tail is equal to cached head, refresh its value
            // to ensure the queue is not full
            if(next_tail == cached_head_) {
                // Update the cached_head to real head
                cached_head_ = head_.load(std::memory_order_acquire);
                // if the next tail really is the head, then queue is full
                if(next_tail == cached_head_) {
                    return false;
                }
            }

            // Perform a placement new in the queue node, this will first get
            // the address of the queue node, and directly construct a new T
            // with perfect forwarding
            new (queue[tail].get()) T(std::forward<Args>(args)...);
            // Move up the tail index to the next itemto know where to push
            tail_.store(next_tail, std::memory_order::release);
            return true;
        }

        std::optional<T> pop() {
            const std::size_t head = head_.load(std::memory_order_relaxed);
            // Similar to push_queue logic
            if(head == cached_tail_) {
                cached_tail_ = tail_.load(std::memory_order_acquire);
                if(head == cached_tail_) {
                    return std::nullopt; // Queue is empty right now
                }
            }

            T* node = queue[head].get();
            std::optional<T> task{std::in_place, std::move(*node)};
            node->~T(); // Explicitly destroy whatever data was at the node
            head_.store((head + 1) & bit_mask_, std::memory_order_release);
            return task;
        }

        bool empty() const noexcept {
            // If head == tail, the queue is full
            return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
        }

        std::size_t capacity() const noexcept {
            return capacity_;
        }

        // Once the queue is initialized, its data should not be
        // copyable or movable, thus those functions are never made
        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;
        SPSCQueue(SPSCQueue&&) = delete;
        SPSCQueue& operator=(SPSCQueue&&) = delete;

        private:
        std::size_t check_cap(std::size_t cap) {
            if (cap < 2) {
                throw std::invalid_argument("MPMCQueue must have a non-zero positive capacity >= 2");
            }
            return std::bit_ceil(cap);
        }

        struct Node {
            alignas(T) std::byte data_[sizeof(T)];
            T* get() noexcept {
                // Cast the actual byte array address as type T and return a 
                // pointer for that whole object, use launder here because since
                // its raw-bytes, to be usable as T it must be laundered 
                // (idk how else to do it as the queue needs to be general)
                return std::launder(reinterpret_cast<T*>(data_));
            }
        };

        std::size_t capacity_;
        std::size_t bit_mask_;
        std::unique_ptr<Node[]> queue;

        // Have atomic pointing to tail and head of queue
        alignas(cache_line_size) std::atomic<std::size_t> tail_{0};
        // Only for producer
        alignas(cache_line_size) std::size_t cached_head_{0};
        alignas(cache_line_size) std::atomic<std::size_t> head_{0};
        // Only for consumer
        alignas(cache_line_size) std::size_t cached_tail_{0};

    };
} // namespace lockless

#endif // _LOCKLESS_CONTAINERS_SPSC_QUEUE_H_