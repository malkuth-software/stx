#pragma once
// Caller-supplied storage for stx_raster3d: a named allocator with its
// context, and the move-only buffer every owner in the package keeps its
// storage in. Nothing in the package allocates any other way.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace stx_raster3d {

// What an operation reports. An operation that fails says what it leaves
// behind in its own contract; none throws.
enum class Status : std::uint8_t {
    ok,
    invalid_argument, // a shape, count or reference that cannot be used
    nonfinite_input,  // a coordinate or value that is not finite
    out_of_memory,    // the allocator refused
    too_large         // a stated budget (triangles, samples) would be passed
};

// Allocates `bytes` aligned to `alignment` (a power of two) and returns null
// on failure; release receives the address with the same size and
// alignment. Both are called on the thread that owns the buffer, never
// concurrently for one buffer. The context is the allocator's own and
// outlives every buffer made with it.
struct Allocator final {
    void* context = nullptr;
    void* (*allocate)(void* context, std::size_t bytes, std::size_t alignment) noexcept = nullptr;
    void (*release)(void* context, void* address, std::size_t bytes, std::size_t alignment) noexcept = nullptr;
};

// The global aligned operator new and delete (nothrow).
[[nodiscard]] Allocator system_allocator() noexcept;
[[nodiscard]] inline bool allocator_valid(const Allocator& allocator) noexcept {
    return allocator.allocate != nullptr && allocator.release != nullptr;
}

// A contiguous array of trivially copyable elements in storage from one
// allocator. Move-only; moving transfers the storage and leaves the source
// empty with the same allocator. Growth never frees the old storage before
// the new storage exists, so a refused growth leaves the buffer unchanged.
template<class T>
class Buffer final {
    static_assert(std::is_trivially_copyable_v<T>, "Buffer holds trivially copyable elements only");

public:
    explicit Buffer(const Allocator& allocator) noexcept : allocator_(allocator) {}
    ~Buffer() { release(); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept
        : allocator_(other.allocator_), data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0U;
        other.capacity_ = 0U;
    }
    Buffer& operator=(Buffer&& other) noexcept {
        if (this == &other) return *this;
        release();
        allocator_ = other.allocator_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0U;
        other.capacity_ = 0U;
        return *this;
    }

    // Makes room for `capacity` elements, keeping the live ones. False when
    // the size overflows or the allocator refuses; the buffer is unchanged.
    [[nodiscard]] bool reserve(std::size_t capacity) noexcept {
        if (capacity <= capacity_) return true;
        if (!allocator_valid(allocator_)) return false;
        if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(T)) return false;
        const std::size_t bytes = capacity * sizeof(T);
        void* const fresh = allocator_.allocate(allocator_.context, bytes, alignof(T));
        if (fresh == nullptr) return false;
        if (size_ > 0U) std::memcpy(fresh, static_cast<const void*>(data_), size_ * sizeof(T));
        release_storage();
        data_ = static_cast<T*>(fresh);
        capacity_ = capacity;
        return true;
    }
    // Sets the live length to `count`; elements past the old length are
    // unspecified until written. False (and unchanged) when growth fails.
    [[nodiscard]] bool resize_for_overwrite(std::size_t count) noexcept {
        if (!reserve(count)) return false;
        size_ = count;
        return true;
    }
    // Sets the live length to `count`, every element `value`.
    [[nodiscard]] bool assign(std::size_t count, const T& value) noexcept {
        if (!reserve(count)) return false;
        for (std::size_t index = 0U; index < count; ++index) data_[index] = value;
        size_ = count;
        return true;
    }
    // Appends one element, doubling the capacity when full.
    [[nodiscard]] bool push_back(const T& value) noexcept {
        if (size_ == capacity_) {
            const std::size_t grown = capacity_ < 16U ? 16U : capacity_ * 2U;
            if (grown < capacity_ || !reserve(grown)) return false;
        }
        data_[size_] = value;
        ++size_;
        return true;
    }
    // Forgets the live elements and keeps the storage.
    void clear() noexcept { size_ = 0U; }
    // Gives the storage back to the allocator.
    void release() noexcept {
        release_storage();
        size_ = 0U;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
    [[nodiscard]] T& operator[](std::size_t index) noexcept { return data_[index]; }
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept { return data_[index]; }
    [[nodiscard]] std::span<T> view() & noexcept { return std::span<T>(data_, size_); }
    [[nodiscard]] std::span<const T> view() const& noexcept { return std::span<const T>(data_, size_); }
    std::span<const T> view() const&& = delete;
    [[nodiscard]] std::size_t storage_bytes() const noexcept { return capacity_ * sizeof(T); }

private:
    void release_storage() noexcept {
        if (data_ != nullptr) allocator_.release(allocator_.context, data_, capacity_ * sizeof(T), alignof(T));
        data_ = nullptr;
        capacity_ = 0U;
    }

    Allocator allocator_{};
    T* data_ = nullptr;
    std::size_t size_ = 0U;
    std::size_t capacity_ = 0U;
};

} // namespace stx_raster3d
