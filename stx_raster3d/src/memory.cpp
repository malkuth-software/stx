// The system allocator and the inline executor.

#include <stx_raster3d/executor.hpp>
#include <stx_raster3d/memory.hpp>

#include <cstddef>
#include <new>

namespace stx_raster3d {

namespace {

void* system_allocate(void*, std::size_t bytes, std::size_t alignment) noexcept {
    void* const address = ::operator new(bytes, std::align_val_t(alignment), std::nothrow);
    return address;
}

void system_release(void*, void* address, std::size_t, std::size_t alignment) noexcept {
    ::operator delete(address, std::align_val_t(alignment), std::nothrow);
}

void run_inline(void*, std::size_t count, Task task) noexcept {
    for (std::size_t index = 0U; index < count; ++index) task.run(task.context, index);
}

} // namespace

Allocator system_allocator() noexcept {
    Allocator allocator;
    allocator.allocate = &system_allocate;
    allocator.release = &system_release;
    return allocator;
}

Executor inline_executor() noexcept {
    Executor executor;
    executor.run = &run_inline;
    return executor;
}

} // namespace stx_raster3d
