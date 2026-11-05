/*
 * Copyright 2018 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkData.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/private/SkAssert.h"
#include "include/private/SkDebug.h"
#include "include/private/SkPathRef.h"
#include "include/private/SkTPin.h"
#include "include/private/SkTo.h"
#include "src/core/SkAutoMalloc.h"
#include "src/core/SkBuffer.h"
#include "src/core/SkPathEnums.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkRRectPriv.h"
#include "src/core/SkSafeMath.h"

#include <cstddef>
#include <cstdint>
#include <optional>

enum SerializationOffsets {
    kType_SerializationShift = 28,       // requires 4 bits
    kDirection_SerializationShift = 26,  // requires 2 bits
    kFillType_SerializationShift = 8,    // requires 8 bits
    // low-8-bits are version
    kVersion_SerializationMask = 0xFF,
};

enum SerializationVersions {
    // kPathPrivFirstDirection_Version = 1,
    // kPathPrivLastMoveToIndex_Version = 2,
    // kPathPrivTypeEnumVersion = 3,
    kJustPublicData_Version = 4,            // introduced Feb/2018
    kVerbsAreStoredForward_Version = 5,     // introduced Sept/2019

    kMin_Version     = kJustPublicData_Version,
    kCurrent_Version = kVerbsAreStoredForward_Version
};

enum SerializationType {
    kGeneral = 0,
    kRRect = 1
};

static unsigned extract_version(uint32_t packed) {
    const unsigned version = packed & kVersion_SerializationMask;
    return version;
}

static SkPathFillType extract_filltype(uint32_t packed) {
    const SkPathFillType fillType =
            static_cast<SkPathFillType>((packed >> kFillType_SerializationShift) & 0x3);
    return fillType;
}

static SerializationType extract_serializationtype(uint32_t packed) {
    const SerializationType type =
            static_cast<SerializationType>((packed >> kType_SerializationShift) & 0xF);
    return type;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

// Writes the rounded-rectangle form (header, rectangle and radii, start index) when the path
// was built as an oval or rounded rectangle. Returns 0 otherwise. A null storage queries the
// size; storage must be 4-byte aligned and hold that many bytes.
size_t SkPath::writeToMemoryAsRRect(void* storage) const {
    SkRRect rrect{};
    SkPathDirection firstDir = SkPathDirection::kCW;
    unsigned start = 0;

    const std::optional<SkPathOvalInfo> oinfo = getOvalInfo();
    const std::optional<SkPathRRectInfo> rinfo = oinfo.has_value() ? std::nullopt
                                                                    : getRRectInfo();
    if (oinfo.has_value()) {
        rrect.setOval(oinfo.value().fBounds);
        firstDir = oinfo.value().fDirection;
        // Convert to rrect start indices.
        start = oinfo.value().fStartIndex * 2;
    } else if (rinfo.has_value()) {
        rrect = rinfo.value().fRRect;
        firstDir = rinfo.value().fDirection;
        start = rinfo.value().fStartIndex;
    } else {
        return 0;
    }

    // packed header, rrect, start index.
    const size_t sizeNeeded = sizeof(int32_t) + SkRRect::kSizeInMemory + sizeof(int32_t);
    if (!storage) {
        return sizeNeeded;
    }

    const int32_t packed = (static_cast<int>(fFillType) << kFillType_SerializationShift) |
                           (static_cast<int>(firstDir) << kDirection_SerializationShift) |
                           (SerializationType::kRRect << kType_SerializationShift) |
                           kCurrent_Version;

    SkWBuffer buffer(storage);
    buffer.write32(packed);
    SkRRectPriv::WriteToBuffer(rrect, &buffer);
    buffer.write32(SkToS32(start));
    buffer.padToAlign4();
    SkASSERT(sizeNeeded == buffer.pos());
    return buffer.pos();
}

// Writes the header, the three counts, points, conic weights and forward verbs, padded to four
// bytes. A null storage queries the size; 0 means the size is unrepresentable.
size_t SkPath::writeToMemory(void* storage) const {
    SkDEBUGCODE(validate();)

    const size_t rrectBytes = writeToMemoryAsRRect(storage);
    if (rrectBytes != 0) {
        return rrectBytes;
    }

    const int32_t packed = (static_cast<int>(fFillType) << kFillType_SerializationShift) |
                           (SerializationType::kGeneral << kType_SerializationShift) |
                           kCurrent_Version;

    const SkSpan<const SkPoint> pointSpan = points();
    const SkSpan<const SkPathVerb> verbSpan = verbs();
    const SkSpan<const float> conics = conicWeights();

    const int32_t pts = SkToS32(pointSpan.size());
    const int32_t cnx = SkToS32(conics.size());
    const int32_t vbs = SkToS32(verbSpan.size());

    SkSafeMath safe{};
    size_t size = 4 * sizeof(int32_t);
    size = safe.add(size, safe.mul(pts, sizeof(SkPoint)));
    size = safe.add(size, safe.mul(cnx, sizeof(float)));
    size = safe.add(size, safe.mul(vbs, sizeof(uint8_t)));
    size = safe.alignUp(size, 4);
    if (!safe) {
        return 0;
    }
    if (!storage) {
        return size;
    }

    SkWBuffer buffer(storage);
    buffer.write32(packed);
    buffer.write32(pts);
    buffer.write32(cnx);
    buffer.write32(vbs);
    buffer.write(pointSpan.data(), pointSpan.size_bytes());
    buffer.write(conics.data(), conics.size_bytes());
    buffer.write(verbSpan.data(), verbSpan.size_bytes());
    buffer.padToAlign4();

    SkASSERT(buffer.pos() == size);
    return size;
}

sk_sp<SkData> SkPath::serialize() const {
    const size_t size = writeToMemory(nullptr);
    sk_sp<SkData> data = SkData::MakeUninitialized(size);
    writeToMemory((*data).writable_data());
    return data;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// reading

static std::optional<SkPath> read_rrect_path(const void* storage, size_t length,
                                             size_t* bytesRead) {
    SkASSERT(bytesRead);
    SkRBuffer buffer(storage, length);
    uint32_t packed = 0;
    if (!buffer.readU32(&packed)) {
        *bytesRead = 0;
        return {};
    }

    SkASSERT(extract_serializationtype(packed) == SerializationType::kRRect);

    const uint8_t dir = (packed >> kDirection_SerializationShift) & 0x3;
    const SkPathFillType fillType = extract_filltype(packed);

    SkPathDirection rrectDir = SkPathDirection::kCW;
    switch (dir) {
        case static_cast<int>(SkPathFirstDirection::kCW):
            rrectDir = SkPathDirection::kCW;
            break;
        case static_cast<int>(SkPathFirstDirection::kCCW):
            rrectDir = SkPathDirection::kCCW;
            break;
        default:
            *bytesRead = 0;
            return {};
    }

    SkRRect rrect{};
    if (!SkRRectPriv::ReadFromBuffer(&buffer, &rrect)) {
        *bytesRead = 0;
        return {};
    }

    int32_t start = 0;
    if (!buffer.readS32(&start) || start != SkTPin(start, 0, 7)) {
        *bytesRead = 0;
        return {};
    }

    SkPath path = SkPath::RRect(rrect, rrectDir, SkToUInt(start));
    path.setFillType(fillType);
    buffer.skipToAlign4();
    *bytesRead = buffer.pos();
    return path;
}

namespace {

// The counts that follow the header of a general path.
struct SerializedCounts {
    uint32_t pts = 0;
    uint32_t cnx = 0;
    uint32_t vbs = 0;
};

}  // namespace

// storage must be 4-byte aligned: points and weights are read in place. A misaligned or
// truncated buffer, an unknown version or type, or invalid path data returns nullopt with
// *bytesRead set to 0.
std::optional<SkPath> SkPath::ReadFromMemory(const void* storage, size_t length, size_t* bytesRead) {
    size_t bytesStorage = 0;
    if (!bytesRead) {
        bytesRead = &bytesStorage;
    }
    if (reinterpret_cast<uintptr_t>(storage) % alignof(SkPoint) != 0) {
        *bytesRead = 0;
        return {};
    }
    SkRBuffer buffer(storage, length);
    uint32_t packed = 0;
    if (!buffer.readU32(&packed)) {
        *bytesRead = 0;
        return {};
    }
    const unsigned version = extract_version(packed);

    const bool verbsAreForward = (version == kVerbsAreStoredForward_Version);
    if (!verbsAreForward && version != kJustPublicData_Version) SK_UNLIKELY {
        // Old/unsupported version.
        *bytesRead = 0;
        return {};
    }

    switch (extract_serializationtype(packed)) {
        case SerializationType::kRRect: {
            std::optional<SkPath> rrectPath = read_rrect_path(storage, length, bytesRead);
            return rrectPath;
        }
        case SerializationType::kGeneral:
            break;  // fall out
        default:
            *bytesRead = 0;
            return {};
    }

    // To minimize the number of reads done a structure with the counts is used.
    SerializedCounts counts{};
    if (!buffer.read(&counts, sizeof(counts))) {
        *bytesRead = 0;
        return {};
    }

    const SkPoint* points = buffer.skipCount<SkPoint>(counts.pts);
    const SkScalar* conics = buffer.skipCount<SkScalar>(counts.cnx);
    const SkPathVerb* verbs = buffer.skipCount<SkPathVerb>(counts.vbs);
    buffer.skipToAlign4();
    if (!buffer.isValid()) {
        *bytesRead = 0;
        return {};
    }
    SkASSERT(buffer.pos() <= length);

    if (counts.vbs == 0) {
        if (counts.pts == 0 && counts.cnx == 0) {
            SkPath path(extract_filltype(packed));
            *bytesRead = buffer.pos();
            return path;
        }
        // No verbs but points and/or conic weights is a not a valid path.
        *bytesRead = 0;
        return {};
    }

    // Older versions stored verbs backwards; reverse them into owned storage.
    SkAutoMalloc reversedStorage{};
    if (!verbsAreForward) SK_UNLIKELY {
        SkPathVerb* tmpVerbs = static_cast<SkPathVerb*>(reversedStorage.reset(counts.vbs));
        for (unsigned i = 0; i < counts.vbs; ++i) {
            tmpVerbs[i] = verbs[counts.vbs - i - 1];
        }
        verbs = tmpVerbs;
    }

    *bytesRead = buffer.pos();
    const SkPath path = SkPath::Raw(SkSpan<const SkPoint>(points, counts.pts),
                                    SkSpan<const SkPathVerb>(verbs, counts.vbs),
                                    SkSpan<const SkScalar>(conics, counts.cnx),
                                    extract_filltype(packed),
                                    false);
    return path;
}
