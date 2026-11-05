// Differential corpus for curve geometry: quadratic, cubic and conic evaluation, chopping,
// extrema, inflections, curvature, cusps, monotonic chops, conic-to-quad conversion, weight
// transformation and unit arcs, the double-precision Bezier, quadratic and cubic solvers,
// the cubic clipper and the cubic ease map.

#include "differential_corpus.h"

#include "include/core/SkCubicMap.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSpan.h"
#include "src/core/SkBezierCurves.h"
#include "src/core/SkCubicClipper.h"
#include "src/core/SkCubics.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkQuads.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

const std::array<float, 24> kSpecials{
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -3.0f, 1e-7f, 1e-30f, corpus::kDenormal,
    corpus::kFloatMin, 1e20f, -1e20f, 3e38f, corpus::kFloatMax, -corpus::kFloatMax,
    corpus::kInf, -corpus::kInf, corpus::kNaN, 16777217.0f, 2147483648.0f, -2147483904.0f,
    0.001f, 1e-4f,
};

// Positive conic weights; non-finite ones are forced to 1 by SkConic::setW.
const std::array<float, 13> kWeights{
    1e-6f, 0.1f, 0.5f, 0.70710678f, 1.0f, 1.5f, 2.0f, 10.0f, 1e6f, 1e20f,
    corpus::kInf, corpus::kNaN, 0.99999994f,
};

const std::array<float, 7> kTolerances{
    0.25f, 0.01f, 0.0f, -1.0f, corpus::kNaN, 1e-30f, 4.0f,
};

float pick_special(Sequence& sequence) {
    const float value = kSpecials[sequence.pick(kSpecials.size())];
    return value;
}

// Mostly ordinary values in [-64, 64] at eighth steps, occasionally a special value.
float pick_coordinate(Sequence& sequence, bool allowSpecial) {
    if (allowSpecial && sequence.next() % 12 == 0) {
        const float special = pick_special(sequence);
        return special;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 129) - 64;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

// A parameter in [0, 1]: a 64th step, or one of the values next to the ends.
float pick_unit_t(Sequence& sequence) {
    const uint32_t choice = sequence.next() % 72;
    float t = 0;
    if (choice <= 64) {
        t = static_cast<float>(choice) / 64.0f;
    } else if (choice == 65) {
        t = 1e-7f;
    } else if (choice == 66) {
        t = 0.99999994f;
    } else if (choice == 67) {
        t = corpus::kDenormal;
    } else if (choice == 68) {
        t = 0.33333334f;
    } else if (choice == 69) {
        t = 0.6666667f;
    } else if (choice == 70) {
        t = 1e-30f;
    } else {
        t = 0.5000001f;
    }
    return t;
}

// A parameter strictly inside (0, 1).
float pick_open_t(Sequence& sequence) {
    const uint32_t step = 1 + sequence.next() % 63;
    const float t = static_cast<float>(step) / 64.0f;
    return t;
}

// Fills count points. Some points repeat the previous one so degenerate control polygons and
// coincident end points occur regularly.
void fill_points(Sequence& sequence, SkPoint* points, int count, bool allowSpecial) {
    for (int i = 0; i < count; ++i) {
        if (i > 0 && sequence.next() % 7 == 0) {
            points[i] = points[i - 1];
        } else {
            points[i] = SkPoint::Make(pick_coordinate(sequence, allowSpecial),
                                      pick_coordinate(sequence, allowSpecial));
        }
    }
}

void append_point(Output& output, const SkPoint& point) {
    append(output, point.fX);
    append(output, point.fY);
}

void append_points(Output& output, const SkPoint* points, int count) {
    for (int i = 0; i < count; ++i) {
        append_point(output, points[i]);
    }
}

void append_scalars(Output& output, const float* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        append(output, values[i]);
    }
}

void append_rect(Output& output, const SkRect& rect) {
    append(output, rect.fLeft);
    append(output, rect.fTop);
    append(output, rect.fRight);
    append(output, rect.fBottom);
}

void append_conic(Output& output, const SkConic& conic) {
    append_points(output, conic.fPts, 3);
    append(output, conic.fW);
}

const std::array<float, 6> kEvalTs{0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.1f};

void quad_root_corpus(Output& output, Sequence& sequence) {
    const std::array<float, 8> coefficients{
        0.0f, 1.0f, -1.0f, 2.0f, 1e-30f, 1e30f, corpus::kInf, corpus::kNaN,
    };
    for (const float a : coefficients) {
        for (const float b : coefficients) {
            for (const float c : coefficients) {
                float roots[2] = {-7, -7};
                const int count = SkFindUnitQuadRoots(a, b, c, roots);
                append(output, count);
                append_scalars(output, roots, 2);
                ++output.cases;
            }
        }
    }
    for (int i = 0; i < 3000; ++i) {
        const float a = pick_coordinate(sequence, true);
        const float b = pick_coordinate(sequence, true);
        const float c = pick_coordinate(sequence, true);
        float roots[2] = {-7, -7};
        const int count = SkFindUnitQuadRoots(a, b, c, roots);
        append(output, count);
        append_scalars(output, roots, 2);
        ++output.cases;
    }
}

void quad_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 2500; ++i) {
        SkPoint quad[3] = {};
        fill_points(sequence, quad, 3, i % 5 == 0);
        for (const float t : kEvalTs) {
            append_point(output, SkEvalQuadAt(quad, t));
            append_point(output, SkEvalQuadTangentAt(quad, t));
            SkPoint point{-7, -7};
            SkVector tangent{-7, -7};
            SkEvalQuadAt(quad, t, &point, &tangent);
            append_point(output, point);
            append_point(output, tangent);
            SkEvalQuadAt(quad, t, &point);
            append_point(output, point);
        }
        SkPoint chopped[5] = {};
        SkChopQuadAt(quad, chopped, pick_open_t(sequence));
        append_points(output, chopped, 5);
        SkChopQuadAtHalf(quad, chopped);
        append_points(output, chopped, 5);

        float extremum = -7;
        append(output, SkFindQuadExtrema(quad[0].fX, quad[1].fX, quad[2].fX, &extremum));
        append(output, extremum);
        extremum = -7;
        append(output, SkFindQuadExtrema(quad[0].fY, quad[1].fY, quad[2].fY, &extremum));
        append(output, extremum);

        SkPoint monotonic[5] = {};
        const int yChops = SkChopQuadAtYExtrema(quad, monotonic);
        append(output, yChops);
        append_points(output, monotonic, 3 + 2 * yChops);
        const int xChops = SkChopQuadAtXExtrema(quad, monotonic);
        append(output, xChops);
        append_points(output, monotonic, 3 + 2 * xChops);
        append(output, SkFindQuadMaxCurvature(quad));
        ++output.cases;
    }
}

// Cubics whose control polygon crosses itself; the first has a cusp at t = 1/2.
void make_cusp_cubic(Sequence& sequence, SkPoint cubic[4]) {
    const float scale = static_cast<float>(1 + sequence.next() % 64) * 0.5f;
    const float dx = pick_coordinate(sequence, false);
    const float dy = pick_coordinate(sequence, false);
    const float bend = static_cast<float>(sequence.next() % 5) * 0.0625f;
    cubic[0] = SkPoint::Make(dx, dy);
    cubic[1] = SkPoint::Make(dx + scale, dy + scale);
    cubic[2] = SkPoint::Make(dx + bend * scale, dy + scale);
    cubic[3] = SkPoint::Make(dx + scale, dy);
}

void append_cubic_chops(Output& output, Sequence& sequence, const SkPoint cubic[4]) {
    SkPoint seven[7] = {};
    SkChopCubicAt(cubic, seven, pick_unit_t(sequence));
    append_points(output, seven, 7);
    SkChopCubicAt(cubic, seven, 1.0f);
    append_points(output, seven, 7);
    SkChopCubicAt(cubic, seven, 0.0f);
    append_points(output, seven, 7);
    SkChopCubicAtHalf(cubic, seven);
    append_points(output, seven, 7);

    float t0 = pick_unit_t(sequence);
    float t1 = pick_unit_t(sequence);
    if (t1 < t0) {
        const float swapped = t0;
        t0 = t1;
        t1 = swapped;
    }
    SkPoint ten[10] = {};
    SkChopCubicAt(cubic, ten, t0, t1);
    append_points(output, ten, 10);
    SkChopCubicAt(cubic, ten, t0, 1.0f);
    append_points(output, ten, 10);

    // Sorted parameter lists of 0 to 4 values. Lists of four stay below 1 (see the
    // candidate-only aliasing check).
    const int tCount = static_cast<int>(sequence.next() % 5);
    float tValues[4] = {0, 0, 0, 0};
    for (int i = 0; i < tCount; ++i) {
        float t = 0;
        if (tCount == 4) {
            t = static_cast<float>(sequence.next() % 61) / 64.0f;
        } else {
            t = pick_unit_t(sequence);
        }
        int slot = i;
        while (slot > 0 && tValues[slot - 1] > t) {
            tValues[slot] = tValues[slot - 1];
            --slot;
        }
        tValues[slot] = t;
    }
    SkPoint many[16] = {};
    SkChopCubicAt(cubic, many, tValues, tCount);
    append(output, tCount);
    append_points(output, many, 4 + 3 * tCount);
    SkChopCubicAt(cubic, nullptr, tValues, tCount);
}

void cubic_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 2500; ++i) {
        SkPoint cubic[4] = {};
        if (i % 9 == 0) {
            make_cusp_cubic(sequence, cubic);
        } else {
            fill_points(sequence, cubic, 4, i % 5 == 0);
        }
        for (const float t : kEvalTs) {
            SkPoint location{-7, -7};
            SkVector tangent{-7, -7};
            SkVector curvature{-7, -7};
            SkEvalCubicAt(cubic, t, &location, &tangent, &curvature);
            append_point(output, location);
            append_point(output, tangent);
            append_point(output, curvature);
            SkEvalCubicAt(cubic, t, nullptr, &tangent, nullptr);
            append_point(output, tangent);
        }

        float extrema[2] = {-7, -7};
        append(output, SkFindCubicExtrema(cubic[0].fX, cubic[1].fX, cubic[2].fX, cubic[3].fX,
                                          extrema));
        append_scalars(output, extrema, 2);
        extrema[0] = -7;
        extrema[1] = -7;
        append(output, SkFindCubicExtrema(cubic[0].fY, cubic[1].fY, cubic[2].fY, cubic[3].fY,
                                          extrema));
        append_scalars(output, extrema, 2);

        append_cubic_chops(output, sequence, cubic);

        SkPoint monotonic[10] = {};
        const int yChops = SkChopCubicAtYExtrema(cubic, monotonic);
        append(output, yChops);
        append_points(output, monotonic, 4 + 3 * yChops);
        append(output, SkChopCubicAtYExtrema(cubic, nullptr));
        const int xChops = SkChopCubicAtXExtrema(cubic, monotonic);
        append(output, xChops);
        append_points(output, monotonic, 4 + 3 * xChops);
        append(output, SkChopCubicAtXExtrema(cubic, nullptr));

        float inflections[2] = {-7, -7};
        append(output, SkFindCubicInflections(cubic, inflections));
        append_scalars(output, inflections, 2);

        float curvature[3] = {-7, -7, -7};
        const int curvatureRoots = SkFindCubicMaxCurvature(cubic, curvature);
        append(output, curvatureRoots);
        append_scalars(output, curvature, static_cast<size_t>(curvatureRoots));

        SkPoint curvatureChops[13] = {};
        float curvatureTs[3] = {-7, -7, -7};
        const int pieces = SkChopCubicAtMaxCurvature(cubic, curvatureChops, curvatureTs);
        append(output, pieces);
        append_points(output, curvatureChops, 1 + 3 * pieces);
        append_scalars(output, curvatureTs, static_cast<size_t>(pieces - 1));
        append(output, SkChopCubicAtMaxCurvature(cubic, curvatureChops));
        append_points(output, curvatureChops, 1 + 3 * pieces);
        append(output, SkChopCubicAtMaxCurvature(cubic, nullptr));

        append(output, SkFindCubicCusp(cubic));

        const std::array<float, 4> intercepts{
            (cubic[0].fY + cubic[3].fY) * 0.5f, cubic[3].fY, pick_coordinate(sequence, true),
            (cubic[0].fX + cubic[3].fX) * 0.5f,
        };
        for (const float intercept : intercepts) {
            SkPoint mono[7] = {};
            append_bool(output, SkChopMonoCubicAtY(cubic, intercept, mono));
            append_points(output, mono, 7);
            SkPoint monoX[7] = {};
            append_bool(output, SkChopMonoCubicAtX(cubic, intercept, monoX));
            append_points(output, monoX, 7);
        }
        ++output.cases;
    }
}

void conic_corpus(Output& output, Sequence& sequence) {
    SkPoint quads[1 + 2 * 32] = {};
    for (int i = 0; i < 2500; ++i) {
        SkPoint points[3] = {};
        fill_points(sequence, points, 3, i % 5 == 0);
        const float weight = kWeights[sequence.pick(kWeights.size())];
        const SkConic conic(points, weight);
        append_conic(output, conic);

        for (const float t : kEvalTs) {
            append_point(output, conic.evalAt(t));
            append_point(output, conic.evalTangentAt(t));
            SkPoint point{-7, -7};
            SkVector tangent{-7, -7};
            conic.evalAt(t, &point, &tangent);
            append_point(output, point);
            append_point(output, tangent);
        }

        SkConic halves[2] = {conic, conic};
        append_bool(output, conic.chopAt(pick_unit_t(sequence), halves));
        append_conic(output, halves[0]);
        append_conic(output, halves[1]);

        float t1 = pick_unit_t(sequence);
        float t2 = pick_unit_t(sequence);
        if (t2 < t1) {
            const float swapped = t1;
            t1 = t2;
            t2 = swapped;
        }
        const std::array<std::array<float, 2>, 4> ranges{{
            {{t1, t2}}, {{0.0f, t2}}, {{t1, 1.0f}}, {{0.0f, 1.0f}},
        }};
        for (const std::array<float, 2>& range : ranges) {
            SkConic part(points, weight);
            conic.chopAt(range[0], range[1], &part);
            append_conic(output, part);
        }

        conic.chop(halves);
        append_conic(output, halves[0]);
        append_conic(output, halves[1]);

        for (const float tolerance : kTolerances) {
            append(output, conic.computeQuadPOW2(tolerance));
        }
        for (int pow2 = 0; pow2 <= 5; ++pow2) {
            const int quadCount = conic.chopIntoQuadsPOW2(quads, pow2);
            append(output, quadCount);
            append_points(output, quads, 1 + 2 * quadCount);
        }

        float extremum = -7;
        append_bool(output, conic.findXExtrema(&extremum));
        append(output, extremum);
        extremum = -7;
        append_bool(output, conic.findYExtrema(&extremum));
        append(output, extremum);
        SkConic split[2] = {conic, conic};
        append_bool(output, conic.chopAtYExtrema(split));
        append_conic(output, split[0]);
        append_conic(output, split[1]);

        SkRect bounds = SkRect::MakeEmpty();
        conic.computeTightBounds(&bounds);
        append_rect(output, bounds);
        conic.computeFastBounds(&bounds);
        append_rect(output, bounds);

        SkAutoConicToQuads converter;
        const float tolerance = kTolerances[sequence.pick(kTolerances.size())];
        const SkPoint* converted = converter.computeQuads(conic, tolerance);
        append(output, converter.countQuads());
        append_points(output, converted, 1 + 2 * converter.countQuads());
        const std::array<float, 4> rawWeights{weight, 0.0f, -2.0f, -corpus::kInf};
        for (const float rawWeight : rawWeights) {
            const SkPoint* raw = converter.computeQuads(points, rawWeight, 0.25f);
            append(output, converter.countQuads());
            append_points(output, raw, 1 + 2 * converter.countQuads());
        }
        ++output.cases;
    }
}

void conic_weight_corpus(Output& output, Sequence& sequence) {
    std::array<SkMatrix, 6> matrices{};
    matrices[0] = SkMatrix::I();
    matrices[1] = SkMatrix::Scale(3, -0.5f);
    matrices[2] = SkMatrix::MakeAll(1, 0, 0, 0, 1, 0, 0.001f, 0, 1);
    matrices[3] = SkMatrix::MakeAll(2, 0.5f, 3, -0.25f, 1, 4, 0.002f, -0.003f, 1);
    matrices[4] = SkMatrix::MakeAll(1, 0, 0, 0, 1, 0, 0.1f, 0.1f, 0.5f);
    matrices[5] = SkMatrix::MakeAll(1, 0, 0, 0, 1, 0, 1e-20f, 0, 1e-20f);
    for (int i = 0; i < 800; ++i) {
        SkPoint points[3] = {};
        fill_points(sequence, points, 3, i % 4 == 0);
        const float weight = kWeights[sequence.pick(kWeights.size())];
        for (const SkMatrix& matrix : matrices) {
            append(output, SkConic::TransformW(points, weight, matrix));
        }
        ++output.cases;
    }
}

SkVector unit_vector(double degrees) {
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const SkVector vector{static_cast<float>(std::cos(radians)),
                          static_cast<float>(std::sin(radians))};
    return vector;
}

void unit_arc_corpus(Output& output) {
    const std::array<double, 15> sweeps{
        0, 1e-5, 1, 30, 45, 89.999, 90, 90.001, 135, 180, 200, 270, 300, 359.99, 360,
    };
    const SkMatrix scale = SkMatrix::Scale(2, 3);
    const SkMatrix rotate = SkMatrix::RotateDeg(33, {4, 5});
    const std::array<const SkMatrix*, 3> userMatrices{nullptr, &scale, &rotate};
    for (int start = 0; start < 360; start += 15) {
        for (const double sweep : sweeps) {
            const SkVector uStart = unit_vector(start);
            const SkVector uStop = unit_vector(start + sweep);
            for (int direction = 0; direction < 2; ++direction) {
                const SkPathDirection dir =
                        direction == 0 ? SkPathDirection::kCW : SkPathDirection::kCCW;
                for (const SkMatrix* userMatrix : userMatrices) {
                    SkConic conics[SkConic::kMaxConicsForArc] = {};
                    for (SkConic& conic : conics) {
                        conic.set(SkPoint{0, 0}, SkPoint{0, 0}, SkPoint{0, 0}, 1);
                    }
                    const int count = SkConic::BuildUnitArc(uStart, uStop, dir, userMatrix,
                                                            conics);
                    append(output, count);
                    for (int i = 0; i < count; ++i) {
                        append_conic(output, conics[i]);
                    }
                    ++output.cases;
                }
            }
        }
    }
}

double pick_double(Sequence& sequence) {
    const uint32_t choice = sequence.next() % 16;
    double value = 0;
    if (choice == 0) {
        value = 0;
    } else if (choice == 1) {
        value = 1e-12;
    } else if (choice == 2) {
        value = -1e-9;
    } else if (choice == 3) {
        value = 1e300;
    } else if (choice == 4) {
        value = std::numeric_limits<double>::infinity();
    } else if (choice == 5) {
        value = std::numeric_limits<double>::quiet_NaN();
    } else {
        value = static_cast<double>(pick_coordinate(sequence, false)) / 3.0;
    }
    return value;
}

void solver_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 3000; ++i) {
        double curve[8] = {};
        for (double& value : curve) {
            value = static_cast<double>(pick_coordinate(sequence, i % 6 == 0));
        }
        const double t = static_cast<double>(pick_unit_t(sequence));
        const std::array<double, 2> evaluated = SkBezierCubic::EvalAt(curve, t);
        append(output, evaluated[0]);
        append(output, evaluated[1]);
        append(output, SkBezierCubic::EvalAt(curve, 0.0)[1]);
        append(output, SkBezierCubic::EvalAt(curve, 1.0)[0]);
        double twoCurves[14] = {};
        SkBezierCubic::Subdivide(curve, t, twoCurves);
        for (const double value : twoCurves) {
            append(output, value);
        }
        for (int axis = 0; axis < 2; ++axis) {
            const std::array<double, 4> polynomial =
                    SkBezierCubic::ConvertToPolynomial(curve, axis == 1);
            for (const double coefficient : polynomial) {
                append(output, coefficient);
            }
        }

        SkPoint cubic[4] = {};
        fill_points(sequence, cubic, 4, i % 6 == 0);
        const float y = i % 3 == 0 ? cubic[0].fY : (cubic[0].fY + cubic[3].fY) * 0.5f;
        float storage[3] = {-7, -7, -7};
        const SkSpan<const float> cubicHits =
                SkBezierCubic::IntersectWithHorizontalLine(SkSpan<const SkPoint>(cubic, 4), y,
                                                           storage);
        append(output, cubicHits.size());
        append_scalars(output, cubicHits.data(), cubicHits.size());
        float quadStorage[2] = {-7, -7};
        const SkSpan<const float> quadHits =
                SkBezierQuad::IntersectWithHorizontalLine(SkSpan<const SkPoint>(cubic, 3), y,
                                                          quadStorage);
        append(output, quadHits.size());
        append_scalars(output, quadHits.data(), quadHits.size());

        const double A = pick_double(sequence);
        const double B = pick_double(sequence);
        const double C = pick_double(sequence);
        double D = pick_double(sequence);
        if (i % 7 == 0) {
            D = -(A + B + C);  // 1 is a root
        }
        append(output, SkQuads::Discriminant(A, B, C));
        const SkQuads::RootResult result = SkQuads::Roots(A, B, C);
        append(output, result.discriminant);
        append(output, result.root0);
        append(output, result.root1);
        double quadRoots[2] = {-7, -7};
        const int quadCount = SkQuads::RootsReal(A, B, C, quadRoots);
        append(output, quadCount);
        append(output, quadRoots[0]);
        append(output, quadRoots[1]);
        append(output, SkQuads::EvalAt(A, B, C, t));

        double cubicRoots[3] = {-7, -7, -7};
        const int realCount = SkCubics::RootsReal(A, B, C, D, cubicRoots);
        append(output, realCount);
        for (const double root : cubicRoots) {
            append(output, root);
        }
        double validRoots[3] = {-7, -7, -7};
        const int validCount = SkCubics::RootsValidT(A, B, C, D, validRoots);
        append(output, validCount);
        for (const double root : validRoots) {
            append(output, root);
        }
        double searchedRoots[3] = {-7, -7, -7};
        const int searchedCount = SkCubics::BinarySearchRootsValidT(A, B, C, D, searchedRoots);
        append(output, searchedCount);
        for (const double root : searchedRoots) {
            append(output, root);
        }
        append(output, SkCubics::EvalAt(A, B, C, D, t));
        double coefficients[4] = {A, B, C, D};
        append(output, SkCubics::EvalAt(coefficients, t));
        ++output.cases;
    }
}

void clipper_corpus(Output& output, Sequence& sequence) {
    const std::array<SkIRect, 4> clips{
        SkIRect::MakeLTRB(-16, -16, 16, 16), SkIRect::MakeLTRB(0, 0, 64, 8),
        SkIRect::MakeLTRB(-100, -100, 100, 100), SkIRect::MakeLTRB(5, 5, 5, 5),
    };
    for (int i = 0; i < 2000; ++i) {
        SkPoint cubic[4] = {};
        fill_points(sequence, cubic, 4, i % 8 == 0);
        if (i % 2 == 0) {
            // Make the cubic monotonic in Y, ascending or descending.
            for (int a = 1; a < 4; ++a) {
                int b = a;
                while (b > 0 && cubic[b - 1].fY > cubic[b].fY) {
                    const SkPoint moved = cubic[b];
                    cubic[b] = cubic[b - 1];
                    cubic[b - 1] = moved;
                    --b;
                }
            }
            if (i % 4 == 0) {
                const SkPoint first = cubic[0];
                cubic[0] = cubic[3];
                cubic[3] = first;
                const SkPoint second = cubic[1];
                cubic[1] = cubic[2];
                cubic[2] = second;
            }
        }
        const std::array<float, 3> ys{
            (cubic[0].fY + cubic[3].fY) * 0.5f, cubic[0].fY, pick_coordinate(sequence, true),
        };
        for (const float y : ys) {
            float t = -7;
            append_bool(output, SkCubicClipper::ChopMonoAtY(cubic, y, &t));
            append(output, t);
        }
        for (const SkIRect& clip : clips) {
            SkCubicClipper clipper;
            clipper.setClip(clip);
            SkPoint clipped[4] = {};
            append_bool(output, clipper.clipCubic(cubic, clipped));
            append_points(output, clipped, 4);
        }
        ++output.cases;
    }
}

void cubic_map_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1500; ++i) {
        const SkPoint p1{static_cast<float>(sequence.next() % 33) / 32.0f,
                         static_cast<float>(static_cast<int32_t>(sequence.next() % 65) - 16) /
                                 32.0f};
        const SkPoint p2{static_cast<float>(sequence.next() % 33) / 32.0f,
                         static_cast<float>(static_cast<int32_t>(sequence.next() % 65) - 16) /
                                 32.0f};
        SkPoint q1 = p1;
        SkPoint q2 = p2;
        if (i % 11 == 0) {
            q1 = p2;
            q2 = p2;
        } else if (i % 13 == 0) {
            q1 = SkPoint{0, p1.fY};
            q2 = SkPoint{0, p2.fY};
        } else if (i % 17 == 0) {
            q1 = SkPoint{-0.5f, p1.fY};
            q2 = SkPoint{1.5f, p2.fY};
        }
        append_bool(output, SkCubicMap::IsLinear(q1, q2));
        const SkCubicMap map(q1, q2);
        for (int step = 0; step <= 32; ++step) {
            const float x = static_cast<float>(step) / 32.0f;
            append(output, map.computeYFromX(x));
            append_point(output, map.computeFromT(x));
        }
        append(output, map.computeYFromX(-1.0f));
        append(output, map.computeYFromX(2.0f));
        append(output, map.computeYFromX(1e-11f));
        append(output, map.computeYFromX(0.99999994f));
        ++output.cases;
    }
}

void curve_corpus(Output& output) {
    Sequence sequence{};
    quad_root_corpus(output, sequence);
    quad_corpus(output, sequence);
    cubic_corpus(output, sequence);
    conic_corpus(output, sequence);
    conic_weight_corpus(output, sequence);
    unit_arc_corpus(output);
    solver_corpus(output, sequence);
    clipper_corpus(output, sequence);
    cubic_map_corpus(output, sequence);
}

#ifndef STX_SKIA_REFERENCE
bool corrected_behavior() {
    // A two-parameter chop ending at 1 keeps the original end point when src is dst. The
    // original read src[3] after the first chop had overwritten it.
    SkPoint shared[10] = {{0, 0}, {10, 20}, {30, 20}, {40, 0}};
    SkChopCubicAt(shared, shared, 0.5f, 1.0f);
    for (int i = 7; i < 10; ++i) {
        if (shared[i] != SkPoint::Make(40, 0)) return false;
    }

    // The multi-parameter chop reaches that case when a later pair ends at 1.
    const SkPoint cubic[4] = {{0, 0}, {10, 20}, {30, 20}, {40, 0}};
    const float tValues[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    SkPoint chopped[16] = {};
    SkChopCubicAt(cubic, chopped, tValues, 4);
    if (chopped[15] != SkPoint::Make(40, 0)) return false;
    if (chopped[12] != SkPoint::Make(40, 0)) return false;

    // Default construction is a defined unit-weight conic at the origin.
    const SkConic conic{};
    if (conic.fW != 1) return false;
    for (const SkPoint& point : conic.fPts) {
        if (point != SkPoint::Make(0, 0)) return false;
    }

    // A default clipper has an empty clip and rejects every cubic.
    SkCubicClipper clipper;
    SkPoint clipped[4] = {};
    if (clipper.clipCubic(cubic, clipped)) return false;
    return true;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!corrected_behavior()) {
        std::printf("curve geometry: corrected behavior FAIL\n");
        return 1;
    }
#endif
    Output output{};
    curve_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("curve geometry: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
