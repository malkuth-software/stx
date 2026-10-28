/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkStream_DEFINED
#define SkStream_DEFINED

#include "include/core/SkTypes.h"

#include <cstddef>
#include <cstring>

/**
 *  SkStream -- abstraction for a source of bytes. Subclasses can be backed by
 *  memory, or a file, or something else.
 */
class SK_API SkStream {
public:
    virtual ~SkStream() {}
    SkStream() {}

    /** Reads or skips size number of bytes.
     *  If buffer == NULL, skip size bytes, return how many were skipped.
     *  If buffer != NULL, copy size bytes into buffer, return how many were copied.
     *  @param buffer when NULL skip size bytes, otherwise copy size bytes into buffer
     *  @param size the number of bytes to skip or copy
     *  @return the number of bytes actually read.
     */
    virtual size_t read(void* buffer, size_t size) = 0;

    /** Skip size number of bytes.
     *  @return the actual number bytes that could be skipped.
     */
    size_t skip(size_t size) {
        return (*this).read(nullptr, size);
    }

    /** Returns true when all the bytes in the stream have been read.
     *  As SkStream represents synchronous I/O, isAtEnd returns false when the
     *  final stream length isn't known yet, even when all the bytes available
     *  so far have been read.
     *  This may return true early (when there are no more bytes to be read)
     *  or late (after the first unsuccessful read).
     */
    virtual bool isAtEnd() const = 0;

    /** Returns true if this stream can report its current position. */
    virtual bool hasPosition() const { return false; }
    /** Returns the current position in the stream. If this cannot be done, returns 0. */
    virtual size_t getPosition() const { return 0; }

    /** Returns true if this stream can report its total length. */
    virtual bool hasLength() const { return false; }
    /** Returns the total length of the stream. If this cannot be done, returns 0. */
    virtual size_t getLength() const { return 0; }

private:
    SkStream(SkStream&&) = delete;
    SkStream(const SkStream&) = delete;
    SkStream& operator=(SkStream&&) = delete;
    SkStream& operator=(const SkStream&) = delete;
};

class SK_API SkWStream {
public:
    virtual ~SkWStream();
    SkWStream() {}

    /** Called to write bytes to a SkWStream. Returns true on success
        @param buffer the address of at least size bytes to be written to the stream
        @param size The number of bytes in buffer to write to the stream
        @return true on success
    */
    virtual bool write(const void* buffer, size_t size) = 0;
    virtual void flush();

    virtual size_t bytesWritten() const = 0;

    // helpers

    /** Writes the bytes of a null-terminated string, without the terminator. A null text
        writes nothing and returns false.
    */
    bool writeText(const char text[]) {
        if (text == nullptr) return false;
        return (*this).write(text, std::strlen(text));
    }

private:
    SkWStream(const SkWStream&) = delete;
    SkWStream& operator=(const SkWStream&) = delete;
};

#endif
