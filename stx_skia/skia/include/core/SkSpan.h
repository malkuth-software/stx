/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSpan_DEFINED
#define SkSpan_DEFINED

/*
 *  Define this to keep the bespoke SkSpan implementation, until clients are ready.
 *  When this is not defined, SkSpan becomes just an alias for std::span
 */
#define SK_USE_LEGACY_SKSPAN

/*
 *  SkSpan handles single POD initialization differently from std::span
 *  This macro allows clients to initialize a span with a single POD that will work with
 *  either implementation of SkSpan.
 *
 *  When we switch to std::span entirely, callsites can remove this macro, and use {{a}}
 */
#ifdef SK_USE_LEGACY_SKSPAN
    #define SKSPAN_INIT_ONE(elem)   {elem}
#else
    #define SKSPAN_INIT_ONE(elem)   {{elem}}
#endif

#ifdef SK_USE_LEGACY_SKSPAN

#include "include/private/SkAssert.h"
#include "include/private/SkTo.h"

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <span>
#include <utility>

// Having this be an export works around IWYU churn related to
// https://github.com/include-what-you-use/include-what-you-use/issues/1121
#include <type_traits> // IWYU pragma: export

// Add macro to check the lifetime of initializer_list arguments. initializer_list has a very
// short life span, and can only be used as a parameter, and not as a variable.
#if defined(__clang__) && defined(__has_cpp_attribute) && __has_cpp_attribute(clang::lifetimebound)
#define SK_CHECK_IL_LIFETIME [[clang::lifetimebound]]
#else
#define SK_CHECK_IL_LIFETIME
#endif

namespace skia_private {
// Keep non-containers out of overload resolution, including variant's conversion
// probes. Validate element access here rather than failing inside the constructor.
template <typename Container, typename Element, typename = void>
struct SpanContainerTraits {
    static constexpr bool compatible = false;
};

template <typename Container, typename Element>
struct SpanContainerTraits<Container, Element,
        std::void_t<decltype(std::data(std::declval<Container&>())),
                    decltype(std::size(std::declval<Container&>()))>> {
    using DataPointer = decltype(std::data(std::declval<Container&>()));
    using Size = decltype(std::size(std::declval<Container&>()));
    static constexpr bool compatible = std::is_convertible_v<DataPointer, Element*> &&
                                       std::is_integral_v<Size>;
};
}

/**
 * SkSpan holds a reference to contiguous data of type T along with a count. SkSpan does not own
 * the data itself but is merely a reference, therefore you must take care with the lifetime of
 * the underlying data.
 *
 * SkSpan is a count and a pointer into existing array or data type that stores its data in
 * contiguous memory like std::vector. Any container that works with std::size() and std::data()
 * can be used.
 *
 * SkSpan makes a convenient parameter for a routine to accept array like things. This allows you to
 * write the routine without overloads for all different container types.
 *
 * Example:
 *     void routine(SkSpan<const int> a) { ... }
 *
 *     std::vector<int> values = {1, 2, 3, 4, 5};
 *
 *     routine(values);
 *
 * A word of caution when working with initializer_list, initializer_lists have a lifetime that is
 * limited to the current statement. The following is correct and safe:
 *
 * Example:
 *     routine({1,2,3,4,5});
 *
 * The following is undefined, and will result in erratic execution:
 *
 * Bad Example:
 *     SkSpan<const int> borrowed = {1, 2, 3, 4, 5}; // Backing array ends here.
 *     routine(borrowed);
 */
template <typename T>
class SkSpan {
public:
    constexpr SkSpan() : fPtr{nullptr}, fSize{0} {}

    template <typename Integer, std::enable_if_t<std::is_integral_v<Integer>, bool> = true>
    constexpr SkSpan(T* ptr, Integer size) : fPtr{ptr}, fSize{SkToSizeT(size)} {
        SkASSERT(ptr || fSize == 0);  // disallow nullptr + a nonzero size
        SkASSERT(fSize < (std::numeric_limits<size_t>::max() / sizeof(T)));
    }
    template <typename U, typename = std::enable_if_t<std::is_same_v<const U, T>>>
    constexpr SkSpan(const SkSpan<U>& that) : fPtr(std::data(that)), fSize(std::size(that)) {}
    constexpr SkSpan(const SkSpan& o) = default;
    template<size_t N> constexpr SkSpan(T(&a)[N]) : SkSpan(a, N) { }
    template<typename Container>
        requires skia_private::SpanContainerTraits<Container, T>::compatible
    constexpr SkSpan(Container& c) : SkSpan(std::data(c), std::size(c)) { }
    // Borrowed views may be temporary; owning containers must be lvalues.
    template<typename U, size_t Extent,
             typename = std::enable_if_t<std::is_convertible_v<U(*)[], T(*)[]>>>
    constexpr SkSpan(std::span<U, Extent> view) : SkSpan(view.data(), view.size()) {}
    SkSpan(std::initializer_list<T> il SK_CHECK_IL_LIFETIME)
            : SkSpan(std::data(il), std::size(il)) {}

    constexpr SkSpan& operator=(const SkSpan& that) = default;

    constexpr T& operator[](size_t index) const {
        const size_t checked = sk_collection_check_bounds(index, fSize);
        return fPtr[checked];
    }
    constexpr T& front() const {
        sk_collection_not_empty(empty());
        return fPtr[0];
    }
    constexpr T& back() const {
        sk_collection_not_empty(empty());
        return fPtr[fSize - 1];
    }
    constexpr T* begin() const { return fPtr; }
    constexpr T* end() const {
        if (fSize == 0) return fPtr;
        T* limit = fPtr + fSize;
        return limit;
    }
    constexpr std::reverse_iterator<T*> rbegin() const {
        const std::reverse_iterator<T*> iterator(end());
        return iterator;
    }
    constexpr std::reverse_iterator<T*> rend() const {
        const std::reverse_iterator<T*> iterator(begin());
        return iterator;
    }
    constexpr T* data() const { return fPtr; }
    constexpr size_t size() const { return fSize; }
    constexpr bool empty() const {
        const bool no_elements = fSize == 0;
        return no_elements;
    }
    constexpr size_t size_bytes() const {
        const size_t bytes = fSize * sizeof(T);
        return bytes;
    }
    constexpr SkSpan<T> first(size_t prefix_length) const {
        const size_t length = sk_collection_check_length(prefix_length, fSize);
        const SkSpan<T> view(fPtr, length);
        return view;
    }
    constexpr SkSpan<T> last(size_t suffix_length) const {
        const size_t length = sk_collection_check_length(suffix_length, fSize);
        const size_t offset = fSize - length;
        const SkSpan<T> view = subspan(offset, length);
        return view;
    }
    constexpr SkSpan<T> subspan(size_t offset) const {
        const size_t checked_offset = sk_collection_check_length(offset, fSize);
        const size_t remaining = fSize - checked_offset;
        const SkSpan<T> view = subspan(checked_offset, remaining);
        return view;
    }
    constexpr SkSpan<T> subspan(size_t offset, size_t count) const {
        const size_t checked_offset = sk_collection_check_length(offset, fSize);
        const size_t remaining = fSize - checked_offset;
        if (count > remaining) SK_UNLIKELY {
            SkUNREACHABLE;
        }
        // An empty default span has no allocation on which to do pointer arithmetic.
        T* start = fPtr;
        if (checked_offset != 0) start += checked_offset;
        const SkSpan<T> view(start, count);
        return view;
    }

private:
    T* fPtr;
    size_t fSize;
};

#else

#include <span>

template <typename T> using SkSpan = std::span<T>;

#endif  // legacy

// These helper functions are temporary and only meant to be used during the Spanify process
// to ease the transition from pointers to spans (e.g. replacing `ptr++`).
// Once the Spanify process is complete, these callsites should be refactored to use
// more idiomatic span patterns, and these functions should be removed.
template <typename T>
constexpr SkSpan<T> SkPreIncrementSpan(SkSpan<T>& s) {
    s = s.subspan(1);
    return s;
}

template <typename T>
constexpr SkSpan<T> SkPostIncrementSpan(SkSpan<T>& s) {
    SkSpan<T> old = s;
    s = s.subspan(1);
    return old;
}

#endif  // SkSpan_DEFINED
