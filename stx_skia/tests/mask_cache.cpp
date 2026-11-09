// Differential corpus for the small-sigma Gaussian filter helper and the blur mask cache.
// The filter section records every factor for a dense sigma grid below 2. The cache section
// adds rounded-rectangle and rectangle-set masks to local caches and records hits, misses,
// returned descriptors, pixel contents and byte charges, through key variations, duplicate
// replacement, purging and discarded storage. Both builds must produce identical bytes.

#include "differential_corpus.h"

#include "include/core/SkBlurTypes.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "include/private/chromium/SkDiscardableMemory.h"
#include "src/core/SkCachedData.h"
#include "src/core/SkGaussFilter.h"
#include "src/core/SkMask.h"
#include "src/core/SkMaskCache.h"
#include "src/core/SkResourceCache.h"
#include "src/core/SkTLazy.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using corpus::Output;
using corpus::append;
using corpus::append_bool;

// Records each factor and checks the shape every Lindeberg kernel below sigma 2 has:
// one to five positive factors, nonincreasing away from the center.
bool record_filter(Output& output, double sigma) {
    const SkGaussFilter filter(sigma);
    append(output, sigma);
    append(output, filter.size());
    append(output, filter.radius());
    append(output, filter.width());
    if (filter.size() < 1 || filter.size() > 5 || filter.radius() + 1 != static_cast<int>(filter.size()) ||
        filter.width() != 2 * filter.radius() + 1) {
        return false;
    }
    double previous = 2.0;
    for (const double factor : filter) {
        append(output, factor);
        if (!(factor > 0.0) || factor > previous) return false;
        previous = factor;
    }
    ++output.cases;
    return true;
}

bool filter_corpus(Output& output) {
    for (int step = 0; step < 2000; ++step) {
        const double sigma = static_cast<double>(step) / 1000.0;
        if (!record_filter(output, sigma)) return false;
    }
    const std::array<double, 6> edges{1e-12, 0.0099999, 0.5320985, 1.0 / 3.0,
                                      1.9999999, std::nextafter(2.0, 0.0)};
    for (const double sigma : edges) {
        if (!record_filter(output, sigma)) return false;
    }
    return true;
}

// Fills cached pixels with a pattern identified by seed.
void fill_pattern(SkCachedData& data, uint32_t seed) {
    uint8_t* pixels = static_cast<uint8_t*>(data.writable_data());
    for (size_t i = 0; i < data.size(); ++i) {
        pixels[i] = static_cast<uint8_t>(seed * 37u + static_cast<uint32_t>(i) * 11u + 5u);
    }
}

SkMask mask_for(const SkIRect& bounds, uint32_t seed) {
    const SkMask::Format format = seed % 3 == 0 ? SkMask::k3D_Format : SkMask::kA8_Format;
    const uint32_t rowBytes = static_cast<uint32_t>(bounds.width());
    const SkMask mask(nullptr, bounds, rowBytes, format);
    return mask;
}

// A hit must return the cache's pixels through the mask; both are recorded.
bool record_lookup(Output& output, SkCachedData* found, const SkTLazy<SkMask>& mask) {
    const sk_sp<SkCachedData> owner(found);
    append_bool(output, owner != nullptr);
    append_bool(output, mask.isValid());
    if (owner == nullptr) {
        ++output.cases;
        const bool unchanged = !mask.isValid();
        return unchanged;
    }
    if (!mask.isValid()) return false;
    const SkMask& descriptor = *mask;
    if (descriptor.fImage != (*owner).data()) return false;
    append(output, descriptor.fBounds);
    append(output, descriptor.fRowBytes);
    append(output, static_cast<int32_t>(descriptor.fFormat));
    append(output, (*owner).size());
    const std::vector<uint8_t> bytes(descriptor.fImage, descriptor.fImage + (*owner).size());
    append(output, corpus::fnv1a(bytes));
    ++output.cases;
    return true;
}

bool find_rrect(Output& output, SkResourceCache* cache, SkScalar sigma, SkBlurStyle style,
                const SkRRect& rrect) {
    SkTLazy<SkMask> mask;
    SkCachedData* found = SkMaskCache::FindAndRef(sigma, style, rrect, &mask, cache);
    const bool ok = record_lookup(output, found, mask);
    return ok;
}

bool find_rects(Output& output, SkResourceCache* cache, SkScalar sigma, SkBlurStyle style,
                SkSpan<const SkRect> rects) {
    SkTLazy<SkMask> mask;
    SkCachedData* found = SkMaskCache::FindAndRef(sigma, style, rects, &mask, cache);
    const bool ok = record_lookup(output, found, mask);
    return ok;
}

// Adds a mask whose pixels are a fresh allocation from cache. The caller's client reference
// is released after the add, so the cache's record is the only owner.
bool add_rrect(SkResourceCache& cache, SkScalar sigma, SkBlurStyle style, const SkRRect& rrect,
               uint32_t seed) {
    const SkIRect bounds = rrect.rect().roundOut().makeOutset(3, 2);
    const SkMask mask = mask_for(bounds, seed);
    const sk_sp<SkCachedData> data(cache.newCachedData(mask.computeTotalImageSize()));
    if (data == nullptr) return false;
    fill_pattern(*data, seed);
    SkMaskCache::Add(sigma, style, rrect, mask, data.get(), &cache);
    return true;
}

bool add_rects(SkResourceCache& cache, SkScalar sigma, SkBlurStyle style,
               SkSpan<const SkRect> rects, uint32_t seed) {
    const SkIRect bounds = rects[0].roundOut().makeOutset(2, 3);
    const SkMask mask = mask_for(bounds, seed);
    const sk_sp<SkCachedData> data(cache.newCachedData(mask.computeTotalImageSize()));
    if (data == nullptr) return false;
    fill_pattern(*data, seed);
    SkMaskCache::Add(sigma, style, rects, mask, data.get(), &cache);
    return true;
}

constexpr std::array<SkBlurStyle, 4> kStyles{kNormal_SkBlurStyle, kSolid_SkBlurStyle,
                                             kOuter_SkBlurStyle, kInner_SkBlurStyle};
constexpr std::array<SkScalar, 3> kSigmas{0.75f, 1.5f, 4.0f};

std::array<SkRRect, 4> sample_rrects() {
    const SkVector radii[4] = {{2, 7}, {9, 5}, {6, 6}, {0, 0}};
    SkRRect complex;
    complex.setRectRadii(SkRect::MakeLTRB(3.25f, 4.5f, 41.75f, 30), radii);
    const std::array<SkRRect, 4> rrects{SkRRect::MakeRect(SkRect::MakeLTRB(2, 3, 20, 17)),
                                        SkRRect::MakeOval(SkRect::MakeLTRB(-4.5f, 1, 18, 25.5f)),
                                        SkRRect::MakeRectXY(SkRect::MakeLTRB(0, 0, 33, 21), 5, 3),
                                        complex};
    return rrects;
}

bool rrect_corpus(Output& output) {
    SkResourceCache cache(size_t{1} << 22);
    const std::array<SkRRect, 4> rrects = sample_rrects();
    uint32_t seed = 1;
    for (const SkRRect& rrect : rrects) {
        for (const SkScalar sigma : kSigmas) {
            for (const SkBlurStyle style : kStyles) {
                if (!add_rrect(cache, sigma, style, rrect, seed)) return false;
                ++seed;
            }
        }
    }
    append(output, cache.getTotalBytesUsed());
    for (const SkRRect& rrect : rrects) {
        for (const SkScalar sigma : kSigmas) {
            for (const SkBlurStyle style : kStyles) {
                if (!find_rrect(output, &cache, sigma, style, rrect)) return false;
                // The key holds the exact sigma, style and rounded rectangle.
                const SkScalar nearby = std::nextafter(sigma, 8.0f);
                if (!find_rrect(output, &cache, nearby, style, rrect)) return false;
                SkRRect moved = rrect;
                moved.offset(1, 0);
                if (!find_rrect(output, &cache, sigma, style, moved)) return false;
                SkRRect shifted = rrect;
                shifted.offset(0, 0.5f);
                if (!find_rrect(output, &cache, sigma, style, shifted)) return false;
            }
        }
    }
    // An unpinned duplicate replaces the earlier record.
    if (!add_rrect(cache, kSigmas[1], kNormal_SkBlurStyle, rrects[2], 900)) return false;
    if (!find_rrect(output, &cache, kSigmas[1], kNormal_SkBlurStyle, rrects[2])) return false;
    append(output, cache.getTotalBytesUsed());
    cache.purgeAll();
    append(output, cache.getTotalBytesUsed());
    if (!find_rrect(output, &cache, kSigmas[0], kNormal_SkBlurStyle, rrects[0])) return false;
    return true;
}

bool rects_corpus(Output& output) {
    SkResourceCache cache(size_t{1} << 22);
    const std::array<SkRect, 2> pair{SkRect::MakeLTRB(4.25f, 6, 40.75f, 31.5f),
                                     SkRect::MakeLTRB(10, 12.5f, 30, 25)};
    const std::array<SkRect, 1> single{SkRect::MakeLTRB(-3.5f, 2.25f, 17, 19)};
    uint32_t seed = 100;
    for (const SkScalar sigma : kSigmas) {
        for (const SkBlurStyle style : kStyles) {
            if (!add_rects(cache, sigma, style, SkSpan<const SkRect>(pair.data(), 2), seed)) return false;
            ++seed;
            if (!add_rects(cache, sigma, style, SkSpan<const SkRect>(single.data(), 1), seed)) return false;
            ++seed;
        }
    }
    append(output, cache.getTotalBytesUsed());
    for (const SkScalar sigma : kSigmas) {
        for (const SkBlurStyle style : kStyles) {
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(pair.data(), 2))) return false;
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(single.data(), 1))) return false;
            // The key holds sizes, the pair's offset and the fractional phase only, so an
            // integer translation hits and a fractional one misses.
            const std::array<SkRect, 2> movedPair{pair[0].makeOffset(7, -3), pair[1].makeOffset(7, -3)};
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(movedPair.data(), 2))) return false;
            const std::array<SkRect, 1> movedSingle{single[0].makeOffset(-12, 5)};
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(movedSingle.data(), 1))) return false;
            const std::array<SkRect, 1> shiftedSingle{single[0].makeOffset(0.25f, 0)};
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(shiftedSingle.data(), 1))) return false;
            const std::array<SkRect, 2> swapped{pair[1], pair[0]};
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(swapped.data(), 2))) return false;
            const std::array<SkRect, 1> firstOnly{pair[0]};
            if (!find_rects(output, &cache, sigma, style, SkSpan<const SkRect>(firstOnly.data(), 1))) return false;
        }
    }
    cache.purgeAll();
    append(output, cache.getTotalBytesUsed());
    if (!find_rects(output, &cache, kSigmas[2], kOuter_SkBlurStyle, SkSpan<const SkRect>(pair.data(), 2))) return false;
    return true;
}

// Discardable storage the test can purge while the cache holds it unlocked.
class Discardable final : public SkDiscardableMemory {
public:
    explicit Discardable(size_t bytes) : fPixels(bytes, 0) {}
    bool lock() override {
        if (fPurged) return false;
        fLocked = true;
        return true;
    }
    void* data() override { return fPixels.data(); }
    void unlock() override { fLocked = false; }
    // Returns false if the storage was still locked.
    bool purge() {
        if (fLocked) return false;
        fPurged = true;
        return true;
    }
private:
    std::vector<uint8_t> fPixels;
    bool fLocked = true;
    bool fPurged = false;
};

// The most recent allocation; valid until its cached data is destroyed.
Discardable* gLastDiscardable = nullptr;

SkDiscardableMemory* make_discardable(size_t bytes) {
    Discardable* memory = new Discardable(bytes);
    gLastDiscardable = memory;
    return memory;
}

bool discard_corpus(Output& output) {
    SkResourceCache cache(&make_discardable);
    const SkRRect kept = SkRRect::MakeRectXY(SkRect::MakeLTRB(1, 2, 26, 15), 4, 4);
    const SkRRect purged = SkRRect::MakeOval(SkRect::MakeLTRB(5, 5, 30, 22));
    if (!add_rrect(cache, 1.5f, kNormal_SkBlurStyle, kept, 7)) return false;
    if (!add_rrect(cache, 1.5f, kNormal_SkBlurStyle, purged, 8)) return false;
    Discardable* purgedStorage = gLastDiscardable;
    append(output, cache.getTotalBytesUsed());
    // Unlocked storage that survives relocks and returns the same pixels.
    if (!find_rrect(output, &cache, 1.5f, kNormal_SkBlurStyle, kept)) return false;
    // Purged storage fails to relock: the lookup misses and the stale record is removed.
    if (purgedStorage == nullptr || !(*purgedStorage).purge()) return false;
    if (!find_rrect(output, &cache, 1.5f, kNormal_SkBlurStyle, purged)) return false;
    append(output, cache.getTotalBytesUsed());
    if (!find_rrect(output, &cache, 1.5f, kNormal_SkBlurStyle, purged)) return false;
    if (!find_rrect(output, &cache, 1.5f, kNormal_SkBlurStyle, kept)) return false;
    return true;
}

// The process-wide cache serves a null local cache.
bool global_corpus(Output& output) {
    const SkRRect rrect = SkRRect::MakeRectXY(SkRect::MakeLTRB(0.5f, 0.5f, 12, 9), 2, 2);
    const SkScalar sigma = 1.25f;
    const SkIRect bounds = SkIRect::MakeLTRB(-3, -3, 15, 12);
    const SkMask mask = mask_for(bounds, 4);
    const sk_sp<SkCachedData> data(SkResourceCache::NewCachedData(mask.computeTotalImageSize()));
    if (data == nullptr) return false;
    fill_pattern(*data, 4);
    SkMaskCache::Add(sigma, kSolid_SkBlurStyle, rrect, mask, data.get());
    SkTLazy<SkMask> found;
    const bool ok = record_lookup(output, SkMaskCache::FindAndRef(sigma, kSolid_SkBlurStyle, rrect, &found), found);
    SkResourceCache::PurgeAll();
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    if (!filter_corpus(output)) { std::puts("gauss filter failed"); return 1; }
    if (!rrect_corpus(output)) { std::puts("rrect mask cache failed"); return 1; }
    if (!rects_corpus(output)) { std::puts("rects mask cache failed"); return 1; }
    if (!discard_corpus(output)) { std::puts("discarded mask cache failed"); return 1; }
    if (!global_corpus(output)) { std::puts("global mask cache failed"); return 1; }
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("mask cache: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
