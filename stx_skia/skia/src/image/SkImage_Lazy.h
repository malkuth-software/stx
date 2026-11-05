/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkImage_Lazy_DEFINED
#define SkImage_Lazy_DEFINED

#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageGenerator.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/private/SkMutex.h"
#include "src/image/SkImage_Base.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class SharedGenerator;
class SkBitmap;
class SkData;
class SkPixmap;
class SkRecorder;
class SkSurface;
enum SkColorType : int;
struct SkIRect;

class SkImage_Lazy : public SkImage_Base {
public:
    struct Validator {
        Validator(sk_sp<SharedGenerator>, const SkColorType*, sk_sp<SkColorSpace>);

        explicit operator bool() const {
            const bool valid = fSharedGenerator != nullptr;
            return valid;
        }

        sk_sp<SharedGenerator> fSharedGenerator = nullptr;
        SkImageInfo            fInfo;
        uint32_t               fUniqueID = 0;
    };

    explicit SkImage_Lazy(Validator* validator);

    // From SkImage.h
    bool isValid(SkRecorder*) const override;
    sk_sp<SkImage> makeColorTypeAndColorSpace(SkRecorder*,
                                              SkColorType targetColorType,
                                              sk_sp<SkColorSpace> targetColorSpace,
                                              RequiredProperties) const override;

    // From SkImage_Base.h
    bool onHasMipmaps() const override {
        // CPU generators supply only the root pixel level.
        return false;
    }

    bool onReadPixels( const SkImageInfo&, void*, size_t, int srcX, int srcY,
                      CachingHint) const override;
    sk_sp<const SkData> onRefEncoded() const override;

    sk_sp<SkImage> onMakeSubset(SkRecorder*, const SkIRect&, RequiredProperties) const override;

    sk_sp<SkSurface> onMakeSurface(SkRecorder*, const SkImageInfo&) const override;

    bool getROPixels( SkBitmap*, CachingHint) const override;
    SkImage_Base::Type type() const override { return SkImage_Base::Type::kLazy; }

    sk_sp<SkImage> onReinterpretColorSpace(sk_sp<SkColorSpace>) const final;

private:
    // Converted images share the generator but carry their own color type/space.
    // Generator calls are serialized by SharedGenerator::fMutex. Immutable
    // generator metadata can be read without locking. Subsets become raster images.
    sk_sp<SharedGenerator> fSharedGenerator = nullptr;

    // Repeated calls to makeColorTypeAndColorSpace will result in a proliferation of unique IDs
    // and SkImage_Lazy instances. Cache the result of the last successful call.
    mutable SkMutex        fOnMakeColorTypeAndSpaceMutex;
    mutable sk_sp<SkImage> fOnMakeColorTypeAndSpaceResult = nullptr;

};

// Shared ownership of one CPU generator. The generator object and its immutable
// metadata live until the last image releases this owner; mutable calls lock it.
class SharedGenerator final : public SkNVRefCnt<SharedGenerator> {
public:
    static sk_sp<SharedGenerator> Make(std::unique_ptr<SkImageGenerator> generator);

private:
    explicit SharedGenerator(std::unique_ptr<SkImageGenerator> generator);
    std::unique_ptr<SkImageGenerator> fGenerator;
    SkMutex fMutex;
    friend class SkImage_Lazy;
};

#endif
