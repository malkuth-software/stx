#include "include/core/SkData.h"
#include "src/core/SkWriter32.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef STX_SKIA_REFERENCE
using ReadValue = decltype(std::declval<const SkWriter32&>().readTAt<uint32_t>(0));
static_assert(std::is_same_v<ReadValue, uint32_t>);
template <typename T> concept HasStreamInput = requires { &T::readFromStream; };
static_assert(!HasStreamInput<SkWriter32>);
#endif

namespace {
void append_word(std::vector<uint8_t>& expected, uint32_t word) {
    const size_t offset = expected.size();
    expected.resize(offset + sizeof(word));
    std::memcpy(expected.data() + offset, &word, sizeof(word));
}

bool records(uint64_t& digest) {
    const std::array<size_t, 14> capacities{0, 1, 2, 3, 4, 7, 8, 15, 16, 32, 63, 64, 257, 4096};
    for (const size_t capacity : capacities) {
        std::array<uint32_t, 1026> external{};
        external.fill(0xa5a5a5a5U);
        SkWriter32 writer(external.data() + 1, capacity);
        std::vector<uint8_t> expected{};
        writer.write8(-1);
        append_word(expected, 255);
        writer.write16(0x76543210);
        append_word(expected, 0x3210);
        writer.write32(0x12345678);
        append_word(expected, 0x12345678U);
        writer.writeScalar(-0.0f);
        append_word(expected, 0x80000000U);
        for (size_t length = 0; length <= 33; ++length) {
            const std::array<uint8_t, 33> bytes{
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33};
            writer.writePad(bytes.data(), length);
            expected.insert(expected.end(), bytes.begin(), bytes.begin() + length);
            while (expected.size() % 4 != 0) expected.push_back(0);
        }
        for (const char* text : {"", "a", "ab", "abc", "abcd", "abcdefgh"}) {
            const size_t before = writer.bytesWritten();
            const size_t length = std::strlen(text);
            const size_t predicted = SkWriter32::WriteStringSize(text);
            writer.writeString(text);
            if (writer.bytesWritten() - before != predicted) return false;
            append_word(expected, static_cast<uint32_t>(length));
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text);
            expected.insert(expected.end(), bytes, bytes + length);
            expected.push_back(0);
            while (expected.size() % 4 != 0) expected.push_back(0);
        }
        for (uint32_t index = 0; index < 6000; ++index) {
            writer.write32(static_cast<int32_t>(index));
            append_word(expected, index);
        }
        if (writer.bytesWritten() != expected.size() || writer.usingInitialStorage()) return false;
        const uint32_t previous = writer.readTAt<uint32_t>(0);
        if (previous != 255) return false;
        const uint32_t replacement = 0xaabbccddU;
        writer.overwriteTAt(0, replacement);
        std::memcpy(expected.data(), &replacement, sizeof(replacement));
        if (writer.readTAt<uint32_t>(0) != replacement || previous != 255) return false;
        std::vector<uint8_t> flattened(expected.size());
        writer.flatten(flattened.data());
        if (flattened != expected) return false;
        sk_sp<SkData> snapshot = writer.snapshotAsData();
        if (!snapshot || (*snapshot).size() != expected.size()) return false;
        if (std::memcmp((*snapshot).data(), expected.data(), expected.size()) != 0) return false;
        writer.rewindToOffset(4);
        writer.write32(99);
        if (writer.bytesWritten() != 8 || writer.readTAt<int32_t>(4) != 99) return false;
        if (std::memcmp((*snapshot).data(), expected.data(), expected.size()) != 0) return false;
        writer.reset(external.data() + 1, capacity);
        if (writer.bytesWritten() != 0 || !writer.usingInitialStorage()) return false;
        if (external.front() != 0xa5a5a5a5U) return false;
        const uint8_t* storage = reinterpret_cast<const uint8_t*>(external.data() + 1);
        for (size_t index = capacity; index < 4100; ++index) {
            if (storage[index] != 0xa5U) return false;
        }
        for (const uint8_t byte : flattened) {
            digest ^= byte;
            digest *= UINT64_C(1099511628211);
        }
    }
    return true;
}

bool inline_and_empty() {
    SkSWriter32<24> writer{};
    for (int32_t value = 0; value < 64; ++value) writer.write32(value);
    if (writer.usingInitialStorage()) return false;
    writer.reset();
    if (!writer.usingInitialStorage() || writer.bytesWritten() != 0) return false;
    for (int32_t value = 0; value < 6; ++value) writer.write32(value + 10);
    if (!writer.usingInitialStorage()) return false;
    if (writer.readTAt<int32_t>(20) != 15) return false;
#ifndef STX_SKIA_REFERENCE
    SkWriter32 empty{};
    if (empty.reserve(0) != nullptr || empty.reservePad(0) != nullptr) return false;
    empty.write(nullptr, 0);
    empty.writePad(nullptr, 0);
    empty.flatten(nullptr);
    sk_sp<SkData> snapshot = empty.snapshotAsData();
    if (!snapshot || (*snapshot).size() != 0 || empty.bytesWritten() != 0) return false;
    if (SkWriter32::WriteStringSize(nullptr) != 8) return false;
    empty.writeString(nullptr);
    if (empty.bytesWritten() != 8 || empty.readTAt<uint32_t>(0) != 0) return false;
#endif
    return true;
}

int benchmark() {
    const std::array<size_t, 3> capacities{0, 64, 4096};
    const size_t iterations = 8192;
    for (const size_t capacity : capacities) {
        std::array<uint32_t, 1024> external{};
        uint64_t digest = 0;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (size_t iteration = 0; iteration < iterations; ++iteration) {
            SkWriter32 writer(external.data(), capacity);
            for (int32_t value = 0; value < 1024; ++value) writer.write32(value);
            digest += writer.readTAt<uint32_t>(4092);
        }
        const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(stop - start).count();
        std::printf("words_capacity_%zu,%zu,%.9f,%llu\n", capacity, iterations * 1024, seconds,
                    static_cast<unsigned long long>(digest));
    }
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        const int result = benchmark();
        return result;
    }
    uint64_t digest = UINT64_C(1469598103934665603);
    if (!records(digest) || !inline_and_empty()) return 1;
    std::printf("writer32: 14 capacity cases, padding, growth, records, snapshots PASS; hash=%llu size=%zu inline=%zu\n",
                static_cast<unsigned long long>(digest), sizeof(SkWriter32), sizeof(SkSWriter32<24>));
    return 0;
}
