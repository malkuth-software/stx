/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkString_DEFINED
#define SkString_DEFINED

#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/private/SkTo.h"
#include "include/private/SkTypeTraits.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

/*  Some helper functions for C strings */
inline int SkStrFind(const char string[], const char substring[]) {
    const char* first = std::strstr(string, substring);
    if (first == nullptr) return -1;
    return SkToInt(first - &string[0]);
}

inline bool SkStrContains(const char string[], char subchar) {
    SkASSERT(string);
    const char text[2] = {subchar, '\0'};
    return SkStrFind(string, text) != -1;
}

/** \class SkString

    Light weight class for managing strings. Uses reference
    counting to make string assignments and copies very fast
    with no extra RAM cost. Assumes UTF8 encoding.
*/
class SK_API SkString {
public:
    // A string holds at most UINT32_MAX bytes; constructing, setting or inserting past that
    // stops the program. A null text with a nonzero length gives that many zero bytes to the
    // constructors and set; insert and equals require a text for a nonzero length. Texts may
    // point into the string they are inserted into or set on.
    SkString();
    explicit SkString(size_t len);
    explicit SkString(const char text[]);
    SkString(const char text[], size_t len);
    SkString(const SkString&);
    SkString(SkString&&);
    ~SkString();

    bool isEmpty() const { return (*fRec).fLength == 0; }
    size_t size() const { return static_cast<size_t>((*fRec).fLength); }
    const char* data() const { return (*fRec).data(); }
    const char* c_str() const { return (*fRec).data(); }

    bool equals(const SkString&) const;
    bool equals(const char text[]) const;
    bool equals(const char text[], size_t len) const;

    bool contains(char subchar) const {
        return SkStrContains((*fRec).data(), subchar);
    }

    friend bool operator==(const SkString& a, const SkString& b) {
        return a.equals(b);
    }
    friend bool operator!=(const SkString& a, const SkString& b) {
        return !a.equals(b);
    }

    // these methods edit the string

    SkString& operator=(const SkString&);
    SkString& operator=(SkString&&);

    char* data();

    void reset();
    void set(const char text[]);
    void set(const char text[], size_t len);

    void insert(size_t offset, const char text[]);
    void insert(size_t offset, const char text[], size_t len);
    void insertUnichar(size_t offset, SkUnichar);

    void append(const char text[]) { (*this).insert(kEnd, text); }
    void append(const char text[], size_t len) { (*this).insert(kEnd, text, len); }
    void append(const SkString& str) { (*this).insert(kEnd, str.c_str(), str.size()); }
    void appendUnichar(SkUnichar uni) { (*this).insertUnichar(kEnd, uni); }

    void printf(const char format[], ...) SK_PRINTF_LIKE(2, 3);
    void printVAList(const char format[], va_list) SK_PRINTF_LIKE(2, 0);
    void appendf(const char format[], ...) SK_PRINTF_LIKE(2, 3);
    void appendVAList(const char format[], va_list) SK_PRINTF_LIKE(2, 0);

    /**
     *  Swap contents between this and other. This function is guaranteed
     *  to never fail or throw.
     */
    void swap(SkString& other);

    using sk_is_trivially_relocatable = std::true_type;

private:
    // An insertion offset past every string: insert clamps it to the end.
    static constexpr size_t kEnd = static_cast<size_t>(-1);

    struct Rec {
    public:
        constexpr Rec(uint32_t len, int32_t refCnt) : fLength(len), fRefCnt(refCnt) {}
        static sk_sp<Rec> Make(const char text[], size_t len);

        char* data() { return fBeginningOfData; }
        const char* data() const { return fBeginningOfData; }

        void ref() const;
        void unref() const;
        bool unique() const;
#ifdef SK_DEBUG
        int32_t getRefCnt() const;
#endif
        uint32_t fLength; // logically size_t, but we want it to stay 32 bits

    private:
        mutable std::atomic<int32_t> fRefCnt;
        char fBeginningOfData[1] = {'\0'};

        // Ensure the unsized delete is called.
        void operator delete(void* p) { ::operator delete(p); }
    };
    sk_sp<Rec> fRec;

    static_assert(::sk_is_trivially_relocatable<sk_sp<Rec>>::value);

#ifdef SK_DEBUG
    SkString& validate();
    const SkString& validate() const;
#else
    SkString& validate() { return *this; }
    const SkString& validate() const { return *this; }
#endif

    static const Rec gEmptyRec;
};

/// Creates a new string and writes into it using a printf()-style format.
SK_API SkString SkStringPrintf(const char* format, ...) SK_PRINTF_LIKE(1, 2);

#endif
