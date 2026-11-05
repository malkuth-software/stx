// Differential corpus for 4x4 matrices and their vectors: construction, element access,
// equality, concatenation with 4x4 and 3x3 matrices, pre/post translation and scaling, rotation
// about arbitrary axes, perspective normalization, inversion, point and vector mapping,
// conversion to and from SkMatrix, and SkMatrixPriv::MapRect with and without perspective. The
// matrices include signed zeros, NaN, infinities, denormals and points behind the w = 0 plane.
// Both builds must produce identical bytes.

#include "differential_corpus.h"

#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkRect.h"
#include "src/core/SkMatrixPriv.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

constexpr std::array<float, 22> kSpecial{0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 3.0f, -7.25f,
                                         1e-3f, 1e3f, 1e20f, -1e20f, 1e-20f, 3e-39f,
                                         corpus::kDenormal, corpus::kFloatMin,
                                         corpus::kFloatMax, -corpus::kFloatMax,
                                         corpus::kInf, -corpus::kInf, corpus::kNaN, 0.1f};

// NaN payloads and signs depend on operand order, which a respelling may change; every NaN is
// recorded as one canonical value.
void append_float(Output& output, float value) {
    const float canonical = std::isnan(value) ? corpus::kNaN : value;
    append(output, canonical);
}

float pick_float(Sequence& sequence) {
    if (sequence.pick(4) == 0) return kSpecial[sequence.pick(kSpecial.size())];
    const int whole = static_cast<int>(sequence.next() % 2001u) - 1000;
    return static_cast<float>(whole) / 64.0f;
}

// A finite value, most often small, for matrices that should stay ordinary.
float pick_ordinary(Sequence& sequence) {
    const int whole = static_cast<int>(sequence.next() % 401u) - 200;
    return static_cast<float>(whole) / 32.0f;
}

void append_m44(Output& output, const SkM44& m) {
    std::array<float, 16> values{};
    m.getColMajor(values.data());
    for (const float value : values) append_float(output, value);
}

void append_v4(Output& output, const SkV4& v) {
    append_float(output, v.x);
    append_float(output, v.y);
    append_float(output, v.z);
    append_float(output, v.w);
}

void append_v3(Output& output, const SkV3& v) {
    append_float(output, v.x);
    append_float(output, v.y);
    append_float(output, v.z);
}

void append_matrix(Output& output, const SkMatrix& m) {
    for (int i = 0; i < 9; ++i) append_float(output, m[i]);
}

std::array<float, 16> column_major(const SkM44& m) {
    std::array<float, 16> values{};
    m.getColMajor(values.data());
    return values;
}

SkM44 from_column_major(const std::array<float, 16>& values) {
    const SkM44 m = SkM44::ColMajor(values.data());
    return m;
}

// Kinds of matrix the generator produces: general, affine (bottom row 0 0 0 1), affine with a
// scaled bottom-right entry, and 2D perspective as SkMatrix would produce.
enum class Shape { kGeneral, kAffine, kScaledW, kPerspective2D, kCount };

SkM44 make_matrix(Sequence& sequence, Shape shape, bool special) {
    std::array<float, 16> values{};
    for (float& value : values) {
        value = special ? pick_float(sequence) : pick_ordinary(sequence);
    }
    switch (shape) {
        case Shape::kGeneral:
            break;
        case Shape::kAffine:
            values[3] = 0;
            values[7] = 0;
            values[11] = 0;
            values[15] = 1;
            break;
        case Shape::kScaledW:
            values[3] = 0;
            values[7] = 0;
            values[11] = 0;
            break;
        case Shape::kPerspective2D:
            values[2] = 0;
            values[6] = 0;
            values[8] = 0;
            values[9] = 0;
            values[10] = 1;
            values[11] = 0;
            values[14] = 0;
            break;
        case Shape::kCount:
            break;
    }
    return from_column_major(values);
}

SkRect make_rect(Sequence& sequence, bool special) {
    const float a = special ? pick_float(sequence) : pick_ordinary(sequence);
    const float b = special ? pick_float(sequence) : pick_ordinary(sequence);
    const float c = special ? pick_float(sequence) : pick_ordinary(sequence);
    const float d = special ? pick_float(sequence) : pick_ordinary(sequence);
    const SkRect rect = SkRect::MakeLTRB(a, b, c, d);
    return rect;
}

void construction_corpus(Output& output) {
    append_m44(output, SkM44());
    append_m44(output, SkM44::Translate(1.5f, -2.0f));
    append_m44(output, SkM44::Translate(1.5f, -2.0f, 7.0f));
    append_m44(output, SkM44::Scale(3.0f, -0.25f));
    append_m44(output, SkM44::Scale(3.0f, -0.25f, 0.0f));
    output.cases += 5;

    Sequence sequence{};
    for (int i = 0; i < 64; ++i) {
        const SkMatrix m = SkMatrix::MakeAll(pick_float(sequence), pick_float(sequence),
                                             pick_float(sequence), pick_float(sequence),
                                             pick_float(sequence), pick_float(sequence),
                                             pick_float(sequence), pick_float(sequence),
                                             pick_float(sequence));
        const SkM44 m44(m);
        append_m44(output, m44);
        append_matrix(output, m44.asM33());
        ++output.cases;
    }

    // Element access and assignment, row by column.
    for (int i = 0; i < 16; ++i) {
        SkM44 m = make_matrix(sequence, Shape::kGeneral, true);
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) append_float(output, m.rc(r, c));
        }
        m.setRC(i / 4, i % 4, 42.5f);
        append_m44(output, m);
        SkM44 set = m;
        set.setIdentity();
        append_m44(output, set);
        set.setTranslate(m.rc(0, 0), m.rc(1, 1), m.rc(2, 2));
        append_m44(output, set);
        set.setScale(m.rc(0, 1), m.rc(1, 2));
        append_m44(output, set);
        set.setScale(m.rc(0, 1), m.rc(1, 2), m.rc(3, 0));
        append_m44(output, set);
        append_bool(output, m.isFinite());
        ++output.cases;
    }
}

void equality_corpus(Output& output) {
    Sequence sequence{};
    for (int i = 0; i < 128; ++i) {
        const SkM44 a = make_matrix(sequence, static_cast<Shape>(sequence.pick(4)), true);
        std::array<float, 16> values = column_major(a);
        const size_t index = sequence.pick(16);
        switch (sequence.pick(4)) {
            case 0:
                break;
            case 1:
                values[index] = -values[index];
                break;
            case 2:
                values[index] = corpus::kNaN;
                break;
            default:
                values[index] = std::nextafter(values[index], corpus::kInf);
                break;
        }
        const SkM44 b = from_column_major(values);
        const SkM44 copy = a;
        append_bool(output, a == b);
        append_bool(output, a != b);
        append_bool(output, a == copy);
        ++output.cases;
    }
    const SkM44 zero = SkM44::Scale(0.0f, 0.0f, 0.0f);
    const SkM44 negativeZero = SkM44::Scale(-0.0f, -0.0f, -0.0f);
    append_bool(output, zero == negativeZero);
    ++output.cases;
}

void concat_corpus(Output& output) {
    Sequence sequence{};
    for (int i = 0; i < 400; ++i) {
        const bool special = i % 4 == 3;
        const SkM44 a = make_matrix(sequence, static_cast<Shape>(sequence.pick(4)), special);
        const SkM44 b = make_matrix(sequence, static_cast<Shape>(sequence.pick(4)), special);
        append_m44(output, a * b);
        append_m44(output, SkM44(a, b));
        SkM44 set = a;
        set.setConcat(a, b);
        append_m44(output, set);
        SkM44 pre = a;
        pre.preConcat(b);
        append_m44(output, pre);
        SkM44 post = a;
        post.postConcat(b);
        append_m44(output, post);
        // Aliased operands.
        SkM44 square = a;
        square.setConcat(square, square);
        append_m44(output, square);

        const SkMatrix m3 = SkMatrix::MakeAll(pick_ordinary(sequence), pick_ordinary(sequence),
                                              pick_ordinary(sequence), pick_ordinary(sequence),
                                              pick_ordinary(sequence), pick_ordinary(sequence),
                                              special ? pick_float(sequence) : 0.0f,
                                              special ? pick_float(sequence) : 0.0f,
                                              special ? pick_float(sequence) : 1.0f);
        SkM44 pre3 = a;
        pre3.preConcat(m3);
        append_m44(output, pre3);

        const float x = special ? pick_float(sequence) : pick_ordinary(sequence);
        const float y = special ? pick_float(sequence) : pick_ordinary(sequence);
        const float z = special ? pick_float(sequence) : pick_ordinary(sequence);
        SkM44 preT = a;
        preT.preTranslate(x, y);
        append_m44(output, preT);
        preT.preTranslate(x, y, z);
        append_m44(output, preT);
        SkM44 postT = a;
        postT.postTranslate(x, y);
        append_m44(output, postT);
        postT.postTranslate(x, y, z);
        append_m44(output, postT);
        SkM44 preS = a;
        preS.preScale(x, y);
        append_m44(output, preS);
        preS.preScale(x, y, z);
        append_m44(output, preS);
        ++output.cases;
    }
}

constexpr std::array<float, 10> kAngles{0.0f, 0.5f, -1.25f, 1.5707964f, 3.1415927f, 6.2831855f,
                                       100.0f, corpus::kInf, corpus::kNaN, -0.0f};

// Records the normalizing forms (Rotate and setRotate), the unit-axis forms (setRotateUnit and
// setRotateUnitSinCos), or both, about one axis at every angle.
void append_rotations(Output& output, const SkV3& axis, bool normalizing, bool unitForms) {
    for (const float angle : kAngles) {
        SkM44 set = SkM44::Translate(5, 6, 7);
        if (normalizing) {
            append_m44(output, SkM44::Rotate(axis, angle));
            set.setRotate(axis, angle);
            append_m44(output, set);
        }
        if (unitForms) {
            set.setRotateUnit(axis, angle);
            append_m44(output, set);
            set.setRotateUnitSinCos(axis, std::sin(angle), std::cos(angle));
            append_m44(output, set);
        }
        ++output.cases;
    }
}

// Axes whose squared length is a normal float, is zero, or is not finite rotate as they did. The
// others now normalize through their largest component (m44-corrected.raw); the unit-axis forms
// do not normalize and are shared for every axis.
void rotation_corpus(Output& output, Output& corrected) {
    const std::array<SkV3, 11> retained{SkV3{1, 0, 0}, SkV3{0, -1, 0}, SkV3{0, 0, 1},
                                        SkV3{1, 1, 1}, SkV3{3, -4, 12}, SkV3{0, 0, 0},
                                        SkV3{corpus::kInf, 0, 0}, SkV3{corpus::kNaN, 1, 0},
                                        SkV3{-0.0f, -0.0f, -1.0f}, SkV3{0.6f, 0.8f, 0},
                                        SkV3{-5, 0.25f, 9}};
    const std::array<SkV3, 7> renormalized{SkV3{1e-20f, 0, 0}, SkV3{1e-23f, 2e-23f, 0},
                                           SkV3{1e20f, 1e20f, 0}, SkV3{3e38f, 0, 0},
                                           SkV3{corpus::kDenormal, 0, 0},
                                           SkV3{1e-30f, 1e-30f, 1e-30f},
                                           SkV3{2e19f, -3e19f, 6e19f}};
    for (const SkV3& axis : retained) append_rotations(output, axis, true, true);
    for (const SkV3& axis : renormalized) {
        append_rotations(output, axis, false, true);
        append_rotations(corrected, axis, true, false);
    }
}

// The bottom-right entries for which scaling in float and in double agree on every ordinary
// matrix: 1 and zero leave the matrix alone, and powers of two scale exactly.
void normalize_corpus(Output& output, Output& corrected) {
    const std::array<float, 6> exact{1.0f, 0.0f, -0.0f, 2.0f, -4.0f, 0.25f};
    const std::array<float, 13> rescaled{-3.0f, 0.1f, 7.0f, 1e-20f, 3e-39f, corpus::kDenormal,
                                         1e30f, corpus::kInf, -corpus::kInf, corpus::kNaN,
                                         1.0000001f, 0.33333334f, 2.0f};
    Sequence sequence{};
    for (const float bottom : exact) {
        for (int i = 0; i < 24; ++i) {
            std::array<float, 16> values = column_major(make_matrix(sequence, Shape::kScaledW,
                                                                    false));
            values[15] = bottom;
            if (i % 6 == 5) values[7] = 0.5f;  // Genuine perspective is left alone.
            SkM44 m = from_column_major(values);
            m.normalizePerspective();
            append_m44(output, m);
            ++output.cases;
        }
    }
    for (size_t b = 0; b < rescaled.size(); ++b) {
        for (int i = 0; i < 24; ++i) {
            // The last entry repeats a power of two with non-finite and extreme entries.
            const bool special = b + 1 == rescaled.size() || i % 3 == 2;
            std::array<float, 16> values = column_major(make_matrix(sequence, Shape::kScaledW,
                                                                    special));
            values[15] = rescaled[b];
            SkM44 m = from_column_major(values);
            m.normalizePerspective();
            append_m44(corrected, m);
            ++corrected.cases;
        }
    }
}

// Matrices with a nonzero entry below 2^-10 can have a double determinant that rounds to zero
// in float, which the reference refused and the candidate does not; they go to the corrected
// output.
bool has_small_entry(const SkM44& m) {
    for (const float value : column_major(m)) {
        if (value != 0 && std::isfinite(value) && std::fabs(value) < 0x1p-10f) return true;
    }
    return false;
}

void append_inversion(Output& output, const SkM44& m) {
    SkM44 inverse = SkM44::Translate(9, 9, 9);
    const bool invertible = m.invert(&inverse);
    append_bool(output, invertible);
    append_m44(output, inverse);
    SkM44 self = m;
    const bool selfInvertible = self.invert(&self);
    append_bool(output, selfInvertible);
    append_m44(output, self);
    ++output.cases;
}

void inversion_corpus(Output& output, Output& corrected) {
    Sequence sequence{};
    for (int i = 0; i < 400; ++i) {
        const bool special = i % 5 == 4;
        const SkM44 m = make_matrix(sequence, static_cast<Shape>(sequence.pick(4)), special);
        append_inversion(has_small_entry(m) ? corrected : output, m);
    }
    // Singular, nearly singular and tiny-scale matrices.
    const std::array<SkM44, 6> special{SkM44::Scale(0, 1, 1), SkM44::Scale(1e-12f, 1e-12f),
                                       SkM44::Scale(1e-20f, 1e-20f), SkM44::Scale(1e20f, 1e20f),
                                       SkM44(1, 2, 3, 4, 2, 4, 6, 8, 0, 0, 1, 0, 0, 0, 0, 1),
                                       SkM44::Scale(corpus::kFloatMin, 1)};
    for (const SkM44& m : special) {
        Output& destination = has_small_entry(m) ? corrected : output;
        SkM44 inverse{};
        append_bool(destination, m.invert(&inverse));
        append_m44(destination, inverse);
        ++destination.cases;
    }
}

void map_corpus(Output& output) {
    Sequence sequence{};
    for (int i = 0; i < 300; ++i) {
        const bool special = i % 4 == 3;
        const SkM44 m = make_matrix(sequence, static_cast<Shape>(sequence.pick(4)), special);
        const float x = special ? pick_float(sequence) : pick_ordinary(sequence);
        const float y = special ? pick_float(sequence) : pick_ordinary(sequence);
        const float z = special ? pick_float(sequence) : pick_ordinary(sequence);
        const float w = special ? pick_float(sequence) : pick_ordinary(sequence);
        append_v4(output, m.map(x, y, z, w));
        append_v4(output, m * SkV4{x, y, z, w});
        append_v3(output, m * SkV3{x, y, z});
        ++output.cases;
    }
}

void map_rect_corpus(Output& output) {
    Sequence sequence{};
    for (int i = 0; i < 1200; ++i) {
        const bool special = i % 6 == 5;
        const Shape shape = static_cast<Shape>(sequence.pick(4));
        SkM44 m = make_matrix(sequence, shape, special);
        if (shape == Shape::kPerspective2D && i % 2 == 0) {
            // Small perspective terms put some corners behind the w = 0 plane.
            m.setRC(3, 0, pick_ordinary(sequence) / 16.0f);
            m.setRC(3, 1, pick_ordinary(sequence) / 16.0f);
            m.setRC(3, 3, pick_ordinary(sequence) / 4.0f);
        }
        const SkRect src = make_rect(sequence, special);
        const SkRect mapped = SkMatrixPriv::MapRect(m, src);
        append_float(output, mapped.fLeft);
        append_float(output, mapped.fTop);
        append_float(output, mapped.fRight);
        append_float(output, mapped.fBottom);
        ++output.cases;
    }
}

void vector_corpus(Output& output) {
    Sequence sequence{};
    for (int i = 0; i < 200; ++i) {
        const bool special = i % 4 == 3;
        const SkV3 a{special ? pick_float(sequence) : pick_ordinary(sequence),
                     pick_ordinary(sequence), pick_ordinary(sequence)};
        const SkV3 b{pick_ordinary(sequence), special ? pick_float(sequence) : 0.5f,
                     pick_ordinary(sequence)};
        const float s = special ? pick_float(sequence) : pick_ordinary(sequence);
        append_bool(output, a == b);
        append_bool(output, a != b);
        append_float(output, SkV3::Dot(a, b));
        append_v3(output, SkV3::Cross(a, b));
        append_v3(output, SkV3::Normalize(a));
        append_v3(output, -a);
        append_v3(output, a + b);
        append_v3(output, a - b);
        append_v3(output, a * b);
        append_v3(output, a * s);
        append_v3(output, s * a);
        SkV3 c = a;
        c += b;
        c -= SkV3{s, s, s};
        c *= b;
        c *= s;
        append_v3(output, c);
        append_float(output, a.lengthSquared());
        append_float(output, a.length());
        append_float(output, a.dot(b));
        append_v3(output, a.cross(b));
        append_v3(output, a.normalize());
        append_float(output, a.ptr()[2]);

        const SkV4 p{a.x, a.y, a.z, s};
        const SkV4 q{b.x, b.y, b.z, pick_ordinary(sequence)};
        append_bool(output, p == q);
        append_bool(output, p != q);
        append_float(output, SkV4::Dot(p, q));
        append_v4(output, SkV4::Normalize(p));
        append_v4(output, -p);
        append_v4(output, p + q);
        append_v4(output, p - q);
        append_v4(output, p * q);
        append_v4(output, p * s);
        append_v4(output, s * p);
        append_float(output, p.lengthSquared());
        append_float(output, p.length());
        append_float(output, p.dot(q));
        append_v4(output, p.normalize());
        SkV4 r = p;
        for (int k = 0; k < 4; ++k) {
            append_float(output, p[k]);
            r[k] = q[k] + 1.0f;
        }
        append_v4(output, r);
        ++output.cases;
    }
}

#ifndef STX_SKIA_REFERENCE
bool same_bits(const SkM44& a, const SkM44& b) {
    const std::array<float, 16> x = column_major(a);
    const std::array<float, 16> y = column_major(b);
    for (size_t i = 0; i < x.size(); ++i) {
        uint32_t bx = 0;
        uint32_t by = 0;
        std::memcpy(&bx, &x[i], sizeof(bx));
        std::memcpy(&by, &y[i], sizeof(by));
        if (bx != by) return false;
    }
    return true;
}

// Equality is elementwise IEEE comparison, so it does not depend on object identity.
bool nan_never_equals() {
    std::array<float, 16> values = column_major(SkM44::Translate(1, 2));
    values[13] = corpus::kNaN;
    const SkM44 m = from_column_major(values);
    const SkM44 copy = m;
    const SkM44& same = m;
    const bool consistent = !(m == same) && (m != same) && !(m == copy);
    const SkM44 ordinary = SkM44::Scale(2, 3);
    return consistent && ordinary == ordinary;
}

// A 2D matrix with a scaled bottom-right entry scales its entries exactly as SkMatrix does.
bool normalization_matches_3x3() {
    Sequence sequence{};
    const std::array<float, 8> bottoms{-3.0f, 0.1f, 7.0f, 1e-20f, 1.0000001f, 0.33333334f,
                                       -0.7f, 1e5f};
    for (const float bottom : bottoms) {
        for (int i = 0; i < 64; ++i) {
            SkMatrix m3 = SkMatrix::MakeAll(pick_ordinary(sequence), pick_ordinary(sequence),
                                            pick_ordinary(sequence), pick_ordinary(sequence),
                                            pick_ordinary(sequence), pick_ordinary(sequence),
                                            0, 0, bottom);
            SkM44 m44(m3);
            m3.normalizePerspective();
            m44.normalizePerspective();
            const SkMatrix converted = m44.asM33();
            // SkM44 scales its bottom row too, so a negative X leaves -0 there where SkMatrix
            // keeps +0; the bottom rows agree by value.
            for (int k = 6; k < 9; ++k) {
                if (converted[k] != m3[k]) return false;
            }
            for (int k = 0; k < 6; ++k) {
                uint32_t a = 0;
                uint32_t b = 0;
                const float ca = converted[k];
                const float cb = m3[k];
                std::memcpy(&a, &ca, sizeof(a));
                std::memcpy(&b, &cb, sizeof(b));
                if (a != b) return false;
            }
        }
    }
    return true;
}

// A tiny bottom-right entry no longer turns zero entries into NaN; a non-finite one, or a
// scaled entry that overflows, leaves the matrix as it was.
bool normalization_stays_finite() {
    SkM44 tiny = SkM44::Scale(0.5f, 0.25f);
    tiny.setRC(3, 3, 3e-39f);
    tiny.normalizePerspective();
    const bool tinyScaled = tiny.isFinite() && tiny.rc(3, 3) == 1 && tiny.rc(0, 1) == 0 &&
                            tiny.rc(0, 3) == 0 && tiny.rc(0, 0) > 1e38f;
    SkM44 denormal = SkM44::Scale(1e-10f, 1e-10f, 1e-10f);
    denormal.setRC(3, 3, corpus::kDenormal);
    denormal.normalizePerspective();
    const bool denormalScaled = denormal.isFinite() && denormal.rc(3, 3) == 1 &&
                                denormal.rc(1, 0) == 0;
    bool unchanged = true;
    const std::array<float, 4> bottoms{1e-10f, corpus::kInf, -corpus::kInf, corpus::kNaN};
    for (const float bottom : bottoms) {
        SkM44 m = SkM44::Translate(1e30f, -2);
        m.setRC(3, 3, bottom);
        const SkM44 before = m;
        m.normalizePerspective();
        unchanged = unchanged && same_bits(m, before);
    }
    return tinyScaled && denormalScaled && unchanged;
}

// Finite, nonzero axes of any length rotate about their direction.
bool rotation_axes_normalize() {
    struct Pair {
        SkV3 axis{};
        SkV3 direction{};
    };
    const std::array<Pair, 6> pairs{Pair{SkV3{1e20f, 1e20f, 0}, SkV3{1, 1, 0}},
                                    Pair{SkV3{3e38f, 0, 0}, SkV3{1, 0, 0}},
                                    Pair{SkV3{1e-20f, 0, 0}, SkV3{1, 0, 0}},
                                    Pair{SkV3{0x1p-80f, 0x1p-79f, 0}, SkV3{0.5f, 1, 0}},
                                    Pair{SkV3{0, -corpus::kDenormal, 0}, SkV3{0, -1, 0}},
                                    Pair{SkV3{-0x1p100f, 0x1p101f, 0x1p102f},
                                         SkV3{-0.25f, 0.5f, 1}}};
    for (const Pair& pair : pairs) {
        for (const float angle : kAngles) {
            if (!same_bits(SkM44::Rotate(pair.axis, angle),
                           SkM44::Rotate(pair.direction, angle))) {
                return false;
            }
        }
    }
    const bool zero = same_bits(SkM44::Rotate(SkV3{0, 0, 0}, 1), SkM44());
    const bool infinite = same_bits(SkM44::Rotate(SkV3{corpus::kInf, 1, 0}, 1), SkM44());
    return zero && infinite;
}

// A null inverse asks only whether the matrix is invertible, and gets the same answer.
bool invertibility_without_storage() {
    Sequence sequence{};
    for (int i = 0; i < 256; ++i) {
        const SkM44 m = make_matrix(sequence, static_cast<Shape>(sequence.pick(4)), i % 4 == 3);
        SkM44 inverse{};
        if (m.invert(nullptr) != m.invert(&inverse)) return false;
    }
    const std::array<SkM44, 4> edges{SkM44::Scale(0, 1), SkM44::Scale(1e-20f, 1e-20f),
                                     SkM44::Scale(corpus::kFloatMin, corpus::kFloatMin),
                                     SkM44::Scale(1e30f, 1e30f, 1e30f)};
    for (const SkM44& m : edges) {
        SkM44 inverse{};
        if (m.invert(nullptr) != m.invert(&inverse)) return false;
    }
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!nan_never_equals()) { std::puts("m44: NaN equality failed"); return 1; }
    if (!normalization_matches_3x3()) { std::puts("m44: 3x3 agreement failed"); return 1; }
    if (!normalization_stays_finite()) { std::puts("m44: finite normalization failed"); return 1; }
    if (!rotation_axes_normalize()) { std::puts("m44: axis normalization failed"); return 1; }
    if (!invertibility_without_storage()) { std::puts("m44: null inverse failed"); return 1; }
#endif
    Output output{};
    Output corrected{};
    construction_corpus(output);
    equality_corpus(output);
    concat_corpus(output);
    rotation_corpus(output, corrected);
    normalize_corpus(output, corrected);
    inversion_corpus(output, corrected);
    map_corpus(output);
    map_rect_corpus(output);
    vector_corpus(output);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], corrected)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long correctedHash = corpus::fnv1a(corrected.bytes);
    std::printf("m44: %zu cases, %zu bytes, hash=%llu; corrected %zu cases, hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, corrected.cases, correctedHash);
    return 0;
}
