#ifndef _LOCKLESS_BACKOFF_H_
#define _LOCKLESS_BACKOFF_H_

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace lockless {

// Implemented the exponential backoff algorithm used for compare-and-swap
// actions
class ExponentialBackoff {
  public:
    static constexpr uint32_t spin_limit = 8;
    static constexpr uint32_t yield_limit = 16;

    // Exponential backoff algorithm
    void backoff() noexcept {
        if (count_ < spin_limit) {
            // Spin
            spin_pause();
        } else if (count_ < yield_limit) {
            // Yield
            std::this_thread::yield();
        } else {
            // Sleep
            std::this_thread::sleep_for(std::chrono::nanoseconds(count_ * 100));
        }
        // Grow the count for each failure
        count_ = std::min(count_ + 1, max_count_);
    }

    // Reset after a success
    void reset() noexcept { count_ = 0; }

    uint32_t count() const noexcept { return count_; }

  private:
    uint32_t count_ = 0;
    uint32_t max_count_ = 32;

    static void spin_pause() noexcept { asm volatile("yield" ::: "memory"); }
};

} // namespace lockless

#endif // _LOCKLESS_BACKOFF_H_