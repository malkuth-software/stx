// Differential corpus for the numeric helpers: SkFloatingPoint.h (saturating conversions, rounding,
// NaN and finiteness tests, IEEE division, midpoints), SkFixed.h (16.16 and 32.32 fixed point),
// SkMath.h (shifts, powers of two, alpha multiplies), sk_doubles_nearly_equal_ulps and
// sk_double_nearly_zero, SkFloatUtils' SkFloatingPoint, and SkFloatInterpFunc. Inputs stay where
// both builds are defined; out-of-range inputs to the unchecked conversions are left to the
// candidate's own checks. Both builds must produce identical bytes (numeric-core.raw).
//
// A second artifact (fixed-limits.raw) records every SkFixed limit where both builds are
// defined: SK_FixedMin itself, the float conversion past either end and at NaN, the double
// conversion at the ends of its range, SkFixedDiv past either end, and rounding, averaging,
// multiplying and magnitude at SK_FixedMin. The candidate's SK_FixedMin is INT32_MIN and its
// conversions saturate to exactly INT32_MIN or INT32_MAX; the reference's SK_FixedMin is
// -INT32_MAX and its float conversion stops 128 short of either limit, so the builds differ
// there and only there.
//
// The candidate alone checks those out-of-range inputs, where the intake helpers were undefined:
// an empty array to SkIsFinite, division by zero in SkFixedDiv, NaN and huge values to the
// double-to-int conversions and SkDoubleToFixed, fixed-point rounding and averaging near
// SK_FixedMax, and the limits above, including SkFDot6Div(-32768, -1), whose 32-bit division
// overflowed.

#include "differential_corpus.h"

#include "include/core/SkScalar.h"
#include "include/private/SkFixed.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMath.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTo.h"
#include "src/core/SkFDot6.h"
#include "src/utils/SkFloatUtils.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

std::vector<float> float_samples() {
    std::vector<float> values{0.0f, -0.0f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f, 0.49999997f,
                              1e-30f, -1e-30f, corpus::kDenormal, corpus::kFloatMin, 1234.5678f,
                              -98765.4f, 16777216.0f, 2147483520.0f, -2147483520.0f,
                              2147483648.0f, -2147483648.0f, 3e9f, -3e9f, 1e20f, -1e20f,
                              corpus::kFloatMax, -corpus::kFloatMax, corpus::kInf, -corpus::kInf,
                              corpus::kNaN};
    Sequence sequence{};
    for (int index = 0; index < 200; ++index) {
        const float unit = static_cast<float>(sequence.next()) / 4294967296.0f;
        const float scale = static_cast<float>(1 << sequence.pick(31));
        values.push_back((unit - 0.5f) * scale);
    }
    return values;
}

void float_cases(Output& output) {
    for (const float x : float_samples()) {
        append(output, static_cast<int32_t>(sk_float_saturate2int(x)));
        append(output, static_cast<int64_t>(sk_float_saturate2int64(x)));
        append(output, static_cast<int32_t>(sk_double_saturate2int(static_cast<double>(x))));
        append(output, static_cast<int32_t>(sk_float_floor2int(x)));
        append(output, static_cast<int32_t>(sk_float_round2int(x)));
        append(output, static_cast<int32_t>(sk_float_ceil2int(x)));
        append(output, sk_float_round(x));
        append_bool(output, SkIsNaN(x));
        append_bool(output, SkIsFinite(x));
        append_bool(output, SkIsFinite(x, 1.0f, -2.0f));
        append_bool(output, SkIsFinite(1.0f, x));
        append(output, static_cast<int32_t>(sk_float_sgn(x)));
        append(output, sk_float_degrees_to_radians(x));
        append(output, sk_float_radians_to_degrees(x));
        append(output, sk_float_midpoint(x, 3.0f));
        append(output, sk_ieee_float_divide(x, 0.0f));
        append(output, sk_ieee_float_divide(1.0f, x));
        append(output, sk_ieee_double_divide(static_cast<double>(x), 0.0));
        append(output, sk_double_to_float(static_cast<double>(x) * 1e30));
        if (x >= 0.0f) append(output, sk_float_rsqrt(x));
        // Only where the unsaturated conversions are defined.
        if (SkIsFinite(x) && std::fabs(x) < 2.0e9f) {
            append(output, static_cast<int32_t>(sk_float_floor2int_no_saturate(x)));
            append(output, static_cast<int32_t>(sk_float_round2int_no_saturate(x)));
            append(output, static_cast<int32_t>(sk_float_ceil2int_no_saturate(x)));
            const double d = static_cast<double>(x);
            append(output, static_cast<int32_t>(sk_double_floor2int(d)));
            append(output, static_cast<int32_t>(sk_double_round2int(d)));
            append(output, static_cast<int32_t>(sk_double_ceil2int(d)));
            append(output, sk_double_round(d));
        }
        ++output.cases;
    }
    const std::array<float, 5> finite{1.0f, -2.0f, 3.5f, 0.0f, 1e30f};
    const std::array<float, 3> withInf{1.0f, corpus::kInf, 2.0f};
    const std::array<float, 3> withNaN{corpus::kNaN, 1.0f, 2.0f};
    for (int count = 1; count <= 5; ++count) {
        append_bool(output, SkIsFinite(finite.data(), count));
    }
    append_bool(output, SkIsFinite(withInf.data(), 3));
    append_bool(output, SkIsFinite(withInf.data(), 1));
    append_bool(output, SkIsFinite(withNaN.data(), 3));
    append_bool(output, SkIsNaN(corpus::kNaN));
    append_bool(output, SkIsNaN(std::numeric_limits<double>::quiet_NaN()));
    ++output.cases;
}

void fixed_cases(Output& output) {
    const std::array<int32_t, 16> fixeds{0, 1, -1, SK_Fixed1, -SK_Fixed1, SK_FixedHalf,
                                         -SK_FixedHalf, SK_FixedHalf - 1, 3 * SK_Fixed1 + 7,
                                         -5 * SK_Fixed1 - 9, 0x12345678, -0x12345678,
                                         SK_FixedMax - SK_Fixed1, -SK_FixedMax + SK_Fixed1,
                                         SK_FixedPI, SK_FixedSqrt2};
    for (const int32_t x : fixeds) {
        append(output, SkFixedToFloat(x));
        append(output, SkFixedToDouble(x));
        append(output, static_cast<int32_t>(SkFixedRoundToInt(x)));
        append(output, static_cast<int32_t>(SkFixedCeilToInt(x)));
        append(output, static_cast<int32_t>(SkFixedFloorToInt(x)));
        append(output, SkFixedRoundToFixed(x));
        append(output, SkFixedCeilToFixed(x));
        append(output, SkFixedFloorToFixed(x));
        append(output, static_cast<int64_t>(SkFixedToFixed3232(x)));
        append(output, SkFixed3232ToFixed(SkFixedToFixed3232(x)));
        for (const int32_t y : fixeds) {
            append(output, SkFixedMul(x, y));
            append(output, static_cast<int32_t>(SkFixedAve(x / 2, y / 2)));
            if (y != 0) {
                // A negative quotient past the range is SK_FixedMin, which the profiles spell
                // differently; fixed-limits.raw records it, and here it is folded onto
                // -SK_FixedMax so that this artifact stays shared.
                const SkFixed quotient = SkFixedDiv(x, y);
                append(output, quotient == SK_FixedMin ? -SK_FixedMax : quotient);
            }
        }
        ++output.cases;
    }
    // The limits of SkFloatToFixed are in fixed-limits.raw; 32.32 has its own.
    for (const float f : {0.0f, 1.0f, -1.0f, 0.5f, 3.25f, -32767.5f, 32767.99f}) {
        append(output, static_cast<int32_t>(SkFloatToFixed(f)));
        append(output, static_cast<int32_t>(SkScalarToFixed(f)));
    }
    for (const float f : {0.0f, 1.0f, -1.0f, 0.5f, 3.25f, -32767.5f, 32767.99f, 1e9f, -1e9f,
                          corpus::kInf, corpus::kNaN}) {
        append(output, static_cast<int64_t>(SkFloatToFixed3232(f)));
    }
    for (const double d : {0.0, 1.0, -1.0, 0.25, 12345.678, -32768.0, 32767.0}) {
        append(output, static_cast<int32_t>(SkDoubleToFixed(d)));
    }
    for (const int n : {0, 1, -1, 7, -32768, 32767}) {
        append(output, static_cast<int32_t>(SkIntToFixed(n)));
        append(output, static_cast<int64_t>(SkIntToFixed3232(n)));
        append(output, static_cast<int32_t>(SkFixed3232ToInt(SkIntToFixed3232(n))));
    }
    append(output, SkFixed3232ToFloat(SkIntToFixed3232(3)));
    ++output.cases;
}

struct FixedQuotient {
    int64_t numerator;
    int64_t denominator;
};

void fixed_limit_cases(Output& output) {
    append(output, static_cast<int32_t>(SK_FixedMin));
    append(output, static_cast<int32_t>(SK_FixedMax));
    ++output.cases;

    // The last float below 2^15 scales to 2147483520, the largest in range; the next one down
    // past -2^15 is the first beyond SK_FixedMin.
    const float lastBelowTop = std::nextafter(32768.0f, 0.0f);
    const float firstPastBottom = std::nextafter(-32768.0f, -65536.0f);
    for (const float f : {lastBelowTop, 32768.0f, -32768.0f, firstPastBottom, 1e9f, -1e9f,
                          corpus::kInf, -corpus::kInf, corpus::kNaN}) {
        append(output, static_cast<int32_t>(SkFloatToFixed(f)));
        append(output, static_cast<int32_t>(SkScalarToFixed(f)));
        ++output.cases;
    }

    // Past either end, and at NaN, the reference's double conversion is undefined; the
    // candidate's own checks cover those.
    for (const double d : {32767.99999, -32768.0, 0.5 / SK_Fixed1, -0.5 / SK_Fixed1}) {
        append(output, static_cast<int32_t>(SkDoubleToFixed(d)));
        ++output.cases;
    }

    // Division by zero is undefined in the reference; the candidate's own checks cover it.
    const std::array<FixedQuotient, 6> quotients{{{32768, 1},
                                                  {-32768, 1},
                                                  {-32769, 1},
                                                  {-32768, -1},
                                                  {INT32_MAX, 1},
                                                  {INT32_MIN, 1}}};
    for (const FixedQuotient& quotient : quotients) {
        append(output, SkFixedDiv(quotient.numerator, quotient.denominator));
        ++output.cases;
    }

    // Held in a variable: the reference's SK_FixedMin is not exactly a float, and the constant
    // conversion warns.
    const SkFixed minimum = SK_FixedMin;
    append(output, SkFixedToFloat(minimum));
    append(output, SkFixedToDouble(minimum));
    append(output, static_cast<int32_t>(SkFixedRoundToInt(SK_FixedMin)));
    append(output, static_cast<int32_t>(SkFixedCeilToInt(SK_FixedMin)));
    append(output, static_cast<int32_t>(SkFixedFloorToInt(SK_FixedMin)));
    append(output, SkFixedRoundToFixed(SK_FixedMin));
    append(output, SkFixedCeilToFixed(SK_FixedMin));
    append(output, SkFixedFloorToFixed(SK_FixedMin));
    // SkFixedAve(SK_FixedMin, SK_FixedMin) overflowed in the reference; the candidate checks it.
    append(output, static_cast<int32_t>(SkFixedAve(SK_FixedMin, SK_FixedMax)));
    append(output, SkFixedMul(SK_FixedMin, SK_Fixed1));
    append(output, SkAbs32(SK_FixedMin));
    ++output.cases;
}

void math_cases(Output& output) {
    for (const int32_t value : {0, 1, -1, 0x40000000, 0x7FFFFFFF, -0x7FFFFFFF - 1}) {
        for (const int32_t shift : {0, 1, 15, 16, 31}) {
            append(output, SkLeftShift(value, shift));
            append(output, SkLeftShift(static_cast<int64_t>(value), shift));
            append(output, SkLeftShift(static_cast<int64_t>(value), shift + 32));
        }
    }
    for (const uint32_t value : {0u, 1u, 2u, 3u, 4u, 6u, 64u, 0x80000000u, 0xFFFFFFFFu}) {
        append_bool(output, SkIsPow2(value));
        append_bool(output, SkIsPow2(static_cast<int>(value & 0x7FFFFFFF)));
    }
    for (unsigned a = 0; a < 256; ++a) {
        for (unsigned b = 0; b < 256; b += 5) {
            append(output, static_cast<uint8_t>(SkMulDiv255Round(a, b)));
        }
    }
    for (int shift = 1; shift <= 8; ++shift) {
        for (const unsigned a : {0u, 1u, 255u, 1000u, 32767u}) {
            for (const unsigned b : {0u, 3u, 255u, 32767u}) {
                append(output, static_cast<uint32_t>(SkMul16ShiftRound(a, b, shift)));
            }
        }
    }
    append(output, sk_64_mul(123456789, -98765));
    ++output.cases;
}

void ulp_cases(Output& output) {
    std::vector<double> values{0.0, -0.0, 1.0, -1.0, 1.0 + 1e-16, 1.0 + 4e-15, 1.0 + 1e-12,
                               1e-310, -1e-310, 2.2250738585072014e-308, 1e300, -1e300,
                               std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN(), 1e-7, 1e-8, 3.0};
    for (const double a : values) {
        append_bool(output, sk_double_nearly_zero(a));
        for (const double b : values) {
            append_bool(output, sk_doubles_nearly_equal_ulps(a, b));
            append_bool(output, sk_doubles_nearly_equal_ulps(a, b, 0));
            append_bool(output, sk_doubles_nearly_equal_ulps(a, b, 255));
            const SkFloatingPoint<double, 4> da(a);
            const SkFloatingPoint<double, 4> db(b);
            append_bool(output, da.AlmostEquals(db));
            const SkFloatingPoint<float, 2> fa(static_cast<float>(a));
            const SkFloatingPoint<float, 2> fb(static_cast<float>(b));
            append_bool(output, fa.AlmostEquals(fb));
            append_bool(output, fa.is_nan());
            append(output, static_cast<uint32_t>(fa.exponent_bits()));
            append(output, static_cast<uint32_t>(fa.fraction_bits()));
        }
        ++output.cases;
    }
    // Neighbouring floats a few ULPs apart.
    float x = 1.0f;
    for (int step = 0; step < 6; ++step) {
        const SkFloatingPoint<float, 4> a(1.0f);
        const SkFloatingPoint<float, 4> b(x);
        append_bool(output, a.AlmostEquals(b));
        x = std::nextafter(x, 2.0f);
    }
    ++output.cases;
}

void interp_cases(Output& output) {
    const std::array<float, 5> keys{0.0f, 1.0f, 1.0f, 4.0f, 9.0f};
    const std::array<float, 5> values{10.0f, 20.0f, 30.0f, 40.0f, 0.0f};
    for (const float key : {-1.0f, 0.0f, 0.5f, 1.0f, 2.5f, 4.0f, 8.0f, 9.0f, 20.0f}) {
        for (int length = 1; length <= 5; ++length) {
            append(output, SkFloatInterpFunc(key, keys.data(), values.data(), length));
        }
    }
    ++output.cases;
}

#ifndef STX_SKIA_REFERENCE
// Every conversion into SkFixed saturates to the same two limits.
bool fixed_limits_agree() {
    if (SK_FixedMin != INT32_MIN || SK_FixedMax != INT32_MAX) return false;
    const float floatNaN = corpus::kNaN;
    if (SkFloatToFixed(32768.0f) != INT32_MAX || SkFloatToFixed(-32768.0f) != INT32_MIN) {
        return false;
    }
    if (SkFloatToFixed(corpus::kInf) != INT32_MAX || SkFloatToFixed(-corpus::kInf) != INT32_MIN) {
        return false;
    }
    if (SkFloatToFixed(floatNaN) != INT32_MAX) return false;
    if (SkFloatToFixed(std::nextafter(32768.0f, 0.0f)) != 2147483520) return false;
    if (SkDoubleToFixed(32768.0) != INT32_MAX || SkDoubleToFixed(-32768.00001) != INT32_MIN) {
        return false;
    }
    const double doubleInf = std::numeric_limits<double>::infinity();
    if (SkDoubleToFixed(doubleInf) != INT32_MAX || SkDoubleToFixed(-doubleInf) != INT32_MIN) {
        return false;
    }
    if (SkFixedDiv(0, 0) != 0 || SkFixedDiv(INT32_MIN, 0) != INT32_MIN) return false;
    if (SkFixedDiv(-32768, 1) != INT32_MIN || SkFixedDiv(-32769, 1) != INT32_MIN) return false;
    if (SkFixedDiv(32768, 1) != INT32_MAX || SkFixedDiv(-32768, -1) != INT32_MAX) return false;
    if (SkFDot6Div(-32768, -1) != INT32_MAX || SkFDot6Div(-32768, 1) != INT32_MIN) return false;
    if (SkFDot6Div(32767, -1) != -32767 * SK_Fixed1) return false;
    return SkAbs32(SK_FixedMin) == SK_FixedMax;
}

bool out_of_range_defined() {
    const float* none = nullptr;
    if (!SkIsFinite(none, 0)) return false;
    if (SkFixedDiv(5, 0) != INT32_MAX || SkFixedDiv(-5, 0) != INT32_MIN) return false;
    if (SkFixedDiv(0, 0) != 0) return false;
    const double huge = 1e300;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (sk_double_round2int(huge) != SK_MaxS32 || sk_double_floor2int(-huge) != SK_MinS32) {
        return false;
    }
    if (sk_double_ceil2int(nan) != SK_MaxS32) return false;
    if (SkDoubleToFixed(1e10) != INT32_MAX || SkDoubleToFixed(-1e10) != INT32_MIN) return false;
    if (SkDoubleToFixed(-32768.0) != INT32_MIN || SkDoubleToFixed(nan) != SK_MaxS32) return false;
    if (SkFixedRoundToInt(SK_FixedMax) != 32768 || SkFixedCeilToInt(SK_FixedMax) != 32768) {
        return false;
    }
    if (SkFixedRoundToFixed(SK_FixedMax) != 0x7FFF0000) return false;
    if (SkFixedCeilToFixed(SK_FixedMax - 3) != 0x7FFF0000) return false;
    if (SkFixedAve(SK_FixedMax, SK_FixedMax) != SK_FixedMax) return false;
    if (SkFixedAve(SK_FixedMin, SK_FixedMin) != SK_FixedMin) return false;
    return fixed_limits_agree();
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!out_of_range_defined()) {
        std::puts("numeric core: an out-of-range input gave the wrong result");
        return 1;
    }
#endif
    Output output{};
    float_cases(output);
    fixed_cases(output);
    math_cases(output);
    ulp_cases(output);
    interp_cases(output);
    Output limits{};
    fixed_limit_cases(limits);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], limits)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long limitsHash = corpus::fnv1a(limits.bytes);
    std::printf("numeric core: %zu cases, %zu bytes, hash=%llu; limits %zu cases, hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, limits.cases, limitsHash);
    return 0;
}
