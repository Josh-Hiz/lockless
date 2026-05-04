#ifndef _LOCKLESS_MEMORY_H_
#define _LOCKLESS_MEMORY_H_

#include <atomic>

namespace lockless {

template <typename T> T relaxed_load(const std::atomic<T> &a) noexcept {
    return a.load(std::memory_order_relaxed);
}

template <typename T> T acquire_load(const std::atomic<T> &a) noexcept {
    return a.load(std::memory_order_acquire);
}

template <typename T> T seq_cst_load(const std::atomic<T> &a) noexcept {
    return a.load(std::memory_order_seq_cst);
}

template <typename T> void relaxed_store(std::atomic<T> &a, T val) noexcept {
    a.store(val, std::memory_order_relaxed);
}

template <typename T> void release_store(std::atomic<T> &a, T val) noexcept {
    a.store(val, std::memory_order_release);
}

template <typename T>
bool cas_weak(std::atomic<T> &a, T &expected, T desired,
              std::memory_order success_order,
              std::memory_order failure_order) noexcept {
    return a.compare_exchange_weak(expected, desired, success_order,
                                   failure_order);
}

template <typename T>
bool acq_rel_cas(std::atomic<T> &a, T &expected, T desired) noexcept {
    return cas_weak(a, expected, desired, std::memory_order_acq_rel,
                    std::memory_order_acquire);
}

template <typename T>
bool release_cas(std::atomic<T> &a, T &expected, T desired) noexcept {
    return cas_weak(a, expected, desired, std::memory_order_release,
                    std::memory_order_relaxed);
}

} // namespace lockless

#endif // _LOCKLESS_MEMORY_H_