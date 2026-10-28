// Differential corpus for TArray and STArray over their whole public interface. A seeded
// sequence of operations runs on six array types: heap and inline storage, with trivial
// elements, elements relocated through their move constructors, and nontrivial elements marked
// trivially relocatable (moved as bytes). After every operation the corpus records the size,
// capacity, emptiness, whether the data is null or inside the object, every element value and
// the number of live elements. Both builds must produce identical bytes (tarray-core.raw).
//
// Sizes with exact capacities go to a second artifact (tarray-core-capacity.raw), which differs
// on purpose: the intake move constructor builds its storage from SkSpan(that), recording the
// size as the capacity, so a moved array under-reports the storage it owns. The candidate (since
// 40875c9c) moves the real capacity. The shared artifact keeps capacity >= size.
//
// Only inputs both builds define are used: no element of an array is passed back into it, and
// default-initialized trivial elements are assigned before they are read. The candidate alone
// checks appending the array's own elements and the Release stops; arguments beginning with "--"
// select a stop.

#include "differential_corpus.h"

#include "include/core/SkSpan.h"
#include "include/private/SkTArray.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;
using skia_private::STArray;
using skia_private::TArray;

int gTrackedLive = 0;
int gRelocatedLive = 0;
Output gCapacities{};

// Relocated through its move constructor; a moved-from element reads -1.
struct Tracked {
    int value = 0;
    Tracked() { ++gTrackedLive; }
    explicit Tracked(int v) : value(v) { ++gTrackedLive; }
    Tracked(const Tracked& other) : value(other.value) { ++gTrackedLive; }
    Tracked(Tracked&& other) noexcept : value(other.value) {
        other.value = -1;
        ++gTrackedLive;
    }
    Tracked& operator=(const Tracked& other) {
        value = other.value;
        return *this;
    }
    Tracked& operator=(Tracked&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Tracked() { --gTrackedLive; }
    bool operator==(const Tracked& other) const { return value == other.value; }
};
static_assert(!sk_is_trivially_relocatable_v<Tracked>);

// Nontrivial, but declared trivially relocatable, so TArray moves it as bytes.
struct Relocated {
    int value = 0;
    Relocated() { ++gRelocatedLive; }
    explicit Relocated(int v) : value(v) { ++gRelocatedLive; }
    Relocated(const Relocated& other) : value(other.value) { ++gRelocatedLive; }
    Relocated(Relocated&& other) noexcept : value(other.value) {
        other.value = -1;
        ++gRelocatedLive;
    }
    Relocated& operator=(const Relocated& other) {
        value = other.value;
        return *this;
    }
    Relocated& operator=(Relocated&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Relocated() { --gRelocatedLive; }
    bool operator==(const Relocated& other) const { return value == other.value; }
    using sk_is_trivially_relocatable = std::true_type;
};
static_assert(sk_is_trivially_relocatable_v<Relocated>);

int value_of(int element) { return element; }
int value_of(const Tracked& element) { return element.value; }
int value_of(const Relocated& element) { return element.value; }

template <typename T>
T make(int value) {
    if constexpr (std::is_same_v<T, int>) {
        return value;
    } else {
        return T(value);
    }
}

template <typename T>
void set_value(T& element, int value) {
    if constexpr (std::is_same_v<T, int>) {
        element = value;
    } else {
        element.value = value;
    }
}

template <typename T>
int live_count() {
    if constexpr (std::is_same_v<T, Tracked>) {
        return gTrackedLive;
    } else if constexpr (std::is_same_v<T, Relocated>) {
        return gRelocatedLive;
    } else {
        return 0;
    }
}

template <typename Array>
void record(Output& output, const Array& array) {
    using T = typename Array::value_type;
    append(output, static_cast<int32_t>(array.size()));
    append_bool(output, array.capacity() >= array.size());
    append(gCapacities, static_cast<int32_t>(array.size()));
    append(gCapacities, static_cast<int32_t>(array.capacity()));
    ++gCapacities.cases;
    append_bool(output, array.empty());
    append_bool(output, array.data() == nullptr);
    const std::byte* object = reinterpret_cast<const std::byte*>(&array);
    const std::byte* data = reinterpret_cast<const std::byte*>(array.data());
    const std::less_equal<const std::byte*> notAfter{};
    const std::less<const std::byte*> before{};
    const bool inside = data != nullptr && notAfter(object, data) &&
                        before(data, object + sizeof(array));
    append_bool(output, inside);
    for (int index = 0; index < array.size(); ++index) {
        append(output, static_cast<int32_t>(value_of(array[index])));
    }
    append(output, static_cast<int32_t>(live_count<T>()));
    ++output.cases;
}

// Assigns values to elements [from, size) that a default-initializing operation created.
template <typename Array>
void fill_from(Array& array, int from, int& next) {
    for (int index = from; index < array.size(); ++index) {
        set_value(array[index], next++);
    }
}

constexpr int kLimit = 160;

template <typename Array>
void run(Output& output, uint64_t seed, int steps) {
    using T = typename Array::value_type;
    Sequence sequence{};
    sequence.state = seed;
    Array array{};
    Array other{};
    int next = 1;
    for (int step = 0; step < steps; ++step) {
        const int operation = static_cast<int>(sequence.pick(30));
        append(output, static_cast<uint8_t>(operation));
        const int room = kLimit - array.size();
        const size_t largest = static_cast<size_t>(room < 12 ? room : 12);
        const int count = room > 0 ? static_cast<int>(sequence.pick(largest)) + 1 : 0;
        switch (operation) {
            case 0:
                if (room > 0) {
                    const T value = make<T>(next++);
                    array.push_back(value);
                }
                break;
            case 1:
                if (room > 0) {
                    T value = make<T>(next++);
                    array.push_back(std::move(value));
                }
                break;
            case 2:
                if (room > 0) array.emplace_back(make<T>(next++));
                break;
            case 3:
                if (room > 0) set_value(array.push_back(), next++);
                break;
            case 4: {
                const int before = array.size();
                if (room > 0) array.push_back_n(count);
                fill_from(array, before, next);
                break;
            }
            case 5:
                if (room > 0) {
                    const T value = make<T>(next++);
                    array.push_back_n(count, value);
                }
                break;
            case 6:
                if (room > 0) {
                    std::array<T, 12> source{};
                    for (T& element : source) set_value(element, next++);
                    array.push_back_n(count, source.data());
                }
                break;
            case 7:
                if (room > 0) {
                    std::array<T, 12> source{};
                    for (T& element : source) set_value(element, next++);
                    array.move_back_n(count, source.data());
                    for (const T& element : source) {
                        append(output, static_cast<int32_t>(value_of(element)));
                    }
                }
                break;
            case 8:
                if (!array.empty()) array.pop_back();
                break;
            case 9:
                if (!array.empty()) {
                    const size_t choices = static_cast<size_t>(array.size()) + 1;
                    array.pop_back_n(static_cast<int>(sequence.pick(choices)));
                }
                break;
            case 10: {
                const int before = array.size();
                array.resize_back(static_cast<int>(sequence.pick(kLimit + 1)));
                fill_from(array, before, next);
                break;
            }
            case 11: {
                const int before = array.size();
                array.resize(sequence.pick(kLimit + 1));
                fill_from(array, before, next);
                break;
            }
            case 12:
                array.reset(static_cast<int>(sequence.pick(kLimit / 2)));
                fill_from(array, 0, next);
                break;
            case 13: {
                std::array<T, 12> source{};
                for (T& element : source) set_value(element, next++);
                array.reset(SkSpan<const T>(source.data(), sequence.pick(13)));
                break;
            }
            case 14:
                array.reserve(static_cast<int>(sequence.pick(2 * kLimit)));
                break;
            case 15:
                array.reserve_exact(static_cast<int>(sequence.pick(2 * kLimit)));
                break;
            case 16:
                if (!array.empty()) {
                    const size_t choices = static_cast<size_t>(array.size());
                    array.removeShuffle(static_cast<int>(sequence.pick(choices)));
                }
                break;
            case 17:
                array.clear();
                break;
            case 18:
                array.shrink_to_fit();
                break;
            case 19:
                array.swap(other);
                record(output, other);
                break;
            case 20:
                if (array.size() + other.size() <= kLimit) array.move_back(other);
                record(output, other);
                break;
            case 21:
                array = other;
                break;
            case 22:
                array = std::move(other);
                record(output, other);
                break;
            case 23: {
                // Grows the second array so that swaps, moves and assignments carry content.
                const int before = other.size();
                other.push_back_n(static_cast<int>(sequence.pick(20)));
                fill_from(other, before, next);
                if (other.size() > kLimit / 2) other.clear();
                break;
            }
            case 24: {
                const Array copy(array);
                record(output, copy);
                append_bool(output, copy == array);
                append_bool(output, copy != array);
                append_bool(output, other == array);
                append_bool(output, other != array);
                break;
            }
            case 25: {
                Array moving(array);
                const Array moved(std::move(moving));
                record(output, moved);
                record(output, moving);
                break;
            }
            case 26:
                if (!array.empty()) {
                    const size_t choices = static_cast<size_t>(array.size());
                    const int index = static_cast<int>(sequence.pick(choices));
                    append(output, static_cast<int32_t>(value_of(array.front())));
                    append(output, static_cast<int32_t>(value_of(array.back())));
                    append(output, static_cast<int32_t>(value_of(array.fromBack(index))));
                    append(output, static_cast<int32_t>(value_of(array.at(index))));
                    set_value(array[index], next++);
                }
                break;
            case 27: {
                const Array& self = array;
                array = self;
                break;
            }
            case 28:
                array.swap(array);
                break;
            case 29: {
                int sum = 0;
                for (const T& element : array) sum += value_of(element);
                append(output, static_cast<int32_t>(sum));
                append(output, static_cast<uint64_t>(array.size_bytes()));
                break;
            }
        }
        record(output, array);
    }
}

template <typename Array>
void construction_cases(Output& output) {
    using T = typename Array::value_type;
    std::array<T, 20> source{};
    for (size_t index = 0; index < source.size(); ++index) {
        set_value(source[index], static_cast<int>(index) * 3 + 1);
    }
    for (const int count : {0, 1, 2, 3, 4, 5, 8, 9, 17, 20}) {
        const Array reserved(count);
        record(output, reserved);
        const Array copied(source.data(), count);
        record(output, copied);
        const Array spanned(SkSpan<const T>(source.data(), static_cast<size_t>(count)));
        record(output, spanned);
    }
    const Array listed{make<T>(5), make<T>(6), make<T>(7)};
    record(output, listed);
}

#ifndef STX_SKIA_REFERENCE
// Each mode violates one Release-checked precondition and must stop the program. The first five
// guard the existing bounds and emptiness checks, which trap through SkUNREACHABLE in the intake
// code too. At the last, the intake code reads elements it is still constructing.
int stop_mode(const char* mode) {
    TArray<int> values{1, 2, 3, 4};
    values.reserve_exact(16);
    TArray<int> empty{};
    if (std::strcmp(mode, "--index-past-end") == 0) {
        std::printf("%d\n", values[4]);
    } else if (std::strcmp(mode, "--index-negative") == 0) {
        std::printf("%d\n", values[-1]);
    } else if (std::strcmp(mode, "--front-empty") == 0) {
        std::printf("%d\n", empty.front());
    } else if (std::strcmp(mode, "--back-empty") == 0) {
        std::printf("%d\n", empty.back());
    } else if (std::strcmp(mode, "--pop-empty") == 0) {
        empty.pop_back();
    } else if (std::strcmp(mode, "--append-past-live") == 0) {
        // Capacity suffices, so no growth: the source runs past the live elements.
        values.push_back_n(4, values.data() + 2);
    } else {
        return 2;
    }
    return 0;
}

// Appending a run of the array's own live elements, with and without growth, copies them.
bool own_elements_append() {
    TArray<int> roomy{1, 2, 3, 4};
    roomy.reserve_exact(16);
    roomy.push_back_n(2, roomy.data() + 2);
    TArray<int> tight{1, 2, 3, 4};
    tight.shrink_to_fit();
    tight.push_back_n(4, tight.data());
    const TArray<int> roomyExpected{1, 2, 3, 4, 3, 4};
    const TArray<int> tightExpected{1, 2, 3, 4, 1, 2, 3, 4};
    return roomy == roomyExpected && tight == tightExpected;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strncmp(argv[1], "--", 2) == 0) return stop_mode(argv[1]);
    if (!own_elements_append()) {
        std::puts("tarray core: appending the array's own elements went wrong");
        return 1;
    }
#endif
    Output output{};
    construction_cases<TArray<int>>(output);
    construction_cases<TArray<Tracked>>(output);
    construction_cases<TArray<Relocated>>(output);
    construction_cases<STArray<4, int>>(output);
    construction_cases<STArray<3, Tracked>>(output);
    construction_cases<STArray<2, Relocated>>(output);
    for (uint64_t seed = 1; seed <= 4; ++seed) {
        run<TArray<int>>(output, seed, 1500);
        run<TArray<Tracked>>(output, seed + 10, 1500);
        run<TArray<Relocated>>(output, seed + 20, 1500);
        run<STArray<4, int>>(output, seed + 30, 1500);
        run<STArray<3, Tracked>>(output, seed + 40, 1500);
        run<STArray<2, Relocated>>(output, seed + 50, 1500);
    }
    // Every element has ended once its arrays have.
    if (gTrackedLive != 0 || gRelocatedLive != 0) {
        std::printf("tarray core: %d tracked and %d relocated elements outlived their arrays\n",
                    gTrackedLive, gRelocatedLive);
        return 1;
    }
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], gCapacities)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long capacityHash = corpus::fnv1a(gCapacities.bytes);
    std::printf("tarray core: %zu cases, %zu bytes, hash=%llu; capacities hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, capacityHash);
    return 0;
}
