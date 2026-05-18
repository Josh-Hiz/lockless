#ifndef LOCKLESS_TASK_H
#define LOCKLESS_TASK_H

#include <functional>

namespace lockless {

using Task = std::function<void()>;

}  // namespace lockless

#endif  // LOCKLESS_TASK_H