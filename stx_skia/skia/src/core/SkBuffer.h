/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkBuffer_DEFINED
#define SkBuffer_DEFINED

#include "include/private/SkAssert.h"
#include "include/private/SkNoncopyable.h"
#include "src/core/SkSafeMath.h"

#include <cstddef>
#include <cstdint>

using SkScalar = float;

/** \class SkRBuffer

    Light weight class for reading data from a memory block.
    The RBuffer is given the buffer to read from, with either a specified size
    or no size (in which case no range checking is performed). It is iillegal
    to attempt to read a value from an empty RBuffer (data == null).
*/
class SkRBuffer : SkNoncopyable {
public:
    SkRBuffer() = default;

    /** Initialize RBuffer with a data point and length.
    */
    SkRBuffer(const void* data, size_t size) {
        SkASSERT_RELEASE(data != nullptr || size == 0);
        fData = static_cast<const char*>(data);
        fPos = static_cast<const char*>(data);
        fStop = static_cast<const char*>(data) + size;
    }

    /** Return the number of bytes that have been read from the beginning
        of the data pointer.
    */
    size_t pos() const { return static_cast<size_t>(fPos - fData); }
    /** Return the total size of the data pointer. Only defined if the length was
        specified in the constructor or in a call to reset().
    */
    size_t size() const { return static_cast<size_t>(fStop - fData); }
    /** Return true if the buffer has read to the end of the data pointer.
        Only defined if the length was specified in the constructor or in a call
        to reset(). Always returns true if the length was not specified.
    */
    bool eof() const { return fPos >= fStop; }

    size_t available() const { return static_cast<size_t>(fStop - fPos); }

    bool isValid() const { return fValid; }

    /** Read the specified number of bytes from the data pointer. If buffer is not
        null, copy those bytes into buffer; if it is null, only skip them.
    */
    bool read(void* buffer, size_t size);
    bool skipToAlign4();

    bool readU8(uint8_t* x)   { return (*this).read(x, 1); }
    bool readS32(int32_t* x)  { return (*this).read(x, 4); }
    bool readU32(uint32_t* x) { return (*this).read(x, 4); }

    // returns nullptr on failure
    const void* skip(size_t bytes);
    template <typename T> const T* skipCount(size_t count) {
        return static_cast<const T*>((*this).skip(SkSafeMath::Mul(count, sizeof(T))));
    }

private:
    const char* fData = nullptr;
    const char* fPos = nullptr;
    const char* fStop = nullptr;
    bool        fValid = true;
};

/** \class SkWBuffer

    Light weight class for writing data to a memory block.
    The WBuffer is given the buffer to write into, with either a specified size
    or no size, in which case no range checking is performed. An empty WBuffer
    is legal, in which case no data is ever written, but the relative pos()
    is updated. Writing past the specified size of a sized buffer stops the
    program.
*/
class SkWBuffer : SkNoncopyable {
public:
    SkWBuffer() = default;
    explicit SkWBuffer(void* data) { reset(data); }
    SkWBuffer(void* data, size_t size) { reset(data, size); }

    void reset(void* data) {
        fData = static_cast<char*>(data);
        fOffset = 0;
        fBounded = false;  // no bounds checking
    }

    // A null 'data' (with a size of 0) only measures, as in the intake code, whose null end
    // pointer meant no bounds checking.
    void reset(void* data, size_t size) {
        SkASSERT_RELEASE(data != nullptr || size == 0);
        fData = static_cast<char*>(data);
        fOffset = 0;
        fCapacity = size;
        fBounded = data != nullptr;
    }

    size_t  pos() const { return fOffset; }
    void*   skip(size_t size); // return start of skipped data

    void write(const void* buffer, size_t size) {
        if (size) {
            (*this).writeBytes(buffer, size);
        }
    }

    size_t  padToAlign4();

    void    writePtr(const void* x) { (*this).writeBytes(&x, sizeof(x)); }
    void    writeScalar(SkScalar x) { (*this).writeBytes(&x, 4); }
    void    write32(int32_t x) { (*this).writeBytes(&x, 4); }
    void    write16(int16_t x) { (*this).writeBytes(&x, 2); }
    void    write8(int8_t x) { (*this).writeBytes(&x, 1); }
    void    writeBool(bool x) { (*this).write8(x); }

private:
    // Advances by 'size', copying from 'buffer' when both it and the storage are present. A
    // sized buffer refuses to pass its end.
    void    writeBytes(const void* buffer, size_t size);

    // The position is an offset, so that a measuring buffer (no storage) does no arithmetic on
    // a null pointer.
    char*  fData = nullptr;
    size_t fOffset = 0;
    size_t fCapacity = 0;
    bool   fBounded = false;
};

#endif
