#pragma once
#include "stx_vorbis/types.hpp"
#include <algorithm>
#include <memory_resource>
#include <new>
#include <stdexcept>
namespace stx_vorbis::detail {
// Counts exact allocator requests, excluding the upstream allocator's bookkeeping.
// No global allocator, shared scratch, or synchronization is introduced.
class Memory final : public std::pmr::memory_resource {
public:
    explicit Memory(const std::size_t limit, std::pmr::memory_resource* const upstream)
        : limit_(limit), upstream_(upstream == nullptr ? std::pmr::new_delete_resource() : upstream) {}
    std::size_t current{0};
    std::size_t peak{0};
    bool limited{false};
private:
    void* do_allocate(const std::size_t bytes, const std::size_t alignment) override {
        if (bytes > limit_ - current) { limited = true; throw std::bad_alloc(); }
        void* const result = (*upstream_).allocate(bytes, alignment);
        current += bytes;
        peak = std::max(peak, current);
        return result;
    }
    void do_deallocate(void* const address, const std::size_t bytes, const std::size_t alignment) override {
        (*upstream_).deallocate(address, bytes, alignment);
        current -= bytes;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    const std::size_t limit_;
    std::pmr::memory_resource* const upstream_;
};
struct DecodeFailure final { Status status; std::size_t bit; };
inline void require(const bool condition, const Status status = Status::invalid_header, const std::size_t bit = 0) {
    if (!condition) throw DecodeFailure{status, bit};
}
inline std::size_t product(const std::size_t first, const std::size_t second, const std::size_t limit) {
    require(second == 0 || first <= limit / second, Status::resource_limit);
    const std::size_t result = first * second;
    return result;
}
inline unsigned int ilog(std::uint32_t value) noexcept {
    unsigned int result = 0;
    while (value != 0) { ++result; value >>= 1; }
    return result;
}
} // namespace stx_vorbis::detail
