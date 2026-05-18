#ifndef LOCKLESS_CACHE_H
#define LOCKLESS_CACHE_H

#include <cstddef>
#include <new>

namespace lockless { 
    // Get the correct cache sizes, works on most operating systems 
    // (Windows, MacOS, etc.)
    // https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size
    #ifdef __cpp_lib_hardware_interference_size
        inline constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size;
    #else
        inline constexpr std::size_t cache_line_size = 64;
    #endif

} // namespace lockless

#endif // _LOCKLESS_CACHE_H_