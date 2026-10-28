/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"
#if defined(SK_BUILD_FOR_WIN)

#include "include/private/SkNoncopyable.h"
#include "include/private/SkTFitsIn.h"
#include "src/core/SkLeanWindows.h"
#include "src/core/SkOSFile.h"

#include <io.h>
#include <stdio.h>
#include <sys/stat.h>

void sk_fsync(FILE* f) {
    _commit(sk_fileno(f));
}

bool sk_exists(const char *path, SkFILE_Flags flags) {
    int mode = 0; // existence
    if (flags & kRead_SkFILE_Flag) {
        mode |= 4; // read
    }
    if (flags & kWrite_SkFILE_Flag) {
        mode |= 2; // write
    }
    return (0 == _access(path, mode));
}

typedef struct {
    ULONGLONG fVolume;
    ULONGLONG fLsbSize;
    ULONGLONG fMsbSize;
} SkFILEID;

static bool sk_ino(FILE* f, SkFILEID* id) {
    int fileno = _fileno((FILE*)f);
    if (fileno < 0) {
        return false;
    }

    HANDLE file = (HANDLE)_get_osfhandle(fileno);
    if (INVALID_HANDLE_VALUE == file) {
        return false;
    }

    //TODO: call GetFileInformationByHandleEx on Vista and later with FileIdInfo.
    BY_HANDLE_FILE_INFORMATION info;
    if (0 == GetFileInformationByHandle(file, &info)) {
        return false;
    }
    id->fVolume = info.dwVolumeSerialNumber;
    id->fLsbSize = info.nFileIndexLow + (((ULONGLONG)info.nFileIndexHigh) << 32);
    id->fMsbSize = 0;

    return true;
}

bool sk_fidentical(FILE* a, FILE* b) {
    SkFILEID aID, bID;
    return sk_ino(a, &aID) && sk_ino(b, &bID)
           && aID.fLsbSize == bID.fLsbSize
           && aID.fMsbSize == bID.fMsbSize
           && aID.fVolume == bID.fVolume;
}

class [[nodiscard]] SkAutoNullKernelHandle : SkNoncopyable {
public:
    SkAutoNullKernelHandle(const HANDLE handle) : fHandle(handle) { }
    ~SkAutoNullKernelHandle() { CloseHandle(fHandle); }
    operator HANDLE() const { return fHandle; }
    bool isValid() const { return SkToBool(fHandle); }
private:
    HANDLE fHandle;
};
typedef SkAutoNullKernelHandle SkAutoWinMMap;

void sk_fmunmap(const void* addr, size_t) {
    UnmapViewOfFile(addr);
}

void* sk_fdmmap(int fileno, size_t* length) {
    HANDLE file = (HANDLE)_get_osfhandle(fileno);
    if (INVALID_HANDLE_VALUE == file) {
        return nullptr;
    }

    LARGE_INTEGER fileSize;
    if (0 == GetFileSizeEx(file, &fileSize)) {
        //TODO: use SK_TRACEHR(GetLastError(), "Could not get file size.") to report.
        return nullptr;
    }
    if (!SkTFitsIn<size_t>(fileSize.QuadPart)) {
        return nullptr;
    }

    SkAutoWinMMap mmap(CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0, nullptr));
    if (!mmap.isValid()) {
        //TODO: use SK_TRACEHR(GetLastError(), "Could not create file mapping.") to report.
        return nullptr;
    }

    // Eventually call UnmapViewOfFile
    void* addr = MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);
    if (nullptr == addr) {
        //TODO: use SK_TRACEHR(GetLastError(), "Could not map view of file.") to report.
        return nullptr;
    }

    *length = static_cast<size_t>(fileSize.QuadPart);
    return addr;
}

int sk_fileno(FILE* f) {
    return _fileno((FILE*)f);
}

void* sk_fmmap(FILE* f, size_t* length) {
    int fileno = sk_fileno(f);
    if (fileno < 0) {
        return nullptr;
    }

    return sk_fdmmap(fileno, length);
}

size_t sk_qread(FILE* file, void* buffer, size_t count, size_t offset) {
    int fileno = sk_fileno(file);
    HANDLE fileHandle = (HANDLE)_get_osfhandle(fileno);
    if (INVALID_HANDLE_VALUE == file) {
        return SIZE_MAX;
    }

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    ULARGE_INTEGER winOffset;
    winOffset.QuadPart = offset;
    overlapped.Offset = winOffset.LowPart;
    overlapped.OffsetHigh = winOffset.HighPart;

    if (!SkTFitsIn<DWORD>(count)) {
        count = std::numeric_limits<DWORD>::max();
    }

    DWORD bytesRead;
    if (ReadFile(fileHandle, buffer, static_cast<DWORD>(count), &bytesRead, &overlapped)) {
        return bytesRead;
    }
    if (GetLastError() == ERROR_HANDLE_EOF) {
        return 0;
    }
    return SIZE_MAX;
}

#endif//defined(SK_BUILD_FOR_WIN)
