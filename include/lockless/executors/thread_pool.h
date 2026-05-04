#ifndef _LOCKLESS_THREAD_POOL_H_
#define _LOCKLESS_THREAD_POOL_H_

#include "../containers/mpmc_queue.h"
#include "../core/backoff.h"
#include "task.h"
#include <atomic>
#include <functional>
#include <future>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace lockless {

// My implementation of a Work-Stealing Thread Pool, similar to something I did
// in Python for Stevens SSMIF
class StaticThreadPool {
  public:
    explicit StaticThreadPool(
        std::size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads ? num_threads : 1), global_queue_(4096),
          shutdown_(false), pending_(0) {
        workers_.reserve(num_threads_);
        local_queues_.reserve(num_threads_);

        for (std::size_t i = 0; i < num_threads_; ++i) {
            local_queues_.emplace_back(std::make_unique<MPMCQueue<Task>>(512));
        }

        for (std::size_t i = 0; i < num_threads_; ++i) {
            workers_.emplace_back(&StaticThreadPool::worker_loop, this, i);
        }
    }

    ~StaticThreadPool() { shutdown(); }

    StaticThreadPool(const StaticThreadPool &) = delete;
    StaticThreadPool &operator=(const StaticThreadPool &) = delete;

    template <typename F> void submit(F &&f) {
        pending_.fetch_add(1, std::memory_order_relaxed);
        // Wrap task to decrement pending on completion
        global_queue_.push(Task([fn = std::forward<F>(f), this]() mutable {
            fn();
            pending_.fetch_sub(1, std::memory_order_release);
        }));
    }

    template <typename F>
    auto submit_with_future(F &&f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto promise = std::make_shared<std::promise<R>>();
        auto future = promise->get_future();
        submit([fn = std::forward<F>(f), p = std::move(promise)]() mutable {
            try {
                if constexpr (std::is_void_v<R>) {
                    fn();
                    p->set_value();
                } else {
                    p->set_value(fn());
                }
            } catch (...) {
                p->set_exception(std::current_exception());
            }
        });
        return future;
    }

    // Wait for all tasks, then stop.
    void shutdown() {
        if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        // Wait until all submitted tasks have completed.
        while (pending_.load(std::memory_order_acquire) > 0)
            std::this_thread::yield();
        for (auto &t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    std::size_t thread_count() const noexcept { return num_threads_; }

  private:
    void worker_loop(std::size_t idx) {
        std::mt19937 rng(static_cast<uint32_t>(idx));
        std::uniform_int_distribution<std::size_t> dist(0, num_threads_ - 1);
        ExponentialBackoff backoff;

        while (true) {
            std::optional<Task> task;

            task = local_queues_[idx]->pop();
            if (!task) {
                task = global_queue_.pop();
            }
            if (!task) {
                std::size_t victim = dist(rng);
                if (victim != idx) {
                    task = local_queues_[victim]->pop();
                }
            }

            if (task) {
                (*task)();
                backoff.reset();
            } else {
                // Only exit when shutdown and no work remains
                if (shutdown_.load(std::memory_order_acquire) &&
                    pending_.load(std::memory_order_acquire) == 0)
                    break;
                backoff.backoff();
            }
        }
    }

    const std::size_t num_threads_;
    MPMCQueue<Task> global_queue_;
    std::vector<std::unique_ptr<MPMCQueue<Task>>> local_queues_;
    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_;
    std::atomic<int> pending_;
};

} // namespace lockless

#endif // _LOCKLESS_THREAD_POOL_H_