#ifndef LOCKLESS_EBR_H
#define LOCKLESS_EBR_H

#include "lockless_cache.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>

namespace lockless {

// Simplified Epoch-Based Reclamation
//
// Each thread is either "inside" a critical section (slot holds the current
// global epoch) or "outside" (slot holds INACTIVE).
//
// Retire appends a pointer to the current-epoch list on the calling
// thread's per-thread retire vector. After each retire we try to advance
// the global epoch; if every active thread is at the current epoch, we
// advance and free this thread's two-epoch-old list.
//
// Three epochs: with two you cannot distinguish "thread just entered the
// new epoch" from "thread is still in the old one"; the third epoch is the
// grace period that proves no thread can still be holding a retired ptr.
// https://aturon.github.io/blog/2015/08/27/epoch/ and C++ Concurrency and action book
inline constexpr std::size_t kMaxThreads = 32;
inline constexpr std::size_t kEpochs     = 3;

class EbrDomain;

class EbrGuard {
public:
    explicit EbrGuard(EbrDomain& domain);
    ~EbrGuard();

    EbrGuard(const EbrGuard&) = delete;
    EbrGuard& operator=(const EbrGuard&) = delete;
    EbrGuard(EbrGuard&&) = delete;
    EbrGuard& operator=(EbrGuard&&) = delete;

    // Schedule ptr for deferred deletion. Will be freed only after every
    // thread currently inside its critical section has exited.
    template <typename T>
    void retire(T* ptr);

private:
    EbrDomain*  domain_;
    std::size_t slot_idx_;
};

class EbrDomain {
public:
    EbrDomain() = default;

    EbrDomain(const EbrDomain&) = delete;
    EbrDomain& operator=(const EbrDomain&) = delete;

    // Drains every retire list. Safe only if no thread is in a critical
    // section at destruction (the standard "no concurrent access at dtor"
    // contract).
    ~EbrDomain() {
        for (auto& slot : slots_) {
            for (auto& list : slot.retired) {
                for (auto& r : list) {
                    r.deleter(r.ptr);
                }
                list.clear();
            }
        }
    }

private:
    friend class EbrGuard;

    struct RetiredNode {
        void* ptr;
        void (*deleter)(void*);
    };

    struct alignas(cache_line_size) Node {
        static constexpr uint64_t kInactive = std::numeric_limits<uint64_t>::max();
        std::atomic<uint64_t>    epoch{kInactive};
        std::vector<RetiredNode> retired[kEpochs];
    };

    // Lazy per-thread slot assignment. Each thread atomically grabs the
    // next free index on its first call into this domain.
    std::size_t assign_slot() {
        thread_local std::size_t slot = static_cast<std::size_t>(-1);
        if (slot == static_cast<std::size_t>(-1)) {
            slot = num_threads_.fetch_add(1, std::memory_order_relaxed);
            if (slot >= kMaxThreads) std::terminate();
        }
        return slot;
    }

    void enter(std::size_t slot_idx) {
        // seq_cst on both sides: need a global total order between
        // "thread announces epoch V" and "another thread observes slot V".
        // Without it, a reclaimer could advance past us while our store is
        // still in flight, freeing memory we're about to read.
        uint64_t e = global_epoch_.load(std::memory_order_seq_cst);
        slots_[slot_idx].epoch.store(e, std::memory_order_seq_cst);
    }

    void leave(std::size_t slot_idx) {
        slots_[slot_idx].epoch.store(Node::kInactive, std::memory_order_release);
    }

    void retire_impl(std::size_t slot_idx, void* ptr, void (*deleter)(void*)) {
        uint64_t e = global_epoch_.load(std::memory_order_relaxed);
        slots_[slot_idx].retired[e % kEpochs].push_back({ptr, deleter});
        try_advance(slot_idx);
    }

    void try_advance(std::size_t my_slot) {
        uint64_t    current = global_epoch_.load(std::memory_order_seq_cst);
        std::size_t n       = num_threads_.load(std::memory_order_acquire);

        for (std::size_t i = 0; i < n; ++i) {
            uint64_t te = slots_[i].epoch.load(std::memory_order_seq_cst);
            if (te != Node::kInactive && te != current) {
                return;  // someone is still in a previous epoch
            }
        }

        uint64_t expected = current;
        if (!global_epoch_.compare_exchange_strong(
                expected, current + 1,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return;
        }

        // Reclaimthe two-epochs-old list. After the advance, global_epoch
        // is current+1; the list at index (current-1) mod kEpochs has had
        // a full grace period and is safe to free.
        std::size_t reclaim_idx = (current + 2) % kEpochs;   // = (current-1) mod 3
        for (auto& r : slots_[my_slot].retired[reclaim_idx]) r.deleter(r.ptr);
        slots_[my_slot].retired[reclaim_idx].clear();
    }

    std::atomic<uint64_t>            global_epoch_{0};
    std::atomic<std::size_t>         num_threads_{0};
    std::array<Node, kMaxThreads>    slots_;
};

// EbrGuard inline implementations
inline EbrGuard::EbrGuard(EbrDomain& domain)
    : domain_(&domain), slot_idx_(domain.assign_slot()) {
    domain_->enter(slot_idx_);
}

inline EbrGuard::~EbrGuard() {
    domain_->leave(slot_idx_);
}

template <typename T>
void EbrGuard::retire(T* ptr) {
    domain_->retire_impl(slot_idx_, ptr, [](void* p) { delete static_cast<T*>(p); });
}

}  // namespace lockless

#endif  // _LOCKLESS_EBR_H_