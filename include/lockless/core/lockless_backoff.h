#ifndef LOCKLESS_BACKOFF_H
#define LOCKLESS_BACKOFF_H

#include <thread>

namespace lockless {
    // Basic exponential backoff to efficiently busy-wait threads, only
    // useful for mpmc tbh when multiple threads try to push to queue but fail
    class ExponentialBackoff {
        public:
        void backoff() noexcept {
            if(count_ < limit) {
                const unsigned int n = 1 << count_; // 2^count
                for(unsigned int i = 0; i < n; ++i) {
                    asm volatile("yield" ::: "memory");
                } 
            } else if(count_ < yield_limit) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(count_));
            }
            count_ = std::min(count_ + 1, max);
        }
        void reset() noexcept { count_ = 0; }
        unsigned int count() const noexcept { return count_; }

        private:
        unsigned int count_{0};
        static constexpr unsigned int limit = 8;
        static constexpr unsigned int yield_limit = 16;
        static constexpr unsigned int max = 32;
    };
} // namespace lockless

#endif // LOCKLESS_BACKOFF_H