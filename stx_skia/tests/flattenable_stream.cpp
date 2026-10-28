#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkSerialProcs.h"
#include "include/core/SkShader.h"
#include "include/core/SkStream.h"
#include "stream_sink.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "src/core/SkWriteBuffer.h"
#include "src/core/SkReadBuffer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef STX_SKIA_REFERENCE
template <typename T> concept HasFactoryRecorder = requires { &T::setFactoryRecorder; };
static_assert(!HasFactoryRecorder<SkBinaryWriteBuffer>);
template <typename T> concept HasFactoryRegistry = requires { &T::Register; };
template <typename T> concept HasFactoryLookup = requires { &T::NameToFactory; };
static_assert(!HasFactoryRegistry<SkFlattenable>);
static_assert(!HasFactoryLookup<SkFlattenable>);
template <typename T> concept HasFactoryPlayback = requires { &T::setFactoryPlayback; };
template <typename T> concept HasRecursionLimit = requires { &T::setRecursionLimit; };
static_assert(!HasFactoryPlayback<SkReadBuffer>);
static_assert(!HasRecursionLimit<SkReadBuffer>);
template <typename T> concept HasStreamInput = requires { &T::writeStream; };
static_assert(!HasStreamInput<SkBinaryWriteBuffer>);
#endif

namespace {
bool reader_contract() {
    // A null effect consumes exactly one word; following primitive data remains readable.
    const std::array<uint32_t, 2> sentinel{0, 0x12345678U};
    SkReadBuffer reader(sentinel.data(), sizeof(sentinel));
    if (reader.readRawFlattenable() != nullptr || !reader.isValid()) return false;
    if (reader.offset() != sizeof(uint32_t) || reader.readUInt() != sentinel[1]) return false;
    if (!reader.isValid() || !reader.eof()) return false;
#ifndef STX_SKIA_REFERENCE
    const std::array<uint32_t, 5> tokens{1, 0x100U, 0x10000U, 0x80000000U, 0xffffffffU};
    for (const uint32_t token : tokens) {
        const std::array<uint32_t, 2> input{token, sentinel[1]};
        SkReadBuffer rejected(input.data(), sizeof(input));
        if (rejected.readRawFlattenable() != nullptr || rejected.isValid()) return false;
        if (!rejected.eof() || rejected.readUInt() != 0) return false;
    }
    // A valid, non-null empty extent cannot supply even the null marker.
    SkReadBuffer empty(sentinel.data(), 0);
    if (empty.readRawFlattenable() != nullptr || empty.isValid()) return false;
#endif
    return true;
}

bool writer_reuse() {
#ifndef STX_SKIA_REFERENCE
    const SkSerialProcs procs{};
    SkBinaryWriteBuffer reused(procs);
    reused.write(nullptr, 0);
    reused.writePad32(nullptr, 0);
    reused.writeToMemory(nullptr);
    sk_sp<SkData> empty = reused.snapshotAsData();
    if (!empty || (*empty).size() != 0 || reused.bytesWritten() != 0) return false;
    sk_sp<SkShader> shader = SkShaders::Color(0xff123456U);
    if (!shader) return false;
    SkBinaryWriteBuffer fresh(procs);
    fresh.writeFlattenable(shader.get());
    fresh.writeFlattenable(shader.get());
    fresh.writeUInt(0x12345678U);
    sk_sp<SkData> expected = fresh.snapshotAsData();
    if (!expected) return false;
    std::array<uint32_t, 1026> external{};
    const std::array<size_t, 4> capacities{0, 4, 16, 4096};
    for (const size_t capacity : capacities) {
        for (uint32_t& word : external) word = 0xaabbccddU;
        reused.reset(external.data() + 1, capacity);
        if (reused.bytesWritten() != 0 || !reused.usingInitialStorage()) return false;
        reused.writeFlattenable(shader.get());
        reused.writeFlattenable(shader.get());
        reused.writeUInt(0x12345678U);
        sk_sp<SkData> snapshot = reused.snapshotAsData();
        if (!snapshot || *snapshot != *expected) return false;
        const bool fits = (*expected).size() <= capacity;
        if (reused.usingInitialStorage() != fits) return false;
        if (external.front() != 0xaabbccddU || external[1 + capacity / 4] != 0xaabbccddU) return false;
        ByteSink stream{};
        if (!reused.writeToStream(&stream) || stream.bytesWritten() != (*expected).size()) return false;
        const std::vector<uint8_t>& copied = stream.bytes();
        if (std::memcmp(copied.data(), (*expected).data(), copied.size()) != 0) return false;
        reused.reset();
        sk_sp<SkData> resetEmpty = reused.snapshotAsData();
        if (!resetEmpty || (*resetEmpty).size() != 0 || *snapshot != *expected) return false;
    }
#endif
    return true;
}

bool append_record(const SkFlattenable& effect, std::vector<uint8_t>& output) {
    sk_sp<SkData> serialized = effect.serialize();
    if (!serialized || (*serialized).size() == 0) return false;
    const size_t bytes = (*serialized).size();
    if (bytes % sizeof(uint32_t) != 0) return false;
    std::vector<uint32_t> storage(bytes / sizeof(uint32_t));
    const size_t written = effect.serialize(storage.data(), bytes);
    if (written != bytes || std::memcmp(storage.data(), (*serialized).data(), bytes) != 0) return false;
    const SkSerialProcs procs{};
    SkBinaryWriteBuffer writer(procs);
    writer.writeFlattenable(&effect);
    if (writer.bytesWritten() != bytes) return false;
    std::vector<uint32_t> direct(storage.size());
    writer.writeToMemory(direct.data());
    if (storage != direct) return false;
    sk_sp<SkFlattenable> decoded = SkFlattenable::Deserialize(
        effect.getFlattenableType(), (*serialized).data(), bytes);
#ifdef STX_SKIA_REFERENCE
    if (!decoded || (*decoded).getFlattenableType() != effect.getFlattenableType()) return false;
#else
    if (decoded) return false;
    if (SkFlattenable::Deserialize(effect.getFlattenableType(), nullptr, bytes)) return false;
    if (SkFlattenable::Deserialize(effect.getFlattenableType(), nullptr, 0)) return false;
    if (effect.serialize(nullptr, bytes) != 0) return false;
    if (effect.serialize(nullptr, 0) != 0) return false;
    std::array<uint32_t, 3> guard{0xaabbccddU, 0xaabbccddU, 0xaabbccddU};
    const std::array<uint32_t, 3> unchanged = guard;
    uint8_t* misaligned = reinterpret_cast<uint8_t*>(guard.data()) + 1;
    if (effect.serialize(misaligned, sizeof(guard) - 1) != 0 || guard != unchanged) return false;
    if (effect.serialize(guard.data(), 0) != 0 || guard != unchanged) return false;
    if (effect.serialize(guard.data() + 1, sizeof(uint32_t)) != 0) return false;
    if (guard.front() != unchanged.front() || guard.back() != unchanged.back()) return false;
#endif
    const uint8_t* data = static_cast<const uint8_t*>((*serialized).data());
    output.insert(output.end(), data, data + bytes);
    return true;
}

bool streams(std::vector<uint8_t>& output, int& cases) {
    for (int variation = 0; variation < 4; ++variation) {
        const float offset = static_cast<float>(variation) * 0.125f;
        const std::array<float, 20> matrix{
            0.75f, 0.0f, 0.0f, 0.0f, offset,
            0.0f, 0.625f, 0.0f, 0.0f, 0.0625f,
            0.0f, 0.0f, 0.5f, 0.0f, 0.125f,
            0.0f, 0.0f, 0.0f, 0.875f, 0.0f};
        sk_sp<SkShader> shader = SkShaders::Color(SkColor4f{offset, 0.5f, 0.75f, 0.625f}, nullptr);
        sk_sp<SkColorFilter> color = SkColorFilters::Matrix(matrix.data());
        sk_sp<SkMaskFilter> blur = SkMaskFilter::MakeBlur(static_cast<SkBlurStyle>(variation), 1.0f + offset);
        sk_sp<SkMaskFilter> shaderMask = SkShaderMaskFilter::Make(shader);
        const std::array<float, 4> intervals{2.0f, 3.0f, 5.0f, 7.0f};
        const SkSpan<const float> intervalView(intervals.data(), intervals.size());
        sk_sp<SkPathEffect> dash = SkDashPathEffect::Make(intervalView, offset);
        if (!shader || !color || !blur || !shaderMask || !dash) return false;
        const std::array<const SkFlattenable*, 5> effects{
            shader.get(), color.get(), blur.get(), shaderMask.get(), dash.get()};
        const SkSerialProcs procs{};
        SkBinaryWriteBuffer growing(procs);
        std::array<uint32_t, 2048> fixedStorage{};
        SkBinaryWriteBuffer fixed(fixedStorage.data(), sizeof(fixedStorage), procs);
        growing.writeFlattenable(nullptr);
        fixed.writeFlattenable(nullptr);
        for (const SkFlattenable* effect : effects) {
            if (!append_record(*effect, output)) return false;
            growing.writeFlattenable(effect);
            fixed.writeFlattenable(effect);
            ++cases;
        }
        // Reuse each type name and a nested shader name in the same stream.
        for (size_t index = effects.size(); index > 0; --index) {
            growing.writeFlattenable(effects[index - 1]);
            fixed.writeFlattenable(effects[index - 1]);
        }
        const size_t bytes = growing.bytesWritten();
        if (!fixed.usingInitialStorage() || fixed.bytesWritten() != bytes) return false;
        if (bytes % sizeof(uint32_t) != 0) return false;
        std::vector<uint32_t> contents(bytes / sizeof(uint32_t));
        growing.writeToMemory(contents.data());
        if (std::memcmp(contents.data(), fixedStorage.data(), bytes) != 0) return false;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(contents.data());
        output.insert(output.end(), data, data + bytes);
        ++cases;
    }
    return true;
}
}

int main(int argc, char** argv) {
    std::vector<uint8_t> output{};
    int cases = 0;
    if (!reader_contract() || !writer_reuse() || !streams(output, cases)) return 1;
    uint64_t digest = UINT64_C(1469598103934665603);
    for (const uint8_t byte : output) {
        digest ^= byte;
        digest *= UINT64_C(1099511628211);
    }
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 2;
        const size_t written = std::fwrite(output.data(), 1, output.size(), file);
        const int closed = std::fclose(file);
        if (written != output.size() || closed != 0) return 3;
    }
    std::printf("flattenable streams: %d cases, %zu bytes, hash=%llu\n", cases, output.size(),
                static_cast<unsigned long long>(digest));
    std::printf("writer_size=%zu reader_size=%zu\n", sizeof(SkBinaryWriteBuffer), sizeof(SkReadBuffer));
    return 0;
}
