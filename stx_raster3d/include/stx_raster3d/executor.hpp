#pragma once
// Caller-supplied execution for stx_raster3d. The renderer hands an executor
// a numbered set of independent tasks (bands of rows); the executor may run
// them in any order, on any threads, and returns when all have returned.
// Every task writes storage no other task of the set touches, and the
// tasks' results never depend on the order, so a picture is the same bytes
// on one thread or many.

#include <cstddef>

namespace stx_raster3d {

// One numbered piece of work: run(context, index). It neither throws nor
// waits on another task of its set.
struct Task final {
    void* context = nullptr;
    void (*run)(void* context, std::size_t index) noexcept = nullptr;
};

// Runs task.run(task.context, index) once for every index in [0, count) and
// returns when every call has returned. The executor's context is its own;
// the task's context is valid until run returns.
struct Executor final {
    void* context = nullptr;
    void (*run)(void* context, std::size_t count, Task task) noexcept = nullptr;
};

// Runs the tasks in index order on the calling thread.
[[nodiscard]] Executor inline_executor() noexcept;

} // namespace stx_raster3d
