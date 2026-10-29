/*
 * Copyright 2019 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkZip_DEFINED
#define SkZip_DEFINED

#include "include/core/SkSpan.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <utility>

// Take a list of things that can be pointers, and use them all in parallel. The iterators and
// accessor operator[] for the class produce a tuple of the items.
template<typename... Ts>
class SkZip {
    using ReturnTuple = std::tuple<Ts&...>;

    class Iterator {
    public:
        using value_type = ReturnTuple;
        using difference_type = ptrdiff_t;
        using pointer = value_type*;
        using reference = value_type;
        using iterator_category = std::input_iterator_tag;
        constexpr Iterator(const SkZip* zip, size_t index) : fZip(zip), fIndex(index) {}
        constexpr Iterator(const Iterator& that) : Iterator(that.fZip, that.fIndex) {}
        constexpr Iterator& operator++() { ++fIndex; return *this; }
        constexpr Iterator operator++(int) { Iterator tmp(*this); operator++(); return tmp; }
        constexpr bool operator==(const Iterator& rhs) const { return fIndex == rhs.fIndex; }
        constexpr bool operator!=(const Iterator& rhs) const { return fIndex != rhs.fIndex; }
        constexpr reference operator*() { return (*fZip)[fIndex]; }
        friend constexpr difference_type operator-(Iterator lhs, Iterator rhs) {
            return lhs.fIndex - rhs.fIndex;
        }

    private:
        const SkZip* const fZip = nullptr;
        size_t fIndex = 0;
    };

    template<typename T>
    inline static constexpr T* nullify = nullptr;

public:
    constexpr SkZip() : fPointers(nullify<Ts>...), fSize(0) {}
    constexpr SkZip(size_t) = delete;
    constexpr SkZip(size_t size, Ts*... ts) : fPointers(ts...), fSize(size) {}
    constexpr SkZip(const SkZip& that) = default;
    constexpr SkZip& operator=(const SkZip &that) = default;

    // Check to see if U can be used for const T or is the same as T
    template <typename U, typename T>
    using CanConvertToConst = typename std::integral_constant<bool,
                    std::is_convertible<U*, T*>::value && sizeof(U) == sizeof(T)>::type;

    // Allow SkZip<const T> to be constructed from SkZip<T>. The intake code wrote enable_if
    // without ::type, which is always a valid type, so the size check never constrained this.
    template<typename... Us,
            typename = std::enable_if_t<std::conjunction<CanConvertToConst<Us, Ts>...>::value>>
    constexpr SkZip(const SkZip<Us...>& that) : fPointers(that.data()), fSize(that.size()) {}

    constexpr ReturnTuple operator[](size_t i) const { return (*this).index(i);}
    constexpr size_t size() const { return fSize; }
    constexpr bool empty() const { return (*this).size() == 0; }
    constexpr ReturnTuple front() const { return (*this).index(0); }
    constexpr ReturnTuple back() const { return (*this).index((*this).size() - 1); }
    constexpr Iterator begin() const { return Iterator(this, 0); }
    constexpr Iterator end() const { return Iterator(this, (*this).size()); }
    template<size_t I>
    constexpr SkSpan<std::tuple_element_t<I, std::tuple<Ts...>>> get() const {
        return SkSpan<std::tuple_element_t<I, std::tuple<Ts...>>>(std::get<I>(fPointers), fSize);
    }
    constexpr std::tuple<Ts*...> data() const { return fPointers; }
    // Lengths and offsets are checked as SkSpan checks them: a bad one stops the program.
    constexpr SkZip first(size_t n) const {
        const size_t length = sk_collection_check_length(n, fSize);
        if (length == 0) { return SkZip(); }
        return SkZip(length, fPointers);
    }
    constexpr SkZip last(size_t n) const {
        const size_t length = sk_collection_check_length(n, fSize);
        if (length == 0) { return SkZip(); }
        return SkZip(length, (*this).pointersAt(fSize - length));
    }
    constexpr SkZip subspan(size_t offset, size_t count) const {
        const size_t start = sk_collection_check_length(offset, fSize);
        const size_t length = sk_collection_check_length(count, fSize - start);
        if (length == 0) { return SkZip(); }
        return SkZip(length, pointersAt(start));
    }

private:
    constexpr SkZip(size_t n, const std::tuple<Ts*...>& pointers) : fPointers(pointers), fSize(n) {}

    constexpr ReturnTuple index(size_t i) const {
        const size_t checked = sk_collection_check_bounds(i, fSize);
        return indexDetail(checked, std::make_index_sequence<sizeof...(Ts)>());
    }

    template<std::size_t... Is>
    constexpr ReturnTuple indexDetail(size_t i, std::index_sequence<Is...>) const {
        return ReturnTuple((std::get<Is>(fPointers))[i]...);
    }

    std::tuple<Ts*...> pointersAt(size_t i) const {
        const size_t checked = sk_collection_check_bounds(i, fSize);
        return pointersAtDetail(checked, std::make_index_sequence<sizeof...(Ts)>());
    }

    template<std::size_t... Is>
    constexpr std::tuple<Ts*...> pointersAtDetail(size_t i, std::index_sequence<Is...>) const {
        return std::tuple<Ts*...>(&(std::get<Is>(fPointers))[i]...);
    }

    std::tuple<Ts*...> fPointers;
    size_t fSize;
};

class SkMakeZipDetail {
    template<typename T> struct DecayPointer{
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        using type = std::conditional_t<std::is_pointer_v<U>, U, T>;
    };
    template<typename T> using DecayPointerT = typename DecayPointer<T>::type;

    template<typename C> struct ContiguousMemory {};
    template<typename T> struct ContiguousMemory<T*> {
        using value_type = T;
        static constexpr value_type* Data(T* t) { return t; }
        static constexpr size_t Size(T* s) { return SIZE_MAX; }
    };
    template<typename T, size_t N> struct ContiguousMemory<T(&)[N]> {
        using value_type = T;
        static constexpr value_type* Data(T(&t)[N]) { return t; }
        static constexpr size_t Size(T(&)[N]) { return N; }
    };
    // In general, we don't want r-value collections, but SkSpans are ok, because they are a view
    // onto an actual container.
    template<typename T> struct ContiguousMemory<SkSpan<T>> {
        using value_type = T;
        static constexpr value_type* Data(SkSpan<T> s) { return s.data(); }
        static constexpr size_t Size(SkSpan<T> s) { return s.size(); }
    };
    // Only accept l-value references to collections.
    template<typename C> struct ContiguousMemory<C&> {
        // decltype is kept: a container's data() may be overloaded on const, so neither a member
        // pointer nor C::value_type names the element type it returns.
        using value_type = std::remove_pointer_t<decltype(std::declval<C>().data())>;
        static constexpr value_type* Data(C& c) { return c.data(); }
        static constexpr size_t Size(C& c) { return c.size(); }
    };
    template<typename C> using Span = ContiguousMemory<DecayPointerT<C>>;
public:
    // Public so that SkMakeZip can name its result type.
    template<typename C> using ValueType = typename Span<C>::value_type;

private:

    template<typename C, typename... Ts> struct PickOneSize {};
    template <typename T, typename... Ts> struct PickOneSize<T*, Ts...> {
        static constexpr size_t Size(T* t, Ts... ts) {
            return PickOneSize<Ts...>::Size(std::forward<Ts>(ts)...);
        }
    };
    template <typename T, typename... Ts, size_t N> struct PickOneSize<T(&)[N], Ts...> {
        static constexpr size_t Size(T(&)[N], Ts...) { return N; }
    };
    template<typename T, typename... Ts> struct PickOneSize<SkSpan<T>, Ts...> {
        static constexpr size_t Size(SkSpan<T> s, Ts...) { return s.size(); }
    };
    template<typename C, typename... Ts> struct PickOneSize<C&, Ts...> {
        static constexpr size_t Size(C& c, Ts...) { return c.size(); }
    };

public:
    template<typename... Ts>
    static constexpr SkZip<ValueType<Ts>...> MakeZip(Ts&& ... ts) {
        // NOLINTBEGIN
        // Pick the first collection that has a size, and use that for the size.
        size_t size = PickOneSize<DecayPointerT<Ts>...>::Size(std::forward<Ts>(ts)...);

        // Check that all sizes are the same; a shorter collection would be read past its end.
        size_t minSize = SIZE_MAX;
        size_t maxSize = 0;
        const size_t sizes[sizeof...(Ts)] = {Span<Ts>::Size(std::forward<Ts>(ts))...};
        for (const size_t s : sizes) {
            if (s != SIZE_MAX) {
                minSize = std::min(minSize, s);
                maxSize = std::max(maxSize, s);
            }
        }
        SkASSERT_RELEASE(minSize == maxSize);

        return SkZip<ValueType<Ts>...>(size, Span<Ts>::Data(std::forward<Ts>(ts))...);
        // NOLINTEND
    }
};

template<typename... Ts>
inline constexpr SkZip<SkMakeZipDetail::ValueType<Ts>...> SkMakeZip(Ts&& ... ts) {
    return SkMakeZipDetail::MakeZip(std::forward<Ts>(ts)...);
}
#endif //SkZip_DEFINED
