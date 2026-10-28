/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkData_DEFINED
#define SkData_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkSpan.h"
#include "include/private/SkAPI.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

class SkStream;

/**
 *  SkData holds a data buffer. It can be created to allocate its own buffer
 *  for the contents, or to share a pointer to the client's buffer. The size and
 *  address of the contents never change for the lifetime of the data object.
 */
class SK_API SkData final : public SkNVRefCnt<SkData> {
public:
    /**
     *  Returns true if this and rhs are the same size, and contain the same contents.
     *  All empty objects compare as equal.
     */
    bool operator==(const SkData& rhs) const;
    bool operator!=(const SkData& rhs) const {
        const bool different = !(*this == rhs);
        return different;
    }

    /**
     * Calls == operator, but first checks if other is null (in which case it returns false)
     */
    bool equals(const SkData* other) const {
        if (other == nullptr) return false;
        const bool equal = *this == *other;
        return equal;
    }

    /**
     * Return true if both arguments are the same size and contain the same bytes, or if both
     * arguments are null.
     */
    static bool Equals(const SkData* a, const SkData* b) {
        if (a == nullptr) {
            const bool both_null = b == nullptr;
            return both_null;
        }
        const bool equal = (*a).equals(b);
        return equal;
    }

    /**
     *  Returns the number of bytes stored.
     */
    size_t size() const {
        const size_t length = fSpan.size();
        return length;
    }

    /**
     *  Returns the ptr to the data.
     */
    const void* data() const {
        const void* address = fSpan.data();
        return address;
    }

    bool empty() const {
        const bool is_empty = fSpan.empty();
        return is_empty;
    }

    const uint8_t* bytes() const {
        const uint8_t* address = reinterpret_cast<const uint8_t*>(data());
        return address;
    }

    SkSpan<const uint8_t> byteSpan() const {
        const SkSpan<const uint8_t> view = {bytes(), size()};
        return view;
    }

    /**
     *  USE WITH CAUTION.
     *  The caller must have exclusive write access, including shared subsets.
     *  The writable borrow lasts only while the backing owner remains alive.
     */
    void* writable_data() {
        void* address = fSpan.data();
        return address;
    }

    /** Attempt to create a deep copy of the original data, using the default allocator.
     *
     *  If  offset+length > this->size(), then this returns nullptr.
     */
    sk_sp<SkData> copySubset(size_t offset, size_t length) const;

    /** Attempt to return a data that is a reference to a subset of the original data,
     *  This will never make a deep copy of the contents, but will retain a reference
     *  to the original data object.
     *
     *  If  offset+length > this->size(), then this returns nullptr.
     */
    sk_sp<SkData> shareSubset(size_t offset, size_t length);
    sk_sp<const SkData> shareSubset(size_t offset, size_t length) const;

    /**
     *  Helper to copy a range of the data into a caller-provided buffer.
     *  Returns the actual number of bytes copied, after clamping offset and
     *  length to the size of this data. If buffer is NULL, it is ignored, and
     *  only the computed number of bytes is returned.
     */
    size_t copyRange(size_t offset, size_t length, void* buffer) const;

    /**
     *  Function that, if provided, will be called when the SkData goes out
     *  of scope, allowing for custom allocation/freeing of the data's contents.
     */
    using ReleaseProc = void (*)(const void* address, void* context);

    /**
     *  Create a new dataref by copying the specified data
     */
    static sk_sp<SkData> MakeWithCopy(const void* data, size_t length);


    /**
     *  Create a new data with uninitialized contents. The caller should call writable_data()
     *  to write into the buffer, but this must be done before another ref() is made.
     */
    static sk_sp<SkData> MakeUninitialized(size_t length);

    /**
     *  Create a new data with zero-initialized contents. The caller should call writable_data()
     *  to write into the buffer, but this must be done before another ref() is made.
     */
    static sk_sp<SkData> MakeZeroInitialized(size_t length);

    /**
     *  Create a new dataref by copying the specified c-string
     *  (a null-terminated array of bytes). The returned SkData will have size()
     *  equal to strlen(cstr) + 1. If cstr is NULL, it will be treated the same
     *  as "".
     */
    static sk_sp<SkData> MakeWithCString(const char cstr[]);

    /**
     *  Create a new dataref, taking the ptr as is, and using the
     *  releaseproc to free it. The proc may be NULL.
     */
    static sk_sp<SkData> MakeWithProc(const void* ptr, size_t length, ReleaseProc proc, void* ctx);

    /**
     *  Call this when the data parameter is already const and will outlive the lifetime of the
     *  SkData. Suitable for globals. For bytes that live only as long as a scope, use
     *  SkBorrowedData, which checks that the borrow ends with the scope.
     */
    static sk_sp<SkData> MakeWithoutCopy(const void* data, size_t length) {
        sk_sp<SkData> result = MakeWithProc(data, length, NoopReleaseProc, nullptr);
        return result;
    }

    /**
     *  Create a new dataref from a pointer allocated by malloc. The Data object
     *  takes ownership of that allocation, and will call sk_free.
     */
    static sk_sp<SkData> MakeFromMalloc(const void* data, size_t length);

    /**
     *  Create a new dataref the file with the specified path.
     *  If the file cannot be opened, this returns NULL.
     */
    static sk_sp<SkData> MakeFromFileName(const char path[]);

    /**
     *  Create a new dataref from a stdio FILE.
     *  This does not take ownership of the FILE, nor close it.
     *  The caller is free to close the FILE at its convenience.
     *  The FILE must be open for reading only.
     *  Returns NULL on failure.
     */
    static sk_sp<SkData> MakeFromFILE(FILE* f);

    /**
     *  Create a new dataref from a file descriptor.
     *  This does not take ownership of the file descriptor, nor close it.
     *  The caller is free to close the file descriptor at its convenience.
     *  The file descriptor must be open for reading only.
     *  Returns NULL on failure.
     */
    static sk_sp<SkData> MakeFromFD(int fd);

    /**
     *  Attempt to read size bytes into a SkData. If the read succeeds, return the data,
     *  else return NULL. Either way the stream's cursor may have been changed as a result
     *  of calling read().
     */
    static sk_sp<SkData> MakeFromStream(SkStream*, size_t size);

    /**
     *  DEPRECATED variant of src->shareSubset(offset, length)
     *
     *  This variant checks if shareSubset() returned null (because offset or length were out-of-range)
     *  and returns an empty SkData, rather than returning null.
     */
    static sk_sp<SkData> MakeSubset(const SkData* src, size_t offset, size_t length) {
        SkData& source = *const_cast<SkData*>(src);
        sk_sp<SkData> result = source.shareSubset(offset, length);
        if (result) return result;
        result = SkData::MakeEmpty();
        return result;
    }

    /**
     *  Returns a new empty dataref (or a reference to a shared empty dataref).
     *  New or shared, the caller must see that unref() is eventually called.
     */
    static sk_sp<SkData> MakeEmpty();

    /**
     *  DEPRECATED -- use empty()
     */
    bool isEmpty() const {
        const bool is_empty = fSpan.empty();
        return is_empty;
    }

private:
    friend class SkNVRefCnt<SkData>;
    ReleaseProc         fReleaseProc;
    void*               fReleaseProcContext;
    SkSpan<std::byte>   fSpan;

    SkData(SkSpan<std::byte>, ReleaseProc, void* context);
    explicit SkData(size_t size);   // inplace new/delete
    ~SkData();

    // Ensure the unsized delete is called.
    void operator delete(void* p);

    // shared internal factory
    static sk_sp<SkData> PrivateNewWithCopy(const void* srcOrNull, size_t length);

    static void NoopReleaseProc(const void*, void*); // {}

    using INHERITED = SkRefCnt;
};

/** Lends bytes the caller owns as SkData for this object's lifetime, so an image built on them
 *  is drawn without a copy. The bytes must stay valid and unchanged until it is destroyed.
 *  Every image, shader or other holder made from data() must be released first: the
 *  destructor stops the program (a Release check) if anything still holds the data, because
 *  the bytes may go away with the scope. Declare it before the images made from it, so they
 *  are destroyed first. */
class SK_API SkBorrowedData {
public:
    SkBorrowedData(const void* bytes, size_t length);
    ~SkBorrowedData();
    SkBorrowedData(const SkBorrowedData&) = delete;
    SkBorrowedData& operator=(const SkBorrowedData&) = delete;

    /** The borrowed data; a holder made from it must not outlive this object. */
    const sk_sp<SkData>& data() const { return fData; }

private:
    sk_sp<SkData> fData;
};

#endif
