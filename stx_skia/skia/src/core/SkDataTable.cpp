/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkDataTable.h"

#include "include/core/SkRefCnt.h"
#include "include/private/SkAssert.h"
#include "include/private/SkMalloc.h"
#include "src/core/SkSafeMath.h"

#include <cstring>

static void malloc_freeproc(void* context) {
    sk_free(context);
}

// Makes empty table
// fElemSize stays 0, which signals that we use fDir instead of fElems.
SkDataTable::SkDataTable() = default;

SkDataTable::SkDataTable(const void* array, size_t elemSize, int count,
                         FreeProc proc, void* context) {
    SkASSERT(count > 0);

    fCount = count;
    fElemSize = elemSize;   // non-zero signals we use fElems instead of fDir
    fU.fElems = static_cast<const char*>(array);
    fFreeProc = proc;
    fFreeProcContext = context;
}

SkDataTable::SkDataTable(const Dir* dir, int count, FreeProc proc, void* ctx) {
    SkASSERT(count > 0);

    fCount = count;
    fElemSize = 0;  // 0 signals that we use fDir instead of fElems
    fU.fDir = dir;
    fFreeProc = proc;
    fFreeProcContext = ctx;
}

SkDataTable::~SkDataTable() {
    if (fFreeProc) {
        fFreeProc(fFreeProcContext);
    }
}

size_t SkDataTable::atSize(int index) const {
    SkASSERT_RELEASE(static_cast<unsigned>(index) < static_cast<unsigned>(fCount));

    if (fElemSize) {
        return fElemSize;
    } else {
        return fU.fDir[index].fSize;
    }
}

const void* SkDataTable::at(int index, size_t* size) const {
    SkASSERT_RELEASE(static_cast<unsigned>(index) < static_cast<unsigned>(fCount));

    if (fElemSize) {
        if (size) {
            *size = fElemSize;
        }
        return fU.fElems + index * fElemSize;
    } else {
        if (size) {
            *size = fU.fDir[index].fSize;
        }
        return fU.fDir[index].fPtr;
    }
}

///////////////////////////////////////////////////////////////////////////////

sk_sp<SkDataTable> SkDataTable::MakeEmpty() {
    static SkDataTable* singleton = new SkDataTable();
    return sk_ref_sp(singleton);
}

sk_sp<SkDataTable> SkDataTable::MakeCopyArrays(const void * const * ptrs,
                                               const size_t sizes[], int count) {
    if (count <= 0) {
        return SkDataTable::MakeEmpty();
    }
    SkASSERT_RELEASE(ptrs != nullptr && sizes != nullptr);

    // The directory and the entries share one allocation; its size must not wrap.
    SkSafeMath safe{};
    size_t dataSize = 0;
    for (int i = 0; i < count; ++i) {
        SkASSERT_RELEASE(ptrs[i] != nullptr || sizes[i] == 0);
        dataSize = safe.add(dataSize, sizes[i]);
    }
    const size_t bufferSize = safe.add(safe.mul(static_cast<size_t>(count), sizeof(Dir)),
                                       dataSize);
    SkASSERT_RELEASE(safe.ok());
    void* buffer = sk_malloc_throw(bufferSize);

    Dir* dir = static_cast<Dir*>(buffer);
    char* elem = reinterpret_cast<char*>(dir + count);
    for (int i = 0; i < count; ++i) {
        dir[i].fPtr = elem;
        dir[i].fSize = sizes[i];
        sk_careful_memcpy(elem, ptrs[i], sizes[i]);
        elem += sizes[i];
    }

    return sk_sp<SkDataTable>(new SkDataTable(dir, count, malloc_freeproc, buffer));
}

sk_sp<SkDataTable> SkDataTable::MakeCopyArray(const void* array, size_t elemSize, int count) {
    if (count <= 0) {
        return SkDataTable::MakeEmpty();
    }
    // An element size of 0 would make the table read its storage as a directory.
    SkASSERT_RELEASE(elemSize > 0 && array != nullptr);

    SkSafeMath safe{};
    const size_t bufferSize = safe.mul(elemSize, static_cast<size_t>(count));
    SkASSERT_RELEASE(safe.ok());
    void* buffer = sk_malloc_throw(bufferSize);
    std::memcpy(buffer, array, bufferSize);

    return sk_sp<SkDataTable>(new SkDataTable(buffer, elemSize, count, malloc_freeproc, buffer));
}

sk_sp<SkDataTable> SkDataTable::MakeArrayProc(const void* array, size_t elemSize, int count,
                                              FreeProc proc, void* ctx) {
    if (count <= 0) {
        return SkDataTable::MakeEmpty();
    }
    // An element size of 0 would make the table read the caller's array as a directory.
    SkASSERT_RELEASE(elemSize > 0 && array != nullptr);
    return sk_sp<SkDataTable>(new SkDataTable(array, elemSize, count, proc, ctx));
}
