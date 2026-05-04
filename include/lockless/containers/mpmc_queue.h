#ifndef _LOCKLESS_MPMC_QUEUE_H_
#define _LOCKLESS_MPMC_QUEUE_H_

#include "../core/backoff.h"
#include "../core/cache.h"
#include "../core/memory.h"
#include <atomic>
#include <bit>
#include <optional>
#include <stdexcept>
#include <vector>

namespace lockless {

template <typename T> class MPMCQueue {
  private:
    struct alignas(cache_line_size) Slot {
        std::atomic<std::size_t> sequence;

        alignas(T) std::byte storage[sizeof(T)];

        T *ptr() noexcept {
            return std::launder(reinterpret_cast<T *>(storage));
        }
        const T *ptr() const noexcept {
            return std::launder(reinterpret_cast<const T *>(storage));
        }
    };

  public:
    explicit MPMCQueue(std::size_t capacity)
        : capacity_(std::bit_ceil(capacity)), mask_(capacity_ - 1),
          slots_(capacity_) {
        if (capacity == 0) {
            throw std::invalid_argument("MPMCQueue capacity must be > 0");
        }

        for (std::size_t i = 0; i < capacity_; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MPMCQueue() {
        while (pop()) {
        }
    }

    MPMCQueue(const MPMCQueue &) = delete;
    MPMCQueue &operator=(const MPMCQueue &) = delete;

    // Push a value
    void push(const T &val) { emplace_blocking(val); }
    void push(T &&val) { emplace_blocking(std::move(val)); }

    // Try to push
    bool try_push(const T &val) { return try_emplace(val); }
    bool try_push(T &&val) { return try_emplace(std::move(val)); }

    // Pop a value
    T pop_blocking() {
        ExponentialBackoff backoff;
        while (true) {
            if (auto val = pop())
                return std::move(*val);
            backoff.backoff();
        }
    }

    // Try to pop
    std::optional<T> pop() {
        std::size_t head = head_.load(std::memory_order_relaxed);

        while (true) {
            Slot &slot = slots_[head & mask_];
            std::size_t seq = slot.sequence.load(std::memory_order_acquire);
            std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                  static_cast<std::ptrdiff_t>(head + 1);

            if (diff == 0) {
                if (head_.compare_exchange_weak(head, head + 1,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    T val = std::move(*slot.ptr());
                    slot.ptr()->~T(); // explicitly destroy
                    slot.sequence.store(head + capacity_,
                                        std::memory_order_release);
                    return val;
                }
                // CAS failed, another consumer claimed it. Retry with updated
                // head.
            } else if (diff < 0) {
                // Sequence is behind, slot hasn't been written yet. Queue is
                // empty.
                return std::nullopt;
            }
            // diff > 0: another thread advanced head
            head = head_.load(std::memory_order_relaxed);
        }
    }

    std::size_t size_approx() const noexcept {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        return tail > head ? tail - head : 0;
    }

    bool empty() const noexcept { return size_approx() == 0; }
    std::size_t capacity() const noexcept { return capacity_; }

  private:
    template <typename... Args> bool try_emplace(Args &&...args) {
        std::size_t tail = tail_.load(std::memory_order_relaxed);

        while (true) {
            Slot &slot = slots_[tail & mask_];
            std::size_t seq = slot.sequence.load(std::memory_order_acquire);
            std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                                  static_cast<std::ptrdiff_t>(tail);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(tail, tail + 1,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed)) {
                    ::new (slot.storage) T(std::forward<Args>(args)...);
                    slot.sequence.store(tail + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            }
            tail = tail_.load(std::memory_order_relaxed);
        }
    }

    template <typename... Args> void emplace_blocking(Args &&...args) {
        ExponentialBackoff backoff;
        while (!try_emplace(std::forward<Args>(args)...))
            backoff.backoff();
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<Slot> slots_;

    alignas(cache_line_size) std::atomic<std::size_t> head_{0};
    alignas(cache_line_size) std::atomic<std::size_t> tail_{0};
};

} // namespace lockless

#endif // _LOCKLESS_MPMC_QUEUE_H_