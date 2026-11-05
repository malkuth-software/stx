/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSynchronizedResourceCache_DEFINED
#define SkSynchronizedResourceCache_DEFINED

#include "include/private/SkDebug.h"
#include "include/private/SkMutex.h"
#include "src/core/SkResourceCache.h"

#include <cstddef>

class SkCachedData;

// Serializes each operation, including callbacks. Callbacks must not reenter.
// Destruction requires all concurrent calls and record users to have finished.
class SkSynchronizedResourceCache final : public SkResourceCache {
public:
    bool find(const Key& key, FindVisitor, void* context) override;
    void add(Rec*, void* payload = nullptr) override;

    void visitAll(Visitor, void* context) override;

    size_t getTotalBytesUsed() const override;
    size_t getTotalByteLimit() const override;
    size_t setTotalByteLimit(size_t newLimit) override;

    size_t setSingleAllocationByteLimit(size_t) override;
    size_t getSingleAllocationByteLimit() const override;
    size_t getEffectiveSingleAllocationByteLimit() const override;

    void purgeAll() override;
    void purgeSharedID(uint64_t sharedID) override;
    void checkMessages();

    DiscardableFactory discardableFactory() const override;

    SkCachedData* newCachedData(size_t bytes) override;

    void dump() const override;

    explicit SkSynchronizedResourceCache(DiscardableFactory factory);
    explicit SkSynchronizedResourceCache(size_t byteLimit);
    ~SkSynchronizedResourceCache() override;

private:
    mutable SkMutex fMutex{};
};
#endif
