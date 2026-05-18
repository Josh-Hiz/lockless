#ifndef LOCKLESS_FORK_JOIN_POOL_H
#define LOCKLESS_FORK_JOIN_POOL_H

#include "../containers/lockless_mpmc_queue.h"
#include "../core/lockless_backoff.h"
#include "lockless_task.h"
#include <atomic>
#include <cstddef>
#include <future>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <optional>
#include <functional>

namespace lockless {
    class ForkJoinPool {
    public:
        explicit ForkJoinPool(std::size_t n = std::thread::hardware_concurrency()) {
            queues_.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                queues_.emplace_back(std::make_unique<MPMCQueue<Task>>(QUEUE_CAPACITY));
            }
            workers_.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                workers_.emplace_back([this, i](std::stop_token st) { 
                    worker_loop(st, i); 
                });
            }
        }

        ~ForkJoinPool() {
            // jthread auto-joins on destruction
            for (auto& w : workers_) {
                w.request_stop();
            }
        }

        ForkJoinPool(const ForkJoinPool&) = delete;
        ForkJoinPool& operator=(const ForkJoinPool&) = delete;
        ForkJoinPool(ForkJoinPool&&) = delete;
        ForkJoinPool& operator=(ForkJoinPool&&)= delete;

        template <typename F>
        void submit(F&& f) {
            const std::size_t idx = rr_.fetch_add(1, std::memory_order_relaxed) % queues_.size();
            queues_[idx]->push(Task(std::forward<F>(f)));
        }

        template <typename F>
        auto submit_with_future(F&& f) -> std::future<std::invoke_result_t<F>> {
            auto pt = std::make_shared<std::packaged_task<std::invoke_result_t<F>()>>(std::forward<F>(f));
            auto fut = pt->get_future();
            submit([pt]() { (*pt)(); });
            return fut;
        }

    private:
        void worker_loop(std::stop_token st, std::size_t my_idx) {
            ExponentialBackoff bo;
            while (!st.stop_requested()) {
                if (auto t = try_get_work(my_idx)) {
                    (*t)();
                    bo.reset();
                } else {
                    bo.backoff();
                }
            }
        }

        std::optional<Task> try_get_work(std::size_t my_idx) {
            // Own queue first
            if (auto t = queues_[my_idx]->try_pop()) {
                return t;
            }
            // If the thread has no more tasks left, steal from neighboring threads
            const std::size_t n = queues_.size();
            for (std::size_t k = 1; k < n; ++k) {
                const std::size_t victim = (my_idx + k) % n;
                if (auto t = queues_[victim]->try_pop()) {
                    return t;
                }
            }
            return std::nullopt;
        }

        static constexpr std::size_t QUEUE_CAPACITY = 1024;
        std::vector<std::unique_ptr<MPMCQueue<Task>>> queues_;
        // All workers are jthreads so they automatically join on destruction
        std::vector<std::jthread> workers_;
        std::atomic<std::size_t> rr_{0};
    };

}  // namespace lockless

#endif  // LOCKLESS_FORK_JOIN_POOL_H