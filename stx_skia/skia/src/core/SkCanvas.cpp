/*
 * Copyright 2008 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkCanvas.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkBlendMode.h"
#include "include/core/SkBlender.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImage.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRSXform.h"
#include "include/core/SkRasterHandleAllocator.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkRegion.h"
#include "include/core/SkShader.h"
#include "include/core/SkStrokeRec.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/core/SkTypes.h"
#include "include/core/SkVertices.h"
#include "include/private/SkDebug.h"
#include "include/private/SkFloatingPoint.h"
#include "include/private/SkSafe32.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTemplates.h"
#include "include/private/SkTo.h"
#include "include/utils/SkNoDrawCanvas.h"
#include "src/core/SkBlenderBase.h"
#include "src/core/SkCanvasPriv.h"
#include "src/core/SkDevice.h"
#include "src/core/SkLatticeIter.h"
#include "src/core/SkMSAN.h"
#include "src/core/SkMaskFilterBase.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkPaintPriv.h"
#include "src/core/SkSpecialImage.h"
#include "src/core/SkSurfacePriv.h"
#include "src/core/SkVerticesPriv.h"
#include "src/effects/colorfilters/SkColorFilterBase.h"
#include "src/image/SkSurface_Base.h"
#include "src/utils/SkPatchUtils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <tuple>
#include <utility>

// This is a test: static_assert with no message is a c++17 feature,
// and std::max() is constexpr only since the c++14 stdlib.
static_assert(std::max(3,4) == 4);

///////////////////////////////////////////////////////////////////////////////////////////////////

SK_MAKE_BITMASK_OPS(SkCanvas::PredrawFlags)

/*
 *  Return true if the drawing this rect would hit every pixels in the canvas.
 *
 *  Returns false if
 *  - rect does not contain the canvas' bounds
 *  - paint is not fill
 *  - paint would blur or otherwise change the coverage of the rect
 */
bool SkCanvas::wouldOverwriteEntireSurface(const SkRect* rect, const SkPaint* paint,
                                           SkEnumBitMask<PredrawFlags> flags) const {
    // Convert flags to a ShaderOverrideOpacity enum
    SkPaintPriv::ShaderOverrideOpacity overrideOpacity = SkPaintPriv::kNone_ShaderOverrideOpacity;
    if (flags & PredrawFlags::kOpaqueShaderOverride) {
        overrideOpacity = SkPaintPriv::kOpaque_ShaderOverrideOpacity;
    } else if (flags & PredrawFlags::kNonOpaqueShaderOverride) {
        overrideOpacity = SkPaintPriv::kNotOpaque_ShaderOverrideOpacity;
    }

    const SkISize size = (*this).getBaseLayerSize();
    const SkRect bounds = SkRect::MakeIWH(size.width(), size.height());

    // if we're clipped at all, we can't overwrite the entire surface
    {
        const SkDevice* root = (*this).rootDevice();
        const SkDevice* top = (*this).topDevice();
        if (root != top) {
            return false;   // we're in a saveLayer, so conservatively don't assume we'll overwrite
        }
        if (!(*root).isClipWideOpen()) {
            return false;
        }
    }

    if (rect) {
        if (!(*this).getTotalMatrix().isScaleTranslate()) {
            return false; // conservative
        }

        SkRect devRect = SkRect::MakeEmpty();
        (*this).getTotalMatrix().mapRectScaleTranslate(&devRect, *rect);
        if (!devRect.contains(bounds)) {
            return false;
        }
    }

    if (paint) {
        const SkPaint::Style paintStyle = (*paint).getStyle();
        if (!(paintStyle == SkPaint::kFill_Style ||
              paintStyle == SkPaint::kStrokeAndFill_Style)) {
            return false;
        }
        if ((*paint).getMaskFilter() || (*paint).getPathEffect()) {
            return false; // conservative
        }
    }
    const bool overwrites = SkPaintPriv::Overwrites(paint, overrideOpacity);
    return overwrites;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

bool SkCanvas::predrawNotify(bool willOverwritesEntireSurface) {
    if (fSurfaceBase) {
        if (!(*fSurfaceBase).aboutToDraw(willOverwritesEntireSurface
                                       ? SkSurface::kDiscard_ContentChangeMode
                                       : SkSurface::kRetain_ContentChangeMode)) {
            return false;
        }
    }
    return true;
}

bool SkCanvas::predrawNotify(const SkRect* rect, const SkPaint* paint,
                             SkEnumBitMask<PredrawFlags> flags) {
    if (fSurfaceBase) {
        SkSurface::ContentChangeMode mode = SkSurface::kRetain_ContentChangeMode;
        // Since willOverwriteAllPixels() may not be complete free to call, we only do so if
        // there is an outstanding snapshot, since w/o that, there will be no copy-on-write
        // and therefore we don't care which mode we're in.
        //
        if ((*fSurfaceBase).outstandingImageSnapshot()) {
            if ((*this).wouldOverwriteEntireSurface(rect, paint, flags)) {
                mode = SkSurface::kDiscard_ContentChangeMode;
            }
        }
        if (!(*fSurfaceBase).aboutToDraw(mode)) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

SkCanvas::Layer::Layer(sk_sp<SkDevice> device, const SkPaint& paint)
    : fDevice(std::move(device)), fPaint(paint) {
    SkASSERT(fDevice);
}

SkCanvas::BackImage::BackImage(sk_sp<SkSpecialImage> img, SkIPoint loc)
        : fImage(std::move(img)), fLoc(loc) {}
SkCanvas::BackImage::BackImage(const BackImage&) = default;
SkCanvas::BackImage::BackImage(BackImage&&) = default;
SkCanvas::BackImage& SkCanvas::BackImage::operator=(const BackImage&) = default;
SkCanvas::BackImage::~BackImage() = default;

SkCanvas::MCRec::MCRec(SkDevice* device) : fDevice(device) {
    SkASSERT(fDevice);
}

SkCanvas::MCRec::MCRec(const MCRec* prev) : fDevice((*prev).fDevice), fMatrix((*prev).fMatrix) {
    SkASSERT(fDevice);
}

SkCanvas::MCRec::~MCRec() = default;

void SkCanvas::MCRec::newLayer(sk_sp<SkDevice> layerDevice, const SkPaint& restorePaint) {
    SkASSERT(!fBackImage);
    fLayer = std::make_unique<Layer>(std::move(layerDevice), restorePaint);
    fDevice = (*fLayer).fDevice.get();
}

void SkCanvas::MCRec::reset(SkDevice* device) {
    SkASSERT(!fLayer);
    SkASSERT(device);
    SkASSERT(fDeferredSaveCount == 0);
    fDevice = device;
    fMatrix.setIdentity();
}

class SkCanvas::AutoUpdateQRBounds {
public:
    explicit AutoUpdateQRBounds(SkCanvas* canvas) : fCanvas(canvas) {
        // pre-condition, fQuickRejectBounds and other state should be valid before anything
        // modifies the device's clip.
        (*fCanvas).validateClip();
    }
    ~AutoUpdateQRBounds() {
        (*fCanvas).fQuickRejectBounds = (*fCanvas).computeDeviceClipBounds();
        // post-condition, we should remain valid after re-computing the bounds
        (*fCanvas).validateClip();
    }

private:
    SkCanvas* fCanvas;

    AutoUpdateQRBounds(AutoUpdateQRBounds&&) = delete;
    AutoUpdateQRBounds(const AutoUpdateQRBounds&) = delete;
    AutoUpdateQRBounds& operator=(AutoUpdateQRBounds&&) = delete;
    AutoUpdateQRBounds& operator=(const AutoUpdateQRBounds&) = delete;
};

/////////////////////////////////////////////////////////////////////////////

std::optional<PreparedDraw> SkCanvas::aboutToDraw(
        const SkPaint& paint,
        const SkRect* rawBounds,
        SkEnumBitMask<PredrawFlags> flags) {
    if (flags & PredrawFlags::kCheckForOverwrite) {
        if (!(*this).predrawNotify(rawBounds, &paint, flags)) {
            return std::nullopt;
        }
    } else {
        if (!(*this).predrawNotify()) {
            return std::nullopt;
        }
    }

    std::optional<PreparedDraw> prepared(std::in_place, paint);
    return prepared;
}

std::optional<PreparedDraw> SkCanvas::aboutToDraw(
        const SkPaint& paint,
        const SkRect* rawBounds) {
    return (*this).aboutToDraw(paint, rawBounds, PredrawFlags::kNone);
}

////////////////////////////////////////////////////////////////////////////

void SkCanvas::resetForNextPicture(const SkIRect& bounds) {
    (*this).restoreAll();
    // A restriction set at the root save level survives the restores; the reset ends it.
    fClipRestriction.reset();

    // We're peering through a lot of structs here.  Only at this scope do we know that the device
    // is a SkNoPixelsDevice.
    SkASSERT((*fRootDevice).isNoPixelsDevice());
    // The root of a canvas reset for pictures is always a no-pixels device.
    SkNoPixelsDevice* asNoPixelsDevice = static_cast<SkNoPixelsDevice*>(fRootDevice.get());
    if (!(*asNoPixelsDevice).resetForNextPicture(bounds)) {
        fRootDevice = sk_make_sp<SkNoPixelsDevice>(bounds,
                                                   (*fRootDevice).surfaceProps(),
                                                   (*fRootDevice).imageInfo().refColorSpace());
    }

    (*fMCRec).reset(fRootDevice.get());
    fQuickRejectBounds = (*this).computeDeviceClipBounds();
}

void SkCanvas::init(sk_sp<SkDevice> device) {
    if (!device) {
        device = sk_make_sp<SkNoPixelsDevice>(SkIRect::MakeEmpty(), fProps);
    }

    // From this point on, SkCanvas will always have a device
    SkASSERT(device);

    fSaveCount = 1;
    fMCRec = new (fMCStack.push_back()) MCRec(device.get());

    // The root device and the canvas should always have the same pixel geometry
    SkASSERT(fProps.pixelGeometry() == (*device).surfaceProps().pixelGeometry());

    fSurfaceBase = nullptr;
    fRootDevice = std::move(device);
    fQuickRejectBounds = (*this).computeDeviceClipBounds();
}

SkCanvas::SkCanvas() : fMCStack(sizeof(MCRec), fMCRecStorage, sizeof(fMCRecStorage)) {
    (*this).init(nullptr);
}

SkCanvas::SkCanvas(int width, int height, const SkSurfaceProps* props)
        : fMCStack(sizeof(MCRec), fMCRecStorage, sizeof(fMCRecStorage))
        , fProps(SkSurfacePropsCopyOrDefault(props)) {
    (*this).init(sk_make_sp<SkNoPixelsDevice>(
            SkIRect::MakeWH(std::max(width, 0), std::max(height, 0)), fProps));
}

SkCanvas::SkCanvas(const SkIRect& bounds)
        : fMCStack(sizeof(MCRec), fMCRecStorage, sizeof(fMCRecStorage)) {
    const SkIRect r = bounds.isEmpty() ? SkIRect::MakeEmpty() : bounds;
    (*this).init(sk_make_sp<SkNoPixelsDevice>(r, fProps));
}

SkCanvas::SkCanvas(sk_sp<SkDevice> device)
        : fMCStack(sizeof(MCRec), fMCRecStorage, sizeof(fMCRecStorage))
        , fProps((*device).surfaceProps()) {
    (*this).init(std::move(device));
}

SkCanvas::~SkCanvas() {
    // Mark all pending layers to be discarded during restore (rather than drawn)
    // The deque holds MCRec objects constructed in place by internalSave().
    SkDeque::Iter iter(fMCStack, SkDeque::Iter::kFront_IterStart);
    while (true) {
        MCRec* rec = static_cast<MCRec*>(iter.next());
        if (!rec) {
            break;
        }
        if ((*rec).fLayer) {
            (*(*rec).fLayer).fDiscard = true;
        }
    }

    // free up the contents of our deque
    (*this).restoreAll();    // restore everything but the base state
    (*this).internalRestore();    // restore the last, since we're going away
}

SkSurface* SkCanvas::getSurface() const {
    return fSurfaceBase;
}

SkISize SkCanvas::getBaseLayerSize() const {
    return (*(*this).rootDevice()).imageInfo().dimensions();
}

SkDevice* SkCanvas::topDevice() const {
    SkASSERT((*fMCRec).fDevice);
    return (*fMCRec).fDevice;
}

bool SkCanvas::readPixels(const SkPixmap& pm, int x, int y) {
    if (!pm.addr()) {
        return false;
    }
    const bool read = (*(*this).rootDevice()).readPixels(pm, x, y);
    return read;
}

bool SkCanvas::readPixels(const SkImageInfo& dstInfo, void* dstP, size_t rowBytes, int x, int y) {
    return (*this).readPixels({ dstInfo, dstP, rowBytes}, x, y);
}

bool SkCanvas::readPixels(const SkBitmap& bm, int x, int y) {
    const std::optional<SkPixmap> pm = bm.peekPixels();
    if (!pm) {
        return false;
    }
    const bool read = (*this).readPixels(*pm, x, y);
    return read;
}

bool SkCanvas::writePixels(const SkBitmap& bitmap, int x, int y) {
    const std::optional<SkPixmap> pm = bitmap.peekPixels();
    if (pm) {
        return (*this).writePixels((*pm).info(), (*pm).addr(), (*pm).rowBytes(), x, y);
    }
    return false;
}

bool SkCanvas::writePixels(const SkImageInfo& srcInfo, const void* pixels, size_t rowBytes,
                           int x, int y) {
    SkDevice* device = (*this).rootDevice();

    // This check gives us an early out and prevents generation ID churn on the surface.
    // This is purely optional: it is a subset of the checks performed by SkWritePixelsRec.
    SkIRect srcRect = SkIRect::MakeXYWH(x, y, srcInfo.width(), srcInfo.height());
    if (!srcRect.intersect({0, 0, (*device).width(), (*device).height()})) {
        return false;
    }

    // Tell our owning surface to bump its generation ID.
    const bool completeOverwrite = srcRect.size() == (*device).imageInfo().dimensions();
    if (!(*this).predrawNotify(completeOverwrite)) {
        return false;
    }

    // This can still fail, most notably in the case of a invalid color type or alpha type
    // conversion.  We could pull those checks into this function and avoid the unnecessary
    // generation ID bump.  But then we would be performing those checks twice, since they
    // are also necessary at the bitmap/pixmap entry points.
    return (*device).writePixels({srcInfo, pixels, rowBytes}, x, y);
}

//////////////////////////////////////////////////////////////////////////////

void SkCanvas::checkForDeferredSave() {
    if ((*fMCRec).fDeferredSaveCount > 0) {
        (*this).doSave();
    }
}

int SkCanvas::getSaveCount() const {
#ifdef SK_DEBUG
    int count = 0;
    SkDeque::Iter iter(fMCStack, SkDeque::Iter::kFront_IterStart);
    while (true) {
        const MCRec* rec = static_cast<const MCRec*>(iter.next());
        if (!rec) {
            break;
        }
        count += 1 + (*rec).fDeferredSaveCount;
    }
    SkASSERT(count == fSaveCount);
#endif
    return fSaveCount;
}

int SkCanvas::save() {
    fSaveCount += 1;
    (*fMCRec).fDeferredSaveCount += 1;
    return (*this).getSaveCount() - 1;  // return our prev value
}

void SkCanvas::doSave() {
    (*this).willSave();

    SkASSERT((*fMCRec).fDeferredSaveCount > 0);
    (*fMCRec).fDeferredSaveCount -= 1;
    (*this).internalSave();
}

void SkCanvas::restore() {
    if ((*fMCRec).fDeferredSaveCount > 0) {
        SkASSERT(fSaveCount > 1);
        fSaveCount -= 1;
        (*fMCRec).fDeferredSaveCount -= 1;
    } else {
        // An unbalanced restore is a caller defect; the base state is never popped.
        SkASSERT_RELEASE(fMCStack.count() > 1);
        (*this).willRestore();
        SkASSERT(fSaveCount > 1);
        fSaveCount -= 1;
        (*this).internalRestore();
        (*this).didRestore();
    }
}

void SkCanvas::restoreToCount(int count) {
    const int current = (*this).getSaveCount();
    SkASSERT_RELEASE(count >= 1 && count <= current);
    const int restores = current - count;
    for (int index = 0; index < restores; ++index) {
        (*this).restore();
    }
}

void SkCanvas::restoreAll() {
    (*this).restoreToCount(1);
}

void SkCanvas::internalSave() {
    fMCRec = new (fMCStack.push_back()) MCRec(fMCRec);

    (*(*this).topDevice()).pushClipStack();
}

int SkCanvas::saveLayer(const SkRect* bounds, const SkPaint* paint) {
    return (*this).saveLayer(SaveLayerRec(bounds, paint, 0));
}

int SkCanvas::saveLayer(const SaveLayerRec& rec) {
    if (rec.fPaint && (*this).nothingToDraw(*rec.fPaint)) {
        // no need for the layer (or any of the draws until the matching restore()
        (*this).save();
        (*this).clipRect({0,0,0,0});
    } else {
        const SaveLayerStrategy strategy = (*this).getSaveLayerStrategy(rec);
        fSaveCount += 1;
        (*this).internalSaveLayer(rec, strategy);
    }
    return (*this).getSaveCount() - 1;
}

int SkCanvas::only_axis_aligned_saveBehind(const SkRect* bounds) {
    if (bounds && !(*this).getLocalClipBounds().intersects(*bounds)) {
        // Assuming clips never expand, if the request bounds is outside of the current clip
        // there is no need to copy/restore the area, so just devolve back to a regular save.
        (*this).save();
    } else {
        const bool doTheWork = (*this).onDoSaveBehind(bounds);
        fSaveCount += 1;
        (*this).internalSave();
        if (doTheWork) {
            (*this).internalSaveBehind(bounds);
        }
    }
    return (*this).getSaveCount() - 1;
}

// Preserve the established layer precision for low-alpha source formats.
static SkColorType layer_color_type(const SkColorInfo& info) {
    if (info.bytesPerPixel() <= 4 && info.colorType() != kRGBA_8888_SkColorType &&
        info.colorType() != kBGRA_8888_SkColorType) {
        return kN32_SkColorType;
    }
    return info.colorType();
}

void SkCanvas::internalSaveLayer(const SaveLayerRec& rec, SaveLayerStrategy strategy) {
    (*this).internalSave();
    if ((*this).isClipEmpty()) {
        return;
    }

    SkPaint restorePaint;
    if (rec.fPaint != nullptr) {
        restorePaint = *rec.fPaint;
    }
    restorePaint.setStyle(SkPaint::kFill_Style);
    restorePaint.setPathEffect(nullptr);
    restorePaint.setMaskFilter(nullptr);
    restorePaint.setAntiAlias(true);

    SkDevice& parent = *(*this).topDevice();
    const SkM44 matrix = parent.localToDevice44();
    SkM44 inverse;
    SkIRect bounds = parent.devClipBounds();
    bool valid = matrix.isFinite() && matrix.invert(&inverse);
    const SkColorFilter* colorFilter = restorePaint.getColorFilter();
    const SkBlender* blender = restorePaint.getBlender();
    const bool initialized = (rec.fSaveLayerFlags & kInitWithPrevious_SaveLayerFlag) != 0;
    // Effects that change transparent black, and initialized layers drawn back with any
    // effect, need the whole clip rather than the requested bounds.
    const bool changesTransparent =
            (colorFilter != nullptr && (*as_CFB(colorFilter)).affectsTransparentBlack()) ||
            (blender != nullptr && (*as_BB(blender)).affectsTransparentBlack());
    const bool blendsInitialContents =
            initialized &&
            (colorFilter != nullptr || blender != nullptr || restorePaint.getAlphaf() < 1.f);
    const bool needsFullClip = changesTransparent || blendsInitialContents;
    if (valid && rec.fBounds != nullptr && !needsFullClip && (*rec.fBounds).isEmpty()) {
        valid = false;
    }
    if (valid && rec.fBounds != nullptr && !needsFullClip) {
        const SkMatrix plane = matrix.asM33();
        const SkRect mapped = plane.mapRect(*rec.fBounds);
        // Preserve the original layer-bound tolerance at near-integral edges.
        // Floating transform error must not create an extra row or column.
        const SkRect adjusted = mapped.makeInset(1e-3f, 1e-3f);
        const SkIRect content = adjusted.roundOut();
        valid = bounds.intersect(content);
    }
    if (!valid || bounds.isEmpty()) {
        AutoUpdateQRBounds update(this);
        parent.clipRect(SkRect::MakeEmpty(), SkClipOp::kIntersect, false);
        return;
    }

    sk_sp<SkDevice> device;
    if (strategy == kFullLayer_SaveLayerStrategy) {
        const SkColorType colorType = (rec.fSaveLayerFlags & kF16ColorType) != 0
            ? kRGBA_F16_SkColorType : layer_color_type(parent.imageInfo().colorInfo());
        sk_sp<SkColorSpace> colorSpace = parent.imageInfo().refColorSpace();
        if (rec.fColorSpace != nullptr) {
            colorSpace = sk_ref_sp(rec.fColorSpace);
        }
        const SkImageInfo info = SkImageInfo::Make(bounds.width(), bounds.height(), colorType,
                                                 kPremul_SkAlphaType, std::move(colorSpace));
        const SkPixelGeometry geometry = (rec.fSaveLayerFlags & kPreserveLCDText_SaveLayerFlag) != 0
            ? fProps.pixelGeometry() : kUnknown_SkPixelGeometry;
        const SkDevice::CreateInfo createInfo(info, geometry, fAllocator.get());
        device = parent.createDevice(createInfo, rec.fPaint);
    }
    bool copyPrevious = initialized;
    if (!device) {
        const SkIRect localBounds = SkIRect::MakeWH(bounds.width(), bounds.height());
        device = sk_make_sp<SkNoPixelsDevice>(localBounds, fProps,
                                              (*this).imageInfo().refColorSpace());
        copyPrevious = false;
    }
    (*device).setDeviceCoordinateSystem(parent.deviceToGlobal(), parent.globalToDevice(), matrix,
                                       bounds.left(), bounds.top());
    if (copyPrevious) {
        const SkPaint copyPaint;
        const SkSamplingOptions sampling;
        (*device).drawDevice(&parent, sampling, copyPaint);
    }
    (*fMCRec).newLayer(std::move(device), restorePaint);
    fQuickRejectBounds = (*this).computeDeviceClipBounds();
}

int SkCanvas::saveLayerAlphaf(const SkRect* bounds, float alpha) {
    if (alpha >= 1.0f) {
        return (*this).saveLayer(bounds, nullptr);
    } else {
        SkPaint tmpPaint;
        tmpPaint.setAlphaf(alpha);
        return (*this).saveLayer(bounds, &tmpPaint);
    }
}

void SkCanvas::internalSaveBehind(const SkRect* localBounds) {
    SkDevice* device = (*this).topDevice();

    // Map the local bounds into the top device's coordinate space (this is not
    // necessarily the full global CTM transform).
    SkIRect devBounds = SkIRect::MakeEmpty();
    if (localBounds) {
        const SkRect tmp = (*device).localToDevice().mapRect(*localBounds);
        if (!devBounds.intersect(tmp.round(), (*device).devClipBounds())) {
            devBounds.setEmpty();
        }
    } else {
        devBounds = (*device).devClipBounds();
    }
    if (devBounds.isEmpty()) {
        return;
    }

    // This is getting the special image from the current device, which is then drawn into (both by
    // a client, and the drawClippedToSaveBehind below). Since this is not saving a layer, with its
    // own device, we need to explicitly copy the back image contents so that its original content
    // is available when we splat it back later during restore.
    sk_sp<SkSpecialImage> backImage = (*device).snapSpecial(devBounds, /* forceCopy= */ true);
    if (!backImage) {
        return;
    }

    // we really need the save, so we can wack the fMCRec
    (*this).checkForDeferredSave();

    (*fMCRec).fBackImage =
            std::make_unique<BackImage>(BackImage{std::move(backImage), devBounds.topLeft()});

    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kClear);
    (*this).drawClippedToSaveBehind(paint);
}

void SkCanvas::internalRestore() {
    SkASSERT(!fMCStack.empty());

    // now detach these from fMCRec so we can pop(). Gets freed after its drawn
    std::unique_ptr<Layer> layer = std::move((*fMCRec).fLayer);
    std::unique_ptr<BackImage> backImage = std::move((*fMCRec).fBackImage);

    // now do the normal restore()
    (*fMCRec).~MCRec();       // balanced in save()
    fMCStack.pop_back();
    fMCRec = static_cast<MCRec*>(fMCStack.back());

    if (!fMCRec) {
        // This was the last record, restored during the destruction of the SkCanvas
        return;
    }

    (*(*this).topDevice()).popClipStack();
    (*(*this).topDevice()).setGlobalCTM((*fMCRec).fMatrix);

    if (backImage) {
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kDstOver);
        (*(*this).topDevice()).drawSpecial((*backImage).fImage.get(),
                                       SkMatrix::Translate((*backImage).fLoc),
                                       SkSamplingOptions(),
                                       paint);
    }

    // Draw the layer's device contents into the now-current older device. We can't call public
    // draw functions since we don't want to record them.
    if (layer && !(*(*layer).fDevice).isNoPixelsDevice() && !(*layer).fDiscard) {
        (*(*layer).fDevice).setImmutable();

        // Don't go through PreparedDraw since device draws are so closely tied to
        // internalSaveLayer and internalRestore.
        if ((*this).predrawNotify()) {
            SkDevice* dstDev = (*this).topDevice();
            const SkSamplingOptions sampling;
            (*dstDev).drawDevice((*layer).fDevice.get(), sampling, (*layer).fPaint);
        }
    }

    // Reset the clip restriction if the restore went past the save point that had added it.
    if (fClipRestriction.has_value() && (*this).getSaveCount() < (*fClipRestriction).fSaveCount) {
        fClipRestriction.reset();
    }
    // Update the quick-reject bounds in case the restore changed the top device or the
    // removed save record had included modifications to the clip stack.
    fQuickRejectBounds = (*this).computeDeviceClipBounds();
    (*this).validateClip();
}

sk_sp<SkSurface> SkCanvas::makeSurface(const SkImageInfo& info, const SkSurfaceProps* props) {
    const SkSurfaceProps& surfaceProps = props ? *props : fProps;
    sk_sp<SkSurface> surface = (*this).onNewSurface(info, surfaceProps);
    return surface;
}

sk_sp<SkSurface> SkCanvas::onNewSurface(const SkImageInfo& info, const SkSurfaceProps& props) {
    return (*(*this).rootDevice()).makeSurface(info, props);
}

SkImageInfo SkCanvas::imageInfo() const {
    return (*this).onImageInfo();
}

SkImageInfo SkCanvas::onImageInfo() const {
    return (*(*this).rootDevice()).imageInfo();
}

bool SkCanvas::getProps(SkSurfaceProps* props) const {
    return (*this).onGetProps(props, /*top=*/false);
}

SkSurfaceProps SkCanvas::getBaseProps() const {
    SkSurfaceProps props;
    (*this).onGetProps(&props, /*top=*/false);
    return props;
}

SkSurfaceProps SkCanvas::getTopProps() const {
    SkSurfaceProps props;
    (*this).onGetProps(&props, /*top=*/true);
    return props;
}

bool SkCanvas::onGetProps(SkSurfaceProps* props, bool top) const {
    if (props) {
        *props = top ? (*(*this).topDevice()).surfaceProps() : fProps;
    }
    return true;
}

std::optional<SkPixmap> SkCanvas::peekPixels() {
    std::optional<SkPixmap> pixels = (*this).onPeekPixels();
    return pixels;
}

std::optional<SkPixmap> SkCanvas::onPeekPixels() {
    std::optional<SkPixmap> pixels = (*(*this).rootDevice()).peekPixels();
    return pixels;
}

void* SkCanvas::accessTopLayerPixels(SkImageInfo* info, size_t* rowBytes, SkIPoint* origin) {
    SkPixmap pmap;
    if (!(*this).onAccessTopLayerPixels(&pmap)) {
        return nullptr;
    }
    if (info) {
        *info = pmap.info();
    }
    if (rowBytes) {
        *rowBytes = pmap.rowBytes();
    }
    if (origin) {
        // If the caller requested the origin, they presumably are expecting the returned pixels to
        // be axis-aligned with the root canvas. If the top level device isn't axis aligned, that's
        // not the case. Until we update accessTopLayerPixels() to accept a coord space matrix
        // instead of an origin, just don't expose the pixels in that case. Note that this means
        // that layers with complex coordinate spaces can still report their pixels if the caller
        // does not ask for the origin (e.g. just to dump its output to a file, etc).
        if ((*(*this).topDevice()).isPixelAlignedToGlobal()) {
            *origin = (*(*this).topDevice()).getOrigin();
        } else {
            return nullptr;
        }
    }
    return pmap.writable_addr();
}

bool SkCanvas::onAccessTopLayerPixels(SkPixmap* pmap) {
    return (*(*this).topDevice()).accessPixels(pmap);
}

/////////////////////////////////////////////////////////////////////////////

void SkCanvas::translate(SkScalar dx, SkScalar dy) {
    if (dx || dy) {
        (*this).checkForDeferredSave();
        (*fMCRec).fMatrix.preTranslate(dx, dy);

        (*(*this).topDevice()).setGlobalCTM((*fMCRec).fMatrix);

        (*this).didTranslate(dx,dy);
    }
}

void SkCanvas::scale(SkScalar sx, SkScalar sy) {
    if (sx != 1 || sy != 1) {
        (*this).checkForDeferredSave();
        (*fMCRec).fMatrix.preScale(sx, sy);

        (*(*this).topDevice()).setGlobalCTM((*fMCRec).fMatrix);

        (*this).didScale(sx, sy);
    }
}

void SkCanvas::rotate(SkScalar degrees) {
    SkMatrix m;
    m.setRotate(degrees);
    (*this).concat(m);
}

void SkCanvas::rotate(SkScalar degrees, SkScalar px, SkScalar py) {
    SkMatrix m;
    m.setRotate(degrees, px, py);
    (*this).concat(m);
}

void SkCanvas::skew(SkScalar sx, SkScalar sy) {
    SkMatrix m;
    m.setSkew(sx, sy);
    (*this).concat(m);
}

void SkCanvas::concat(const SkMatrix& matrix) {
    if (matrix.isIdentity()) {
        return;
    }
    (*this).concat(SkM44(matrix));
}

void SkCanvas::internalConcat44(const SkM44& m) {
    (*this).checkForDeferredSave();

    (*fMCRec).fMatrix.preConcat(m);

    (*(*this).topDevice()).setGlobalCTM((*fMCRec).fMatrix);
}

void SkCanvas::concat(const SkM44& m) {
    (*this).internalConcat44(m);
    // notify subclasses
    (*this).didConcat44(m);
}

void SkCanvas::internalSetMatrix(const SkM44& m) {
    (*fMCRec).fMatrix = m;

    (*(*this).topDevice()).setGlobalCTM((*fMCRec).fMatrix);
}

void SkCanvas::setMatrix(const SkMatrix& matrix) {
    (*this).setMatrix(SkM44(matrix));
}

void SkCanvas::setMatrix(const SkM44& m) {
    (*this).checkForDeferredSave();
    (*this).internalSetMatrix(m);
    (*this).didSetM44(m);
}

void SkCanvas::resetMatrix() {
    (*this).setMatrix(SkM44());
}

//////////////////////////////////////////////////////////////////////////////

void SkCanvas::clipRect(const SkRect& rect, SkClipOp op, bool doAA) {
    if (!rect.isFinite()) {
        return;
    }
    (*this).checkForDeferredSave();
    const ClipEdgeStyle edgeStyle = doAA ? kSoft_ClipEdgeStyle : kHard_ClipEdgeStyle;
    (*this).onClipRect(rect.makeSorted(), op, edgeStyle);
}

void SkCanvas::onClipRect(const SkRect& rect, SkClipOp op, ClipEdgeStyle edgeStyle) {
    SkASSERT(rect.isSorted());
    const bool isAA = kSoft_ClipEdgeStyle == edgeStyle;

    AutoUpdateQRBounds aqr(this);
    (*(*this).topDevice()).clipRect(rect, op, isAA);
}

void SkCanvas::androidFramework_setDeviceClipRestriction(const SkIRect& rect) {
    // The device clip restriction is a surface-space rectangular intersection that cannot be
    // drawn outside of. The rectangle is remembered so that subsequent resetClip calls still
    // respect the restriction. Other than clip resetting, all clip operations restrict the set
    // of renderable pixels, so once set, the restriction will be respected until the canvas
    // save stack is restored past the point this function was invoked. Unfortunately, the current
    // implementation relies on the clip stack of the underyling SkDevices, which leads to some
    // awkward behavioral interactions (see skbug.com/40043342).
    //
    // Namely, a canvas restore() could undo the clip restriction's rect, and if
    // setDeviceClipRestriction were called at a nested save level, there's no way to undo just the
    // prior restriction and re-apply the new one. It also only makes sense to apply to the base
    // device; any other device for a saved layer will be clipped back to the base device during its
    // matched restore. As such, we:
    // - Remember the save count that added the clip restriction and end the restriction when
    //   we've restored past that point to keep our state in sync with the device's clip stack.
    // - Stop unless we're on the base device: a restriction set in a layer would be escaped by a
    //   clip reset in that layer and never reach the base device.
    // - Stop if a restriction is already active: it cannot be undone separately, and ignoring
    //   the new one would let drawing escape it.
    // - Historically, the empty rect would reset the clip restriction but it only could do so
    //   partially since the device's clips wasn't adjusted. Resetting is now handled
    //   automatically via SkCanvas::restore(), so empty input rects are skipped.
    SkASSERT_RELEASE((*this).topDevice() == (*this).rootDevice());
    SkASSERT_RELEASE(!fClipRestriction.has_value());

    if (!rect.isEmpty()) {
        fClipRestriction = ClipRestriction{rect, (*this).getSaveCount()};

        // A non-empty clip restriction immediately applies an intersection op (ignoring the ctm).
        // so we have to resolve the save.
        (*this).checkForDeferredSave();
        AutoUpdateQRBounds aqr(this);
        // Use clipRegion() since that operates in canvas-space, whereas clipRect() would apply the
        // device's current transform first.
        (*(*this).topDevice()).clipRegion(SkRegion(rect), SkClipOp::kIntersect);
    }
}

void SkCanvas::internal_private_resetClip() {
    (*this).checkForDeferredSave();
    (*this).onResetClip();
}

void SkCanvas::onResetClip() {
    SkIRect deviceRestriction = (*(*this).topDevice()).imageInfo().bounds();
    if (fClipRestriction.has_value() && (*this).topDevice() == (*this).rootDevice()) {
        // Respect the device clip restriction when resetting the clip if we're on the base device.
        // If we're not on the base device, then the "reset" applies to the top device's clip stack,
        // and the clip restriction will be respected automatically during a restore of the layer.
        if (!deviceRestriction.intersect((*fClipRestriction).fRect)) {
            deviceRestriction = SkIRect::MakeEmpty();
        }
    }

    AutoUpdateQRBounds aqr(this);
    (*(*this).topDevice()).replaceClip(deviceRestriction);
}

void SkCanvas::clipRRect(const SkRRect& rrect, SkClipOp op, bool doAA) {
    (*this).checkForDeferredSave();
    const ClipEdgeStyle edgeStyle = doAA ? kSoft_ClipEdgeStyle : kHard_ClipEdgeStyle;
    if (rrect.isRect()) {
        (*this).onClipRect(rrect.getBounds(), op, edgeStyle);
    } else {
        (*this).onClipRRect(rrect, op, edgeStyle);
    }
}

void SkCanvas::onClipRRect(const SkRRect& rrect, SkClipOp op, ClipEdgeStyle edgeStyle) {
    const bool isAA = kSoft_ClipEdgeStyle == edgeStyle;

    AutoUpdateQRBounds aqr(this);
    (*(*this).topDevice()).clipRRect(rrect, op, isAA);
}

void SkCanvas::clipPath(const SkPath& path, SkClipOp op, bool doAA) {
    (*this).checkForDeferredSave();
    const ClipEdgeStyle edgeStyle = doAA ? kSoft_ClipEdgeStyle : kHard_ClipEdgeStyle;

    if (!path.isInverseFillType() && (*fMCRec).fMatrix.asM33().rectStaysRect()) {
        SkRect r = SkRect::MakeEmpty();
        if (path.isRect(&r)) {
            (*this).onClipRect(r, op, edgeStyle);
            return;
        }
        SkRRect rrect;
        if (path.isOval(&r)) {
            rrect.setOval(r);
            (*this).onClipRRect(rrect, op, edgeStyle);
            return;
        }
        if (path.isRRect(&rrect)) {
            (*this).onClipRRect(rrect, op, edgeStyle);
            return;
        }
    }

    (*this).onClipPath(path, op, edgeStyle);
}

void SkCanvas::onClipPath(const SkPath& path, SkClipOp op, ClipEdgeStyle edgeStyle) {
    const bool isAA = kSoft_ClipEdgeStyle == edgeStyle;

    AutoUpdateQRBounds aqr(this);
    (*(*this).topDevice()).clipPath(path, op, isAA);
}

void SkCanvas::clipShader(sk_sp<SkShader> sh, SkClipOp op) {
    if (sh) {
        if ((*sh).isOpaque()) {
            if (op == SkClipOp::kIntersect) {
                // we don't occlude anything, so skip this call
            } else {
                SkASSERT(op == SkClipOp::kDifference);
                // we occlude everything, so set the clip to empty
                (*this).clipRect({0,0,0,0});
            }
        } else {
            (*this).checkForDeferredSave();
            (*this).onClipShader(std::move(sh), op);
        }
    }
}

void SkCanvas::onClipShader(sk_sp<SkShader> sh, SkClipOp op) {
    AutoUpdateQRBounds aqr(this);
    (*(*this).topDevice()).clipShader(sh, op);
}

void SkCanvas::clipRegion(const SkRegion& rgn, SkClipOp op) {
    (*this).checkForDeferredSave();
    (*this).onClipRegion(rgn, op);
}

void SkCanvas::onClipRegion(const SkRegion& rgn, SkClipOp op) {
    AutoUpdateQRBounds aqr(this);
    (*(*this).topDevice()).clipRegion(rgn, op);
}

void SkCanvas::validateClip() const {
#ifdef SK_DEBUG
    const SkRect tmp = (*this).computeDeviceClipBounds();
    if ((*this).isClipEmpty()) {
        SkASSERT(fQuickRejectBounds.isEmpty());
    } else {
        SkASSERT(tmp == fQuickRejectBounds);
    }
#endif
}

bool SkCanvas::androidFramework_isClipAA() const {
    return (*(*this).topDevice()).isClipAntiAliased();
}

void SkCanvas::temporary_internal_getRgnClip(SkRegion* rgn) {
    SkASSERT_RELEASE(rgn != nullptr);
    (*rgn).setEmpty();
    const SkDevice& device = *(*this).topDevice();
    if (device.isPixelAlignedToGlobal()) {
        device.android_utils_clipAsRgn(rgn);
        const SkIPoint origin = device.getOrigin();
        if (origin.x() != 0 || origin.y() != 0) {
            (*rgn).translate(origin.x(), origin.y());
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

bool SkCanvas::isClipEmpty() const {
    return (*(*this).topDevice()).isClipEmpty();
}

bool SkCanvas::isClipRect() const {
    return (*(*this).topDevice()).isClipRect();
}

bool SkCanvas::quickReject(const SkRect& src) const {
#ifdef SK_DEBUG
    // Verify that fQuickRejectBounds are set properly.
    (*this).validateClip();
#endif

    const SkRect devRect = SkMatrixPriv::MapRect((*fMCRec).fMatrix, src);
    const bool rejected = !devRect.isFinite() || !devRect.intersects(fQuickRejectBounds);
    return rejected;
}

bool SkCanvas::quickReject(const SkPath& path) const {
    const bool rejected = path.isEmpty() || (*this).quickReject(path.getBounds());
    return rejected;
}

bool SkCanvas::nothingToDraw(const SkPaint& paint) const {
    const bool preserves = (*(*this).topDevice()).surfaceProps().preservesTransparentDraws();
    const bool nothing = !preserves && paint.nothingToDraw();
    return nothing;
}

bool SkCanvas::internalQuickReject(const SkRect& bounds, const SkPaint& paint,
                                   const SkMatrix* matrix) {
    if (!bounds.isFinite() || (*this).nothingToDraw(paint)) {
        return true;
    }

    if (paint.canComputeFastBounds()) {
        SkRect tmp = matrix ? (*matrix).mapRect(bounds) : bounds;
        const bool rejected = (*this).quickReject(paint.computeFastBounds(tmp, &tmp));
        return rejected;
    }

    return false;
}

SkRect SkCanvas::getLocalClipBounds() const {
    const SkIRect ibounds = (*this).getDeviceClipBounds();
    if (ibounds.isEmpty()) {
        return SkRect::MakeEmpty();
    }

    const std::optional<SkMatrix> inverse = (*fMCRec).fMatrix.asM33().invert();
    // if we can't invert the CTM, we can't return local clip bounds
    if (!inverse) {
        return SkRect::MakeEmpty();
    }

    // adjust it outwards in case we are antialiasing
    const int margin = 1;

    const SkRect localBounds = (*inverse).mapRect(SkRect::Make(ibounds.makeOutset(margin, margin)));
    return localBounds;
}

SkIRect SkCanvas::getDeviceClipBounds() const {
    return (*this).computeDeviceClipBounds(/*outsetForAA=*/false).roundOut();
}

SkRect SkCanvas::computeDeviceClipBounds(bool outsetForAA) const {
    const SkDevice* dev = (*this).topDevice();
    if ((*dev).isClipEmpty()) {
        return SkRect::MakeEmpty();
    }
    SkRect devClipBounds =
            SkMatrixPriv::MapRect((*dev).deviceToGlobal(), SkRect::Make((*dev).devClipBounds()));
    if (outsetForAA) {
        // Expand bounds out by 1 in case we are anti-aliasing.  We store the
        // bounds as floats to enable a faster quick reject implementation.
        devClipBounds.outset(1.f, 1.f);
    }
    return devClipBounds;
}

///////////////////////////////////////////////////////////////////////

SkMatrix SkCanvas::getTotalMatrix() const {
    return (*fMCRec).fMatrix.asM33();
}

SkM44 SkCanvas::getLocalToDevice() const {
    return (*fMCRec).fMatrix;
}

GrRecordingContext* SkCanvas::recordingContext() const {
    return (*(*this).topDevice()).recordingContext();
}

skgpu::graphite::Recorder* SkCanvas::recorder() const {
    return (*(*this).topDevice()).recorder();
}

SkRecorder* SkCanvas::baseRecorder() const {
    return (*(*this).topDevice()).baseRecorder();
}

void SkCanvas::drawDRRect(const SkRRect& outer, const SkRRect& inner,
                          const SkPaint& paint) {
    if (outer.isEmpty()) {
        return;
    }
    if (inner.isEmpty()) {
        (*this).drawRRect(outer, paint);
        return;
    }

    // We don't have this method (yet), but technically this is what we should
    // be able to return ...
    // if (!outer.contains(inner))) {
    //
    // For now at least check for containment of bounds
    if (!outer.getBounds().contains(inner.getBounds())) {
        return;
    }

    (*this).onDrawDRRect(outer, inner, paint);
}

void SkCanvas::drawPaint(const SkPaint& paint) {
    (*this).onDrawPaint(paint);
}

void SkCanvas::drawRect(const SkRect& r, const SkPaint& paint) {
    // To avoid redundant logic in our culling code and various backends, we always sort rects
    // before passing them along.
    (*this).onDrawRect(r.makeSorted(), paint);
}

void SkCanvas::drawClippedToSaveBehind(const SkPaint& paint) {
    (*this).onDrawBehind(paint);
}

void SkCanvas::drawRegion(const SkRegion& region, const SkPaint& paint) {
    if (region.isEmpty()) {
        return;
    }

    if (region.isRect()) {
        (*this).drawIRect(region.getBounds(), paint);
        return;
    }

    (*this).onDrawRegion(region, paint);
}

void SkCanvas::drawOval(const SkRect& r, const SkPaint& paint) {
    // To avoid redundant logic in our culling code and various backends, we always sort rects
    // before passing them along.
    (*this).onDrawOval(r.makeSorted(), paint);
}

void SkCanvas::drawRRect(const SkRRect& rrect, const SkPaint& paint) {
    (*this).onDrawRRect(rrect, paint);
}

void SkCanvas::drawPoints(PointMode mode, SkSpan<const SkPoint> pts, const SkPaint& paint) {
    if (!pts.empty()) {
        (*this).onDrawPoints(mode, pts.size(), pts.data(), paint);
    }
}

void SkCanvas::drawVertices(const sk_sp<SkVertices>& vertices, SkBlendMode mode,
                            const SkPaint& paint) {
    (*this).drawVertices(vertices.get(), mode, paint);
}

void SkCanvas::drawVertices(const SkVertices* vertices, SkBlendMode mode, const SkPaint& paint) {
    if (nullptr == vertices) {
        return;
    }

    // We expect fans to be converted to triangles when building or deserializing SkVertices.
    SkASSERT((*vertices).priv().mode() != SkVertices::kTriangleFan_VertexMode);

    (*this).onDrawVerticesObject(vertices, mode, paint);
}

void SkCanvas::drawPath(const SkPath& path, const SkPaint& paint) {
    (*this).onDrawPath(path, paint);
}

// Returns true if the rect can be "filled" : non-empty and finite
static bool fillable(const SkRect& r) {
    const SkScalar w = r.width();
    const SkScalar h = r.height();
    const bool fills = SkIsFinite(w, h) && w > 0 && h > 0;
    return fills;
}

static SkPaint clean_paint_for_lattice(const SkPaint* paint) {
    SkPaint cleaned;
    if (paint) {
        cleaned = *paint;
        cleaned.setMaskFilter(nullptr);
        cleaned.setAntiAlias(false);
    }
    return cleaned;
}

void SkCanvas::drawImageNine(const SkImage* image, const SkIRect& center, const SkRect& dst,
                             SkFilterMode filter, const SkPaint* paint) {
    if (nullptr == image) {
        return;
    }

    const int xdivs[] = {center.fLeft, center.fRight};
    const int ydivs[] = {center.fTop, center.fBottom};

    Lattice lat;
    lat.fXDivs = xdivs;
    lat.fYDivs = ydivs;
    lat.fRectTypes = nullptr;
    lat.fXCount = 2;
    lat.fYCount = 2;
    lat.fBounds = nullptr;
    lat.fColors = nullptr;
    (*this).drawImageLattice(image, lat, dst, filter, paint);
}

void SkCanvas::drawImageLattice(const SkImage* image, const Lattice& lattice, const SkRect& dst,
                                SkFilterMode filter, const SkPaint* paint) {
    if (nullptr == image) {
        return;
    }
    if (dst.isEmpty()) {
        return;
    }

    SkIRect bounds = SkIRect::MakeEmpty();
    Lattice latticePlusBounds = lattice;
    if (!latticePlusBounds.fBounds) {
        bounds = SkIRect::MakeWH((*image).width(), (*image).height());
        latticePlusBounds.fBounds = &bounds;
    }

    const SkPaint latticePaint = clean_paint_for_lattice(paint);
    if (SkLatticeIter::Valid((*image).width(), (*image).height(), latticePlusBounds)) {
        (*this).onDrawImageLattice2(image, latticePlusBounds, dst, filter, &latticePaint);
    } else {
        (*this).drawImageRect(image, SkRect::MakeIWH((*image).width(), (*image).height()), dst,
                            SkSamplingOptions(filter), &latticePaint, kStrict_SrcRectConstraint);
    }
}

void SkCanvas::drawAtlas(const SkImage* atlas, SkSpan<const SkRSXform> xform,
                         SkSpan<const SkRect> tex, SkSpan<const SkColor> colors, SkBlendMode mode,
                         const SkSamplingOptions& sampling, const SkRect* cull,
                         const SkPaint* paint) {
    if (nullptr == atlas) {
        return;
    }
    size_t count = std::min(xform.size(), tex.size());
    if (!colors.empty()) {
        count = std::min(count, colors.size());
    }
    // onDrawAtlas2 counts sprites in an int; a larger count draws nothing rather than wrapping.
    if (count == 0 || count > static_cast<size_t>(INT32_MAX)) {
        return;
    }
    (*this).onDrawAtlas2(atlas, xform.data(), tex.data(), colors.data(), static_cast<int>(count),
                       mode, sampling, cull, paint);
}

void SkCanvas::experimental_DrawEdgeAAQuad(const SkRect& rect, const SkPoint clip[4],
                                           QuadAAFlags aaFlags, const SkColor4f& color,
                                           SkBlendMode mode) {
    // Make sure the rect is sorted before passing it along
    (*this).onDrawEdgeAAQuad(rect.makeSorted(), clip, aaFlags, color, mode);
}

void SkCanvas::experimental_DrawEdgeAAImageSet(const ImageSetEntry imageSet[], int cnt,
                                               const SkPoint dstClips[],
                                               const SkMatrix preViewMatrices[],
                                               const SkSamplingOptions& sampling,
                                               const SkPaint* paint,
                                               SrcRectConstraint constraint) {
    // Route single, rectangular quads to drawImageRect() to take advantage of image filter
    // optimizations that avoid a layer.
    if (paint && (*paint).getMaskFilter() && cnt == 1) {
        const ImageSetEntry& entry = imageSet[0];
        // If the preViewMatrix is skipped or a positive-scale + translate matrix, we can apply it
        // to the entry's dstRect w/o changing output behavior.
        const bool canMapDstRect = entry.fMatrixIndex < 0 ||
            (preViewMatrices[entry.fMatrixIndex].isScaleTranslate() &&
             preViewMatrices[entry.fMatrixIndex].getScaleX() > 0.f &&
             preViewMatrices[entry.fMatrixIndex].getScaleY() > 0.f);
        if (!entry.fHasClip && canMapDstRect) {
            SkRect dst = entry.fDstRect;
            if (entry.fMatrixIndex >= 0) {
                preViewMatrices[entry.fMatrixIndex].mapRect(&dst);
            }
            (*this).drawImageRect(entry.fImage.get(), entry.fSrcRect, dst,
                                sampling, paint, constraint);
            return;
        } // Else the entry is doing more than can be represented by drawImageRect
    } // Else no filter, or many entries that should be filtered together
    (*this).onDrawEdgeAAImageSet2(imageSet, cnt, dstClips, preViewMatrices, sampling, paint,
                                constraint);
}

//////////////////////////////////////////////////////////////////////////////
//  These are the virtual drawing methods
//////////////////////////////////////////////////////////////////////////////

void SkCanvas::onDiscard() {
    if (fSurfaceBase) {
        sk_ignore_unused_variable((*fSurfaceBase).aboutToDraw(SkSurface::kDiscard_ContentChangeMode));
    }
}

void SkCanvas::onDrawPaint(const SkPaint& paint) {
    (*this).internalDrawPaint(paint);
}

void SkCanvas::internalDrawPaint(const SkPaint& paint) {
    // drawPaint does not call internalQuickReject() because computing its geometry is not free
    // (see getLocalClipBounds(), and the two conditions below are sufficient.
    if ((*this).nothingToDraw(paint) || (*this).isClipEmpty()) {
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(paint, nullptr, PredrawFlags::kCheckForOverwrite);
    if (layer) {
        (*(*this).topDevice()).drawPaint((*layer).paint);
    }
}

void SkCanvas::onDrawPoints(PointMode mode, size_t count, const SkPoint pts[],
                            const SkPaint& paint) {
    if (static_cast<long>(count) <= 0 || (*this).nothingToDraw(paint)) {
        return;
    }
    SkASSERT(pts != nullptr);

    // Enforce paint style matches implicit behavior of drawPoints
    SkPaint strokePaint = paint;
    strokePaint.setStyle(SkPaint::kStroke_Style);

    SkRect boundsStorage = SkRect::MakeEmpty();
    const SkRect* boundsPtr = nullptr;

    /*
     *  Computing the bounds can actually slow us down (since we check inside).
     *  But if there is a filter, then it is useful to limit the size of
     *  its offscreen, hence we only compute it in those cases.
     *
     *  Note: it would be "correct" to never compute this, it is just considered
     *        an optimization opportunity.
     */
    if (paint.getMaskFilter()) {
        const std::optional<SkRect> bounds = SkRect::Bounds({pts, count});
        if (!bounds) {
            return;
        }
        if ((*this).internalQuickReject(bounds.value(), strokePaint)) {
            return;
        }
        boundsStorage = bounds.value();
        boundsPtr = &boundsStorage;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(strokePaint, boundsPtr);
    if (layer) {
        (*(*this).topDevice()).drawPoints(mode, {pts, count}, (*layer).paint);
    }
}

void SkCanvas::onDrawRect(const SkRect& r, const SkPaint& paint) {
    SkASSERT(r.isSorted());
    if ((*this).internalQuickReject(r, paint)) {
        return;
    }

    std::optional<PreparedDraw> layer = aboutToDraw(paint, &r, PredrawFlags::kCheckForOverwrite);

    if (layer) {
        (*(*this).topDevice()).drawRect(r, (*layer).paint);
    }
}

void SkCanvas::onDrawRegion(const SkRegion& region, const SkPaint& paint) {
    const SkRect bounds = SkRect::Make(region.getBounds());
    if ((*this).internalQuickReject(bounds, paint)) {
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(paint, &bounds);
    if (layer) {
        (*(*this).topDevice()).drawRegion(region, (*layer).paint);
    }
}

void SkCanvas::onDrawBehind(const SkPaint& paint) {
    SkDevice* dev = (*this).topDevice();
    if (!dev) {
        return;
    }

    SkIRect bounds = SkIRect::MakeEmpty();
    SkDeque::Iter iter(fMCStack, SkDeque::Iter::kBack_IterStart);
    while (true) {
        const MCRec* rec = static_cast<const MCRec*>(iter.prev());
        if (!rec) {
            return; // no backimages, so nothing to draw
        }
        if ((*rec).fBackImage) {
            // drawBehind should only have been called when the saveBehind record is active;
            // if this fails, it means a real saveLayer was made w/o being restored first.
            SkASSERT(dev == (*rec).fDevice);
            bounds = SkIRect::MakeXYWH((*(*rec).fBackImage).fLoc.fX, (*(*rec).fBackImage).fLoc.fY,
                                       (*(*(*rec).fBackImage).fImage).width(),
                                       (*(*(*rec).fBackImage).fImage).height());
            break;
        }
    }

    // The backimage location (and thus bounds) were defined in the device's space, so mark it
    // as a clip. We use a clip instead of just drawing a rect in case the paint has an image
    // filter on it (which is applied before any auto-layer so the filter is clipped).
    (*dev).pushClipStack();
    {
        // We also have to temporarily whack the device matrix since clipRegion is affected by the
        // global-to-device matrix and clipRect is affected by the local-to-device.
        const SkAutoDeviceTransformRestore adtr(dev, SkM44());
        (*dev).clipRect(SkRect::Make(bounds), SkClipOp::kIntersect, /* aa */ false);
        // ~adtr will reset the local-to-device matrix so that drawPaint() shades correctly.
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(paint);
    if (layer) {
        (*(*this).topDevice()).drawPaint((*layer).paint);
    }

    (*dev).popClipStack();
}

void SkCanvas::onDrawOval(const SkRect& oval, const SkPaint& paint) {
    SkASSERT(oval.isSorted());
    if ((*this).internalQuickReject(oval, paint)) {
        return;
    }

    std::optional<PreparedDraw> layer = aboutToDraw(paint, &oval);

    if (layer) {
        (*(*this).topDevice()).drawOval(oval, (*layer).paint);
    }
}

void SkCanvas::onDrawArc(const SkRect& oval, SkScalar startAngle,
                         SkScalar sweepAngle, bool useCenter,
                         const SkPaint& paint) {
    SkASSERT(oval.isSorted());
    if ((*this).internalQuickReject(oval, paint)) {
        return;
    }

    std::optional<PreparedDraw> layer = aboutToDraw(paint, &oval);

    if (layer) {
        (*(*this).topDevice()).drawArc(SkArc::Make(oval, startAngle, sweepAngle, useCenter),
                                   (*layer).paint);
    }
}

void SkCanvas::onDrawRRect(const SkRRect& rrect, const SkPaint& paint) {
    const SkRect& bounds = rrect.getBounds();

    // Delegating to simpler draw operations
    if (rrect.isRect()) {
        // call the non-virtual version
        (*this).SkCanvas::drawRect(bounds, paint);
        return;
    }
    if (rrect.isOval()) {
        // call the non-virtual version
        (*this).SkCanvas::drawOval(bounds, paint);
        return;
    }

    if ((*this).internalQuickReject(bounds, paint)) {
        return;
    }

    std::optional<PreparedDraw> layer = aboutToDraw(paint, &bounds);

    if (layer) {
        (*(*this).topDevice()).drawRRect(rrect, (*layer).paint);
    }
}

void SkCanvas::onDrawDRRect(const SkRRect& outer, const SkRRect& inner, const SkPaint& paint) {
    const SkRect& bounds = outer.getBounds();
    if ((*this).internalQuickReject(bounds, paint)) {
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(paint, &bounds);
    if (layer) {
        (*(*this).topDevice()).drawDRRect(outer, inner, (*layer).paint);
    }
}

void SkCanvas::onDrawPath(const SkPath& path, const SkPaint& paint) {
    if (!path.isFinite()) {
        return;
    }

    const SkRect& pathBounds = path.getBounds();
    if (!path.isInverseFillType() && (*this).internalQuickReject(pathBounds, paint)) {
        return;
    }
    if (path.isInverseFillType() && pathBounds.width() <= 0 && pathBounds.height() <= 0) {
        (*this).internalDrawPaint(paint);
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(paint, path.isInverseFillType() ? nullptr : &pathBounds);
    if (layer) {
        (*(*this).topDevice()).drawPath(path, (*layer).paint);
    }
}

// Clean-up the paint to match the drawing semantics for drawImage et al. (skbug.com/40039059).
static SkPaint clean_paint_for_drawImage(const SkPaint* paint) {
    SkPaint cleaned;
    if (paint) {
        cleaned = *paint;
        cleaned.setStyle(SkPaint::kFill_Style);
        cleaned.setPathEffect(nullptr);
    }
    return cleaned;
}

// drawVertices fills triangles and ignores mask filter and path effect,
// so canonicalize the paint before checking quick reject.
static SkPaint clean_paint_for_drawVertices(SkPaint paint) {
    paint.setStyle(SkPaint::kFill_Style);
    paint.setMaskFilter(nullptr);
    paint.setPathEffect(nullptr);
    return paint;
}

// TODO: Delete this since it is no longer used
void SkCanvas::onDrawImage2(const SkImage* image, SkScalar x, SkScalar y,
                            const SkSamplingOptions& sampling, const SkPaint* paint) {
    SkUNREACHABLE;
}

static SkSamplingOptions clean_sampling_for_constraint(
        const SkSamplingOptions& sampling,
        SkCanvas::SrcRectConstraint constraint) {
    if (constraint == SkCanvas::kStrict_SrcRectConstraint) {
        if (sampling.mipmap != SkMipmapMode::kNone) {
            return SkSamplingOptions(sampling.filter);
        }
        if (sampling.isAniso()) {
            return SkSamplingOptions(SkFilterMode::kLinear);
        }
    }
    return sampling;
}

void SkCanvas::onDrawImageRect2(const SkImage* image, const SkRect& src, const SkRect& dst,
                                const SkSamplingOptions& sampling, const SkPaint* paint,
                                SrcRectConstraint constraint) {
    const SkPaint realPaint = clean_paint_for_drawImage(paint);
    const SkSamplingOptions realSampling = clean_sampling_for_constraint(sampling, constraint);

    if ((*this).internalQuickReject(dst, realPaint)) {
        return;
    }

    if ((*(*this).topDevice()).shouldDrawAsTiledImageRect()) {
        if ((*(*this).topDevice()).drawAsTiledImageRect(
                    this, image, &src, dst, realSampling, realPaint, constraint)) {
            return;
        }
    }

    const PredrawFlags opacity = (*image).isOpaque() ? PredrawFlags::kOpaqueShaderOverride
                                                     : PredrawFlags::kNonOpaqueShaderOverride;
    std::optional<PreparedDraw> layer =
            (*this).aboutToDraw(realPaint, &dst, PredrawFlags::kCheckForOverwrite | opacity);
    if (layer) {
        (*(*this).topDevice()).drawImageRect(image, &src, dst, realSampling, (*layer).paint,
                                         constraint);
    }
}

void SkCanvas::onDrawImageLattice2(const SkImage* image, const Lattice& lattice, const SkRect& dst,
                                   SkFilterMode filter, const SkPaint* paint) {
    const SkPaint realPaint = clean_paint_for_drawImage(paint);

    if ((*this).internalQuickReject(dst, realPaint)) {
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(realPaint, &dst);
    if (layer) {
        (*(*this).topDevice()).drawImageLattice(image, lattice, dst, filter, (*layer).paint);
    }
}

void SkCanvas::drawImage(const SkImage* image, SkScalar x, SkScalar y,
                         const SkSamplingOptions& sampling, const SkPaint* paint) {
    if (nullptr == image) {
        return;
    }

    (*this).drawImageRect(image,
                        /*src=*/SkRect::MakeWH((*image).width(), (*image).height()),
                        /*dst=*/SkRect::MakeXYWH(x, y, (*image).width(), (*image).height()),
                        sampling,
                        paint,
                        kFast_SrcRectConstraint);
}

void SkCanvas::drawImageRect(const SkImage* image, const SkRect& src, const SkRect& dst,
                             const SkSamplingOptions& sampling, const SkPaint* paint,
                             SrcRectConstraint constraint) {
    if (nullptr == image) {
        return;
    }
    if (!fillable(dst) || !fillable(src)) {
        return;
    }
    (*this).onDrawImageRect2(image, src, dst, sampling, paint, constraint);
}

void SkCanvas::drawImageRect(const SkImage* image, const SkRect& dst,
                             const SkSamplingOptions& sampling, const SkPaint* paint) {
    if (nullptr == image) {
        return;
    }
    (*this).drawImageRect(image, SkRect::MakeIWH((*image).width(), (*image).height()), dst, sampling,
                        paint, kFast_SrcRectConstraint);
}

void SkCanvas::onDrawVerticesObject(const SkVertices* vertices, SkBlendMode bmode,
                                    const SkPaint& paint) {
    const SkPaint simplePaint = clean_paint_for_drawVertices(paint);

    const SkRect& bounds = (*vertices).bounds();
    if ((*this).internalQuickReject(bounds, simplePaint)) {
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(simplePaint, &bounds);
    if (layer) {
        (*(*this).topDevice()).drawVertices(vertices, SkBlender::Mode(bmode), (*layer).paint);
    }
}

void SkCanvas::drawPatch(const SkPoint cubics[12], const SkColor colors[4],
                         const SkPoint texCoords[4], SkBlendMode bmode,
                         const SkPaint& paint) {
    if (nullptr == cubics) {
        return;
    }

    (*this).onDrawPatch(cubics, colors, texCoords, bmode, paint);
}

void SkCanvas::onDrawPatch(const SkPoint cubics[12], const SkColor colors[4],
                           const SkPoint texCoords[4], SkBlendMode bmode,
                           const SkPaint& paint) {
    const std::optional<SkRect> bounds =
            SkRect::Bounds({cubics, static_cast<size_t>(SkPatchUtils::kNumCtrlPts)});
    if (!bounds) {
        return; // we don't draw if the bounds are not finite
    }

    // drawPatch has the same behavior restrictions as drawVertices
    const SkPaint simplePaint = clean_paint_for_drawVertices(paint);

    // Since a patch is always within the convex hull of the control points, we discard it when its
    // bounding rectangle is completely outside the current clip.
    if ((*this).internalQuickReject(bounds.value(), simplePaint)) {
        return;
    }

    const SkRect r = bounds.value();
    std::optional<PreparedDraw> layer = (*this).aboutToDraw(simplePaint, &r);
    if (layer) {
        (*(*this).topDevice()).drawPatch(cubics, colors, texCoords, SkBlender::Mode(bmode),
                                     (*layer).paint);
    }
}

void SkCanvas::onDrawAtlas2(const SkImage* atlas, const SkRSXform xform[], const SkRect tex[],
                            const SkColor colors[], int count, SkBlendMode bmode,
                            const SkSamplingOptions& sampling, const SkRect* cull,
                            const SkPaint* paint) {
    // drawAtlas is a combination of drawVertices and drawImage...
    SkPaint realPaint = clean_paint_for_drawVertices(clean_paint_for_drawImage(paint));
    realPaint.setShader((*atlas).makeShader(sampling));

    if (cull && (*this).internalQuickReject(*cull, realPaint)) {
        return;
    }

    // drawAtlas should not have mask filters on its paint, so we don't need to worry about
    // converting its "drawImage" behavior into the paint to work with the auto-mask-filter system.
    SkASSERT(!realPaint.getMaskFilter());
    std::optional<PreparedDraw> layer = (*this).aboutToDraw(realPaint);
    if (layer) {
        const size_t N = SkToSizeT(count);
        (*(*this).topDevice()).drawAtlas({xform, N}, {tex, N}, {colors, colors ? N : 0},
                                     SkBlender::Mode(bmode), (*layer).paint);
    }
}

void SkCanvas::onDrawEdgeAAQuad(const SkRect& r, const SkPoint clip[4], QuadAAFlags edgeAA,
                                const SkColor4f& color, SkBlendMode mode) {
    SkASSERT(r.isSorted());

    SkPaint paint{color};
    paint.setBlendMode(mode);
    if ((*this).internalQuickReject(r, paint)) {
        return;
    }

    if ((*this).predrawNotify()) {
        (*(*this).topDevice()).drawEdgeAAQuad(r, clip, edgeAA, color, mode);
    }
}

void SkCanvas::onDrawEdgeAAImageSet2(const ImageSetEntry imageSet[], int count,
                                     const SkPoint dstClips[], const SkMatrix preViewMatrices[],
                                     const SkSamplingOptions& sampling, const SkPaint* paint,
                                     SrcRectConstraint constraint) {
    if (count <= 0) {
        // Nothing to draw
        return;
    }

    const SkPaint realPaint = clean_paint_for_drawImage(paint);
    const SkSamplingOptions realSampling = clean_sampling_for_constraint(sampling, constraint);

    // We could calculate the set's dstRect union to always check quickReject(), but we can't reject
    // individual entries and Chromium's occlusion culling already makes it likely that at least one
    // entry will be visible. So, we only calculate the draw bounds when it's trivial (count == 1),
    // or we need it for the autolooper (since it greatly improves image filter perf).
    const bool needsAutoLayer = SkToBool(realPaint.getMaskFilter());
    const bool setBoundsValid = count == 1 || needsAutoLayer;
    SkRect setBounds = imageSet[0].fDstRect;
    if (imageSet[0].fMatrixIndex >= 0) {
        // Account for the per-entry transform that is applied prior to the CTM when drawing
        preViewMatrices[imageSet[0].fMatrixIndex].mapRect(&setBounds);
    }
    if (needsAutoLayer) {
        for (int i = 1; i < count; ++i) {
            SkRect entryBounds = imageSet[i].fDstRect;
            if (imageSet[i].fMatrixIndex >= 0) {
                preViewMatrices[imageSet[i].fMatrixIndex].mapRect(&entryBounds);
            }
            setBounds.joinPossiblyEmptyRect(entryBounds);
        }
    }

    // If we happen to have the draw bounds, though, might as well check quickReject().
    if (setBoundsValid && (*this).internalQuickReject(setBounds, realPaint)) {
        return;
    }

    std::optional<PreparedDraw> layer = (*this).aboutToDraw(realPaint, setBoundsValid ? &setBounds : nullptr);
    if (layer) {
        (*(*this).topDevice()).drawEdgeAAImageSet(imageSet, count, dstClips, preViewMatrices,
                                              realSampling, (*layer).paint, constraint);
    }
}

//////////////////////////////////////////////////////////////////////////////
// These methods are NOT virtual, and therefore must call back into virtual
// methods, rather than actually drawing themselves.
//////////////////////////////////////////////////////////////////////////////

void SkCanvas::drawColor(const SkColor4f& c, SkBlendMode mode) {
    SkPaint paint;
    paint.setColor(c);
    paint.setBlendMode(mode);
    (*this).drawPaint(paint);
}

void SkCanvas::drawPoint(SkScalar x, SkScalar y, const SkPaint& paint) {
    const SkPoint pt[1] = {{ x, y }};
    (*this).drawPoints(kPoints_PointMode, pt, paint);
}

void SkCanvas::drawLine(SkScalar x0, SkScalar y0, SkScalar x1, SkScalar y1, const SkPaint& paint) {
    const SkPoint pts[2] = {{x0, y0}, {x1, y1}};
    (*this).drawPoints(kLines_PointMode, pts, paint);
}

void SkCanvas::drawCircle(SkScalar cx, SkScalar cy, SkScalar radius, const SkPaint& paint) {
    const SkScalar r0 = radius < 0 ? 0 : radius;

    const SkRect r = SkRect::MakeLTRB(cx - r0, cy - r0, cx + r0, cy + r0);
    (*this).drawOval(r, paint);
}

void SkCanvas::drawRoundRect(const SkRect& r, SkScalar rx, SkScalar ry,
                             const SkPaint& paint) {
    if (rx > 0 && ry > 0) {
        SkRRect rrect;
        rrect.setRectXY(r, rx, ry);
        (*this).drawRRect(rrect, paint);
    } else {
        (*this).drawRect(r, paint);
    }
}

void SkCanvas::drawArc(const SkRect& oval, SkScalar startAngle,
                       SkScalar sweepAngle, bool useCenter,
                       const SkPaint& paint) {
    if (oval.isEmpty() || !sweepAngle) {
        return;
    }
    (*this).onDrawArc(oval, startAngle, sweepAngle, useCenter, paint);
}

///////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////

SkCanvas::ImageSetEntry::ImageSetEntry() = default;
SkCanvas::ImageSetEntry::~ImageSetEntry() = default;
SkCanvas::ImageSetEntry::ImageSetEntry(const ImageSetEntry&) = default;
SkCanvas::ImageSetEntry& SkCanvas::ImageSetEntry::operator=(const ImageSetEntry&) = default;

SkCanvas::ImageSetEntry::ImageSetEntry(sk_sp<const SkImage> image, const SkRect& srcRect,
                                       const SkRect& dstRect, int matrixIndex, float alpha,
                                       unsigned aaFlags, bool hasClip)
                : fImage(std::move(image))
                , fSrcRect(srcRect)
                , fDstRect(dstRect)
                , fMatrixIndex(matrixIndex)
                , fAlpha(alpha)
                , fAAFlags(aaFlags)
                , fHasClip(hasClip) {}

SkCanvas::ImageSetEntry::ImageSetEntry(sk_sp<const SkImage> image, const SkRect& srcRect,
                                       const SkRect& dstRect, float alpha, unsigned aaFlags)
                : fImage(std::move(image))
                , fSrcRect(srcRect)
                , fDstRect(dstRect)
                , fAlpha(alpha)
                , fAAFlags(aaFlags) {}

///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<SkCanvas> SkCanvas::MakeRasterDirect(const SkImageInfo& info, void* pixels,
                                                     size_t rowBytes, const SkSurfaceProps* props) {
    if (!SkSurfaceValidateRasterInfo(info, rowBytes)) {
        return nullptr;
    }

    SkBitmap bitmap;
    if (!bitmap.installPixels(info, pixels, rowBytes)) {
        return nullptr;
    }

    if (props) {
        return std::make_unique<SkCanvas>(bitmap, *props);
    }
    return std::make_unique<SkCanvas>(bitmap);
}

///////////////////////////////////////////////////////////////////////////////

SkNoDrawCanvas::SkNoDrawCanvas(int width, int height)
    : INHERITED(SkIRect::MakeWH(width, height)) {}

SkNoDrawCanvas::SkNoDrawCanvas(const SkIRect& bounds)
    : INHERITED(bounds) {}

SkCanvas::SaveLayerStrategy SkNoDrawCanvas::getSaveLayerStrategy(const SaveLayerRec& rec) {
    (void)(*this).INHERITED::getSaveLayerStrategy(rec);
    return kNoLayer_SaveLayerStrategy;
}

bool SkNoDrawCanvas::onDoSaveBehind(const SkRect*) {
    return false;
}

///////////////////////////////////////////////////////////////////////////////

static_assert(static_cast<int>(SkRegion::kDifference_Op) ==
              static_cast<int>(SkClipOp::kDifference));
static_assert(static_cast<int>(SkRegion::kIntersect_Op) ==
              static_cast<int>(SkClipOp::kIntersect));

///////////////////////////////////////////////////////////////////////////////////////////////////

SkRasterHandleAllocator::Handle SkCanvas::accessTopRasterHandle() const {
    const SkDevice* dev = (*this).topDevice();
    if (fAllocator) {
        SkRasterHandleAllocator::Handle handle = (*dev).getRasterHandle();
        SkIRect clip = (*dev).devClipBounds();
        if (!clip.intersect({0, 0, (*dev).width(), (*dev).height()})) {
            clip.setEmpty();
        }

        (*fAllocator).updateHandle(handle, (*dev).localToDevice(), clip);
        return handle;
    }
    return nullptr;
}

static bool install(SkBitmap* bm, const SkImageInfo& info,
                    const SkRasterHandleAllocator::Rec& rec) {
    const bool installed =
            (*bm).installPixels(info, rec.fPixels, rec.fRowBytes, rec.fReleaseProc, rec.fReleaseCtx);
    return installed;
}

SkRasterHandleAllocator::Handle SkRasterHandleAllocator::allocBitmap(const SkImageInfo& info,
                                                                     SkBitmap* bm) {
    SkRasterHandleAllocator::Rec rec;
    if (!(*this).allocHandle(info, &rec) || !install(bm, info, rec)) {
        return nullptr;
    }
    return rec.fHandle;
}

std::unique_ptr<SkCanvas>
SkRasterHandleAllocator::MakeCanvas(std::unique_ptr<SkRasterHandleAllocator> alloc,
                                    const SkImageInfo& info, const Rec* rec,
                                    const SkSurfaceProps* props) {
    if (!alloc || !SkSurfaceValidateRasterInfo(info, rec ? (*rec).fRowBytes : kIgnoreRowBytesValue)) {
        return nullptr;
    }

    SkBitmap bm;
    Handle hndl = nullptr;

    if (rec) {
        if (install(&bm, info, *rec)) {
            hndl = (*rec).fHandle;
        }
    } else {
        hndl = (*alloc).allocBitmap(info, &bm);
    }
    if (!hndl) {
        return nullptr;
    }
    std::unique_ptr<SkCanvas> canvas(new SkCanvas(bm, std::move(alloc), hndl, props));
    return canvas;
}
