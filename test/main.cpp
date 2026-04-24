#include <atomic>
#include <iostream>
#include <thread>

static std::atomic<int> counter{0};
static std::atomic<bool> ready{false};
static int data = 0;

// memory_order_relaxed just says:
// "Just do the atomic operation. I don't care about
// ordering relative to anything else". In other words
// it doesn't matter if instructions are reordered or not here
static void task_1() {
    for (int i = 0; i < 1000; i++) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

static void task_2_1() {}

static void task_2_2() {}

int main() {
    std::thread t1(task_1);
    std::thread t2(task_1);

    t1.join();
    t2.join();
    int val = counter.load(std::memory_order_relaxed);
    std::cout << "Counter = " << val << std::endl;
    return 0;
}