/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkMaskBlurFilter.h"

#include "include/private/SkFloatingPoint.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "src/core/SkArenaAlloc.h"
#include "src/core/SkColorPriv.h"
#include "src/core/SkGaussFilter.h"
#include "src/core/SkVx.h"

#include <cmath>
#include <climits>

namespace {

class PlanGauss final {
public:
    explicit PlanGauss(double sigma) {
        const int possibleWindow = static_cast<int>(floor(sigma * 3 * sqrt(2 * SK_DoublePI) / 4 + 0.5));
        const int window = std::max(1, possibleWindow);

        fPass0Size = window - 1;
        fPass1Size = window - 1;
        fPass2Size = (window & 1) == 1 ? window - 1 : window;

        // Calculating the border is tricky. I will go through the odd case which is simpler, and
        // then through the even case. Given a stack of filters seven wide for the odd case of
        // three passes.
        //
        //        S
        //     aaaAaaa
        //     bbbBbbb
        //     cccCccc
        //        D
        //
        // The furthest changed pixel is when the filters are in the following configuration.
        //
        //                 S
        //           aaaAaaa
        //        bbbBbbb
        //     cccCccc
        //        D
        //
        //  The A pixel is calculated using the value S, the B uses A, and the C uses B, and
        // finally D is C. So, with a window size of seven the border is nine. In general, the
        // border is 3*((window - 1)/2).
        //
        // For even cases the filter stack is more complicated. The spec specifies two passes
        // of even filters and a final pass of odd filters. A stack for a width of six looks like
        // this.
        //
        //       S
        //    aaaAaa
        //     bbBbbb
        //    cccCccc
        //       D
        //
        // The furthest pixel looks like this.
        //
        //               S
        //          aaaAaa
        //        bbBbbb
        //    cccCccc
        //       D
        //
        // For a window of size, the border value is seven. In general the border is 3 *
        // (window/2) -1.
        fBorder = (window & 1) == 1 ? 3 * ((window - 1) / 2) : 3 * (window / 2) - 1;
        fSlidingWindow = 2 * fBorder + 1;

        // If the window is odd then the divisor is just window ^ 3 otherwise,
        // it is window * window * (window + 1) = window ^ 2 + window ^ 3;
        const int window2 = window * window;
        const int window3 = window2 * window;
        const int divisor = (window & 1) == 1 ? window3 : window3 + window2;

        fWeight = static_cast<uint64_t>(round(1.0 / divisor * (1ull << 32)));
    }

    size_t bufferSize() const {
        const size_t count = static_cast<size_t>(fPass0Size + fPass1Size + fPass2Size);
        return count;
    }

    int    border()     const { return fBorder; }

public:
    class Scan {
    public:
        Scan(uint64_t weight, int noChangeCount,
             uint32_t* buffer0, uint32_t* buffer0End,
             uint32_t* buffer1, uint32_t* buffer1End,
             uint32_t* buffer2, uint32_t* buffer2End)
            : fWeight{weight}
            , fNoChangeCount{noChangeCount}
            , fBuffer0{buffer0}
            , fBuffer0End{buffer0End}
            , fBuffer1{buffer1}
            , fBuffer1End{buffer1End}
            , fBuffer2{buffer2}
            , fBuffer2End{buffer2End}
        { }

        template <typename AlphaIter> void blur(const AlphaIter srcBegin, const AlphaIter srcEnd,
                    uint8_t* dst, int dstStride, int dstCount) const {
            // The destination is a strided column. Its logical one-past position
            // can lie beyond the backing allocation; retain a count, not that pointer.
            uint32_t* buffer0Cursor = fBuffer0;
            uint32_t* buffer1Cursor = fBuffer1;
            uint32_t* buffer2Cursor = fBuffer2;
            const size_t bufferCount = static_cast<size_t>(fBuffer2End - fBuffer0);
            const size_t bufferBytes = bufferCount * sizeof(*fBuffer0);
            const size_t stride = static_cast<size_t>(dstStride);
            int forward = 0;
            std::memset(fBuffer0, 0x00, bufferBytes);

            uint32_t sum0 = 0;
            uint32_t sum1 = 0;
            uint32_t sum2 = 0;

            // Consume the source generating pixels.
            for (AlphaIter src = srcBegin; src < srcEnd; ++src, ++forward) {
                const uint32_t leadingEdge = *src;
                sum0 += leadingEdge;
                sum1 += sum0;
                sum2 += sum1;

                dst[static_cast<size_t>(forward) * stride] = finalScale(sum2);

                sum2 -= *buffer2Cursor;
                *buffer2Cursor = sum1;
                buffer2Cursor = (buffer2Cursor + 1) < fBuffer2End ? buffer2Cursor + 1 : fBuffer2;

                sum1 -= *buffer1Cursor;
                *buffer1Cursor = sum0;
                buffer1Cursor = (buffer1Cursor + 1) < fBuffer1End ? buffer1Cursor + 1 : fBuffer1;

                sum0 -= *buffer0Cursor;
                *buffer0Cursor = leadingEdge;
                buffer0Cursor = (buffer0Cursor + 1) < fBuffer0End ? buffer0Cursor + 1 : fBuffer0;
            }

            // The leading edge is off the right side of the mask.
            for (int i = 0; i < fNoChangeCount; i++) {
                const uint32_t leadingEdge = 0;
                sum0 += leadingEdge;
                sum1 += sum0;
                sum2 += sum1;

                dst[static_cast<size_t>(forward) * stride] = finalScale(sum2);

                sum2 -= *buffer2Cursor;
                *buffer2Cursor = sum1;
                buffer2Cursor = (buffer2Cursor + 1) < fBuffer2End ? buffer2Cursor + 1 : fBuffer2;

                sum1 -= *buffer1Cursor;
                *buffer1Cursor = sum0;
                buffer1Cursor = (buffer1Cursor + 1) < fBuffer1End ? buffer1Cursor + 1 : fBuffer1;

                sum0 -= *buffer0Cursor;
                *buffer0Cursor = leadingEdge;
                buffer0Cursor = (buffer0Cursor + 1) < fBuffer0End ? buffer0Cursor + 1 : fBuffer0;

                ++forward;
            }

            // Starting from the right, fill in the rest of the buffer.
            std::memset(fBuffer0, 0, bufferBytes);

            sum0 = 0;
            sum1 = 0;
            sum2 = 0;

            int reverse = dstCount;
            AlphaIter src = srcEnd;
            while (reverse > forward) {
                --reverse;
                --src;
                const uint32_t leadingEdge = *src;
                sum0 += leadingEdge;
                sum1 += sum0;
                sum2 += sum1;

                dst[static_cast<size_t>(reverse) * stride] = finalScale(sum2);

                sum2 -= *buffer2Cursor;
                *buffer2Cursor = sum1;
                buffer2Cursor = (buffer2Cursor + 1) < fBuffer2End ? buffer2Cursor + 1 : fBuffer2;

                sum1 -= *buffer1Cursor;
                *buffer1Cursor = sum0;
                buffer1Cursor = (buffer1Cursor + 1) < fBuffer1End ? buffer1Cursor + 1 : fBuffer1;

                sum0 -= *buffer0Cursor;
                *buffer0Cursor = leadingEdge;
                buffer0Cursor = (buffer0Cursor + 1) < fBuffer0End ? buffer0Cursor + 1 : fBuffer0;
            }
        }

    private:
        inline static constexpr uint64_t kHalf = static_cast<uint64_t>(1) << 31;

        uint8_t finalScale(uint32_t sum) const {
            const uint64_t rounded = (fWeight * sum + kHalf) >> 32;
            const uint8_t value = SkTo<uint8_t>(rounded);
            return value;
        }

        uint64_t  fWeight;
        int       fNoChangeCount;
        uint32_t* fBuffer0;
        uint32_t* fBuffer0End;
        uint32_t* fBuffer1;
        uint32_t* fBuffer1End;
        uint32_t* fBuffer2;
        uint32_t* fBuffer2End;
    };

    // The caller supplies bufferSize() reusable uint32_t elements. The scan
    // borrows them; each call overwrites all three sliding-window buffers.
    Scan makeBlurScan(int width, uint32_t* buffer) const {
        uint32_t* buffer0 = buffer;
        uint32_t* buffer0End = buffer0 + fPass0Size;
        uint32_t* buffer1 = buffer0End;
        uint32_t* buffer1End = buffer1 + fPass1Size;
        uint32_t* buffer2 = buffer1End;
        uint32_t* buffer2End = buffer2 + fPass2Size;
        const int noChangeCount = fSlidingWindow > width ? fSlidingWindow - width : 0;
        const Scan scan(fWeight, noChangeCount, buffer0, buffer0End,
                        buffer1, buffer1End, buffer2, buffer2End);
        return scan;
    }

    uint64_t fWeight;
    int      fBorder;
    int      fSlidingWindow;
    int      fPass0Size;
    int      fPass1Size;
    int      fPass2Size;
};

}  // namespace

// NB 135 is the largest sigma that will not cause a buffer full of 255 mask values to overflow
// using the Gauss filter. It also limits the size of buffers used hold intermediate values. The
// additional + 1 added to window represents adding one more leading element before subtracting the
// trailing element.
// Explanation of maximums:
//   sum0 = (window + 1) * 255
//   sum1 = (window + 1) * sum0 -> (window + 1) * (window + 1) * 255
//   sum2 = (window + 1) * sum1 -> (window + 1) * (window + 1) * (window + 1) * 255 -> window^3 * 255
//
//   The value (window + 1)^3 * 255 must fit in a uint32_t. So,
//      (window + 1)^3 * 255 < 2^32. window = 255.
//
//   window = floor(sigma * 3 * sqrt(2 * kPi) / 4)
//   For window <= 255, the largest value for sigma is 135.
SkMaskBlurFilter::SkMaskBlurFilter(double sigmaW, double sigmaH)
    : fSigmaW{SkTPin(sigmaW, 0.0, 135.0)}
    , fSigmaH{SkTPin(sigmaH, 0.0, 135.0)}
{
    SkASSERT(sigmaW >= 0);
    SkASSERT(sigmaH >= 0);
}

bool SkMaskBlurFilter::hasNoBlur() const {
    // If the sigma value is less than a certain amount, the window will be 0 which means
    // there is effectively no blur. Using Wolfram alpha to solve the equation used for
    // possibleWindow above shows that the threshold is (2 * sqrt(2/pi))/3
#if defined(SK_USE_LARGER_NO_BLUR_THRESHOLD)
    constexpr double kNoWindowSigma = 0.531923;
#else
    // However, historically we used 1/3 as the cutoff. Clients who might have pixel tests
    // that depend on this can be updated one at a time.
    constexpr double kNoWindowSigma = 1./3.;
#endif
    const bool identity = fSigmaW < kNoWindowSigma && fSigmaH <= kNoWindowSigma;
    return identity;
}

// We favor A8 masks, and if we need to work with another format, we'll convert to A8 first.
// Each of these converts width (up to 8) mask values to A8.
static void bw_to_a8(uint8_t* a8, const uint8_t* from, int width) {
    SkASSERT(0 < width && width <= 8);

    uint8_t masks = *from;
    for (int i = 0; i < width; ++i) {
        a8[i] = (masks >> (7 - i)) & 1 ? 0xFF
                                       : 0x00;
    }
}
static void lcd_to_a8(uint8_t* a8, const uint8_t* from, int width) {
    SkASSERT(0 < width && width <= 8);

    for (int i = 0; i < width; ++i) {
        const unsigned int rgb = reinterpret_cast<const uint16_t*>(from)[i];
        const unsigned int r = SkPacked16ToR32(rgb);
        const unsigned int g = SkPacked16ToG32(rgb);
        const unsigned int b = SkPacked16ToB32(rgb);
        a8[i] = static_cast<uint8_t>((r + g + b) / 3);
    }
}
static void argb32_to_a8(uint8_t* a8, const uint8_t* from, int width) {
    SkASSERT(0 < width && width <= 8);
    for (int i = 0; i < width; ++i) {
        uint32_t rgba = reinterpret_cast<const uint32_t*>(from)[i];
        a8[i] = static_cast<uint8_t>(SkGetPackedA32(rgba));
    }
}
// Converts width (1 to 8) source pixels to A8 coverage; bw_to_a8, lcd_to_a8 or argb32_to_a8.
using ToA8 = void(uint8_t* a8, const uint8_t* from, int width);

using fp88 = skvx::Vec<8, uint16_t>; // 8-wide fixed point 8.8

static fp88 load(const uint8_t* from, int width, ToA8* toA8) {
    // Our fast path is a full 8-byte load of A8.
    // So we'll conditionally handle the two slow paths using tmp:
    //    - if we have a function to convert another mask to A8, use it;
    //    - if not but we have less than 8 bytes to load, load them one at a time.
    uint8_t tmp[8] = {0,0,0,0, 0,0,0,0};
    if (toA8) {
        toA8(tmp, from, width);
        from = tmp;
    } else if (width < 8) {
        for (int i = 0; i < width; ++i) {
            tmp[i] = from[i];
        }
        from = tmp;
    }

    // Load A8 and convert to 8.8 fixed-point.
    const skvx::byte8 bytes = skvx::byte8::Load(from);
    const fp88 expanded = skvx::cast<uint16_t>(bytes);
    const fp88 fixed = expanded << 8;
    return fixed;
}

static void store(uint8_t* to, const fp88& v, int width) {
    skvx::byte8 b = skvx::cast<uint8_t>(v >> 8);
    if (width == 8) {
        b.store(to);
    } else {
        uint8_t buffer[8] = {};
        b.store(buffer);
        for (int i = 0; i < width; i++) {
            to[i] = buffer[i];
        }
    }
}

static constexpr uint16_t _____ = 0u;
static constexpr uint16_t kHalf = 0x80u;

// In all the blur_x_radius_N and blur_y_radius_N functions the gaussian values are encoded
// in 0.16 format, none of the values is greater than one. The incoming mask values are in 8.8
// format. The resulting multiply has a 8.24 format, by the mulhi truncates the lower 16 bits
// resulting in a 8.8 format.
//
// The blur_x_radius_N function below blur along a row of pixels using a kernel with radius N. This
// system is setup to minimize the number of multiplies needed.
//
// Explanation:
//    Blurring a specific mask value is given by the following equation where D_n is the resulting
// mask value and S_n is the source value. The example below is for a filter with a radius of 1
// and a width of 3 (radius == (width-1)/2). The indexes for the source and destination are
// aligned. The filter is given by G_n where n is the symmetric filter value.
//
//   D[n] = S[n-1]*G[1] + S[n]*G[0] + S[n+1]*G[1].
//
// We can start the source index at an offset relative to the destination separated by the
// radius. This results in a non-traditional restating of the above filter.
//
//  D[n] = S[n]*G[1] + S[n+1]*G[0] + S[n+2]*G[1]
//
// If we look at three specific consecutive destinations the following equations result:
//
//   D[5] = S[5]*G[1] + S[6]*G[0] + S[7]*G[1]
//   D[7] = S[6]*G[1] + S[7]*G[0] + S[8]*G[1]
//   D[8] = S[7]*G[1] + S[8]*G[0] + S[9]*G[1].
//
// In the above equations, notice that S[7] is used in all three. In particular, two values are
// used: S[7]*G[0] and S[7]*G[1]. So, S[7] is only multiplied twice, but used in D[5], D[6] and
// D[7].
//
// From the point of view of a source value we end up with the following three equations.
//
// Given S[7]:
//   D[5] += S[7]*G[1]
//   D[6] += S[7]*G[0]
//   D[7] += S[7]*G[1]
//
// In General:
//   D[n]   += S[n]*G[1]
//   D[n+1] += S[n]*G[0]
//   D[n+2] += S[n]*G[1]
//
// Now these equations can be ganged using SIMD to form:
//   D[n..n+7]   += S[n..n+7]*G[1]
//   D[n+1..n+8] += S[n..n+7]*G[0]
//   D[n+2..n+9] += S[n..n+7]*G[1]
// The next set of values becomes.
//   D[n+8..n+15]  += S[n+8..n+15]*G[1]
//   D[n+9..n+16]  += S[n+8..n+15]*G[0]
//   D[n+10..n+17] += S[n+8..n+15]*G[1]
// You can see that the D[n+8] and D[n+9] values overlap the two sets, using parts of both
// S[n..7] and S[n+8..n+15].
//
// Just one more transformation allows the code to maintain all working values in
// registers. I introduce the notation {0, S[n..n+7] * G[k]} to mean that the value where 0 is
// prepended to the array of values to form {0, S[n] * G[k], ..., S[n+7]*G[k]}.
//
//   D[n..n+7]  += S[n..n+7] * G[1]
//   D[n..n+8]  += {0, S[n..n+7] * G[0]}
//   D[n..n+9]  += {0, 0, S[n..n+7] * G[1]}
//
// Now we can encode D[n..n+7] in a single Sk8h register called d0, and D[n+8..n+15] in a
// register d8. In addition, S[0..n+7] becomes s0.
//
// The translation of the {0, S[n..n+7] * G[k]} is translated in the following way below.
//
// Sk8h v0 = s0*G[0]
// Sk8h v1 = s0*G[1]
// /* D[n..n+7]  += S[n..n+7] * G[1] */
// d0 += v1;
// /* D[n..n+8]  += {0, S[n..n+7] * G[0]} */
// d0 += {_____, v0[0], v0[1], v0[2], v0[3], v0[4], v0[5], v0[6]}
// d1 += {v0[7], _____, _____, _____, _____, _____, _____, _____}
// /* D[n..n+9]  += {0, 0, S[n..n+7] * G[1]} */
// d0 += {_____, _____, v1[0], v1[1], v1[2], v1[3], v1[4], v1[5]}
// d1 += {v1[6], v1[7], _____, _____, _____, _____, _____, _____}
// Where we rely on the compiler to generate efficient code for the {____, n, ....} notation.

static void blur_x_radius_1(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88&, const fp88&, const fp88&,
        fp88* d0, fp88* d8) {

    const fp88 v1 = mulhi(s0, g1);
    const fp88 v0 = mulhi(s0, g0);

    // D[n..n+7]  += S[n..n+7] * G[1]
    *d0 += v1;

    //D[n..n+8]  += {0, S[n..n+7] * G[0]}
    *d0 += fp88{_____, v0[0], v0[1], v0[2], v0[3], v0[4], v0[5], v0[6]};
    *d8 += fp88{v0[7], _____, _____, _____, _____, _____, _____, _____};

    // D[n..n+9]  += {0, 0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, _____, v1[0], v1[1], v1[2], v1[3], v1[4], v1[5]};
    *d8 += fp88{v1[6], v1[7], _____, _____, _____, _____, _____, _____};

}

static void blur_x_radius_2(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88&, const fp88&,
        fp88* d0, fp88* d8) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);
    const fp88 v2 = mulhi(s0, g2);

    // D[n..n+7]  += S[n..n+7] * G[2]
    *d0 += v2;

    // D[n..n+8]  += {0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, v1[0], v1[1], v1[2], v1[3], v1[4], v1[5], v1[6]};
    *d8 += fp88{v1[7], _____, _____, _____, _____, _____, _____, _____};

    // D[n..n+9]  += {0, 0, S[n..n+7] * G[0]}
    *d0 += fp88{_____, _____, v0[0], v0[1], v0[2], v0[3], v0[4], v0[5]};
    *d8 += fp88{v0[6], v0[7], _____, _____, _____, _____, _____, _____};

    // D[n..n+10]  += {0, 0, 0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, _____, _____, v1[0], v1[1], v1[2], v1[3], v1[4]};
    *d8 += fp88{v1[5], v1[6], v1[7], _____, _____, _____, _____, _____};

    // D[n..n+11]  += {0, 0, 0, 0, S[n..n+7] * G[2]}
    *d0 += fp88{_____, _____, _____, _____, v2[0], v2[1], v2[2], v2[3]};
    *d8 += fp88{v2[4], v2[5], v2[6], v2[7], _____, _____, _____, _____};
}

static void blur_x_radius_3(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88&,
        fp88* d0, fp88* d8) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);
    const fp88 v2 = mulhi(s0, g2);
    const fp88 v3 = mulhi(s0, g3);

    // D[n..n+7]  += S[n..n+7] * G[3]
    *d0 += v3;

    // D[n..n+8]  += {0, S[n..n+7] * G[2]}
    *d0 += fp88{_____, v2[0], v2[1], v2[2], v2[3], v2[4], v2[5], v2[6]};
    *d8 += fp88{v2[7], _____, _____, _____, _____, _____, _____, _____};

    // D[n..n+9]  += {0, 0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, _____, v1[0], v1[1], v1[2], v1[3], v1[4], v1[5]};
    *d8 += fp88{v1[6], v1[7], _____, _____, _____, _____, _____, _____};

    // D[n..n+10]  += {0, 0, 0, S[n..n+7] * G[0]}
    *d0 += fp88{_____, _____, _____, v0[0], v0[1], v0[2], v0[3], v0[4]};
    *d8 += fp88{v0[5], v0[6], v0[7], _____, _____, _____, _____, _____};

    // D[n..n+11]  += {0, 0, 0, 0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, _____, _____, _____, v1[0], v1[1], v1[2], v1[3]};
    *d8 += fp88{v1[4], v1[5], v1[6], v1[7], _____, _____, _____, _____};

    // D[n..n+12]  += {0, 0, 0, 0, 0, S[n..n+7] * G[2]}
    *d0 += fp88{_____, _____, _____, _____, _____, v2[0], v2[1], v2[2]};
    *d8 += fp88{v2[3], v2[4], v2[5], v2[6], v2[7], _____, _____, _____};

    // D[n..n+13]  += {0, 0, 0, 0, 0, 0, S[n..n+7] * G[3]}
    *d0 += fp88{_____, _____, _____, _____, _____, _____, v3[0], v3[1]};
    *d8 += fp88{v3[2], v3[3], v3[4], v3[5], v3[6], v3[7], _____, _____};
}

static void blur_x_radius_4(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88& g4,
        fp88* d0, fp88* d8) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);
    const fp88 v2 = mulhi(s0, g2);
    const fp88 v3 = mulhi(s0, g3);
    const fp88 v4 = mulhi(s0, g4);

    // D[n..n+7]  += S[n..n+7] * G[4]
    *d0 += v4;

    // D[n..n+8]  += {0, S[n..n+7] * G[3]}
    *d0 += fp88{_____, v3[0], v3[1], v3[2], v3[3], v3[4], v3[5], v3[6]};
    *d8 += fp88{v3[7], _____, _____, _____, _____, _____, _____, _____};

    // D[n..n+9]  += {0, 0, S[n..n+7] * G[2]}
    *d0 += fp88{_____, _____, v2[0], v2[1], v2[2], v2[3], v2[4], v2[5]};
    *d8 += fp88{v2[6], v2[7], _____, _____, _____, _____, _____, _____};

    // D[n..n+10]  += {0, 0, 0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, _____, _____, v1[0], v1[1], v1[2], v1[3], v1[4]};
    *d8 += fp88{v1[5], v1[6], v1[7], _____, _____, _____, _____, _____};

    // D[n..n+11]  += {0, 0, 0, 0, S[n..n+7] * G[0]}
    *d0 += fp88{_____, _____, _____, _____, v0[0], v0[1], v0[2], v0[3]};
    *d8 += fp88{v0[4], v0[5], v0[6], v0[7], _____, _____, _____, _____};

    // D[n..n+12]  += {0, 0, 0, 0, 0, S[n..n+7] * G[1]}
    *d0 += fp88{_____, _____, _____, _____, _____, v1[0], v1[1], v1[2]};
    *d8 += fp88{v1[3], v1[4], v1[5], v1[6], v1[7], _____, _____, _____};

    // D[n..n+13]  += {0, 0, 0, 0, 0, 0, S[n..n+7] * G[2]}
    *d0 += fp88{_____, _____, _____, _____, _____, _____, v2[0], v2[1]};
    *d8 += fp88{v2[2], v2[3], v2[4], v2[5], v2[6], v2[7], _____, _____};

    // D[n..n+14]  += {0, 0, 0, 0, 0, 0, 0, S[n..n+7] * G[3]}
    *d0 += fp88{_____, _____, _____, _____, _____, _____, _____, v3[0]};
    *d8 += fp88{v3[1], v3[2], v3[3], v3[4], v3[5], v3[6], v3[7], _____};

    // D[n..n+15]  += {0, 0, 0, 0, 0, 0, 0, 0, S[n..n+7] * G[4]}
    *d8 += v4;
}

using BlurX = void(const fp88& s0,
                   const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88& g4,
                   fp88* d0, fp88* d8);

// BlurX will only be one of the functions blur_x_radius_(1|2|3|4).
static void blur_row(
        BlurX blur,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88& g4,
        const uint8_t* src, int srcW,
              uint8_t* dst, int dstW) {
    // Clear the buffer to handle summing wider than source.
    fp88 d0(kHalf);
    fp88 d8(kHalf);

    // Go by multiples of 8 in src.
    int x = 0;
    for (; x <= srcW - 8; x += 8) {
        const fp88 source = load(src, 8, nullptr);
        blur(source, g0, g1, g2, g3, g4, &d0, &d8);

        store(dst, d0, 8);

        d0 = d8;
        d8 = fp88(kHalf);

        src += 8;
        dst += 8;
    }

    // There are src values left, but the remainder of src values is not a multiple of 8.
    int srcTail = srcW - x;
    if (srcTail > 0) {

        const fp88 source = load(src, srcTail, nullptr);
        blur(source, g0, g1, g2, g3, g4, &d0, &d8);

        int dstTail = std::min(8, dstW - x);
        store(dst, d0, dstTail);

        d0 = d8;
        dst += dstTail;
        x += dstTail;
    }

    // There are dst mask values to complete.
    int dstTail = dstW - x;
    if (dstTail > 0) {
        store(dst, d0, dstTail);
    }
}

// BlurX will only be one of the functions blur_x_radius_(1|2|3|4).
static void blur_x_rect(BlurX blur,
                        uint16_t* gauss,
                        const uint8_t* src, size_t srcStride, int srcW,
                        uint8_t* dst, size_t dstStride, int dstW, int dstH) {

    const fp88 g0(gauss[0]);
    const fp88 g1(gauss[1]);
    const fp88 g2(gauss[2]);
    const fp88 g3(gauss[3]);
    const fp88 g4(gauss[4]);

    // Blur *ALL* the rows.
    for (int y = 0; y < dstH; y++) {
        const size_t row = static_cast<size_t>(y);
        blur_row(blur, g0, g1, g2, g3, g4, src + row * srcStride, srcW,
                 dst + row * dstStride, dstW);
    }
}

static void direct_blur_x(int radius, uint16_t* gauss,
                          const uint8_t* src, size_t srcStride, int srcW,
                          uint8_t* dst, size_t dstStride, int dstW, int dstH) {

    switch (radius) {
        case 1:
            blur_x_rect(blur_x_radius_1, gauss, src, srcStride, srcW, dst, dstStride, dstW, dstH);
            break;

        case 2:
            blur_x_rect(blur_x_radius_2, gauss, src, srcStride, srcW, dst, dstStride, dstW, dstH);
            break;

        case 3:
            blur_x_rect(blur_x_radius_3, gauss, src, srcStride, srcW, dst, dstStride, dstW, dstH);
            break;

        case 4:
            blur_x_rect(blur_x_radius_4, gauss, src, srcStride, srcW, dst, dstStride, dstW, dstH);
            break;

        default:
            SK_ABORT("Unsupported internal Gaussian radius.");
    }
}

// The operations of the blur_y_radius_N functions work on a theme similar to the blur_x_radius_N
// functions, but end up being simpler because there is no complicated shift of registers. We
// start with the non-traditional form of the gaussian filter. In the following r is the value
// when added generates the next value in the column.
//
//   D[n+0r] = S[n+0r]*G[1]
//           + S[n+1r]*G[0]
//           + S[n+2r]*G[1]
//
// Expanding out in a way similar to blur_x_radius_N for specific values of n.
//
//   D[n+0r] = S[n-2r]*G[1] + S[n-1r]*G[0] + S[n+0r]*G[1]
//   D[n+1r] = S[n-1r]*G[1] + S[n+0r]*G[0] + S[n+1r]*G[1]
//   D[n+2r] = S[n+0r]*G[1] + S[n+1r]*G[0] + S[n+2r]*G[1]
//
// We can see that S[n+0r] is in all three D[] equations, but is only multiplied twice. Now we
// can look at the calculation form the point of view of a source value.
//
//   Given S[n+0r]:
//   D[n+0r] += S[n+0r]*G[1];
//   /* D[n+0r] is done and can be stored now. */
//   D[n+1r] += S[n+0r]*G[0];
//   D[n+2r]  = S[n+0r]*G[1];
//
// Remember, by induction, that D[n+0r] == S[n-2r]*G[1] + S[n-1r]*G[0] before adding in
// S[n+0r]*G[1]. So, after the addition D[n+0r] has finished calculation and can be stored. Also,
// notice that D[n+2r] is receiving its first value from S[n+0r]*G[1] and is not added in. Notice
// how values flow in the following two iterations in source.
//
//   D[n+0r] += S[n+0r]*G[1]
//   D[n+1r] += S[n+0r]*G[0]
//   D[n+2r]  = S[n+0r]*G[1]
//   /* ------- */
//   D[n+1r] += S[n+1r]*G[1]
//   D[n+2r] += S[n+1r]*G[0]
//   D[n+3r]  = S[n+1r]*G[1]
//
// Instead of using memory we can introduce temporaries d01 and d12. The update step changes
// to the following.
//
//   answer = d01 + S[n+0r]*G[1]
//   d01    = d12 + S[n+0r]*G[0]
//   d12    =       S[n+0r]*G[1]
//   return answer
//
// Finally, this can be ganged into SIMD style.
//   answer[0..7] = d01[0..7] + S[n+0r..n+0r+7]*G[1]
//   d01[0..7]    = d12[0..7] + S[n+0r..n+0r+7]*G[0]
//   d12[0..7]    =             S[n+0r..n+0r+7]*G[1]
//   return answer[0..7]
static fp88 blur_y_radius_1(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88&, const fp88&, const fp88&,
        fp88* d01, fp88* d12, fp88*, fp88*, fp88*, fp88*, fp88*, fp88*) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);

    fp88 answer = *d01 + v1;
           *d01 = *d12 + v0;
           *d12 =        v1 + kHalf;

    return answer;
}

static fp88 blur_y_radius_2(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88&, const fp88&,
        fp88* d01, fp88* d12, fp88* d23, fp88* d34, fp88*, fp88*, fp88*, fp88*) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);
    const fp88 v2 = mulhi(s0, g2);

    fp88 answer = *d01 + v2;
           *d01 = *d12 + v1;
           *d12 = *d23 + v0;
           *d23 = *d34 + v1;
           *d34 =        v2 + kHalf;

    return answer;
}

static fp88 blur_y_radius_3(
        const fp88& s0,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88&,
        fp88* d01, fp88* d12, fp88* d23, fp88* d34, fp88* d45, fp88* d56, fp88*, fp88*) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);
    const fp88 v2 = mulhi(s0, g2);
    const fp88 v3 = mulhi(s0, g3);

    fp88 answer = *d01 + v3;
           *d01 = *d12 + v2;
           *d12 = *d23 + v1;
           *d23 = *d34 + v0;
           *d34 = *d45 + v1;
           *d45 = *d56 + v2;
           *d56 =        v3 + kHalf;

    return answer;
}

static fp88 blur_y_radius_4(
    const fp88& s0,
    const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88& g4,
    fp88* d01, fp88* d12, fp88* d23, fp88* d34, fp88* d45, fp88* d56, fp88* d67, fp88* d78) {
    const fp88 v0 = mulhi(s0, g0);
    const fp88 v1 = mulhi(s0, g1);
    const fp88 v2 = mulhi(s0, g2);
    const fp88 v3 = mulhi(s0, g3);
    const fp88 v4 = mulhi(s0, g4);

    fp88 answer = *d01 + v4;
           *d01 = *d12 + v3;
           *d12 = *d23 + v2;
           *d23 = *d34 + v1;
           *d34 = *d45 + v0;
           *d45 = *d56 + v1;
           *d56 = *d67 + v2;
           *d67 = *d78 + v3;
           *d78 =        v4 + kHalf;

    return answer;
}

using BlurY = fp88(const fp88& s0,
                   const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88& g4,
                   fp88* d01, fp88* d12, fp88* d23, fp88* d34,
                   fp88* d45, fp88* d56, fp88* d67, fp88* d78);

// Store two live tail rows without constructing a strided one-past pointer.
static void store_pair(uint8_t* dst, size_t stride, int row, int width,
                       const fp88& first, const fp88& second) {
    store(dst + static_cast<size_t>(row) * stride, first, width);
    store(dst + static_cast<size_t>(row + 1) * stride, second, width);
}

// BlurY is one of the fixed radius kernels; input and output are disjoint here.
static void blur_column(
        ToA8 toA8,
        BlurY blur, int radius, int width,
        const fp88& g0, const fp88& g1, const fp88& g2, const fp88& g3, const fp88& g4,
        const uint8_t* src, size_t srcRB, int srcH,
        uint8_t* dst, size_t dstRB) {
    fp88 d01(kHalf);
    fp88 d12(kHalf);
    fp88 d23(kHalf);
    fp88 d34(kHalf);
    fp88 d45(kHalf);
    fp88 d56(kHalf);
    fp88 d67(kHalf);
    fp88 d78(kHalf);
    for (int y = 0; y < srcH; ++y) {
        const size_t row = static_cast<size_t>(y);
        const fp88 source = load(src + row * srcRB, width, toA8);
        const fp88 blurred = blur(source, g0, g1, g2, g3, g4,
                                  &d01, &d12, &d23, &d34, &d45, &d56, &d67, &d78);
        store(dst + row * dstRB, blurred, width);
    }
    if (radius >= 1) store_pair(dst, dstRB, srcH, width, d01, d12);
    if (radius >= 2) store_pair(dst, dstRB, srcH + 2, width, d23, d34);
    if (radius >= 3) store_pair(dst, dstRB, srcH + 4, width, d45, d56);
    if (radius >= 4) store_pair(dst, dstRB, srcH + 6, width, d67, d78);
}

// BlurY will be one of blur_y_radius_(1|2|3|4).
static void blur_y_rect(ToA8 toA8, const int strideOf8,
                        BlurY blur, int radius, uint16_t *gauss,
                        const uint8_t *src, size_t srcRB, int srcW, int srcH,
                        uint8_t *dst, size_t dstRB) {

    const fp88 g0(gauss[0]);
    const fp88 g1(gauss[1]);
    const fp88 g2(gauss[2]);
    const fp88 g3(gauss[3]);
    const fp88 g4(gauss[4]);

    int x = 0;
    for (; x <= srcW - 8; x += 8) {
        blur_column(toA8, blur, radius, 8,
                    g0, g1, g2, g3, g4,
                    src, srcRB, srcH,
                    dst, dstRB);
        src += strideOf8;
        dst += 8;
    }

    int xTail = srcW - x;
    if (xTail > 0) {
        blur_column(toA8, blur, radius, xTail,
                    g0, g1, g2, g3, g4,
                    src, srcRB, srcH,
                    dst, dstRB);
    }
}

static void direct_blur_y(ToA8 toA8, const int strideOf8,
                          int radius, uint16_t* gauss,
                          const uint8_t* src, size_t srcRB, int srcW, int srcH,
                          uint8_t* dst, size_t dstRB) {

    switch (radius) {
        case 1:
            blur_y_rect(toA8, strideOf8, blur_y_radius_1, 1, gauss,
                        src, srcRB, srcW, srcH,
                        dst, dstRB);
            break;

        case 2:
            blur_y_rect(toA8, strideOf8, blur_y_radius_2, 2, gauss,
                        src, srcRB, srcW, srcH,
                        dst, dstRB);
            break;

        case 3:
            blur_y_rect(toA8, strideOf8, blur_y_radius_3, 3, gauss,
                        src, srcRB, srcW, srcH,
                        dst, dstRB);
            break;

        case 4:
            blur_y_rect(toA8, strideOf8, blur_y_radius_4, 4, gauss,
                        src, srcRB, srcW, srcH,
                        dst, dstRB);
            break;

        default:
            SK_ABORT("Unsupported internal Gaussian radius.");
    }
}

// Gaussian coefficients are 0.16 fixed-point. The caller zero-initializes the
// full fixed-capacity arrays because all five vector arguments are loaded, even
// when a smaller-radius kernel ignores their high positions.
static void prepare_gauss(const SkGaussFilter& filter, uint16_t* factors) {
    int index = 0;
    for (const double coefficient : filter) {
        const double scaled = round(coefficient * (1 << 16));
        factors[index] = static_cast<uint16_t>(scaled);
        ++index;
    }
}

static SkIPoint small_blur(double sigmaX, double sigmaY, const SkMask& src, SkMaskBuilder* dst) {
    SkASSERT(sigmaX == sigmaY); // TODO
    SkASSERT(0.01 <= sigmaX && sigmaX < 2);
    SkASSERT(0.01 <= sigmaY && sigmaY < 2);

    const SkGaussFilter filterX(sigmaX);
    const SkGaussFilter filterY(sigmaY);
    const int radiusX = filterX.radius();
    const int radiusY = filterY.radius();

    SkASSERT(radiusX <= 4 && radiusY <= 4);

    uint16_t gaussFactorsX[SkGaussFilter::kGaussArrayMax] = {};
    uint16_t gaussFactorsY[SkGaussFilter::kGaussArrayMax] = {};
    prepare_gauss(filterX, gaussFactorsX);
    prepare_gauss(filterY, gaussFactorsY);

    *dst = SkMaskBuilder::PrepareDestination(radiusX, radiusY, src);
    if (src.fImage == nullptr) {
        const SkIPoint border = {SkTo<int32_t>(radiusX), SkTo<int32_t>(radiusY)};
        return border;
    }
    if ((*dst).fImage == nullptr) {
        (*dst).bounds().setEmpty();
        return {0, 0};
    }

    const int srcW = src.fBounds.width();
    const int srcH = src.fBounds.height();
    const int dstW = (*dst).fBounds.width();
    const int dstH = (*dst).fBounds.height();
    const size_t srcRB = src.fRowBytes;
    const size_t dstRB = (*dst).fRowBytes;

    //TODO: handle bluring in only one direction.

    // Blur vertically and copy to destination.
    switch (src.fFormat) {
        case SkMask::kBW_Format:
            direct_blur_y(bw_to_a8, 1,
                          radiusY, gaussFactorsY,
                          src.fImage, srcRB, srcW, srcH,
                          (*dst).image() + radiusX, dstRB);
            break;
        case SkMask::kA8_Format:
            direct_blur_y(nullptr, 8,
                          radiusY, gaussFactorsY,
                          src.fImage, srcRB, srcW, srcH,
                          (*dst).image() + radiusX, dstRB);
            break;
        case SkMask::kARGB32_Format:
            direct_blur_y(argb32_to_a8, 32,
                          radiusY, gaussFactorsY,
                          src.fImage, srcRB, srcW, srcH,
                          (*dst).image() + radiusX, dstRB);
            break;
        case SkMask::kLCD16_Format:
            direct_blur_y(lcd_to_a8, 16, radiusY, gaussFactorsY,
                          src.fImage, srcRB, srcW, srcH,
                          (*dst).image() + radiusX, dstRB);
            break;
        default:
            SK_ABORT("Unhandled format.");
    }

    // Blur horizontally in place.
    direct_blur_x(radiusX, gaussFactorsX,
                  (*dst).fImage + radiusX,  dstRB, srcW,
                  (*dst).image(),           dstRB, dstW, dstH);

    const SkIPoint border = {radiusX, radiusY};
    return border;
}

// TODO: assuming sigmaW = sigmaH. Allow different sigmas. Right now the
// API forces the sigmas to be the same.
SkIPoint SkMaskBlurFilter::blur(const SkMask& src, SkMaskBuilder* dst) const {

    if (fSigmaW < 2.0 && fSigmaH < 2.0) {
        const SkIPoint border = small_blur(fSigmaW, fSigmaH, src, dst);
        return border;
    }

    // 1024 is a place holder guess until more analysis can be done.
    SkSTArenaAlloc<1024> alloc;

    PlanGauss planW(fSigmaW);
    PlanGauss planH(fSigmaH);

    const int borderW = planW.border();
    const int borderH = planH.border();
    SkASSERT(borderH >= 0 && borderW >= 0);

    *dst = SkMaskBuilder::PrepareDestination(borderW, borderH, src);
    if (src.fImage == nullptr) {
        const SkIPoint border = {SkTo<int32_t>(borderW), SkTo<int32_t>(borderH)};
        return border;
    }
    if ((*dst).fImage == nullptr) {
        (*dst).bounds().setEmpty();
        return {0, 0};
    }

    const int srcW = src.fBounds.width();
    const int srcH = src.fBounds.height();
    const int dstW = (*dst).fBounds.width();
    const int dstH = (*dst).fBounds.height();
    SkASSERT(srcW >= 0 && srcH >= 0 && dstW >= 0 && dstH >= 0);

    const size_t bufferSize = std::max(planW.bufferSize(), planH.bufferSize());
    uint32_t* buffer = alloc.makeArrayDefault<uint32_t>(bufferSize);

    // Blur both directions.
    const int tmpW = srcH;
    const int tmpH = dstW;

    // Make sure not to overflow the multiply for the tmp buffer size.
    if (tmpH > std::numeric_limits<int>::max() / tmpW) {
        return {0, 0};
    }
    const size_t tmpCount = static_cast<size_t>(tmpW) * static_cast<size_t>(tmpH);
    uint8_t* tmp = alloc.makeArrayDefault<uint8_t>(tmpCount);

    // Blur horizontally, and transpose. Source rows are borrowed only while
    // their corresponding destination column is filled.
    const PlanGauss::Scan scanW = planW.makeBlurScan(srcW, buffer);
    switch (src.fFormat) {
        case SkMask::kBW_Format: {
            for (int y = 0; y < srcH; ++y) {
                const size_t row_offset = static_cast<size_t>(y) * src.fRowBytes;
                const uint8_t* row = src.fImage + row_offset;
                const SkMask::AlphaIter<SkMask::kBW_Format> start(row, 0);
                const SkMask::AlphaIter<SkMask::kBW_Format> end(row + srcW / 8, srcW % 8);
                uint8_t* column = tmp + y;
                scanW.blur(start, end, column, tmpW, tmpH);
            }
        } break;
        case SkMask::kA8_Format: {
            for (int y = 0; y < srcH; ++y) {
                const size_t row_offset = static_cast<size_t>(y) * src.fRowBytes;
                const uint8_t* row = src.fImage + row_offset;
                const SkMask::AlphaIter<SkMask::kA8_Format> start(row);
                const SkMask::AlphaIter<SkMask::kA8_Format> end(row + srcW);
                uint8_t* column = tmp + y;
                scanW.blur(start, end, column, tmpW, tmpH);
            }
        } break;
        case SkMask::kARGB32_Format: {
            for (int y = 0; y < srcH; ++y) {
                const size_t row_offset = static_cast<size_t>(y) * src.fRowBytes;
                const uint32_t* row = reinterpret_cast<const uint32_t*>(src.fImage + row_offset);
                const SkMask::AlphaIter<SkMask::kARGB32_Format> start(row);
                const SkMask::AlphaIter<SkMask::kARGB32_Format> end(row + srcW);
                uint8_t* column = tmp + y;
                scanW.blur(start, end, column, tmpW, tmpH);
            }
        } break;
        case SkMask::kLCD16_Format: {
            for (int y = 0; y < srcH; ++y) {
                const size_t row_offset = static_cast<size_t>(y) * src.fRowBytes;
                const uint16_t* row = reinterpret_cast<const uint16_t*>(src.fImage + row_offset);
                const SkMask::AlphaIter<SkMask::kLCD16_Format> start(row);
                const SkMask::AlphaIter<SkMask::kLCD16_Format> end(row + srcW);
                uint8_t* column = tmp + y;
                scanW.blur(start, end, column, tmpW, tmpH);
            }
        } break;
        default:
            SK_ABORT("Unhandled format.");
    }

    // Blur vertically (scan in memory order because of the transposition),
    // and transpose back to the original orientation.
    const PlanGauss::Scan scanH = planH.makeBlurScan(tmpW, buffer);
    for (int y = 0; y < tmpH; y++) {
        const uint8_t* tmpStart = tmp + static_cast<size_t>(y) * tmpW;
        uint8_t* dstStart = (*dst).image() + y;

        scanH.blur(tmpStart, tmpStart + tmpW,
                   dstStart, (*dst).fRowBytes, dstH);
    }

    const SkIPoint border = {SkTo<int32_t>(borderW), SkTo<int32_t>(borderH)};
    return border;
}
