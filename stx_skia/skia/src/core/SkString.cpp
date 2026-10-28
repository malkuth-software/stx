/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkString.h"

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/private/SkDebug.h"
#include "include/private/SkMalloc.h"
#include "include/private/SkTFitsIn.h"
#include "include/private/SkTo.h"
#include "src/core/SkSafeMath.h"
#include "src/core/SkUTF.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <utility>

// number of bytes (on the stack) to receive the printf result
static constexpr size_t kBufferSize = 1024;

struct StringBuffer {
    char* fText = nullptr;
    int fLength = 0;
};

template <int SIZE>
static StringBuffer apply_format_string(const char* format, va_list args, char (&stackBuffer)[SIZE],
                                        SkString* heapBuffer) SK_PRINTF_LIKE(1, 0);

template <int SIZE>
static StringBuffer apply_format_string(const char* format, va_list args, char (&stackBuffer)[SIZE],
                                        SkString* heapBuffer) {
    // First, attempt to print directly to the stack buffer.
    va_list argsCopy{};
    va_copy(argsCopy, args);
    const int outLength = std::vsnprintf(stackBuffer, SIZE, format, args);
    if (outLength < 0) {
        SkDebugf("SkString: vsnprintf reported error.");
        va_end(argsCopy);
        return {stackBuffer, 0};
    }
    if (outLength < SIZE) {
        va_end(argsCopy);
        return {stackBuffer, outLength};
    }

    // Our text was too long to fit on the stack! However, we now know how much space we need to
    // format it. Format the string into our heap buffer. `set` automatically reserves an extra
    // byte at the end of the buffer for a null terminator, so we don't need to add one here.
    (*heapBuffer).set(nullptr, static_cast<size_t>(outLength));
    char* heapBufferDest = (*heapBuffer).data();
    const size_t capacity = static_cast<size_t>(outLength) + 1;
    SkDEBUGCODE(int checkLength =) std::vsnprintf(heapBufferDest, capacity, format, argsCopy);
    SkASSERT(checkLength == outLength);
    va_end(argsCopy);
    return {heapBufferDest, outLength};
}

///////////////////////////////////////////////////////////////////////////////

const SkString::Rec SkString::gEmptyRec(0, 0);

// The bytes a Rec holds before its text.
static size_t size_of_rec(const char* emptyData, const void* emptyRec) {
    const size_t size = static_cast<size_t>(emptyData - static_cast<const char*>(emptyRec));
    return size;
}

// Whether text points into the length bytes at begin or at their terminator. An in-place edit
// moves or overwrites those bytes, so text there must be read before the edit.
static bool points_into(const char* begin, size_t length, const char* text) {
    const std::less_equal<const char*> notAfter{};
    const std::less<const char*> before{};
    const bool inside = notAfter(begin, text) && before(text, begin + length + 1);
    return inside;
}

sk_sp<SkString::Rec> SkString::Rec::Make(const char text[], size_t len) {
    if (len == 0) {
        sk_sp<Rec> empty(const_cast<Rec*>(&gEmptyRec));
        return empty;
    }

    SkSafeMath safe{};
    // We store a 32bit version of the length
    const uint32_t stringLen = safe.castTo<uint32_t>(len);
    // Add the Rec's overhead and 1 for null-termination
    const size_t overhead = size_of_rec(gEmptyRec.data(), &gEmptyRec);
    size_t allocationSize = safe.add(len, overhead + sizeof(char));
    // Align up to a multiple of 4
    allocationSize = safe.alignUp(allocationSize, 4);

    SkASSERT_RELEASE(safe.ok());

    void* storage = ::operator new(allocationSize);
    sk_sp<Rec> rec(new (storage) Rec(stringLen, 1));
    if (text) {
        std::memcpy((*rec).data(), text, len);
    } else {
        std::memset((*rec).data(), 0, len);
    }
    (*rec).data()[len] = 0;
    return rec;
}

void SkString::Rec::ref() const {
    if (this == &SkString::gEmptyRec) {
        return;
    }
    SkAssertResult(fRefCnt.fetch_add(+1, std::memory_order_relaxed));
}

void SkString::Rec::unref() const {
    if (this == &SkString::gEmptyRec) {
        return;
    }
    const int32_t oldRefCnt = fRefCnt.fetch_add(-1, std::memory_order_acq_rel);
    SkASSERT(oldRefCnt);
    if (oldRefCnt == 1) {
        delete this;
    }
}

bool SkString::Rec::unique() const {
    const bool result = fRefCnt.load(std::memory_order_acquire) == 1;
    return result;
}

#ifdef SK_DEBUG
int32_t SkString::Rec::getRefCnt() const {
    return fRefCnt.load(std::memory_order_relaxed);
}

const SkString& SkString::validate() const {
    // make sure no one has written over our global
    SkASSERT(gEmptyRec.fLength == 0);
    SkASSERT(gEmptyRec.getRefCnt() == 0);
    SkASSERT(gEmptyRec.data()[0] == 0);

    if (fRec.get() != &gEmptyRec) {
        SkASSERT((*fRec).fLength > 0);
        SkASSERT((*fRec).getRefCnt() > 0);
        SkASSERT((*fRec).data()[(*fRec).fLength] == 0);
    }
    return *this;
}

SkString& SkString::validate() {
    static_cast<const SkString&>(*this).validate();
    return *this;
}
#endif

///////////////////////////////////////////////////////////////////////////////

SkString::SkString() : fRec(const_cast<Rec*>(&gEmptyRec)) {}

SkString::SkString(size_t len) : fRec(Rec::Make(nullptr, len)) {}

SkString::SkString(const char text[]) : fRec(Rec::Make(text, text ? std::strlen(text) : 0)) {}

SkString::SkString(const char text[], size_t len) : fRec(Rec::Make(text, len)) {}

SkString::SkString(const SkString& src) : fRec(src.validate().fRec) {}

SkString::SkString(SkString&& src) : fRec(std::move(src.validate().fRec)) {
    src.fRec.reset(const_cast<Rec*>(&gEmptyRec));
}

SkString::~SkString() {
    (*this).validate();
}

bool SkString::equals(const SkString& src) const {
    return fRec == src.fRec || (*this).equals(src.c_str(), src.size());
}

bool SkString::equals(const char text[]) const {
    return (*this).equals(text, text ? std::strlen(text) : 0);
}

bool SkString::equals(const char text[], size_t len) const {
    SkASSERT_RELEASE(len == 0 || text != nullptr);

    return (*fRec).fLength == len && sk_careful_memcmp((*fRec).data(), text, len) == 0;
}

SkString& SkString::operator=(const SkString& src) {
    (*this).validate();
    fRec = src.fRec;  // sk_sp<Rec>::operator=(const sk_sp<Ref>&) checks for self-assignment.
    return *this;
}

SkString& SkString::operator=(SkString&& src) {
    (*this).validate();

    if (fRec != src.fRec) {
        (*this).swap(src);
    }
    return *this;
}

void SkString::reset() {
    (*this).validate();
    fRec.reset(const_cast<Rec*>(&gEmptyRec));
}

char* SkString::data() {
    (*this).validate();

    if ((*fRec).fLength) {
        if (!(*fRec).unique()) {
            fRec = Rec::Make((*fRec).data(), (*fRec).fLength);
        }
    }
    return (*fRec).data();
}

void SkString::set(const char text[]) {
    (*this).set(text, text ? std::strlen(text) : 0);
}

void SkString::set(const char text[], size_t len) {
    SkASSERT_RELEASE(SkTFitsIn<uint32_t>(len));
    if (len == 0) {
        (*this).reset();
    } else if ((*fRec).unique() && ((len >> 2) <= ((*fRec).fLength >> 2))) {
        // Use less of the buffer we have without allocating a smaller one. The text may be part
        // of this string, so the copy may overlap.
        char* p = (*this).data();
        if (text) {
            std::memmove(p, text, len);
        } else {
            std::memset(p, 0, len);
        }
        p[len] = '\0';
        (*fRec).fLength = SkToU32(len);
    } else {
        SkString tmp(text, len);
        (*this).swap(tmp);
    }
}

void SkString::insert(size_t offset, const char text[]) {
    (*this).insert(offset, text, text ? std::strlen(text) : 0);
}

void SkString::insert(size_t offset, const char text[], size_t len) {
    if (len) {
        SkASSERT_RELEASE(text != nullptr);
        const size_t length = (*fRec).fLength;
        if (offset > length) {
            offset = length;
        }

        // The result must fit the 32-bit length. Written so that it cannot wrap where size_t
        // is 32 bits wide.
        SkASSERT_RELEASE(len <= UINT32_MAX - length);

        /*  If we're the only owner, and we have room in our allocation for the insert,
            do it in place, rather than allocating a new buffer.

            To know we have room, compare the allocated sizes
            beforeAlloc = SkAlign4(length + 1)
            afterAlloc  = SkAligh4(length + 1 + len)
            but SkAlign4(x) is (x + 3) >> 2 << 2
            which is equivalent for testing to (length + 1 + 3) >> 2 == (length + 1 + 3 + len) >> 2
            and we can then eliminate the +1+3 since that doesn't affec the answer

            Text from this string's own storage would move under the in-place edit; the
            allocating path reads it from the old storage, which stays alive until the swap.
        */
        if ((*fRec).unique() && (length >> 2) == ((length + len) >> 2) &&
            !points_into((*fRec).data(), length, text)) {
            char* dst = (*this).data();

            if (offset < length) {
                std::memmove(dst + offset + len, dst + offset, length - offset);
            }
            std::memcpy(dst + offset, text, len);

            dst[length + len] = 0;
            (*fRec).fLength = SkToU32(length + len);
        } else {
            /*  Seems we should use realloc here, since that is safe if it fails
                (we have the original data), and might be faster than alloc/copy/free.
            */
            SkString tmp((*fRec).fLength + len);
            char* dst = tmp.data();

            if (offset > 0) {
                std::memcpy(dst, (*fRec).data(), offset);
            }
            std::memcpy(dst + offset, text, len);
            if (offset < (*fRec).fLength) {
                std::memcpy(dst + offset + len, (*fRec).data() + offset,
                            (*fRec).fLength - offset);
            }

            (*this).swap(tmp);
        }
    }
}

void SkString::insertUnichar(size_t offset, SkUnichar uni) {
    char buffer[SkUTF::kMaxBytesInUTF8Sequence] = {};
    const size_t len = SkUTF::ToUTF8(uni, buffer);

    if (len) {
        (*this).insert(offset, buffer, len);
    }
}

///////////////////////////////////////////////////////////////////////////////

void SkString::printf(const char format[], ...) {
    va_list args{};
    va_start(args, format);
    (*this).printVAList(format, args);
    va_end(args);
}

void SkString::printVAList(const char format[], va_list args) {
    // The arguments may point into this string, so a long result is formatted into a separate
    // string and replaces this one only afterwards.
    SkString overflow{};
    char stackBuffer[kBufferSize] = {};
    const StringBuffer result = apply_format_string(format, args, stackBuffer, &overflow);

    if (result.fText == stackBuffer) {
        (*this).set(result.fText, static_cast<size_t>(result.fLength));
    } else {
        *this = std::move(overflow);
    }
}

void SkString::appendf(const char format[], ...) {
    va_list args{};
    va_start(args, format);
    (*this).appendVAList(format, args);
    va_end(args);
}

void SkString::appendVAList(const char format[], va_list args) {
    if ((*this).isEmpty()) {
        (*this).printVAList(format, args);
        return;
    }

    SkString overflow{};
    char stackBuffer[kBufferSize] = {};
    const StringBuffer result = apply_format_string(format, args, stackBuffer, &overflow);

    (*this).append(result.fText, static_cast<size_t>(result.fLength));
}

void SkString::swap(SkString& other) {
    (*this).validate();
    other.validate();

    using std::swap;
    swap(fRec, other.fRec);
}

///////////////////////////////////////////////////////////////////////////////

SkString SkStringPrintf(const char* format, ...) {
    SkString formattedOutput{};
    va_list args{};
    va_start(args, format);
    formattedOutput.printVAList(format, args);
    va_end(args);
    return formattedOutput;
}
