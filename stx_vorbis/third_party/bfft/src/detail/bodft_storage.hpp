#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

namespace bodft {
// Setup-only cursor over caller-owned plan bytes. A null base measures the
// identical layout without writing. No owner or allocator is retained by views.
struct storage_cursor final {
    unsigned char* base{nullptr};
    std::size_t capacity{0};
    std::size_t used{0};

    bool take(const std::size_t count, const std::size_t element_bytes,
              const std::size_t alignment, std::size_t& offset) noexcept {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        if (element_bytes == 0 || alignment == 0) return false;
        if (count > maximum / element_bytes) return false;
        const std::size_t bytes = count * element_bytes;
        const std::size_t remainder = used % alignment;
        const std::size_t padding = remainder == 0 ? 0 : alignment - remainder;
        if (padding > maximum - used) return false;
        const std::size_t aligned = used + padding;
        if (bytes > maximum - aligned) return false;
        const std::size_t end = aligned + bytes;
        if (base != nullptr && end > capacity) return false;
        offset = aligned;
        used = end;
        return true;
    }
};

// BODFT plan view, deliberately limited to trivial coefficient/index storage.
// The caller's plan allocation owns every element; release is not this view's
// responsibility. Elements are created once and are immutable during execution.
template <typename T>
class stored_array final {
public:
    bool prepare(storage_cursor& storage, const std::size_t count) noexcept {
        static_assert(std::is_trivially_destructible<T>::value, "BODFT plan elements must be trivial");
        if (storage.base == nullptr) return false;
        std::size_t offset = 0;
        if (!storage.take(count, sizeof(T), alignof(T), offset)) return false;
        T* const destination = reinterpret_cast<T*>(storage.base + offset);
        for (std::size_t index = 0; index < count; ++index) new (destination + index) T{};
        data_ = destination;
        return true;
    }
    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    T& operator[](const std::size_t index) noexcept { return data_[index]; }
    const T& operator[](const std::size_t index) const noexcept { return data_[index]; }
private:
    T* data_{nullptr};
};

template <typename T>
void provision(stored_array<T>& array, storage_cursor* const storage, const std::size_t count) {
    if (storage == nullptr || !array.prepare(*storage, count)) throw std::bad_alloc();
}
} // namespace bodft
