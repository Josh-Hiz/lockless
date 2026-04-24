cmake_minimum_required(VERSION 3.20)
project(lockless VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(lockless INTERFACE)
add_library(lockless::lockless ALIAS lockless)

target_include_directories(lockless INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(lockless INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:
        -Wall -Wextra -Wpedantic
        -Wno-interference-size>
)

option(SANITIZE "Enable sanitizers (thread|address|undefined)" "")
if(SANITIZE STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
elseif(SANITIZE STREQUAL "address")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

option(LOCKLESS_TESTS "Build tests" ON)
if(LOCKLESS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()