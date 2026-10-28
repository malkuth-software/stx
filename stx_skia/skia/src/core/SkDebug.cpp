/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "include/private/SkDebug.h"

#include <cstdarg>
#include <cstdio>

#include "include/private/SkAssert.h" // IWYU pragma: keep
#include "include/private/SkAttributes.h" // IWYU pragma: keep
#include "include/private/SkLog.h"

#if defined(SK_BUILD_FOR_GOOGLE3)
void SkDebugfForDumpStackTrace(const char* data, void* unused) {
    SkDebugf("%s", data);
}
#endif

#if !defined(SkDebugf)
void SkDebugf(const char format[], ...) {
    va_list args;
    va_start(args, format);
    SkLogVAList(SkLogPriority::kDebug, format, args);
    va_end(args);
}
#endif

void sk_assert_failed(const char* file, int line, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
#if defined(SK_BUILD_FOR_WIN)
    SKIA_LOG_E("%s(%d): fatal error: \"%s\"", file, line, message);
#else
    SKIA_LOG_E("%s:%d: fatal error: \"%s\"", file, line, message);
#endif
    SK_DUMP_GOOGLE3_STACK();
    sk_abort_no_print();
}
