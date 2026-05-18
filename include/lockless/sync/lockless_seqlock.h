#ifndef LOCKLESS_SEQLOCK_H
#define LOCKLESS_SEQLOCK_H

#include "../core/lockless_backoff.h"

#include <atomic>
#include <cstring>
#include <type_traits>
#include <cstddef>

namespace lockless {
    // Implementation of SeqLock:
    // https://github.com/rigtorp/Seqlock/blob/master/include/rigtorp/Seqlock.h
    // https://github.com/sergiu128/seqlock.cpp/blob/master/seqlock/include/seqlock/seqlock.hpp
    // A almost lock-free multiple reader single writer paradigm that is safe and
    // less costly than mpmc, mostly done for fun tbh
    template<typename T>
    class SeqLock {
        
        static_assert(std::is_trivially_copyable_v<T>, "SeqLock<T> requires trivially copyable T");
        static_assert(std::is_nothrow_default_constructible_v<T>, "SeqLock<T> requires nothrow default-constructible T");
        
        public:
        explicit SeqLock(const T& initial) noexcept : seq_(0) {
            std::memcpy(&data_, &initial, sizeof(T));
        }

        T read() const noexcept {
            ExponentialBackoff bo;
            T res;
            std::size_t s1, s2;
            while (true) {
                s1 = seq_.load(std::memory_order_acquire);
                if (s1 & 1u) { 
                    bo.backoff(); 
                    continue; 
                }
                std::memcpy(&res, data_, sizeof(T));
                std::atomic_thread_fence(std::memory_order_acquire);
                s2 = seq_.load(std::memory_order_relaxed);
                if (s1 == s2) {
                    return res;
                }
                bo.backoff();
            }
        }

        void write(const T& val) noexcept {
            const std::size_t s = seq_.load(std::memory_order_relaxed);
            seq_.store(s + 1, std::memory_order_relaxed); // odd sequence number
            std::atomic_thread_fence(std::memory_order_release);    
            std::memcpy(&data_, &val, sizeof(T));
            seq_.store(s + 2, std::memory_order_release); // even again
        }

        private:
        std::atomic<std::size_t> seq_;
        alignas(T) std::byte data_[sizeof(T)]{};

    };
} // namespace lockless

#endif // LOCKLESS_SEQLOCK_H