// Byte corpus helpers shared by candidate/reference differential tests.
// Each test appends results in a fixed order; both builds must produce identical bytes.

#ifndef STX_SKIA_TESTS_DIFFERENTIAL_CORPUS_H
#define STX_SKIA_TESTS_DIFFERENTIAL_CORPUS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace corpus {

struct Output {
    std::vector<uint8_t> bytes{};
    size_t cases = 0;
};

// Appends the object representation of a trivially copyable value.
template <typename T>
void append(Output& output, const T& value) {
    uint8_t raw[sizeof(T)] = {};
    std::memcpy(raw, &value, sizeof(T));
    output.bytes.insert(output.bytes.end(), raw, raw + sizeof(T));
}

inline void append_bool(Output& output, bool value) {
    const uint8_t byte = value ? 1 : 0;
    output.bytes.push_back(byte);
}

// Deterministic 64-bit linear congruential sequence; identical in both builds.
struct Sequence {
    uint64_t state = 0x9E3779B97F4A7C15ull;

    uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const uint32_t value = static_cast<uint32_t>(state >> 32);
        return value;
    }

    size_t pick(size_t count) {
        const size_t index = static_cast<size_t>(next()) % count;
        return index;
    }
};

inline uint64_t fnv1a(const std::vector<uint8_t>& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

// Writes the corpus to path. Returns false if the file cannot be written completely.
inline bool write_file(const char* path, const Output& output) {
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(output.bytes.data(), 1, output.bytes.size(), file);
    const int closed = std::fclose(file);
    const bool complete = written == output.bytes.size() && closed == 0;
    return complete;
}

inline constexpr float kInf = std::numeric_limits<float>::infinity();
inline constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
inline constexpr float kFloatMax = std::numeric_limits<float>::max();
inline constexpr float kFloatMin = std::numeric_limits<float>::min();
inline constexpr float kDenormal = std::numeric_limits<float>::denorm_min();

}  // namespace corpus

#endif
