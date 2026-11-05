/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkCubics.h"

#include "include/private/SkAssert.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkTPin.h"
#include "src/core/SkQuads.h"

#include <algorithm>
#include <cmath>

static bool nearly_equal(double x, double y) {
    if (sk_double_nearly_zero(x)) {
        const bool bothZero = sk_double_nearly_zero(y);
        return bothZero;
    }
    const bool equal = sk_doubles_nearly_equal_ulps(x, y);
    return equal;
}

// When the A coefficient of a cubic is close to 0, there can be floating point error
// that arises from computing a very large root. In those cases, we would rather be
// precise about the smaller 2 roots, so we have this arbitrary cutoff for when A is
// really small or small compared to B.
static bool close_to_a_quadratic(double A, double B) {
    if (sk_double_nearly_zero(B)) {
        const bool bothZero = sk_double_nearly_zero(A);
        return bothZero;
    }
    const bool small = std::abs(A / B) < 1.0e-7;
    return small;
}

int SkCubics::RootsReal(double A, double B, double C, double D, double solution[3]) {
    if (close_to_a_quadratic(A, B)) {
        const int count = SkQuads::RootsReal(B, C, D, solution);
        return count;
    }
    if (sk_double_nearly_zero(D)) {  // 0 is one root
        int num = SkQuads::RootsReal(A, B, C, solution);
        for (int i = 0; i < num; ++i) {
            if (sk_double_nearly_zero(solution[i])) {
                return num;
            }
        }
        solution[num] = 0;
        ++num;
        return num;
    }
    if (sk_double_nearly_zero(A + B + C + D)) {  // 1 is one root
        int num = SkQuads::RootsReal(A, A + B, -D, solution);
        for (int i = 0; i < num; ++i) {
            if (sk_doubles_nearly_equal_ulps(solution[i], 1)) {
                return num;
            }
        }
        solution[num] = 1;
        ++num;
        return num;
    }
    // If A is zero (e.g. B was nan and thus close_to_a_quadratic was false), we will
    // temporarily have infinities rolling about, but will catch that when checking
    // R2MinusQ3.
    const double invA = sk_ieee_double_divide(1, A);
    const double a = B * invA;
    const double b = C * invA;
    const double c = D * invA;
    const double a2 = a * a;
    const double Q = (a2 - b * 3) / 9;
    const double R = (2 * a2 * a - 9 * a * b + 27 * c) / 54;
    const double R2 = R * R;
    const double Q3 = Q * Q * Q;
    const double R2MinusQ3 = R2 - Q3;
    // If one of R2 Q3 is infinite or nan, subtracting them will also be infinite/nan.
    // If both are infinite or nan, the subtraction will be nan.
    // In either case, we have no finite roots.
    if (!SkIsFinite(R2MinusQ3)) {
        return 0;
    }
    const double adiv3 = a / 3;
    double r = 0;
    int count = 0;
    if (R2MinusQ3 < 0) {   // we have 3 real roots
        // the divide/root can, due to finite precisions, be slightly outside of -1...1
        const double theta = acos(SkTPin(R / std::sqrt(Q3), -1., 1.));
        const double neg2RootQ = -2 * std::sqrt(Q);

        r = neg2RootQ * cos(theta / 3) - adiv3;
        solution[count] = r;
        ++count;

        r = neg2RootQ * cos((theta + 2 * SK_DoublePI) / 3) - adiv3;
        if (!nearly_equal(solution[0], r)) {
            solution[count] = r;
            ++count;
        }
        r = neg2RootQ * cos((theta - 2 * SK_DoublePI) / 3) - adiv3;
        if (!nearly_equal(solution[0], r) &&
            (count == 1 || !nearly_equal(solution[1], r))) {
            solution[count] = r;
            ++count;
        }
    } else {  // we have 1 real root
        const double sqrtR2MinusQ3 = std::sqrt(R2MinusQ3);
        double root = fabs(R) + sqrtR2MinusQ3;
        root = std::cbrt(root); // cube root
        if (R > 0) {
            root = -root;
        }
        if (!sk_double_nearly_zero(root)) {
            root += Q / root;
        }
        r = root - adiv3;
        solution[count] = r;
        ++count;
        if (!sk_double_nearly_zero(R2) &&
             sk_doubles_nearly_equal_ulps(R2, Q3)) {
            r = -root / 2 - adiv3;
            if (!nearly_equal(solution[0], r)) {
                solution[count] = r;
                ++count;
            }
        }
    }
    return count;
}

int SkCubics::RootsValidT(double A, double B, double C, double D,
                          double solution[3]) {
    double allRoots[3] = {0, 0, 0};
    const int realRoots = SkCubics::RootsReal(A, B, C, D, allRoots);
    int foundRoots = 0;
    for (int index = 0; index < realRoots; ++index) {
        const double tValue = allRoots[index];
        if (tValue <= 1.00005 && (tValue >= 1.0 || sk_doubles_nearly_equal_ulps(tValue, 1.0))) {
            // Make sure we do not already have 1 (or something very close) in the list of roots.
            if ((foundRoots < 1 || !sk_doubles_nearly_equal_ulps(solution[0], 1)) &&
                (foundRoots < 2 || !sk_doubles_nearly_equal_ulps(solution[1], 1))) {
                solution[foundRoots] = 1;
                ++foundRoots;
            }
        } else if (tValue >= -0.00005 && (tValue <= 0.0 || sk_double_nearly_zero(tValue))) {
            // Make sure we do not already have 0 (or something very close) in the list of roots.
            if ((foundRoots < 1 || !sk_double_nearly_zero(solution[0])) &&
                (foundRoots < 2 || !sk_double_nearly_zero(solution[1]))) {
                solution[foundRoots] = 0;
                ++foundRoots;
            }
        } else if (tValue > 0.0 && tValue < 1.0) {
            solution[foundRoots] = tValue;
            ++foundRoots;
        }
    }
    return foundRoots;
}

static bool approximately_zero(double x) {
    // This cutoff for our binary search hopefully strikes a good balance between
    // performance and accuracy.
    const bool zero = std::abs(x) < 0.00000001;
    return zero;
}

static int find_extrema_valid_t(double A, double B, double C,
                                double t[2]) {
    // To find the local min and max of a cubic, we take the derivative and
    // solve when that is equal to 0.
    // d/dt (A*t^3 + B*t^2 + C*t + D) = 3A*t^2 + 2B*t + C
    double roots[2] = {0, 0};
    const int numRoots = SkQuads::RootsReal(3*A, 2*B, C, roots);
    int validRoots = 0;
    for (int i = 0; i < numRoots; i++) {
        const double tValue = roots[i];
        if (tValue >= 0 && tValue <= 1.0) {
            t[validRoots] = tValue;
            ++validRoots;
        }
    }
    return validRoots;
}

static double binary_search(double A, double B, double C, double D, double start, double stop) {
    SkASSERT(start <= stop);
    const double left = SkCubics::EvalAt(A, B, C, D, start);
    if (approximately_zero(left)) {
        return start;
    }
    const double right = SkCubics::EvalAt(A, B, C, D, stop);
    if (!SkIsFinite(left, right)) {
        return -1; // Not going to deal with one or more endpoints being non-finite.
    }
    if ((left > 0 && right > 0) || (left < 0 && right < 0)) {
        return -1; // We can only have a root if one is above 0 and the other is below 0.
    }

    constexpr int maxIterations = 1000; // prevent infinite loop
    for (int i = 0; i < maxIterations; i++) {
        const double step = (start + stop) / 2;
        const double curr = SkCubics::EvalAt(A, B, C, D, step);
        if (approximately_zero(curr)) {
            return step;
        }
        if ((curr < 0 && left < 0) || (curr > 0 && left > 0)) {
            // go right
            start = step;
        } else {
            // go left
            stop = step;
        }
    }
    return -1;
}

int SkCubics::BinarySearchRootsValidT(double A, double B, double C, double D,
                                      double solution[3]) {
    if (!SkIsFinite(A, B, C, D)) {
        return 0;
    }
    double regions[4] = {0, 0, 0, 1};
    // Find local minima and maxima
    double minMax[2] = {0, 0};
    const int extremaCount = find_extrema_valid_t(A, B, C, minMax);
    int startIndex = 2 - extremaCount;
    if (extremaCount == 1) {
        regions[startIndex + 1] = minMax[0];
    }
    if (extremaCount == 2) {
        // While the roots will be in the range 0 to 1 inclusive, they might not be sorted.
        regions[startIndex + 1] = std::min(minMax[0], minMax[1]);
        regions[startIndex + 2] = std::max(minMax[0], minMax[1]);
    }
    // Starting at regions[startIndex] and going up through regions[3], we have
    // an ascending list of numbers in the range 0 to 1.0, between which are the possible
    // locations of a root.
    int foundRoots = 0;
    for (;startIndex < 3; startIndex++) {
        const double root = binary_search(A, B, C, D, regions[startIndex], regions[startIndex + 1]);
        if (root >= 0) {
            // Check for duplicates
            if ((foundRoots < 1 || !approximately_zero(solution[0] - root)) &&
                (foundRoots < 2 || !approximately_zero(solution[1] - root))) {
                solution[foundRoots] = root;
                ++foundRoots;
            }
        }
    }
    return foundRoots;
}
