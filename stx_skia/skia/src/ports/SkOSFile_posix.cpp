/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"
#include "include/private/SkTFitsIn.h"
#include "include/private/SkTemplates.h"
#include "src/core/SkOSFile.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef SK_BUILD_FOR_IOS
#include "src/ports/SkOSFile_ios.h"
#endif

void sk_fsync(FILE* f) {
#if !defined(SK_BUILD_FOR_ANDROID) && !defined(__UCLIBC__) && !defined(_NEWLIB_VERSION)
    int fd = fileno(f);
    fsync(fd);
#endif
}

bool sk_exists(const char *path, SkFILE_Flags flags) {
    int mode = F_OK;
    if (flags & kRead_SkFILE_Flag) {
        mode |= R_OK;
    }
    if (flags & kWrite_SkFILE_Flag) {
        mode |= W_OK;
    }
#ifdef SK_BUILD_FOR_IOS
    // if the default path fails, check the bundle (but only if read-only)
    if (0 == access(path, mode)) {
        return true;
    } else {
        return (kRead_SkFILE_Flag == flags && ios_get_path_in_bundle(path, nullptr));
    }
#else
    return (0 == access(path, mode));
#endif
}

typedef struct {
    dev_t dev;
    ino_t ino;
} SkFILEID;

static bool sk_ino(FILE* a, SkFILEID* id) {
    int fd = fileno(a);
    if (fd < 0) {
        return 0;
    }
    struct stat status = {};
    if (0 != fstat(fd, &status)) {
        return 0;
    }
    id->dev = status.st_dev;
    id->ino = status.st_ino;
    return true;
}

bool sk_fidentical(FILE* a, FILE* b) {
    SkFILEID aID, bID;
    return sk_ino(a, &aID) && sk_ino(b, &bID)
           && aID.ino == bID.ino
           && aID.dev == bID.dev;
}

void sk_fmunmap(const void* addr, size_t length) {
    munmap(const_cast<void*>(addr), length);
}

void* sk_fdmmap(int fd, size_t* size) {
    struct stat status = {};
    if (0 != fstat(fd, &status)) {
        return nullptr;
    }
    if (!S_ISREG(status.st_mode)) {
        return nullptr;
    }
    if (!SkTFitsIn<size_t>(status.st_size)) {
        return nullptr;
    }
    size_t fileSize = static_cast<size_t>(status.st_size);

    void* addr = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (MAP_FAILED == addr) {
        return nullptr;
    }

    *size = fileSize;
    return addr;
}

int sk_fileno(FILE* f) {
    return fileno(f);
}

void* sk_fmmap(FILE* f, size_t* size) {
    int fd = sk_fileno(f);
    if (fd < 0) {
        return nullptr;
    }

    return sk_fdmmap(fd, size);
}

size_t sk_qread(FILE* file, void* buffer, size_t count, size_t offset) {
    int fd = sk_fileno(file);
    if (fd < 0) {
        return SIZE_MAX;
    }
    ssize_t bytesRead = pread(fd, buffer, count, offset);
    if (bytesRead < 0) {
        return SIZE_MAX;
    }
    return bytesRead;
}
