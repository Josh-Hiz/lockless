#include <lockless/lockless.h>
#include <print>
#include <thread>

static void print_header(const std::string& text) {
    std::print("\n\033[1;36m--- {} ---\033[0m\n", text);
}

static void lockless_spsc_test() {
    print_header("Running SPSCQueue: 1 producer, 1 consumer, 5M items");
    constexpr unsigned int items = 5000000;
    std::uint64_t sum = 0; // Should be the sum from i = 0...1023    
    lockless::SPSCQueue<unsigned int> queue(1024);

    // n * (n-1) / 2 = Sigma from i = 0 to n
    constexpr std::uint64_t gold = (std::uint64_t(items) - 1) * items / 2;

    const auto t0 = std::chrono::steady_clock::now();

    std::thread producer([items, &queue]() {
        for(unsigned int i = 0; i < items; ++i) {
            // Keep attempting to push i until true
            while(!queue.push(i));
        }
    });

    std::thread consumer([items, &queue, &sum]() {
        unsigned int seen = 0;
        while(seen < items) {
            if(auto v = queue.pop()) {
                sum += static_cast<std::uint64_t>(*v);
                ++seen;
            }
        }
    });
    producer.join();
    consumer.join();
    
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::println("Items per second: {}", items / elapsed);
    std::println("Sum = {}\nExpected = {}\nResult = {}", sum, gold, sum == gold ? "PASS" : "FAIL");
}

static void lockless_mpmc_test() {
    print_header("Running MPMCQueue: 4 producers, 4 consumers, 4M items, 1M per producer");
    constexpr unsigned int producers = 4;
    constexpr unsigned int consumers = 4;
    constexpr unsigned int items = 1000000;
    constexpr unsigned int total = producers * items;

    lockless::MPMCQueue<unsigned int> queue(4096);
    
    std::vector<std::thread> producer_v;
    std::vector<std::thread> consumer_v;
    
    std::atomic<std::int64_t> sum{0};
    std::atomic<unsigned int> seen{0};

    constexpr std::int64_t gold = (std::int64_t(total) - 1) * total / 2;
    
    const auto t0 = std::chrono::steady_clock::now();
    
    for(unsigned int i = 0; i < producers; ++i) {
        producer_v.emplace_back([i, &queue](){
            const int b = i * items;
            for(unsigned int j = 0; j < items; ++j) {
                queue.push(b + j);
            }
        });
    }

    for(unsigned int i = 0; i < consumers; ++i) {
        consumer_v.emplace_back([&queue, &seen, &sum](){
            while(seen.load(std::memory_order_acquire) < total) {
                if(auto v = queue.try_pop()) {
                    sum.fetch_add(*v, std::memory_order_relaxed);
                    seen.fetch_add(1, std::memory_order_release);
                }
            }
        });
    }

    for(auto& t : producer_v) {
        t.join();
    }

    for(auto& t : consumer_v) {
        t.join();
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::println("Items per second: {}", total / elapsed);
    auto v = sum.load();
    std::println("Sum = {}\nExpected = {}\nResult = {}", v, gold, v == gold ? "PASS" : "FAIL");
}

static void lockless_stack_test() {
    print_header("Running TreiberStack: 4 producers, 4 consumers, 4M items, 1M per producer");
    constexpr unsigned int producers = 4;
    constexpr unsigned int consumers = 4;
    constexpr unsigned int items = 1000000;
    constexpr unsigned int total = producers * items;

    std::vector<std::thread> producer_v;
    std::vector<std::thread> consumer_v;

    std::atomic<std::int64_t> sum{0};
    std::atomic<unsigned int> seen{0};

    lockless::TreiberStack<unsigned int> stack;

    constexpr std::int64_t gold = (std::int64_t(total) - 1) * total / 2;
    
    const auto t0 = std::chrono::steady_clock::now();
    
    for(unsigned int i = 0; i < producers; ++i) {
        producer_v.emplace_back([i, &stack](){
            const unsigned int b = i * items;
            for(unsigned int j = 0; j < items; ++j) {
                stack.push(b + j);
            }
        });
    }

    for(unsigned int i = 0; i < consumers; ++i) {
        consumer_v.emplace_back([&seen, &stack, &sum](){
            while(seen.load(std::memory_order_acquire) < total) {
                if(auto v = stack.pop()) {
                    sum.fetch_add(*v, std::memory_order_relaxed);
                    seen.fetch_add(1, std::memory_order_release);
                }
            }
        });
    }

    for(auto& t : producer_v) {
        t.join();
    }

    for(auto& t : consumer_v) {
        t.join();
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::println("Items per second: {}", total / elapsed);
    auto v = sum.load();
    std::println("Sum = {}\nExpected = {}\nResult = {}", v, gold, v == gold ? "PASS" : "FAIL");
}

struct FakeNode {
    std::uint64_t original;
    std::uint64_t not_original;
};

static void lockless_seqlock_test() {
    print_header("Running SeqLock: 1 writer, 8 readers, 200K writes");

    constexpr unsigned int writes = 1000000;
    constexpr unsigned int readers = 8;
    
    lockless::SeqLock<FakeNode> seqlock(FakeNode{0, ~std::uint64_t{0}});

    std::atomic<std::uint64_t> torn{0};
    std::atomic<bool> stop{false};

    std::vector<std::thread> reader_v;
    
    const auto t0 = std::chrono::steady_clock::now();
    std::thread writer([&](){
        for(std::uint64_t v = 1; v <= writes; ++v) {
            seqlock.write({v, ~v});
        }
        stop.store(true, std::memory_order_release);
    });

    for(unsigned int i = 0; i < readers; ++i) {
        reader_v.emplace_back([&seqlock, &stop, &torn](){
            while(!stop.load(std::memory_order_acquire)) {
                FakeNode fn = seqlock.read();
                if((fn.original ^ fn.not_original) != ~std::uint64_t{0}) {
                    std::println("{} {}", fn.original, fn.not_original);
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    writer.join();
    for(auto& t : reader_v) {
        t.join();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto v = torn.load();
    std::println("Torn = {}\nExpected = 0\nResult = {}", v, v == 0 ? "PASS" : "FAIL");
}

static void lockless_fork_join_pool_test() {
    print_header("Running ForkJoinPool: vector reduction, 1M elements, 10 sections");

    constexpr unsigned int N = 10000000;
    constexpr unsigned int sections = 16;
    constexpr unsigned int chunk_size = N / sections;
    constexpr std::int64_t gold = std::int64_t(N) * (N - 1) / 2;
    std::int64_t sum = 0;
    std::vector<std::int64_t> data(N);

    for (unsigned int i = 0; i < N; ++i) {
        data[i] = i;
    }

    lockless::ForkJoinPool pool;
    std::vector<std::future<std::int64_t>> futures;
    futures.reserve(sections);

    const auto t0 = std::chrono::steady_clock::now();

    for (unsigned int c = 0; c < sections; ++c) {
        const unsigned int start = c * chunk_size;
        const unsigned int end = (c == sections - 1) ? N : start + chunk_size;
        futures.push_back(pool.submit_with_future([&data, start, end]() {
            std::int64_t local = 0;
            for (unsigned int i = start; i < end; ++i) {
                local += data[i];
            }
            return local;
        }));
    }

    for (auto& f : futures) {
        sum += f.get();
    }
    
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::println("Elements per second: {}", N / elapsed);
    std::println("Sum = {}\nExpected = {}\nResult = {}", sum, gold, sum == gold ? "PASS" : "FAIL");
}

/*
* Runs demo tests for all supported containers, synch primitives, and executors 
  tests include the following:
*     1. SPSC Queue: 1 producer and 1 consumer
*     2. MPMC Queue: 5 producers and 5 consumers
*     3. Lock-free stack: 5 threads pushing and 5 threads popping
*     4. SeqLock: single writer, multiple readers paradigm test
*     5. Thread pool: Take the sum of a large vector using reduction with 
         work-stealing algorithm
*/
static void run_tests() {
    print_header("Running Tests");
    
    const auto t0 = std::chrono::steady_clock::now();
    
    lockless_spsc_test();
    lockless_mpmc_test();
    lockless_stack_test();
    lockless_seqlock_test();
    lockless_fork_join_pool_test();

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    
    std::println("All tests took in total: {} seconds", elapsed);
}

int main() {
    print_header("Lockless version 1.0 demo run");

    run_tests();
    
    print_header("Thank you for using Lockless!");

    return 0;
}
