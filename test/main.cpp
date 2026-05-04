#include "../include/lockless/lockless.h"
#include <atomic>
#include <iostream>
#include <thread>

lockless::SPSCQueue<int> queue(1024);

static std::atomic<int> counter{0};
static std::atomic<bool> ready{false};
static int data = 0;

static void task_1() {
    for (int i = 0; i < 1000; i++) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

static void task_2_1() {}

static void task_2_2() {}

int main() {
    // Producer thread
    std::thread producer([&] {
        for (int i = 0; i < 1000000; ++i)
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
    });

    // Consumer thread
    std::thread consumer([&] {
        int count = 0;
        while (count < 1000000) {
            if (auto val = queue.pop()) {
                std::cout << *val << std::endl;
                ++count;
            }
        }
    });
    std::thread t1(task_1);
    std::thread t2(task_1);

    t1.join();
    t2.join();
    int val = counter.load(std::memory_order_relaxed);
    std::cout << "Counter = " << val << std::endl;
    return 0;
}
