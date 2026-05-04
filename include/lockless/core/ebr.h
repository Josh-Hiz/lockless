#ifndef _LOCKLESS_EBR_H_
#define _LOCKLESS_EBR_H_

#include "cache.h"
#include "memory.h"
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace lockless {

inline constexpr std::size_t kMaxEbrThreads = 64;

inline constexpr std::size_t kNumEpochs = 3;

inline constexpr std::size_t kEbrBatchSize = 64;

class EbrDomain;

struct RetiredNode {
    void *ptr;
    void (*deleter)(void *);

    void reclaim() { deleter(ptr); }
};

// Per-thread state. Each participating thread gets one of these slots.
struct alignas(cache_line_size) EbrThreadState {
    static constexpr uint64_t kInactive = std::numeric_limits<uint64_t>::max();

    std::atomic<uint64_t> epoch{kInactive}; // announced epoch, or kInactive
    std::vector<RetiredNode> retire_lists[kNumEpochs]; // one list per epoch
    std::size_t retire_count{0};
};

class EbrGuard {
  public:
    EbrGuard() = default;
    EbrGuard(EbrDomain *domain, EbrThreadState *state)
        : domain_(domain), state_(state) {}

    // Non-copyable, movable
    EbrGuard(const EbrGuard &) = delete;
    EbrGuard &operator=(const EbrGuard &) = delete;
    EbrGuard(EbrGuard &&o) noexcept : domain_(o.domain_), state_(o.state_) {
        o.domain_ = nullptr;
        o.state_ = nullptr;
    }

    ~EbrGuard() {
        if (domain_)
            exit();
    }

    // Schedule a node for deferred deletion.
    template <typename T> void retire(T *ptr) {
        retire_impl(ptr, [](void *p) { delete static_cast<T *>(p); });
    }

  private:
    void retire_impl(void *ptr, void (*deleter)(void *));
    void exit();

    EbrDomain *domain_{nullptr};
    EbrThreadState *state_{nullptr};
};

class EbrDomain {
  public:
    EbrDomain() : global_epoch_(0) {}

    // Called by a thread before accessing shared data.
    // Returns a guard that, when destroyed, exits the critical section.
    EbrGuard enter() {
        auto *state = get_thread_state();
        uint64_t epoch = global_epoch_.load(std::memory_order_acquire);
        state->epoch.store(epoch, std::memory_order_release);
        return EbrGuard{this, state};
    }

  private:
    friend class EbrGuard;

    // Try to advance the global epoch and reclaim old nodes.
    void try_advance_and_collect(EbrThreadState *state) {
        uint64_t current_epoch = global_epoch_.load(std::memory_order_acquire);

        // Check if all active threads are in the current epoch.
        // If any thread is in an older epoch, we can't advance.
        {
            std::lock_guard lock(slots_mutex_);
            for (std::size_t i = 0; i < num_threads_; ++i) {
                uint64_t te = slots_[i].epoch.load(std::memory_order_acquire);
                if (te != EbrThreadState::kInactive && te != current_epoch) {
                    return; // someone is still in an old epoch
                }
            }
        }

        // All threads are current. Try to advance the epoch.
        uint64_t next_epoch = (current_epoch + 1) % kNumEpochs;
        if (!global_epoch_.compare_exchange_strong(current_epoch, next_epoch,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
            return;
        }

        // Reclaim nodes from two epochs ago
        uint64_t reclaim_epoch = (next_epoch + 1) % kNumEpochs;
        for (auto &node : state->retire_lists[reclaim_epoch]) {
            node.reclaim();
        }
        state->retire_lists[reclaim_epoch].clear();
    }

    void exit_guard(EbrThreadState *state) {
        state->epoch.store(EbrThreadState::kInactive,
                           std::memory_order_release);
    }

    void retire_node(EbrThreadState *state, void *ptr,
                     void (*deleter)(void *)) {
        uint64_t epoch =
            global_epoch_.load(std::memory_order_relaxed) % kNumEpochs;
        state->retire_lists[epoch].push_back({ptr, deleter});
        state->retire_count++;

        if (state->retire_count >= kEbrBatchSize) {
            state->retire_count = 0;
            try_advance_and_collect(state);
        }
    }

    // Each thread gets its own slot via thread_local index.
    EbrThreadState *get_thread_state() {
        static thread_local int my_index = -1;
        if (my_index == -1) {
            std::lock_guard lock(slots_mutex_);
            my_index = static_cast<int>(num_threads_++);
        }
        return &slots_[my_index];
    }

    std::atomic<uint64_t> global_epoch_;
    std::array<EbrThreadState, kMaxEbrThreads> slots_;
    std::size_t num_threads_{0};
    std::mutex slots_mutex_;
};

inline void EbrGuard::retire_impl(void *ptr, void (*deleter)(void *)) {
    domain_->retire_node(state_, ptr, deleter);
}

inline void EbrGuard::exit() { domain_->exit_guard(state_); }

} // namespace lockless

#endif // _LOCKLESS_EBR_H_