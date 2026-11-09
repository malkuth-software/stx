/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkGaussFilter.h"

#include "include/private/SkAssert.h"

#include <cmath>

// The value when we can stop expanding the filter. The spec implies that 3% is acceptable, but
// we just use 1%.
static constexpr double kGoodEnough = 1.0 / 100.0;

// Normalize the values of gauss to 1.0, and make sure they add to one.
// NB if n == 1, then this will force gauss[0] == 1.
static void normalize(int n, double* gauss) {
    // Carefully add from smallest to largest to calculate the normalizing sum.
    double sum = 0;
    for (int i = n-1; i >= 1; i--) {
        sum += 2 * gauss[i];
    }
    sum += gauss[0];

    // Normalize gauss.
    for (int i = 0; i < n; i++) {
        gauss[i] /= sum;
    }

    // The factors should sum to 1. Take any remaining slop, and add it to gauss[0]. Add the
    // values in such a way to maintain the most accuracy.
    sum = 0;
    for (int i = n - 1; i >= 1; i--) {
        sum += 2 * gauss[i];
    }

    gauss[0] = 1 - sum;
}

// The two functions below come from the equations in "Handbook of Mathematical Functions"
// by Abramowitz and Stegun. Specifically, equation 9.6.10 on page 375. Bessel0 is given
// explicitly as 9.6.12
// BesselI_0 for 0 <= sigma < 2.
// NB the k = 0 factor is just sum = 1.0.
static double bessel_i0(double t) {
    const double tSquaredOver4 = t * t / 4.0;
    double sum = 1.0;
    double factor = 1.0;
    int k = 1;
    // Use a variable number of loops. When sigma is small, this only requires 3-4 loops, but
    // when sigma is near 2, it could require 10 loops. The same holds for BesselI_1.
    while (factor > 1.0/1000000.0) {
        factor *= tSquaredOver4 / (k * k);
        sum += factor;
        k += 1;
    }
    return sum;
}

// BesselI_1 for 0 <= sigma < 2.
static double bessel_i1(double t) {
    const double tSquaredOver4 = t * t / 4.0;
    double sum = t / 2.0;
    double factor = sum;
    int k = 1;
    while (factor > 1.0/1000000.0) {
        factor *= tSquaredOver4 / (k * (k + 1));
        sum += factor;
        k += 1;
    }
    return sum;
}

// Fills gauss[0, n] and returns n, the count of factors at or above kGoodEnough; gauss must
// hold SkGaussFilter::kGaussArrayMax values.
static int calculate_bessel_factors(double sigma, double* gauss) {
    const double var = sigma * sigma;

    // The following formula for calculating the Gaussian kernel is from
    // "Scale-Space for Discrete Signals" by Tony Lindeberg.
    // gauss(n; var) = besselI_n(var) / (e^var)
    const double d = std::exp(var);
    double b[SkGaussFilter::kGaussArrayMax] = {bessel_i0(var), bessel_i1(var)};
    gauss[0] = b[0]/d;
    gauss[1] = b[1]/d;

    // The code below is tricky, and written to mirror the recursive equations from the book.
    // The maximum spread for sigma == 2 is guass[4], but in order to know to stop guass[5]
    // is calculated. At this point n == 5 meaning that gauss[0..4] are the factors, but a 6th
    // element was used to calculate them.
    int n = 1;
    // The recurrence relation below is from "Numerical Recipes" 3rd Edition.
    // Equation 6.5.16 p.282
    while (gauss[n] > kGoodEnough) {
        // Sigma below 2 leaves this loop with n <= 5; a larger sigma must not write past the
        // six-entry arrays.
        SkASSERT_RELEASE(n + 1 < SkGaussFilter::kGaussArrayMax);
        b[n+1] = -(2*n/var) * b[n] + b[n-1];
        gauss[n+1] = b[n+1] / d;
        n += 1;
    }

    normalize(n, gauss);

    return n;
}

SkGaussFilter::SkGaussFilter(double sigma) {
    SkASSERT(0 <= sigma && sigma < 2);

    fN = calculate_bessel_factors(sigma, fBasis);
}
