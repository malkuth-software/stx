#include "include/core/SkStream.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPath.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRegion.h"
#include "src/core/SkAAClip.h"
#include "src/core/SkBase64.h"
#include "src/core/SkBlitter.h"
#include "src/core/SkMask.h"
#include "src/core/SkPathRaw.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

namespace {
class MaskCapture final : public SkBlitter {
public:
    void blitH(int, int, int) override { fValid = false; }
    void blitAntiH(int, int, const SkAlpha[], const int16_t[]) override { fValid = false; }
    void blitMask(const SkMask& mask, const SkIRect& clip) override {
        if (mask.fFormat != SkMask::kA8_Format || clip.width() != 8 ||
            clip.height() != 1 || clip.fLeft != 0 || clip.fTop < 0 || clip.fTop >= 8) {
            fValid = false;
            return;
        }
        const uint8_t* row = mask.getAddr8(clip.fLeft, clip.fTop);
        for (int x = 0; x < 8; ++x) {
            fPixels[static_cast<size_t>(clip.fTop) * 8 + x] = row[x];
        }
        ++fRows;
    }

    bool valid() const { return fValid && fRows == 8; }
    const std::array<uint8_t, 64>& pixels() const { return fPixels; }

private:
    std::array<uint8_t, 64> fPixels{};
    int fRows = 0;
    bool fValid = true;
};

class AlphaCapture final : public SkBlitter {
public:
    void blitH(int, int, int) override { fValid = false; }
    void blitAntiH(int x, int y, const SkAlpha alphas[], const int16_t runs[]) override {
        if (x != 0 || y != 0 || fCalls != 0) {
            fValid = false;
            return;
        }
        ++fCalls;
        int position = 0;
        while (position < 4) {
            const int length = runs[position];
            if (length <= 0 || length > 4 - position) {
                fValid = false;
                return;
            }
            for (int index = 0; index < length; ++index) {
                fPixels[position + index] = alphas[position];
            }
            position += length;
        }
        fValid = runs[4] == 0;
    }

    bool valid() const {
        const std::array<uint8_t, 4> expected = {255, 0, 0, 255};
        return fValid && fCalls == 1 && fPixels == expected;
    }
    bool untouched() const { return fValid && fCalls == 0; }

private:
    std::array<uint8_t, 4> fPixels{};
    int fCalls = 0;
    bool fValid = true;
};

bool decode_expected(const char* source, size_t sourceLength,
                     const char* expected, size_t expectedLength) {
    size_t required = 99;
    const SkBase64::Error sized = SkBase64::Decode(source, sourceLength, nullptr, &required);
    if (sized != SkBase64::kNoError || required != expectedLength) return false;
    std::array<unsigned char, 16> output{};
    size_t written = 99;
    const SkBase64::Error decoded = SkBase64::Decode(source, sourceLength,
                                                    output.data(), &written);
    if (decoded != SkBase64::kNoError || written != expectedLength) return false;
    return std::memcmp(output.data(), expected, expectedLength) == 0;
}

bool base64_bounds() {
    const std::array<char, 4> exact = {'T', 'W', 'F', 'u'};
    if (!decode_expected(exact.data(), exact.size(), "Man", 3)) return false;
    const std::array<char, 8> doubled = {'T', 'W', 'F', 'u', 'T', 'W', 'F', 'u'};
    if (!decode_expected(doubled.data(), doubled.size(), "ManMan", 6)) return false;
    if (!decode_expected("TQ==", 4, "M", 1)) return false;
    if (!decode_expected("TWE=", 4, "Ma", 2)) return false;
    if (!decode_expected("TQ", 2, "M", 1)) return false;
    if (!decode_expected("TWE", 3, "Ma", 2)) return false;
    if (!decode_expected("TQ== \n", 6, "M", 1)) return false;
    if (!decode_expected(" \t", 2, "", 0)) return false;
    if (!decode_expected(nullptr, 0, "", 0)) return false;

    const std::array<char, 5> extra = {'A', 'A', 'A', 'A', 'A'};
    const std::array<char, 4> badPad = {'T', 'Q', '=', 'A'};
    const std::array<char, 2> badNul = {'A', 0};
    std::array<unsigned char, 16> output{};
    output.fill(0xa5);
    size_t length = 99;
    if (SkBase64::Decode(extra.data(), extra.size(), output.data(), &length) !=
        SkBase64::kPadError || length != 0) return false;
    if (SkBase64::Decode(badPad.data(), badPad.size(), output.data(), &length) !=
        SkBase64::kPadError || length != 0) return false;
    if (SkBase64::Decode(badNul.data(), badNul.size(), output.data(), &length) !=
        SkBase64::kBadCharError || length != 0) return false;
    for (const unsigned char value : output) {
        if (value != 0xa5) return false;
    }
    return true;
}

bool narrow_draw_inside_wide_clip() {
    const std::array<SkIRect, 2> rectangles = {
        SkIRect::MakeLTRB(0, 0, 1, 1),
        SkIRect::MakeLTRB(3, 0, 50000, 1),
    };
    SkRegion region;
    if (!region.setRects(rectangles.data(), 2)) return false;
    SkAAClip clip;
    if (!clip.setRegion(region) || clip.getBounds().width() != 50000) return false;

    AlphaCapture output;
    SkAAClipBlitter blitter;
    blitter.init(&output, &clip);
    blitter.blitH(0, 0, 4);
    if (!output.valid()) return false;

    const std::array<SkAlpha, 5> alphas = {255, 0, 0, 0, 0};
    const std::array<int16_t, 5> runs = {4, 0, 0, 0, 0};
    AlphaCapture antiOutput;
    SkAAClipBlitter antiBlitter;
    antiBlitter.init(&antiOutput, &clip);
    antiBlitter.blitAntiH(0, 0, alphas.data(), runs.data());
    if (!antiOutput.valid()) return false;

    const std::array<int16_t, 1> emptyRuns = {0};
    AlphaCapture emptyOutput;
    SkAAClipBlitter emptyBlitter;
    emptyBlitter.init(&emptyOutput, &clip);
    emptyBlitter.blitAntiH(0, 0, alphas.data(), emptyRuns.data());
    return emptyOutput.untouched();
}

bool aliased_path_points() {
    SkPathBuilder polygon;
    polygon.moveTo(0, 0);
    for (int index = 1; index < 32; ++index) {
        polygon.lineTo(static_cast<float>(index), static_cast<float>(index + 1));
    }
    const SkSpan<const SkPoint> polygonInput = polygon.points();
    polygon.addPolygon(polygonInput, false);
    const SkSpan<const SkPoint> polygonOutput = polygon.points();
    if (polygonOutput.size() != 64) return false;
    for (int index = 0; index < 32; ++index) {
        const SkPoint expected = polygonOutput[index];
        const SkPoint actual = polygonOutput[index + 32];
        if (actual.fX != expected.fX || actual.fY != expected.fY) return false;
    }

    SkPathBuilder polyline;
    polyline.moveTo(0, 0);
    for (int index = 1; index < 32; ++index) {
        polyline.lineTo(static_cast<float>(index), static_cast<float>(index + 1));
    }
    const SkSpan<const SkPoint> lineInput = polyline.points();
    polyline.polylineTo(lineInput);
    const SkSpan<const SkPoint> lineOutput = polyline.points();
    if (lineOutput.size() != 64) return false;
    for (int index = 0; index < 32; ++index) {
        const SkPoint expected = lineOutput[index];
        const SkPoint actual = lineOutput[index + 32];
        if (actual.fX != expected.fX || actual.fY != expected.fY) return false;
    }

    SkPathBuilder rawBuilder;
    rawBuilder.moveTo(0, 0);
    for (int index = 1; index < 32; ++index) {
        rawBuilder.lineTo(static_cast<float>(index), static_cast<float>(index + 1));
    }
    const SkPathRaw raw = {rawBuilder.points(), rawBuilder.verbs(),
        rawBuilder.conicWeights(), SkRect::MakeWH(32, 33),
        SkPathFillType::kWinding, SkPathConvexity::kUnknown, 0};
    rawBuilder.addRaw(raw, SkPathBuilder::Reserve::kGrow);
    const SkSpan<const SkPoint> rawOutput = rawBuilder.points();
    if (rawOutput.size() != 64) return false;
    for (int index = 0; index < 32; ++index) {
        const SkPoint expected = rawOutput[index];
        const SkPoint actual = rawOutput[index + 32];
        if (actual.fX != expected.fX || actual.fY != expected.fY) return false;
    }
    return true;
}

bool oversized_clip_mask() {
    SkAAClip clip;
    if (!clip.setRect(SkIRect::MakeWH(50000, 50000))) return false;
    SkMaskBuilder mask;
    clip.copyToMask(&mask);
    return mask.isEmpty() && mask.image() == nullptr && mask.rowBytes() == 0;
}

bool oversized_filtered_alpha() {
    SkBitmap source;
    const SkImageInfo info = SkImageInfo::MakeA8(50000, 50000);
    if (!source.setInfo(info)) return false;
    SkPaint paint;
    paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, 3.0F));
    SkBitmap output;
    if (source.extractAlpha(&output, &paint, nullptr)) return false;
    return output.isNull();
}

#if !defined(_WIN32)
bool clipped_large_bw_mask() {
    constexpr size_t rowBytes = 6250;
    constexpr size_t rows = 50000;
    constexpr size_t byteCount = rowBytes * rows;
    void* storage = mmap(nullptr, byteCount, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (storage == MAP_FAILED) return false;

    std::array<uint8_t, 8> compact{};
    uint8_t* largeBytes = static_cast<uint8_t*>(storage);
    for (int y = 0; y < 8; ++y) {
        compact[y] = static_cast<uint8_t>(0xA5U ^ static_cast<unsigned int>(y * 17));
        for (int x = 0; x < 8; ++x) {
            if ((compact[y] & (0x80U >> x)) != 0) {
                const int bit = x + 3;
                largeBytes[static_cast<size_t>(y) * rowBytes + (bit >> 3)] |=
                    static_cast<uint8_t>(0x80U >> (bit & 7));
            }
        }
    }

    const SkMask small(compact.data(), SkIRect::MakeWH(8, 8), 1, SkMask::kBW_Format);
    const SkMask large(largeBytes, SkIRect::MakeLTRB(-3, 0, 49997, 50000),
                       rowBytes, SkMask::kBW_Format);
    SkAAClip aaClip;
    const SkIRect visible = SkIRect::MakeWH(8, 8);
    bool valid = aaClip.setPath(SkPath::Oval(SkRect::MakeWH(8, 8)), visible, true);
    valid = valid && aaClip.getBounds() == visible && !aaClip.quickContains(visible);
    if (valid) {
        MaskCapture smallOutput;
        MaskCapture largeOutput;
        SkAAClipBlitter smallBlitter;
        SkAAClipBlitter largeBlitter;
        smallBlitter.init(&smallOutput, &aaClip);
        largeBlitter.init(&largeOutput, &aaClip);
        smallBlitter.blitMask(small, visible);
        largeBlitter.blitMask(large, visible);
        valid = smallOutput.valid() && largeOutput.valid() &&
                smallOutput.pixels() == largeOutput.pixels();
    }
    munmap(storage, byteCount);
    return valid;
}
#endif
}

int main() {
    if (!base64_bounds()) {
        std::fprintf(stderr, "Base64 bounds contract failed\n");
        return 1;
    }
    if (!narrow_draw_inside_wide_clip()) {
        std::fprintf(stderr, "narrow draw in wide AA clip failed\n");
        return 8;
    }
    if (!aliased_path_points()) {
        std::fprintf(stderr, "aliased path point contract failed\n");
        return 4;
    }
    if (!oversized_clip_mask()) {
        std::fprintf(stderr, "oversized clip mask contract failed\n");
        return 5;
    }
    if (!oversized_filtered_alpha()) {
        std::fprintf(stderr, "oversized filtered alpha contract failed\n");
        return 6;
    }
#if !defined(_WIN32)
    if (!clipped_large_bw_mask()) {
        std::fprintf(stderr, "clipped large BW mask contract failed\n");
        return 7;
    }
#endif
    return 0;
}
