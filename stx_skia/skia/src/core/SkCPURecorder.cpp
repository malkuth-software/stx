/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkCPURecorder.h"

#include "include/core/SkTypes.h"
#include "src/core/SkCPUContextImpl.h"
#include "src/core/SkGraphicsLifetime.h"

#include <memory>

namespace skcpu {

// The process-wide recorder, made on first use; surfaces and devices borrow it.
// SkGraphics::Shutdown() deletes it, before the context that made it.
static Recorder*& recorder_slot() {
    static Recorder* gRecorder = (*ContextImpl::TODO()).makeRecorder().release();
    return gRecorder;
}

Recorder* Recorder::TODO() {
    SkGraphicsCheckRunning();
    Recorder* recorder = recorder_slot();
    return recorder;
}

}  // namespace skcpu

void SkCPURecorderRelease() {
    skcpu::Recorder*& slot = skcpu::recorder_slot();
    delete slot;
    slot = nullptr;
}
