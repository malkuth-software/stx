/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/core/SkCPUContext.h"

#include "include/core/SkTypes.h"
#include "src/core/SkCPUContextImpl.h"
#include "src/core/SkGraphicsLifetime.h"
#include "src/core/SkCPURecorderImpl.h"

namespace skcpu {

std::unique_ptr<const Context> Context::Make(const Context::Options& opts) {
    return std::make_unique<ContextImpl>();
}

std::unique_ptr<const Context> Context::Make() {
    return Context::Make({});
}

std::unique_ptr<Recorder> Context::makeRecorder() const {
    return std::make_unique<RecorderImpl>();
}

// The process-wide context, made on first use; SkGraphics::Shutdown() deletes it.
static const ContextImpl*& context_slot() {
    static const ContextImpl* gContext = static_cast<const ContextImpl*>(Context::Make().release());
    return gContext;
}

const ContextImpl* ContextImpl::TODO() {
    SkGraphicsCheckRunning();
    const ContextImpl* context = context_slot();
    return context;
}

}  // namespace skcpu

void SkCPUContextRelease() {
    const skcpu::ContextImpl*& slot = skcpu::context_slot();
    delete slot;
    slot = nullptr;
}
