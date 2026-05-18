cmake_minimum_required(VERSION 3.25)
project(lockless VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build Lockless
add_library(lockless INTERFACE)
add_library(lockless::lockless ALIAS lockless)

target_include_directories(lockless INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(lockless INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

# Lockless requires threading available
find_package(Threads REQUIRED)
target_link_libraries(lockless INTERFACE Threads::Threads)

# Build a test binary
add_executable(lockless_test test/main.cpp)
target_link_libraries(lockless_test PRIVATE lockless::lockless)

