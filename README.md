# lockless

A C++23 concurrency library providing lock-free data structures and thread executors.

## Features

### Concurrent Containers

1. `SPSCQueue<T>` in `containers/lockless_spsc_queue.h`: Single-producer, single-consumer ring buffer.
2. `MPMCQueue<T>` in `containers/lockless_mpmc_queue.h`: Multi-producer, multi-consumer bounded queue (Vyukov's algorithm).
3. `TreiberStack<T>` in `containers/lockless_stack.h`: Treiber stack using a LIFO linked-list.

### Synchronization Primitives

1. `SeqLock<T>` in `sync/lockless_seqlock.h`: Single-writer multiple-readers where readers never block writers, built on atomics and a sequence counter.

### Threadpool Executors

1. `ForkJoinPool` in `executors/lockless_fork_join_pool.h`: Work-stealing thread pool with per-thread lock-free MPMC queues and a global overflow queue.
2. `Task` in `executors/lockless_task.h`: Move-only, type-erased nullary callable with small-buffer optimization. Used internally by the pool to store enqueued work without allocation in the common case.

`ExponentialBackoff` and EBR guards are also provided but are extremely niche. They were implemented mostly out of interest and for use inside the other primitives. You can find them in `core/lockless_backoff.h` and `core/lockless_ebr.h` if you're curious.

## Requirements

- C++23-capable compiler (GCC 13+, Clang 16+)
- CMake 3.25+

## Building

```bash
git clone https://github.com/Josh-Hiz/lockless
cd lockless
cmake -B build
cmake --build build
```

## Usage

### Adding to your CMake project

```cmake
add_subdirectory(lockless)
target_link_libraries(your_target PRIVATE lockless::lockless)
```

Then include what you need:

```cpp
#include <lockless/lockless.h>                         
#include <lockless/containers/lockless_mpmc_queue.h>   
```

## Examples

See `test/main.cpp` for end-to-end correctness tests covering each primitive.

### SPSC Queue — inter-thread pipeline

```cpp
#include <lockless/containers/lockless_spsc_queue.h>
#include <thread>

lockless::SPSCQueue<int> queue(1024);

// Producer thread
std::thread producer([&]{
    for (int i = 0; i < 1000000; ++i)
        while (!queue.push(i)) std::this_thread::yield();
});

// Consumer thread
std::thread consumer([&]{
    int count = 0;
    while (count < 1000000) {
        if (auto val = queue.pop()) {
            process(*val);
            ++count;
        }
    }
});
```

### MPMC Queue

```cpp
#include <lockless/containers/lockless_mpmc_queue.h>

lockless::MPMCQueue<Task> queue(4096);

// Many producers
queue.push(task);                       // blocking with exponential backoff
// or: queue.try_push(task);            // non-blocking

// Many consumers
if (auto t = queue.try_pop()) execute(*t);
```

### Treiber Stack

```cpp
#include <lockless/containers/lockless_stack.h>

lockless::TreiberStack<int> stack;

// Push from any number of threads
stack.push(42);

// Pop from any number of threads
if (auto v = stack.pop()) {
    process(*v);
}
```

### Fork-Join Thread Pool (Workstealing per-queue threadpool inspired by Java ForkJoinPool)
```cpp
#include <lockless/executors/lockless_fork_join_pool.h>

lockless::ForkJoinPool pool; // one worker per hardware thread

pool.submit([]{ do_work(); });

// Returns a future for the result
auto future = pool.submit_with_future([]{ return heavy_compute(); });
int result = future.get();
// Workers are joined when the pool goes out of scope.
```

### SeqLock

```cpp
#include <lockless/sync/lockless_seqlock.h>

struct Config { int timeout; int retries; };
lockless::SeqLock<Config> config(Config{30, 3});

// Many readers (lock-free; readers retry internally if a writer is active)
Config c = config.read();

// Single writer
config.write(Config{60, 5});
```

## Resources

The following resources were used when creating Lockless:

Williams, A. (2019). *C++ concurrency in action* (2nd ed.). Manning Publications.

Michael, M. M., & Scott, M. L. (1996). Simple, fast, and practical non-blocking and blocking concurrent queue algorithms. *Proceedings of the 15th Annual ACM Symposium on Principles of Distributed Computing*, 267–275. https://doi.org/10.1145/248052.248106

Fraser, K. (2004). *Practical lock-freedom* [Doctoral dissertation, University of Cambridge]. https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-579.pdf

Vyukov, D. (2010). *Bounded MPMC queue*. 1024cores. https://sites.google.com/site/1024cores/home/lock-free-algorithms/queues/bounded-mpmc-queue

*What is Low Latency C++? (Part 2) — Timur Doumler — CppNow 2023*. https://www.youtube.com/watch?v=5uIsadq-nyk

## Disclaimer
THIS LIKELY DOES NOT WORK ON INTEL MACHINES. I developed this purely out of my own interest on a Mac M1 Pro; I highly doubt machines with Intel architectures will play nicely with Lockless given that `ExponentialBackoff` only utilizes ARM instruction hints for yield.

This project was meant for me to learn lock-free concurrency in C++ and isn't intended for use in real projects. It's a "for-fun" library because I'm extremely interested in C++'s memory model and how we achieve lock-free data structures and thread pools. Thread pools have been quite revolutionary and I use them a lot in both personal projects as well as academic work and the student group I am a part of (Stevens Student Managed Investment Fund). I didn't necessarily build this library with all best practices in mind, so it is more than probable that it's slower than mature concurrency libraries.