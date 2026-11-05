// Differential corpus for the template helpers: SkZip and SkMakeZip, SkTLazy,
// SkTCopyOnFirstWrite, SkEnumBitMask, SkTPin, SkTFitsIn, SkTo, the saturating 32-bit helpers,
// SkAbs32, SkSpanPriv and SkCount. Each is exercised over its boundary values and records every
// result. Both builds must produce identical bytes (template-core.raw).
//
// The candidate alone checks what the intake helpers got wrong: SkZip converting between element
// types of different sizes, SkAbs32 of the most negative value, and an empty SkZip subspan at the
// end. Arguments beginning with "--" select a Release-check stop.

#include "differential_corpus.h"

#include "include/core/SkSpan.h"
#include "include/private/SkEnumBitMask.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTFitsIn.h"
#include "include/private/SkTLogic.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTo.h"
#include "src/core/SkSpanPriv.h"
#include "src/core/SkTLazy.h"
#include "src/core/SkZip.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {

using corpus::Output;
using corpus::append;
using corpus::append_bool;

static_assert(std::is_same_v<sknonstd::copy_const_t<int, const float>, const int>);
static_assert(std::is_same_v<sknonstd::copy_cv_t<int, const volatile float>, const volatile int>);
static_assert(std::is_same_v<sknonstd::same_const_t<const int, float>, int>);
static_assert(std::is_same_v<sknonstd::same_cv_t<const int, volatile float>, volatile int>);

template <typename... Ts>
void record_zip(Output& output, const SkZip<Ts...>& zip) {
    append(output, static_cast<uint64_t>(zip.size()));
    append_bool(output, zip.empty());
    for (const std::tuple<Ts&...> row : zip) {
        append(output, static_cast<int64_t>(std::get<0>(row)));
        append(output, static_cast<int64_t>(std::get<1>(row)));
    }
    if (!zip.empty()) {
        append(output, static_cast<int64_t>(std::get<0>(zip.front())));
        append(output, static_cast<int64_t>(std::get<1>(zip.back())));
    }
    ++output.cases;
}

void zip_cases(Output& output) {
    std::array<int32_t, 9> left{};
    std::array<int16_t, 9> right{};
    for (size_t index = 0; index < left.size(); ++index) {
        left[index] = static_cast<int32_t>(index * 11 + 1);
        right[index] = static_cast<int16_t>(-static_cast<int>(index) * 3);
    }
    const SkZip<int32_t, int16_t> zip(left.size(), left.data(), right.data());
    record_zip(output, zip);
    for (size_t n = 0; n <= zip.size(); ++n) {
        record_zip(output, zip.first(n));
        record_zip(output, zip.last(n));
    }
    for (size_t offset = 0; offset < zip.size(); ++offset) {
        for (size_t count = 0; count <= zip.size() - offset; ++count) {
            record_zip(output, zip.subspan(offset, count));
        }
    }
    const SkSpan<int16_t> second = zip.get<1>();
    append(output, static_cast<uint64_t>(second.size()));
    append(output, second[4]);
    // Rows are references into the sources.
    std::get<0>(zip[3]) = 777;
    append(output, left[3]);
    const SkZip<const int32_t, const int16_t> constant = zip;
    record_zip(output, constant);
    // SkMakeZip from an array, a span, a container and a pointer.
    int32_t raw[4] = {5, 6, 7, 8};
    std::vector<int16_t> vector{9, 10, 11, 12};
    const SkSpan<int32_t> span(left.data(), 4);
    record_zip(output, SkMakeZip(raw, vector));
    record_zip(output, SkMakeZip(span, vector.data()));
    record_zip(output, SkMakeZip(vector, raw));
    record_zip(output, SkZip<int32_t, int16_t>());
    ++output.cases;
}

struct Counted {
    static int live;
    static int copies;
    int value = 0;
    Counted() { ++live; }
    explicit Counted(int v) : value(v) { ++live; }
    Counted(const Counted& other) : value(other.value) { ++live; ++copies; }
    Counted(Counted&& other) noexcept : value(other.value) { ++live; other.value = -1; }
    Counted& operator=(const Counted& other) {
        value = other.value;
        ++copies;
        return *this;
    }
    Counted& operator=(Counted&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Counted() { --live; }
};
int Counted::live = 0;
int Counted::copies = 0;

void record_lazy(Output& output, const SkTLazy<Counted>& lazy) {
    append_bool(output, lazy.isValid());
    const Counted* maybe = lazy.getMaybeNull();
    append_bool(output, maybe != nullptr);
    if (maybe != nullptr) {
        append(output, static_cast<int32_t>((*maybe).value));
        append(output, static_cast<int32_t>((*lazy).value));
        append_bool(output, lazy.get() == maybe);
    }
    append(output, static_cast<int32_t>(Counted::live));
    append(output, static_cast<int32_t>(Counted::copies));
    ++output.cases;
}

void lazy_cases(Output& output) {
    {
        SkTLazy<Counted> lazy{};
        record_lazy(output, lazy);
        (*lazy.init(5)).value += 1;
        record_lazy(output, lazy);
        lazy.init(7);
        record_lazy(output, lazy);
        const Counted source(9);
        lazy.set(source);
        record_lazy(output, lazy);
        lazy.set(Counted(11));
        record_lazy(output, lazy);
        SkTLazy<Counted> copy(lazy);
        record_lazy(output, copy);
        SkTLazy<Counted> moved(std::move(copy));
        record_lazy(output, moved);
        record_lazy(output, copy);
        SkTLazy<Counted> assigned{};
        assigned = moved;
        record_lazy(output, assigned);
        assigned = std::move(moved);
        record_lazy(output, assigned);
        lazy.reset();
        record_lazy(output, lazy);
        const SkTLazy<Counted> fromNull(nullptr);
        record_lazy(output, fromNull);
        const SkTLazy<Counted> fromPointer(&source);
        record_lazy(output, fromPointer);
    }
    append(output, static_cast<int32_t>(Counted::live));
    // Copy on first write: reads share the initial object; the first write copies it once.
    {
        const Counted initial(21);
        SkTCopyOnFirstWrite<Counted> cow(initial);
        append_bool(output, cow.get() == &initial);
        append(output, static_cast<int32_t>((*cow).value));
        (*cow.writable()).value = 22;
        append_bool(output, cow.get() != &initial);
        append(output, static_cast<int32_t>(initial.value));
        append(output, static_cast<int32_t>((*cow).value));
        SkTCopyOnFirstWrite<Counted> copied(cow);
        append_bool(output, copied.get() != cow.get());
        append(output, static_cast<int32_t>((*copied).value));
        SkTCopyOnFirstWrite<Counted> shared(&initial);
        SkTCopyOnFirstWrite<Counted> sharedCopy(shared);
        append_bool(output, sharedCopy.get() == &initial);
        SkTCopyOnFirstWrite<Counted> empty{};
        append_bool(output, empty.get() == nullptr);
        empty.initIfNeeded(31);
        append(output, static_cast<int32_t>((*empty).value));
        empty.initIfNeeded(32);
        append(output, static_cast<int32_t>((*empty).value));
        SkTCopyOnFirstWrite<Counted> later{};
        later.init(initial);
        append_bool(output, later.get() == &initial);
        append(output, static_cast<int32_t>(Counted::copies));
    }
    append(output, static_cast<int32_t>(Counted::live));
    ++output.cases;
}

enum class Flags : uint8_t { kNone = 0, kA = 1, kB = 2, kC = 4, kHigh = 0x80 };
SK_MAKE_BITMASK_OPS(Flags)

void bitmask_cases(Output& output) {
    const std::array<Flags, 5> flags{Flags::kNone, Flags::kA, Flags::kB, Flags::kC, Flags::kHigh};
    for (const Flags a : flags) {
        for (const Flags b : flags) {
            const SkEnumBitMask<Flags> both = a | b;
            append(output, both.value());
            append(output, (a & b).value());
            append(output, (a ^ b).value());
            append(output, (~a).value());
            append_bool(output, static_cast<bool>(both & b));
            append_bool(output, both == (b | a));
            append_bool(output, both != SkEnumBitMask<Flags>(a));
            SkEnumBitMask<Flags> mask(a);
            mask |= b;
            mask &= ~SkEnumBitMask<Flags>(Flags::kC);
            mask ^= Flags::kHigh;
            append(output, mask.value());
            ++output.cases;
        }
    }
    append(output, SkEnumBitMask<Flags>().value());
}

void pin_cases(Output& output) {
    const std::array<int, 7> ints{std::numeric_limits<int>::min(), -5, 0, 3, 7, 8,
                                  std::numeric_limits<int>::max()};
    for (const int x : ints) {
        append(output, static_cast<int32_t>(SkTPin(x, 0, 7)));
        append(output, static_cast<int32_t>(SkTPin(x, -5, -5)));
    }
    const std::array<float, 7> floats{-corpus::kInf, -1.5f, 0.0f, 0.25f, 1.0f, 2.0f, corpus::kInf};
    for (const float x : floats) append(output, SkTPin(x, 0.0f, 1.0f));
    // NaN pins to the low bound.
    append(output, SkTPin(corpus::kNaN, 0.0f, 1.0f));
    append(output, SkTPin(corpus::kNaN, -2.0f, 3.0f));
    ++output.cases;
}

template <typename D, typename S>
void fits(Output& output, S value) {
    append_bool(output, SkTFitsIn<D>(value));
}

template <typename S>
void fits_all(Output& output, S value) {
    fits<int8_t>(output, value);
    fits<uint8_t>(output, value);
    fits<int16_t>(output, value);
    fits<uint16_t>(output, value);
    fits<int32_t>(output, value);
    fits<uint32_t>(output, value);
    fits<int64_t>(output, value);
    fits<uint64_t>(output, value);
    ++output.cases;
}

enum class Small : uint8_t { kZero = 0, kMax = 255 };
enum Wide : int64_t { kWideMin = std::numeric_limits<int64_t>::min(), kWideOne = 1 };

void fits_cases(Output& output) {
    const std::array<int64_t, 13> signedValues{std::numeric_limits<int64_t>::min(),
                                               -2147483649LL, -2147483648LL, -32769, -129, -1, 0,
                                               127, 255, 32767, 65535, 2147483647LL,
                                               std::numeric_limits<int64_t>::max()};
    for (const int64_t value : signedValues) {
        fits_all(output, value);
        if (SkTFitsIn<int32_t>(value)) fits_all(output, static_cast<int32_t>(value));
        if (SkTFitsIn<int16_t>(value)) fits_all(output, static_cast<int16_t>(value));
        if (SkTFitsIn<int8_t>(value)) fits_all(output, static_cast<int8_t>(value));
    }
    const std::array<uint64_t, 8> unsignedValues{0, 127, 128, 255, 256, 65536, 4294967295ULL,
                                                 std::numeric_limits<uint64_t>::max()};
    for (const uint64_t value : unsignedValues) {
        fits_all(output, value);
        if (SkTFitsIn<uint32_t>(value)) fits_all(output, static_cast<uint32_t>(value));
        if (SkTFitsIn<uint8_t>(value)) fits_all(output, static_cast<uint8_t>(value));
    }
    fits_all(output, Small::kMax);
    fits_all(output, kWideMin);
    fits_all(output, kWideOne);
    fits<Small>(output, 256);
    fits<Small>(output, 255);
    // SkTo and friends on values that fit.
    append(output, SkToS8(int64_t{-128}));
    append(output, SkToU8(255));
    append(output, SkToU16(65535u));
    append(output, SkToS32(int64_t{-5}));
    append(output, SkToU32(size_t{7}));
    append(output, static_cast<uint64_t>(SkToSizeT(42)));
    append_bool(output, SkToBool(3));
    append_bool(output, SkToBool(0));
    const std::array<int, 5> container{};
    append(output, static_cast<int32_t>(SkCount(container)));
    ++output.cases;
}

void saturating_cases(Output& output) {
    const std::array<int32_t, 9> values{std::numeric_limits<int32_t>::min(), -2147483647, -65536,
                                        -1, 0, 1, 65536, 2147483646,
                                        std::numeric_limits<int32_t>::max()};
    for (const int32_t a : values) {
        for (const int32_t b : values) {
            append(output, Sk32_sat_add(a, b));
            append(output, Sk32_sat_sub(a, b));
            append(output, Sk32_can_overflow_add(a, b));
            append(output, Sk32_can_overflow_sub(a, b));
        }
        if (a != std::numeric_limits<int32_t>::min()) append(output, SkAbs32(a));
        ++output.cases;
    }
    // A typed array: int64_t is long on Linux and long long on macOS, so a braced list mixing
    // numeric_limits with LL literals has no single element type there.
    const std::array<int64_t, 7> wide{std::numeric_limits<int64_t>::min(), -2147483649LL,
                                      -2147483648LL, 0, 2147483647LL, 2147483648LL,
                                      std::numeric_limits<int64_t>::max()};
    for (const int64_t x : wide) {
        append(output, Sk64_pin_to_s32(x));
    }
    ++output.cases;
}

void span_cases(Output& output) {
    std::array<int32_t, 6> a{1, 2, 3, 4, 5, 6};
    std::array<int32_t, 6> b{1, 2, 3, 4, 5, 7};
    const SkSpan<int32_t> sa(a.data(), a.size());
    const SkSpan<int32_t> sb(b.data(), b.size());
    append_bool(output, SkSpanPriv::EQ(sa, sa));
    append_bool(output, SkSpanPriv::EQ(sa, sb));
    append_bool(output, SkSpanPriv::EQ(sa.first(5), sb.first(5)));
    append_bool(output, SkSpanPriv::EQ(sa.first(0), sb.first(0)));
    append_bool(output, SkSpanPriv::EQ(sa.first(3), sb.first(4)));
    std::array<int32_t, 6> c{};
    SkSpanPriv::Copy(SkSpan<int32_t>(c.data(), 4), SkSpan<const int32_t>(b.data() + 2, 4));
    for (const int32_t value : c) append(output, value);
    SkSpanPriv::Copy(SkSpan<int32_t>(c.data(), 0), SkSpan<const int32_t>(b.data(), 0));
    for (const int32_t value : c) append(output, value);
    ++output.cases;
}

#ifndef STX_SKIA_REFERENCE
struct Base {
    int32_t value = 0;
};
struct Derived : Base {
    int32_t extra = 0;
};
// A zip over Derived must not become a zip over Base, which would index with Base's stride.
static_assert(!std::is_constructible_v<SkZip<const Base, int32_t>, const SkZip<Derived, int32_t>&>);
static_assert(std::is_constructible_v<SkZip<const int32_t, const int16_t>,
                                      const SkZip<int32_t, int16_t>&>);

bool corrected_helpers() {
    if (SkAbs32(std::numeric_limits<int32_t>::min()) != std::numeric_limits<int32_t>::max()) {
        return false;
    }
    std::array<int32_t, 3> left{1, 2, 3};
    std::array<int16_t, 3> right{4, 5, 6};
    const SkZip<int32_t, int16_t> zip(3, left.data(), right.data());
    return zip.subspan(3, 0).empty() && zip.last(0).empty();
}

// Each mode violates one Release-checked precondition and must stop the program.
int stop_mode(const char* mode) {
    std::array<int32_t, 3> left{1, 2, 3};
    std::array<int16_t, 3> right{4, 5, 6};
    const SkZip<int32_t, int16_t> zip(3, left.data(), right.data());
    if (std::strcmp(mode, "--zip-index") == 0) {
        std::printf("%d\n", std::get<0>(zip[3]));
    } else if (std::strcmp(mode, "--zip-subspan") == 0) {
        std::printf("%zu\n", zip.subspan(2, 2).size());
    } else if (std::strcmp(mode, "--zip-first") == 0) {
        std::printf("%zu\n", zip.first(4).size());
    } else if (std::strcmp(mode, "--zip-unequal") == 0) {
        std::vector<int16_t> longer{1, 2, 3, 4};
        std::printf("%zu\n", SkMakeZip(left, longer).size());
    } else if (std::strcmp(mode, "--lazy-empty") == 0) {
        SkTLazy<int> lazy{};
        std::printf("%d\n", *lazy);
    } else if (std::strcmp(mode, "--cow-null") == 0) {
        SkTCopyOnFirstWrite<int> cow{};
        *cow.writable() = 1;
    } else if (std::strcmp(mode, "--span-copy-unequal") == 0) {
        std::array<int32_t, 2> small{};
        SkSpanPriv::Copy(SkSpan<int32_t>(small.data(), 2), SkSpan<const int32_t>(left.data(), 3));
    } else {
        return 2;
    }
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strncmp(argv[1], "--", 2) == 0) return stop_mode(argv[1]);
    if (!corrected_helpers()) {
        std::puts("template core: a corrected helper misbehaved");
        return 1;
    }
#endif
    Output output{};
    zip_cases(output);
    lazy_cases(output);
    bitmask_cases(output);
    pin_cases(output);
    fits_cases(output);
    saturating_cases(output);
    span_cases(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("template core: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
