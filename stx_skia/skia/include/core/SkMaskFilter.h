/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkMaskFilter_DEFINED
#define SkMaskFilter_DEFINED

#include "include/core/SkFlattenable.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"

#include <cstddef>
#include <cstdint>

enum SkBlurStyle : int;
struct SkDeserialProcs;

class SkMaskFilter;

/** Why SkMaskFilter::MakeBlur made no filter. The checks run in this order. */
enum class SkBlurRejection : uint8_t {
    kNone,                // a filter was made
    kNonFiniteSigma,      // sigma is infinite or NaN
    kNonPositiveSigma,    // sigma is zero or negative
    kInvalidStyle,        // style is not an SkBlurStyle
};

/** The result of SkMaskFilter::MakeBlur: filter is null exactly when rejection is not kNone. */
struct SkBlurMaskFilterResult {
    sk_sp<SkMaskFilter> filter;
    SkBlurRejection rejection = SkBlurRejection::kNone;
};

/** \class SkMaskFilter

    SkMaskFilter is the base class for object that perform transformations on
    the mask before drawing it. An example subclass is Blur.
*/
class SK_API SkMaskFilter : public SkFlattenable {
public:
    /** Create a blur maskfilter.
     *  @param style      The SkBlurStyle to use
     *  @param sigma      Standard deviation of the Gaussian blur to apply. Must be finite
     *                    and > 0.
     *  @param respectCTM if true the blur's sigma is modified by the CTM.
     *  @return The new blur maskfilter, or the reason none was made
     */
    static SkBlurMaskFilterResult MakeBlur(SkBlurStyle style, SkScalar sigma,
                                           bool respectCTM = true);

    static sk_sp<SkMaskFilter> Deserialize(const void* data, size_t size,
                                           const SkDeserialProcs* procs = nullptr);

private:
    friend class SkFlattenable;
};

#endif
