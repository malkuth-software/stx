/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkTPin_DEFINED
#define SkTPin_DEFINED

#include <algorithm>

/** @return x pinned (clamped) between lo and hi, inclusively.

    Unlike std::clamp(), SkTPin() always returns a value between lo and hi.
    If x is NaN, SkTPin() returns lo but std::clamp() returns NaN.
*/
template <typename T>
// Returns a copy: the intake version returned a reference to one of its arguments, which
// dangled when the caller kept it beyond a call with temporaries, such as SkTPin(x, 0, 7).
constexpr T SkTPin(const T& x, const T& lo, const T& hi) {
    return std::max(lo, std::min(x, hi));
}

#endif
