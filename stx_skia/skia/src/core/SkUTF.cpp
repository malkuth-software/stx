// Copyright 2018 Google LLC
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/core/SkUTF.h"

#include "include/private/SkTFitsIn.h"

#include <cstddef>
#include <cstdint>
#include <limits>

static constexpr bool utf16_is_high_surrogate(uint16_t c) { return (c & 0xFC00) == 0xD800; }

static constexpr bool utf16_is_low_surrogate(uint16_t c) { return (c & 0xFC00) == 0xDC00; }

template <typename T> static constexpr bool is_align2(T x) { return (x & 1) == 0; }

template <typename T> static constexpr bool is_align4(T x) { return (x & 3) == 0; }

// A Unicode scalar value: a code point that is not a surrogate.
static constexpr bool is_scalar_value(uint32_t value) {
    return value <= 0x10FFFF && (value < 0xD800 || value > 0xDFFF);
}

static constexpr bool is_continuation(uint8_t c) { return (c & 0xC0) == 0x80; }

// Decodes one well-formed UTF-8 sequence at 'p' (Unicode Table 3-7) into '*value' and returns
// its length, or returns 0 if the bytes at 'p' are not one. Besides the structure, this rejects
// what the intake decoder accepted: overlong forms, encoded surrogates and values past U+10FFFF.
// Each is excluded by narrowing the range of the second byte after E0, ED, F0 and F4.
static size_t decode_utf8(const uint8_t* p, const uint8_t* end, SkUnichar* value) {
    const size_t available = static_cast<size_t>(end - p);
    const uint8_t lead = p[0];
    if (lead < 0x80) {
        *value = lead;
        return 1;
    }
    size_t length = 0;
    uint32_t result = 0;
    uint8_t secondLow = 0x80;
    uint8_t secondHigh = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        result = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        result = lead & 0x0F;
        if (lead == 0xE0) secondLow = 0xA0;   // overlong
        if (lead == 0xED) secondHigh = 0x9F;  // surrogates
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        result = lead & 0x07;
        if (lead == 0xF0) secondLow = 0x90;   // overlong
        if (lead == 0xF4) secondHigh = 0x8F;  // past U+10FFFF
    } else {
        return 0;  // a continuation byte, C0, C1 or F5 to FF
    }
    if (available < length) return 0;
    if (p[1] < secondLow || p[1] > secondHigh) return 0;
    for (size_t index = 1; index < length; ++index) {
        if (!is_continuation(p[index])) return 0;
        result = (result << 6) | (p[index] & 0x3F);
    }
    *value = static_cast<SkUnichar>(result);
    return length;
}

////////////////////////////////////////////////////////////////////////////////

int SkUTF::CountUTF8(const char* utf8, size_t byteLength) {
    if (!utf8 && byteLength) {
        return -1;
    }
    int count = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8);
    const uint8_t* stop = p + byteLength;
    while (p < stop) {
        SkUnichar value = 0;
        const size_t length = decode_utf8(p, stop, &value);
        if (length == 0 || count == std::numeric_limits<int>::max()) {
            return -1;
        }
        p += length;
        ++count;
    }
    return count;
}

int SkUTF::CountUTF16(const uint16_t* utf16, size_t byteLength) {
    if (!utf16 || !is_align2(intptr_t(utf16)) || !is_align2(byteLength)) {
        return -1;
    }
    const uint16_t* src = utf16;
    const uint16_t* stop = src + (byteLength >> 1);
    int count = 0;
    while (src < stop) {
        unsigned c = *src++;
        if (utf16_is_low_surrogate(c)) {
            return -1;
        }
        if (utf16_is_high_surrogate(c)) {
            if (src >= stop) {
                return -1;
            }
            c = *src++;
            if (!utf16_is_low_surrogate(c)) {
                return -1;
            }
        }
        if (count == std::numeric_limits<int>::max()) {
            return -1;
        }
        count += 1;
    }
    return count;
}

int SkUTF::CountUTF32(const int32_t* utf32, size_t byteLength) {
    if (!is_align4(intptr_t(utf32)) || !is_align4(byteLength) || !SkTFitsIn<int>(byteLength >> 2)) {
        return -1;
    }
    if (!utf32 && byteLength) {
        return -1;
    }
    const uint32_t* ptr = reinterpret_cast<const uint32_t*>(utf32);
    const uint32_t* stop = ptr + (byteLength >> 2);
    while (ptr < stop) {
        if (!is_scalar_value(*ptr)) {
            return -1;
        }
        ptr += 1;
    }
    return static_cast<int>(byteLength >> 2);
}

template <typename T>
static SkUnichar next_fail(const T** ptr, const T* end) {
    *ptr = end;
    return -1;
}

SkUnichar SkUTF::NextUTF8(const char** ptr, const char* end) {
    if (!ptr || !end ) {
        return -1;
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(*ptr);
    const uint8_t* stop = reinterpret_cast<const uint8_t*>(end);
    if (!p || p >= stop) {
        return next_fail(ptr, end);
    }
    SkUnichar value = 0;
    const size_t length = decode_utf8(p, stop, &value);
    if (length == 0) {
        return next_fail(ptr, end);
    }
    *ptr = reinterpret_cast<const char*>(p + length);
    return value;
}

SkUnichar SkUTF::NextUTF8WithReplacement(const char** ptr, const char* end) {
    const SkUnichar val = SkUTF::NextUTF8(ptr, end);
    return val < 0 ? 0xFFFD : val;
}

SkUnichar SkUTF::NextUTF16(const uint16_t** ptr, const uint16_t* end) {
    if (!ptr || !end ) {
        return -1;
    }
    const uint16_t* src = *ptr;
    if (!src || src + 1 > end || !is_align2(intptr_t(src))) {
        return next_fail(ptr, end);
    }
    const uint16_t c = *src++;
    SkUnichar result = c;
    if (utf16_is_low_surrogate(c)) {
        return next_fail(ptr, end);  // srcPtr should never point at low surrogate.
    }
    if (utf16_is_high_surrogate(c)) {
        if (src + 1 > end) {
            return next_fail(ptr, end);  // Truncated string.
        }
        const uint16_t low = *src++;
        if (!utf16_is_low_surrogate(low)) {
            return next_fail(ptr, end);
        }
        /*
        [paraphrased from wikipedia]
        Take the high surrogate and subtract 0xD800, then multiply by 0x400.
        Take the low surrogate and subtract 0xDC00.  Add these two results
        together, and finally add 0x10000 to get the final decoded codepoint.

        unicode = (high - 0xD800) * 0x400 + low - 0xDC00 + 0x10000
        unicode = (high * 0x400) - (0xD800 * 0x400) + low - 0xDC00 + 0x10000
        unicode = (high << 10) - (0xD800 << 10) + low - 0xDC00 + 0x10000
        unicode = (high << 10) + low - ((0xD800 << 10) + 0xDC00 - 0x10000)
        */
        result = (result << 10) + static_cast<SkUnichar>(low) - ((0xD800 << 10) + 0xDC00 - 0x10000);
    }
    *ptr = src;
    return result;
}

SkUnichar SkUTF::NextUTF32(const int32_t** ptr, const int32_t* end) {
    if (!ptr || !end ) {
        return -1;
    }
    const int32_t* s = *ptr;
    if (!s || s + 1 > end || !is_align4(intptr_t(s))) {
        return next_fail(ptr, end);
    }
    const int32_t value = *s;
    if (!is_scalar_value(static_cast<uint32_t>(value))) {
        return next_fail(ptr, end);
    }
    *ptr = s + 1;
    return value;
}

size_t SkUTF::ToUTF8(SkUnichar uni, char utf8[SkUTF::kMaxBytesInUTF8Sequence]) {
    if (!is_scalar_value(static_cast<uint32_t>(uni))) {
        return 0;
    }
    if (uni <= 127) {
        if (utf8) {
            *utf8 = static_cast<char>(uni);
        }
        return 1;
    }
    char tmp[4] = {};
    char* p = tmp;
    size_t count = 1;
    while (uni > 0x7F >> count) {
        *p++ = static_cast<char>(0x80 | (uni & 0x3F));
        uni >>= 6;
        count += 1;
    }
    if (utf8) {
        p = tmp;
        utf8 += count;
        while (p < tmp + count - 1) {
            *--utf8 = *p++;
        }
        *--utf8 = static_cast<char>(~(0xFF >> count) | uni);
    }
    return count;
}

size_t SkUTF::ToUTF16(SkUnichar uni, uint16_t utf16[2]) {
    if (!is_scalar_value(static_cast<uint32_t>(uni))) {
        return 0;
    }
    const int extra = (uni > 0xFFFF) ? 1 : 0;
    if (utf16) {
        if (extra) {
            utf16[0] = static_cast<uint16_t>((0xD800 - 64) + (uni >> 10));
            utf16[1] = static_cast<uint16_t>(0xDC00 | (uni & 0x3FF));
        } else {
            utf16[0] = static_cast<uint16_t>(uni);
        }
    }
    return 1 + extra;
}

int SkUTF::UTF8ToUTF16(uint16_t dst[], int dstCapacity, const char src[], size_t srcByteLength) {
    if (!dst) {
        dstCapacity = 0;
    }
    if (dstCapacity < 0) {
        return -1;
    }

    int dstLength = 0;
    uint16_t* endDst = dst + dstCapacity;
    const char* endSrc = src + srcByteLength;
    while (src < endSrc) {
        const SkUnichar uni = NextUTF8(&src, endSrc);
        if (uni < 0) {
            return -1;
        }

        uint16_t utf16[2] = {};
        size_t count = ToUTF16(uni, utf16);
        const size_t room = static_cast<size_t>(std::numeric_limits<int>::max() - dstLength);
        if (count == 0 || count > room) {
            return -1;
        }
        dstLength += static_cast<int>(count);

        if (dst) {
            uint16_t* elems = utf16;
            while (dst < endDst && count > 0) {
                *dst++ = *elems++;
                count -= 1;
            }
        }
    }
    return dstLength;
}

int SkUTF::UTF16ToUTF8(char dst[], int dstCapacity, const uint16_t src[], size_t srcLength) {
    if (!dst) {
        dstCapacity = 0;
    }
    if (dstCapacity < 0) {
        return -1;
    }

    int dstLength = 0;
    const char* endDst = dst + dstCapacity;
    const uint16_t* endSrc = src + srcLength;
    while (src < endSrc) {
        const SkUnichar uni = NextUTF16(&src, endSrc);
        if (uni < 0) {
            return -1;
        }

        char utf8[SkUTF::kMaxBytesInUTF8Sequence] = {};
        size_t count = ToUTF8(uni, utf8);
        const size_t room = static_cast<size_t>(std::numeric_limits<int>::max() - dstLength);
        if (count == 0 || count > room) {
            return -1;
        }
        dstLength += static_cast<int>(count);

        if (dst) {
            const char* elems = utf8;
            while (dst < endDst && count > 0) {
                *dst++ = *elems++;
                count -= 1;
            }
        }
    }
    return dstLength;
}
