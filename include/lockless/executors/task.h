#ifndef _LOCKLESS_TASK_H_
#define _LOCKLESS_TASK_H_

#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lockless {

class Task {
  public:
    static constexpr std::size_t kInlineSize = 64;

    Task() = default;

    // Construct from any callable (lambda, function pointer, functor).
    template <typename F> Task(F &&f) {
        using Fn = std::decay_t<F>;
        if constexpr (sizeof(Fn) <= kInlineSize &&
                      std::is_nothrow_move_constructible_v<Fn>) {
            // Small enough: store inline in our buffer.
            ::new (storage_) Fn(std::forward<F>(f));
            invoke_ = [](void *p) { (*static_cast<Fn *>(p))(); };
            destroy_ = [](void *p) { static_cast<Fn *>(p)->~Fn(); };
            move_ = [](void *src, void *dst) {
                ::new (dst) Fn(std::move(*static_cast<Fn *>(src)));
            };
            heap_ptr_ = nullptr;
        } else {
            // The callable is too large, so heap allocate instead
            heap_ptr_ = new Fn(std::forward<F>(f));
            invoke_ = [](void *p) { (*static_cast<Fn *>(p))(); };
            destroy_ = [](void *p) { delete static_cast<Fn *>(p); };
            move_ = nullptr; // pointer is just copied on move
        }
    }

    ~Task() {
        if (destroy_) {
            if (heap_ptr_) {
                destroy_(heap_ptr_);
            } else {
                destroy_(storage_);
            }
        }
    }

    // Movable, not copyable
    Task(Task &&other) noexcept { move_from(std::move(other)); }

    Task &operator=(Task &&other) noexcept {
        if (this != &other) {
            this->~Task();
            move_from(std::move(other));
        }
        return *this;
    }

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    // Invoke the stored callable.
    void operator()() {
        if (!invoke_) {
            throw std::bad_function_call{};
        }
        if (heap_ptr_) {
            invoke_(heap_ptr_);
        } else {
            invoke_(storage_);
        }
    }

    explicit operator bool() const noexcept { return invoke_ != nullptr; }

  private:
    void move_from(Task &&other) noexcept {
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        move_ = other.move_;
        heap_ptr_ = other.heap_ptr_;

        if (other.heap_ptr_) {
        } else if (move_) {
            move_(other.storage_, storage_);
        }

        other.invoke_ = nullptr;
        other.destroy_ = nullptr;
        other.move_ = nullptr;
        other.heap_ptr_ = nullptr;
    }

    alignas(std::max_align_t) std::byte storage_[kInlineSize]{};
    void (*invoke_)(void *) = nullptr;
    void (*destroy_)(void *) = nullptr;
    void (*move_)(void *, void *) = nullptr;
    void *heap_ptr_ = nullptr;
};

} // namespace lockless

#endif // _LOCKLESS_TASK_H_