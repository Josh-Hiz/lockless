#ifndef _LOCKLESS_SPSC_QUEUE_H_
#define _LOCKLESS_SPSC_QUEUE_H_

#include "../core/cache.h"
#include "../core/memory.h"
#include <atomic>
#include <bit>
#include <optional>
#include <stdexcept>
#include <vector>

namespace lockless {

template <typename T> class SPSCQueue {
  public:
    explicit SPSCQueue(std::size_t capacity)
        : capacity_(std::bit_ceil(capacity)) // round up to power of 2
          ,
          mask_(capacity_ - 1), buffer_(capacity_) {
        if (capacity == 0) {
            throw std::invalid_argument("SPSCQueue capacity must be > 0");
        }
    }

    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue &) = delete;
    SPSCQueue &operator=(const SPSCQueue &) = delete;

    // Push a value. Returns false if the queue is full.
    bool push(const T &val) { return emplace(val); }

    bool push(T &&val) { return emplace(std::move(val)); }

    template <typename... Args> bool emplace(Args &&...args) {
        const std::size_t tail = relaxed_load(tail_);

        const std::size_t next_tail = (tail + 1) & mask_;
        if (next_tail == acquire_load(head_)) {
            return false; // full
        }

        // Write the value into the slot.
        buffer_[tail] = T(std::forward<Args>(args)...);

        // release store on tail so the consumer sees the write above.
        release_store(tail_, next_tail);
        return true;
    }

    // Pop a value
    std::optional<T> pop() {
        const std::size_t head = relaxed_load(head_);

        if (head == acquire_load(tail_)) {
            return std::nullopt; // empty
        }

        T val = std::move(buffer_[head]);

        release_store(head_, (head + 1) & mask_);
        return val;
    }

    bool empty() const noexcept {
        return relaxed_load(head_) == relaxed_load(tail_);
    }

    std::size_t capacity() const noexcept { return capacity_; }

  private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<T> buffer_;

    alignas(cache_line_size) std::atomic<std::size_t> head_{0};
    alignas(cache_line_size) std::atomic<std::size_t> tail_{0};
};

} // namespace lockless

#endif // _LOCKLESS_SPSC_QUEUE_H_