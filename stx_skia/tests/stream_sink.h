// A write stream that keeps every byte, for tests that need a sink for SkPath::dump or
// writeToStream. The library keeps only the abstract SkWStream; its concrete streams are not
// part of the candidate.

#ifndef STX_SKIA_TESTS_STREAM_SINK_H
#define STX_SKIA_TESTS_STREAM_SINK_H

#include "include/core/SkStream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class ByteSink final : public SkWStream {
public:
    bool write(const void* buffer, size_t size) override {
        const uint8_t* const bytes = static_cast<const uint8_t*>(buffer);
        fBytes.insert(fBytes.end(), bytes, bytes + size);
        return true;
    }
    size_t bytesWritten() const override { return fBytes.size(); }
    const std::vector<uint8_t>& bytes() const { return fBytes; }

private:
    std::vector<uint8_t> fBytes{};
};

#endif
