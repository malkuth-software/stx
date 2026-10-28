/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef SkRecorder_DEFINED
#define SkRecorder_DEFINED

#include "include/private/SkAPI.h"

class SkCanvas;
class SkSurface;

namespace skcpu {
class Recorder;
}

// The library's CPU recorder is the only SkRecorder: callers receive recorders but cannot derive
// new kinds, because the library downcasts every SkRecorder by its type().
class SK_API SkRecorder {
public:
    virtual ~SkRecorder() = default;
    SkRecorder(const SkRecorder&) = delete;
    SkRecorder(SkRecorder&&) = delete;
    SkRecorder& operator=(const SkRecorder&) = delete;

    // Only the CPU recorder is admitted; GPU backends are not built.
    enum class Type {
        kCPU,
    };

    virtual Type type() const = 0;

    virtual skcpu::Recorder* cpuRecorder() = 0;

private:
    SkRecorder() = default;
    friend class skcpu::Recorder;
};

#endif
