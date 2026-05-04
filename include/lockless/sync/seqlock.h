#ifndef _LOCKLESS_SEQLOCK_H_
#define _LOCKLESS_SEQLOCK_H_

#include "../core/memory.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace lockless {

template <typename T> class SeqLock {
  public:
    SeqLock() : seq_(0), data_{} {}
    explicit SeqLock(T initial) : seq_(0), data_(std::move(initial)) {}

    T read() const noexcept {
        while (true) {
            uint64_t seq1 = seq_.load(std::memory_order_acquire);
            // Do a bitwise AND to check if load was successful, neat trick
            // taken from Concurrency in C++
            if (seq1 & 1) {
                std::this_thread::yield();
                continue;
            }

            T copy = data_;

            std::atomic_thread_fence(std::memory_order_acquire);

            uint64_t seq2 = seq_.load(std::memory_order_relaxed);
            if (seq1 == seq2) {
                return copy;
            }
        }
    }

    void write(const T &val) {
        std::lock_guard lock(write_mutex_);
        uint64_t seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_release); // now odd

        data_ = val;

        seq_.store(seq + 2, std::memory_order_release); // now even again
    }

    void write(std::function<void(T &)> fn) {
        std::lock_guard lock(write_mutex_);
        uint64_t seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_release);

        fn(data_);

        seq_.store(seq + 2, std::memory_order_release);
    }

  private:
    std::atomic<uint64_t> seq_;
    T data_;
    std::mutex write_mutex_;
};

} // namespace lockless

#endif // _LOCKLESS_SEQLOCK_H_