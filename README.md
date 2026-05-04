# lockless

A C++23 concurrency library providing lock-free data structures and thread executors.

## Features

### Concurrent Containers

| Type              | Header                         | Description                                                               |
|-------------------|--------------------------------|---------------------------------------------------------------------------|
| `SPSCQueue<T>`    | `containers/spsc_queue.h`      | Single-producer, single-consumer ring buffer.                             |
| `MPMCQueue<T>`    | `containers/mpmc_queue.h`      | Multi-producer, multi-consumer bounded queue (Vyukov's algorithm).        |

### Synchronization

| Type         | Header             | Description                                       |
|--------------|--------------------|---------------------------------------------------|
| `SeqLock<T>` | `sync/seqlock.h`   | Read-optimized lock. Readers never block writers. |

### Threadpool types and thread executors

| Type               | Header                      | Description                                              |
|--------------------|-----------------------------|----------------------------------------------------------|
| `StaticThreadPool` | `executors/thread_pool.h`   | Work-stealing thread pool with per-thread queues.        |
| `Task`             | `executors/task.h`          | Callable.                                                |

### Concurrency Primitives

| Type                             | Header             | Description                                           |
|----------------------------------|--------------------|-------------------------------------------------------|
| `acquire_load` / `release_store` | `core/memory.h`    | Named memory-order wrappers.                          |
| `acq_rel_cas` / `release_cas`    | `core/memory.h`    | Named CAS operations.                                 |
| `CacheLineAligned<T>`            | `core/cache.h`     | Pads T to a full cache line to prevent false sharing. |
| `PaddedAtomic<T>`                | `core/cache.h`     | Atomic on its own cache line.                         |

ExponentialBackoff and EBR guards are also provided, but are extremely niche, this was mostly implemented out of interest.

You can find them in `core/backoff.h` and `core/ebr.h` if you are interested.

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

### With ThreadSanitizer

```bash
cmake -B build -DSANITIZE=thread
cmake --build build 
```

### With AddressSanitizer

```bash
cmake -B build -DSANITIZE=address
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
#include <lockless/containers/mpmc_queue.h> 
```

## Examples

### SPSC Queue — inter-thread pipeline

```cpp
#include <lockless/containers/spsc_queue.h>
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
#include <lockless/containers/mpmc_queue.h>

lockless::MPMCQueue<Task> queue(4096);

// Many producers
queue.push(task);

// Many consumers
if (auto t = queue.pop()) execute(*t);
```

### Thread Pool

```cpp
#include <lockless/executors/thread_pool.h>

lockless::StaticThreadPool pool;  // one thread per core

pool.submit([]{ do_work(); });

// Wait for result
auto future = pool.submit_with_future([]{ return heavy_compute(); });
int result = future.get();

pool.shutdown();
```

### SeqLock

```cpp
#include <lockless/sync/seqlock.h>

struct Config { int timeout; int retries; };
lockless::SeqLock<Config> config(Config{30, 3});

// Readers
Config c = config.read();

// Writer
config.write([](Config& c){ c.timeout = 60; });
```

## TODO

1. Implement a fully automated test.cpp file instead of just testing with them in a main.cpp.

## Resources

I used the following resources when creating Lockless (MLA format):

Williams, A. (2019). *C++ concurrency in action* (2nd ed.). Manning Publications.

Michael, M. M., & Scott, M. L. (1996). Simple, fast, and practical non-blocking and blocking concurrent queue algorithms. *Proceedings of the 15th Annual ACM Symposium on Principles of Distributed Computing*, 267–275. <https://doi.org/10.1145/248052.248106>

Fraser, K. (2004). *Practical lock-freedom* [Doctoral dissertation, University of Cambridge]. <https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-579.pdf>

Vyukov, D. (2010). *Bounded MPMC queue*. 1024cores. <https://sites.google.com/site/1024cores/home/lock-free-algorithms/queues/bounded-mpmc-queue>

## Disclaimer

THIS DOES NOT WORK ON INTEL MACHINES MOST LIKELY. I developed this purely for playing around on my Mac M1 Pro, I highly doubt machines with intel architectures are gonna work with Lockless.

This project was meant for me to learn lock-free concurreny in C++ and I don't think this should be used in real projects, this is a sort of "for-fun" project because I am extremely interested in C++'s memory model and how we can achieve lock-free data-strucutres and thread-pools. Thread-pools have been quite revolutionary and I use them a lot in both personal projects as well as academic work and the student group that I am a part of (Stevens Student Managed Investment Fund). In addition, I didn't necessarily make this library with best practices in mind, so it is more than probable that its slower than your usual concurrency libraries.

I used my Mac M1 Pro to make Lockless with LLVM/Clang auto-formatting (on-save) on VSCode (4-space indent).
