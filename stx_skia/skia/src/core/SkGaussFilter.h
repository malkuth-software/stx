/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkGaussFilter_DEFINED
#define SkGaussFilter_DEFINED

#include <cstddef>

// Define gaussian filters for values of sigma < 2. Produce values good to 1 part in 1,000,000.
// Produces values as defined in "Scale-Space for Discrete Signals" by Tony Lindeberg.
class SkGaussFilter {
public:
    inline static constexpr int kGaussArrayMax = 6;

    explicit SkGaussFilter(double sigma);

    size_t size() const {
        const size_t count = static_cast<size_t>(fN);
        return count;
    }
    int radius() const { return fN - 1; }
    int width() const {
        const int extent = 2 * radius() + 1;
        return extent;
    }

    // Allow a filter to be used in a C++ ranged-for loop over its fN live factors.
    const double* begin() const { return &fBasis[0]; }
    const double* end() const { return &fBasis[fN]; }

private:
    // fBasis[0, fN) holds the normalized factors from the center outward; the factor at fN
    // is the first one below the cutoff, computed only to stop the recurrence.
    double fBasis[kGaussArrayMax] = {};
    int    fN = 0;
};

#endif  // SkGaussFilter_DEFINED
