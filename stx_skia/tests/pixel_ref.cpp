#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMallocPixelRef.h"
#include "include/core/SkPixelRef.h"
#include "include/private/SkIDChangeListener.h"
#include "src/core/SkPixelRefPriv.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <utility>

namespace {
struct Events {
    unsigned int notifications{0};
    unsigned int releases{0};
    unsigned int listener_destructions{0};
    bool released_before_notification{false};
};

class ChangeListener final : public SkIDChangeListener {
public:
    explicit ChangeListener(Events& events) : events_(events) {}
    ~ChangeListener() override { ++events_.listener_destructions; }
    void changed() override {
        ++events_.notifications;
        events_.released_before_notification = events_.releases != 0;
    }
private:
    Events& events_;
};

void record_release(void*, void* context) {
    Events& events = *static_cast<Events*>(context);
    ++events.releases;
}

void record_data_release(const void*, void* context) {
    Events& events = *static_cast<Events*>(context);
    ++events.releases;
}

bool allocations() {
    const SkImageInfo info = SkImageInfo::Make(3, 2, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkPixelRef> allocated = SkMallocPixelRef::MakeAllocate(info, 16);
    if (!allocated || (*allocated).rowBytes() != 16 || (*allocated).isImmutable()) return false;
    const std::byte* pixels = static_cast<const std::byte*>((*allocated).pixels());
    const size_t length = info.computeByteSize(16);
    for (size_t index = 0; index < length; ++index) {
        if (pixels[index] != std::byte{0}) return false;
    }
    if (SkMallocPixelRef::MakeAllocate(info, 8)) return false;
    allocated = SkMallocPixelRef::MakeAllocate(info, 0);
    if (!allocated || (*allocated).rowBytes() != 12) return false;

    Events events{};
    std::array<std::byte, 28> storage{};
    sk_sp<SkData> data = SkData::MakeWithProc(storage.data(), storage.size(), record_data_release, &events);
    sk_sp<SkPixelRef> shared = SkMallocPixelRef::MakeWithData(info, 16, data);
    if (!shared || !(*shared).isImmutable() || (*shared).pixels() != storage.data()) return false;
    data.reset();
    if (events.releases != 0) return false;
    shared.reset();
    if (events.releases != 1) return false;
    data = SkData::MakeZeroInitialized(27);
    if (SkMallocPixelRef::MakeWithData(info, 16, data)) return false;
#ifndef STX_SKIA_REFERENCE
    sk_sp<SkData> absent;
    if (SkMallocPixelRef::MakeWithData(info, 16, absent)) return false;
    data = SkData::MakeZeroInitialized(28);
    if (SkMallocPixelRef::MakeWithData(info, 13, data)) return false;
    sk_sp<SkData> missingBytes = SkData::MakeWithProc(nullptr, 28, nullptr, nullptr);
    if (SkMallocPixelRef::MakeWithData(info, 16, missingBytes)) return false;
#endif
    return true;
}

bool invalidation() {
    Events events{};
    std::array<std::byte, 16> pixels{};
    sk_sp<SkPixelRef> owner = SkMakePixelRefWithProc(2, 2, 8, pixels.data(), record_release, &events);
    // No generation has escaped yet; registrations are discarded without notification.
    (*owner).addGenIDChangeListener(sk_make_sp<ChangeListener>(events));
    if (events.notifications != 0 || events.listener_destructions != 1) return false;
    const uint32_t initial = (*owner).getGenerationID();
    if (initial == 0 || (*owner).getGenerationID() != initial) return false;
    (*owner).addGenIDChangeListener(sk_make_sp<ChangeListener>(events));
    (*owner).notifyPixelsChanged();
    if (events.notifications != 1 || events.listener_destructions != 2) return false;
    const uint32_t replacement = (*owner).getGenerationID();
    if (replacement == 0 || replacement == initial) return false;
    (*owner).notifyPixelsChanged();
    if (events.notifications != 1) return false;
    const uint32_t final_generation = (*owner).getGenerationID();
    if (final_generation == replacement) return false;
    (*owner).addGenIDChangeListener(sk_make_sp<ChangeListener>(events));
    owner.reset();
    if (events.notifications != 2 || events.releases != 1) return false;
    if (!events.released_before_notification) return false;
    return events.listener_destructions == 3;
}

bool listener_lifetimes() {
    Events live{};
    Events revoked{};
    {
        SkIDChangeListener::List listeners;
        sk_sp<ChangeListener> revoked_listener = sk_make_sp<ChangeListener>(revoked);
        listeners.add(revoked_listener);
        (*revoked_listener).markShouldDeregister();
        revoked_listener.reset();
        listeners.add(sk_make_sp<ChangeListener>(live));
        if (listeners.count() != 1 || revoked.listener_destructions != 1) return false;
        listeners.changed();
        if (listeners.count() != 0 || live.notifications != 1) return false;
        listeners.changed();
        if (live.notifications != 1) return false;
        listeners.add(sk_make_sp<ChangeListener>(live));
        listeners.reset();
        if (live.notifications != 1 || live.listener_destructions != 2) return false;
        listeners.add(sk_make_sp<ChangeListener>(live));
    }
    return revoked.notifications == 0 && live.notifications == 2 && live.listener_destructions == 3;
}

struct IDQuery {
    SkPixelRef* owner{nullptr};
    std::array<uint32_t, 64> generations{};
};

void query_generations(IDQuery* query) {
    SkPixelRef& owner = *(*query).owner;
    for (size_t index = 0; index < (*query).generations.size(); ++index) {
        (*query).generations[index] = owner.getGenerationID();
        owner.incrementContentId();
    }
}

bool shared_identity() {
    std::array<std::byte, 4> pixels{};
    sk_sp<SkPixelRef> owner = SkMakePixelRefWithProc(1, 1, 4, pixels.data(), nullptr, nullptr);
    std::array<IDQuery, 8> queries{};
    std::array<std::thread, 8> workers{};
    for (size_t index = 0; index < workers.size(); ++index) {
        queries[index].owner = owner.get();
        workers[index] = std::thread(query_generations, &queries[index]);
    }
    for (std::thread& worker : workers) worker.join();
    const uint32_t generation = (*owner).getGenerationID();
    for (const IDQuery& query : queries) {
        for (const uint32_t value : query.generations) {
            if (value != generation) return false;
        }
    }
    if ((*owner).getContentId() != 512 || generation == 0) return false;
    sk_sp<SkPixelRef> other = SkMakePixelRefWithProc(1, 1, 4, pixels.data(), nullptr, nullptr);
    if ((*other).getPixelStorageId() == (*owner).getPixelStorageId()) return false;
    if ((*other).getGenerationID() == generation) return false;
    return true;
}

bool bitmap_origin_contract() {
    const SkImageInfo info = SkImageInfo::Make(1, 1, kRGBA_8888_SkColorType,
                                               kPremul_SkAlphaType);
    std::array<uint32_t, 1> pixels{};
    sk_sp<SkPixelRef> owner = SkMakePixelRefWithProc(1, 1, sizeof(uint32_t),
                                                    pixels.data(), nullptr, nullptr);
    SkBitmap bitmap;
    if (!bitmap.setInfo(info, sizeof(uint32_t))) return false;
    bitmap.setPixelRef(owner, 0, 0);
    bitmap.eraseColor(SK_ColorWHITE);
    if (pixels[0] == 0) return false;

    std::array<uint32_t, 4> edgePixels{};
    sk_sp<SkPixelRef> edgeOwner = SkMakePixelRefWithProc(2, 2, 2 * sizeof(uint32_t),
                                                        edgePixels.data(), nullptr, nullptr);
    const SkImageInfo zeroWidthInfo = SkImageInfo::Make(0, 1, kRGBA_8888_SkColorType,
                                                       kPremul_SkAlphaType);
    SkBitmap zeroWidth;
    if (!zeroWidth.setInfo(zeroWidthInfo, 0)) return false;
    zeroWidth.setPixelRef(edgeOwner, 1, 1);
    const SkIPoint zeroWidthOrigin = zeroWidth.pixelRefOrigin();
    if (zeroWidthOrigin.x() != 1 || zeroWidthOrigin.y() != 1) return false;

#if !defined(SK_DEBUG) && !defined(STX_SKIA_REFERENCE)
    pixels[0] = 0;
    bitmap.setPixelRef(owner, 1, 0);
    bitmap.eraseColor(SK_ColorWHITE);
    if (pixels[0] == 0) return false;

    pixels[0] = 0;
    bitmap.setPixelRef(owner, -1, -1);
    bitmap.eraseColor(SK_ColorWHITE);
    if (pixels[0] == 0) return false;

    std::array<uint32_t, 4> grid{};
    sk_sp<SkPixelRef> larger = SkMakePixelRefWithProc(2, 2, 2 * sizeof(uint32_t),
                                                     grid.data(), nullptr, nullptr);
    bitmap.setPixelRef(larger, 9, 9);
    bitmap.eraseColor(SK_ColorWHITE);
    if (grid[0] != 0 || grid[1] != 0 || grid[2] != 0 || grid[3] == 0) return false;
    grid[3] = 0;
    bitmap.setPixelRef(larger, 1, 0);
    bitmap.eraseColor(SK_ColorWHITE);
    if (grid[0] != 0 || grid[1] == 0 || grid[2] != 0 || grid[3] != 0) return false;
    grid[1] = 0;
    bitmap.setPixelRef(larger, std::numeric_limits<int>::max(),
                       std::numeric_limits<int>::min());
    bitmap.eraseColor(SK_ColorWHITE);
    if (grid[0] != 0 || grid[1] == 0 || grid[2] != 0 || grid[3] != 0) return false;

    SkBitmap impossible;
    const SkImageInfo largerInfo = SkImageInfo::Make(2, 1, kRGBA_8888_SkColorType,
                                                    kPremul_SkAlphaType);
    if (!impossible.setInfo(largerInfo, 2 * sizeof(uint32_t))) return false;
    impossible.setPixelRef(owner, 0, 0);
    if (impossible.pixelRef() != nullptr || impossible.getPixels() != nullptr) return false;

    sk_sp<SkPixelRef> shortStride = SkMakePixelRefWithProc(2, 2, sizeof(uint32_t),
                                                          grid.data(), nullptr, nullptr);
    bitmap.setPixelRef(std::move(shortStride), 0, 0);
    if (bitmap.pixelRef() != nullptr || bitmap.getPixels() != nullptr) return false;
#endif
    return true;
}
}

int main() {
    if (!allocations() || !invalidation() || !listener_lifetimes() || !shared_identity() ||
        !bitmap_origin_contract()) return 1;
    std::puts("pixel allocation, ownership, invalidation and shared identity: PASS");
    return 0;
}
