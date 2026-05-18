#ifndef LOCKLESS_MPMC_QUEUE_H
#define LOCKLESS_MPMC_QUEUE_H

#include "../core/lockless_cache.h"
#include "../core/lockless_backoff.h"

#include <optional>
#include <atomic>
#include <bit>
#include <cstddef>
#include <stdexcept>
#include <new>
#include <vector>

namespace lockless {
    // Lockless implementation of the Vyukov MPMC queue for lock-free concurrent
    // queue operations, extremely efficient under high contention:
    // https://github.com/couchbase/phosphor/blob/master/thirdparty/dvyukov/include/dvyukov/mpmc_bounded_queue.h
    // https://int08h.com/post/ode-to-a-vyukov-queue/
    template <typename T>
    class MPMCQueue final {
        public:
        explicit MPMCQueue(std::size_t capacity) : capacity_(check_cap(capacity)), bit_mask_(capacity_ - 1), queue(capacity_) {
            // Initialize the sequence numbers (similar to how TCP has seqn)
            // in SPSC this isnt needed since its only 1 thread for each side
            for(std::size_t i = 0; i < capacity_; ++i) {
                queue[i].seq.store(i, std::memory_order_relaxed);
            }
        }

        ~MPMCQueue() {
            // pop all elements of the queue, blocking shouldnt occur
            while(try_pop());
        }

        // Non-blocking push that doesnt use exponential backoff
        bool try_push(const T& v) {
            return try_push_queue(v);
        }

        bool try_push(T&& v) {
            return try_push_queue(std::move(v));
        }

        template<typename... Args>
        bool try_push_queue(Args&&... args) {
            std::size_t tail = tail_.load(std::memory_order_relaxed);
            while(true) {
                Node& node = queue[tail & bit_mask_];
                const std::size_t seq = node.seq.load(std::memory_order_acquire);
                const auto ptr_diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(tail);
                if(ptr_diff == 0) {
                    if(tail_.compare_exchange_weak(tail, tail+1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        // Write the node using placement new and perfect forwarding
                        new (node.data) T(std::forward<Args>(args)...);
                        node.seq.store(tail + 1, std::memory_order_release);
                        return true;
                    }
                } else if(ptr_diff < 0) {
                    // queue is full;
                    return false;
                } else {
                    // Another producer already advanced the tail
                    tail = tail_.load(std::memory_order_relaxed);
                }
            }
        }
        
        std::optional<T> try_pop() {
            std::size_t head = head_.load(std::memory_order_relaxed);
            while(true){
                Node& node = queue[head & bit_mask_];
                const std::size_t seq = node.seq.load(std::memory_order_acquire);
                const auto ptr_diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(head + 1);
                if(ptr_diff == 0) {
                    if(head_.compare_exchange_weak(head, head+1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        T* tp = node.get();
                        std::optional<T> res{std::in_place, std::move(*tp)};
                        tp->~T();
                        node.seq.store(head + capacity_, std::memory_order_release);
                        return res;
                    }
                } else if(ptr_diff < 0) {
                    return std::nullopt;
                } else {
                    head = head_.load(std::memory_order_relaxed);
                }
            }
        }

        // BLOCKING push and pop using exponential backoff
        // https://medium.com/@ak735927/effortless-concurrency-in-java-solving-the-producer-consumer-problem-with-blockingqueue-de6a38e2808f
        void push(const T& v) {
            push_queue(v);
        }

        void push(T&& v) {
            push_queue(std::move(v));
        }
        
        template<typename... Args>
        void push_queue(Args&&... args){
            ExponentialBackoff bo;
            // If push fails, BLOCK
            while(!try_push_queue(std::forward<Args>(args)...)){
                bo.backoff();
            }
        }

        T pop() {
            ExponentialBackoff bo;
            while(true) {
                // If pop fails, BLOCK
                if(auto v = try_pop()) {
                    return std::move(v);
                }
                bo.backoff();
            }
        }

        std::size_t capacity() const noexcept {
            return capacity_;
        }

        MPMCQueue (const MPMCQueue&) = delete;
        MPMCQueue& operator=(const MPMCQueue&) = delete;
        MPMCQueue (MPMCQueue&&) = delete;
        MPMCQueue& operator=(MPMCQueue&&) = delete;        

        private:
        std::size_t check_cap(std::size_t cap) {
            if (cap < 2) {
                throw std::invalid_argument("MPMCQueue must have a non-zero positive capacity >= 2");
            }
            return std::bit_ceil(cap);
        }

        // Align to cache line to prevent false sharing between multiple
        // producers/consumers
        struct alignas(cache_line_size) Node {
            std::atomic<std::size_t> seq;
            // Raw byte array to hold T
            alignas(T) std::byte data[sizeof(T)];
            T* get() noexcept {
                return std::launder(reinterpret_cast<T*>(data));
            }
        };
        const std::size_t capacity_;
        const std::size_t bit_mask_;
        // Use a vector to construct Nodes inplace
        std::vector<Node> queue;
        alignas(cache_line_size) std::atomic<std::size_t> tail_{0};
        alignas(cache_line_size) std::atomic<std::size_t> head_{0};
    };
} // namespace lockless

#endif // LOCKLESS_MPMC_QUEUE_H