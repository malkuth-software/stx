/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSurface_Base_DEFINED
#define SkSurface_Base_DEFINED

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkRecorder.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypes.h"
#include "src/image/SkImage_Base.h"

#include <cstdint>
#include <memory>

class SkPaint;
class SkPixmap;
class SkSurfaceProps;
struct SkIRect;
struct SkImageInfo;

class SkSurface_Base : public SkSurface {
public:
    SkSurface_Base(int width, int height, const SkSurfaceProps*);
    SkSurface_Base(const SkImageInfo&, const SkSurfaceProps*);
    ~SkSurface_Base() override;

    enum class Type {
        kNull,     // intentionally associating 0 with a null canvas
        kRaster,
    };

    virtual Type type() const = 0;

    // True for surfaces instantiated by pixels in CPU memory
    bool isRasterBacked() const {
        const bool matches = type() == Type::kRaster;
        return matches;
    }
    virtual SkRecorder* onGetBaseRecorder() const;

    // Returns a newly allocated canvas whose ownership transfers to this surface.
    virtual SkCanvas* onNewCanvas() = 0;

    virtual sk_sp<SkSurface> onNewSurface(const SkImageInfo&) = 0;

    /**
     *  Allocate an SkImage that represents the current contents of the surface.
     *  This needs to be able to outlive the surface itself (if need be), and
     *  must faithfully represent the current contents, even if the surface
     *  is changed after this called (e.g. it is drawn to via its canvas).
     *
     *  If a subset is specified, the the impl must make a copy, rather than try to wait
     *  on copy-on-write.
     */
    virtual sk_sp<SkImage> onNewImageSnapshot(const SkIRect* subset = nullptr) { return nullptr; }

    virtual sk_sp<SkImage> onMakeTemporaryImage() {
        sk_sp<SkImage> image = makeImageSnapshot();
        return image;
    }

    virtual void onWritePixels(const SkPixmap&, int x, int y) = 0;

    /**
     * Default implementation does a rescale/read and then calls the callback.
     */
    virtual void onAsyncRescaleAndReadPixels(const SkImageInfo&,
                                             const SkIRect srcRect,
                                             RescaleGamma,
                                             RescaleMode,
                                             ReadPixelsCallback,
                                             ReadPixelsContext);
    // Draw a temporary owned snapshot into the supplied canvas synchronously.
    virtual void onDraw(SkCanvas*, SkScalar x, SkScalar y, const SkSamplingOptions&,const SkPaint*);

    /**
     * Called as a performance hint when the Surface is allowed to make it's contents
     * undefined.
     */
    virtual void onDiscard() {}

    /**
     *  If the surface is about to change, we call this so that our subclass
     *  can optionally fork their backend (copy-on-write) in case it was
     *  being shared with the cachedImage.
     *
     *  Returns false if the backing cannot be un-shared.
     */
    [[nodiscard]] virtual bool onCopyOnWrite(ContentChangeMode) = 0;

    /**
     *  Signal the surface to remind its backing store that it's mutable again.
     *  Called only when we _didn't_ copy-on-write; we assume the copies start mutable.
     */
    virtual void onRestoreBackingMutability() {}

    inline SkCanvas* getCachedCanvas();
    inline sk_sp<SkImage> refCachedImage();

    bool hasCachedImage() const {
        const bool present = fCachedImage != nullptr;
        return present;
    }

    // called by SkSurface to compute a new genID
    uint32_t newGenerationID();

private:
    // Canvas ownership is exclusive; getCachedCanvas returns a borrow valid only
    // while this surface lives. Snapshot images may independently outlive it.
    std::unique_ptr<SkCanvas> fCanvas = nullptr;
    sk_sp<SkImage> fCachedImage = nullptr;

    // Returns false if drawing should not take place (allocation failure).
    [[nodiscard]] bool aboutToDraw(ContentChangeMode mode);

    // Returns true if there is an outstanding image-snapshot, indicating that a call to aboutToDraw
    // would trigger a copy-on-write.
    bool outstandingImageSnapshot() const;

    friend class SkCanvas;
    friend class SkSurface;

};

SkCanvas* SkSurface_Base::getCachedCanvas() {
    if (!fCanvas) {
        SkCanvas* created = onNewCanvas();
        fCanvas.reset(created);
        if (fCanvas) {
            (*fCanvas).setSurfaceBase(this);
        }
    }
    SkCanvas* canvas = fCanvas.get();
    return canvas;
}

sk_sp<SkImage> SkSurface_Base::refCachedImage() {
    if (!fCachedImage) {
        fCachedImage = onNewImageSnapshot();
    }
    SkASSERT(!fCanvas || (*fCanvas).getSurfaceBase() == this);
    return fCachedImage;
}

static inline SkSurface_Base* asSB(SkSurface* surface) {
    SkSurface_Base* base = static_cast<SkSurface_Base*>(surface);
    return base;
}

static inline const SkSurface_Base* asConstSB(const SkSurface* surface) {
    const SkSurface_Base* base = static_cast<const SkSurface_Base*>(surface);
    return base;
}

#endif
