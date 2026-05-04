#ifndef _LOCKLESS_CACHE_H_
#define _LOCKLESS_CACHE_H_

#include <atomic>
#include <cstddef>
#include <new>

namespace lockless {

#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t cache_line_size =
    std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t cache_line_size = 64;
#endif

template <typename T> struct alignas(cache_line_size) CacheLineAligned {
    T value;

    // Basically to calculate how much padding to fit on one cache line,
    // take the cache line size, subtract it by the size of the underlying
    // type mod the cache line, if it already fits on one cache line no padding
    // needed but if it doesn't fit it needs to be moved up by that amount.
    char padding[cache_line_size - (sizeof(T) % cache_line_size) ==
                         cache_line_size
                     ? 0
                     : cache_line_size - (sizeof(T) % cache_line_size)];

    CacheLineAligned() = default;

    operator T &() noexcept { return value; }
    operator const T &() const noexcept { return value; }
    T *operator->() noexcept { return &value; }
    const T *operator->() const noexcept { return &value; }
};

template <typename T> struct alignas(cache_line_size) PaddedAtomic {
    std::atomic<T> value{};
    char padding[cache_line_size - sizeof(std::atomic<T>) % cache_line_size ==
                         cache_line_size
                     ? 0
                     : cache_line_size -
                           sizeof(std::atomic<T>) % cache_line_size];

    PaddedAtomic() = default;
    explicit PaddedAtomic(T v) : value(v) {}

    std::atomic<T> *operator->() noexcept { return &value; }
    const std::atomic<T> *operator->() const noexcept { return &value; }
    std::atomic<T> &get() noexcept { return value; }
    const std::atomic<T> &get() const noexcept { return value; }
};

} // namespace lockless

#endif // _LOCKLESS_CACHE_H_