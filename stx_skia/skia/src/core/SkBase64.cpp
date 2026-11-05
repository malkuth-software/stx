/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "src/core/SkBase64.h"

#include "include/private/SkAssert.h"

#include <cstdint>

#define DecodePad -2
#define EncodePad 64

static const char default_encode[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/=";

static const signed char decodeData[] = {
    62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, DecodePad, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6, 7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

static void write_decoded_group(const unsigned char group[4], int count,
                                unsigned char* dst, size_t offset) {
    if (dst == nullptr) return;
    dst[offset] = static_cast<unsigned char>((group[0] << 2) | (group[1] >> 4));
    if (count >= 2) {
        dst[offset + 1] = static_cast<unsigned char>((group[1] << 4) | (group[2] >> 2));
    }
    if (count == 3) {
        dst[offset + 2] = static_cast<unsigned char>((group[2] << 6) | group[3]);
    }
}

// Input and output do not overlap. Each input byte is read within srcLength.
static SkBase64::Error decode_pass(const unsigned char* src, size_t srcLength,
                                   unsigned char* dst, size_t* length) {
    unsigned char group[4] = {0, 0, 0, 0};
    int count = 0;
    int padCount = 0;
    bool paddedEnd = false;
    size_t produced = 0;

    for (size_t index = 0; index < srcLength; ++index) {
        const unsigned char value = src[index];
        if (value == 0) return SkBase64::kBadCharError;
        if (value <= ' ') continue;
        if (paddedEnd) return SkBase64::kPadError;
        if (value < '+' || value > 'z') return SkBase64::kBadCharError;
        const signed char decoded = decodeData[value - '+'];
        if (decoded == DecodePad) {
            if (count < 2 || count >= 4) return SkBase64::kPadError;
            group[count] = 0;
            ++count;
            ++padCount;
            if (count == 4) {
                const int outputCount = 3 - padCount;
                write_decoded_group(group, outputCount, dst, produced);
                produced += static_cast<size_t>(outputCount);
                count = 0;
                paddedEnd = true;
            }
            continue;
        }
        if (decoded < 0) return SkBase64::kBadCharError;
        if (padCount != 0) return SkBase64::kPadError;
        group[count] = static_cast<unsigned char>(decoded);
        ++count;
        if (count == 4) {
            write_decoded_group(group, 3, dst, produced);
            produced += 3;
            count = 0;
        }
    }

    if (padCount != 0 && !paddedEnd) return SkBase64::kPadError;
    if (count == 1) return SkBase64::kPadError;
    if (count >= 2) {
        if (count == 2) group[2] = 0;
        group[3] = 0;
        const int outputCount = count - 1;
        write_decoded_group(group, outputCount, dst, produced);
        produced += static_cast<size_t>(outputCount);
    }
    *length = produced;
    return SkBase64::kNoError;
}

SkBase64::Error SkBase64::Decode(const void* srcv, size_t srcLength, void* dstv,
                                 size_t* dstLength) {
    *dstLength = 0;
    if (srcv == nullptr && srcLength != 0) return kBadCharError;
    const unsigned char* src = static_cast<const unsigned char*>(srcv);
    size_t required = 0;
    const Error valid = decode_pass(src, srcLength, nullptr, &required);
    if (valid != kNoError) return valid;
    if (dstv != nullptr) {
        unsigned char* dst = static_cast<unsigned char*>(dstv);
        size_t written = 0;
        const Error decoded = decode_pass(src, srcLength, dst, &written);
        if (decoded != kNoError) return decoded;
        SkASSERT(written == required);
    }
    *dstLength = required;
    return kNoError;
}

size_t SkBase64::Encode(const void* srcv, size_t length, void* dstv, const char* encodeMap) {
    const unsigned char* src = static_cast<const unsigned char*>(srcv);
    unsigned char* dst = static_cast<unsigned char*>(dstv);

    const char* encode;
    if (nullptr == encodeMap) {
        encode = default_encode;
    } else {
        encode = encodeMap;
    }
    if (dst) {
        size_t remainder = length % 3;
        char unsigned const * const end = &src[length - remainder];
        while (src < end) {
            unsigned a = *src++;
            unsigned b = *src++;
            unsigned c = *src++;
            int      d = c & 0x3F;
            c = (c >> 6 | b << 2) & 0x3F;
            b = (b >> 4 | a << 4) & 0x3F;
            a = a >> 2;
            *dst++ = encode[a];
            *dst++ = encode[b];
            *dst++ = encode[c];
            *dst++ = encode[d];
        }
        if (remainder > 0) {
            int k1 = 0;
            int k2 = EncodePad;
            int a = (uint8_t) *src++;
            if (remainder == 2)
            {
                int b = *src++;
                k1 = b >> 4;
                k2 = (b << 2) & 0x3F;
            }
            *dst++ = encode[a >> 2];
            *dst++ = encode[(k1 | a << 4) & 0x3F];
            *dst++ = encode[k2];
            *dst++ = encode[EncodePad];
        }
    }
    return EncodedSize(length);
}
