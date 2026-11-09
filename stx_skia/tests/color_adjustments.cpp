#include "include/core/SkColorFilter.h"
#include "include/core/SkColorTable.h"
#include "include/core/SkSerialProcs.h"
#include "include/effects/SkColorMatrix.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace {

template <typename T>
concept TemporaryTableView = requires(T&& value) { std::move(value).alphaTable(); };
#ifndef STX_SKIA_REFERENCE
static_assert(!TemporaryTableView<SkColorTable>);
#endif

void AppendBytes(std::vector<uint8_t>& output, const void* data, size_t bytes) {
    const uint8_t* first = static_cast<const uint8_t*>(data);
    output.insert(output.end(), first, first + bytes);
}

bool same_matrix(const SkColorMatrix& first, const SkColorMatrix& second) {
    std::array<float, 20> a{};
    std::array<float, 20> b{};
    first.getRowMajor(a.data());
    second.getRowMajor(b.data());
    const bool same = a == b;
    return same;
}

bool matrix_operations(std::vector<uint8_t>& output) {
    SkColorMatrix identity{};
    SkColorMatrix scale{};
    scale.setScale(2.0f, 3.0f, 4.0f, 0.5f);
    SkColorMatrix translation{};
    translation.postTranslate(0.125f, 0.25f, 0.5f, 0.0625f);
    SkColorMatrix combined{};
    combined.setConcat(scale, translation);
    std::array<float, 20> values{};
    combined.getRowMajor(values.data());
    const std::array<float, 20> expected{
        2.0f, 0, 0, 0, 0.25f,
        0, 3.0f, 0, 0, 0.75f,
        0, 0, 4.0f, 0, 2.0f,
        0, 0, 0, 0.5f, 0.03125f};
    if (values != expected) return false;
    SkColorMatrix aliasOuter = scale;
    aliasOuter.preConcat(translation);
    if (!same_matrix(combined, aliasOuter)) return false;
    SkColorMatrix aliasInner = translation;
    aliasInner.postConcat(scale);
    if (!same_matrix(combined, aliasInner)) return false;
    SkColorMatrix square{};
    square.setConcat(combined, combined);
    combined.setConcat(combined, combined);
    if (!same_matrix(combined, square)) return false;
    combined.setIdentity();
    if (!same_matrix(combined, identity)) return false;
    AppendBytes(output, values.data(), sizeof(values));
    return true;
}

bool matrix_filters(std::vector<uint8_t>& output) {
    for (int variation = 0; variation < 8; ++variation) {
        SkColorMatrix matrix{};
        matrix.setSaturation(static_cast<float>(variation) * 0.25f);
        matrix.postTranslate(0.03125f * static_cast<float>(variation), -0.0625f, 0.125f, 0.0f);
        std::array<float, 20> coefficients{};
        matrix.getRowMajor(coefficients.data());
        AppendBytes(output, coefficients.data(), sizeof(coefficients));
        for (const bool hsla : {false, true}) {
            for (const SkColorFilters::Clamp clamp : {SkColorFilters::Clamp::kYes, SkColorFilters::Clamp::kNo}) {
                sk_sp<SkColorFilter> filter = hsla ? SkColorFilters::HSLAMatrix(matrix)
                                                 : SkColorFilters::Matrix(matrix, clamp);
                if (!filter) return false;
                std::array<float, 20> recovered{};
                if (!(*filter).asAColorMatrix(recovered.data()) || recovered != coefficients) return false;
                for (int sample = 0; sample < 32; ++sample) {
                    const SkColor4f input{
                        static_cast<float>(sample & 3) / 3.0f,
                        static_cast<float>((sample >> 2) & 3) / 3.0f,
                        static_cast<float>((sample >> 4) & 1),
                        static_cast<float>(sample % 5) * 0.25f};
                    const SkColor4f result = (*filter).filterColor4f(input, nullptr, nullptr);
                    if (!std::isfinite(result.fR) || !std::isfinite(result.fG) ||
                        !std::isfinite(result.fB) || !std::isfinite(result.fA)) return false;
                    AppendBytes(output, &result, sizeof(result));
                }
            }
        }
    }
    for (const SkColor add : {0x00000000U, 0x00204080U}) {
        sk_sp<SkColorFilter> lighting = SkColorFilters::Lighting(0x00ff8040U, add);
        if (!lighting) return false;
        const SkColor4f input{0.25f, 0.5f, 0.75f, 0.5f};
        const SkColor4f result = (*lighting).filterColor4f(input, nullptr, nullptr);
        AppendBytes(output, &result, sizeof(result));
    }
    std::array<float, 20> invalid{};
    invalid[0] = std::numeric_limits<float>::infinity();
    if (SkColorFilters::Matrix(invalid.data())) return false;
    invalid[0] = std::numeric_limits<float>::quiet_NaN();
    if (SkColorFilters::HSLAMatrix(invalid.data())) return false;
    return true;
}

bool table_filters(std::vector<uint8_t>& output) {
    std::array<std::array<uint8_t, 256>, 4> values{};
    for (int index = 0; index < 256; ++index) {
        values[0][index] = static_cast<uint8_t>(255 - index / 2);
        values[1][index] = static_cast<uint8_t>(255 - index);
        values[2][index] = static_cast<uint8_t>(index / 2);
        values[3][index] = static_cast<uint8_t>((index * 3) & 255);
    }
    for (unsigned int present = 0; present < 16; ++present) {
        std::array<const uint8_t*, 4> source{};
        for (unsigned int channel = 0; channel < 4; ++channel) {
            if ((present & (1U << channel)) != 0) source[channel] = values[channel].data();
        }
        sk_sp<SkColorTable> table = SkColorTable::Make(source[0], source[1], source[2], source[3]);
        if (present == 0) {
            if (table || SkColorFilters::Table(table)) return false;
            continue;
        }
        if (!table) return false;
        const std::array<const uint8_t*, 4> stored{
            (*table).alphaTable(), (*table).redTable(), (*table).greenTable(), (*table).blueTable()};
        for (unsigned int channel = 0; channel < 4; ++channel) {
            if (stored[channel] == source[channel]) return false;
            for (int index = 0; index < 256; ++index) {
                const uint8_t expected = source[channel] != nullptr ? source[channel][index]
                                                                   : static_cast<uint8_t>(index);
                if (stored[channel][index] != expected) return false;
            }
        }
        const SkSerialProcs procs{};
        SkBinaryWriteBuffer writer(procs);
        (*table).flatten(writer);
        std::array<uint32_t, 260> storage{};
        const size_t bytes = writer.bytesWritten();
        if (bytes > sizeof(storage)) return false;
        writer.writeToMemory(storage.data());
        AppendBytes(output, storage.data(), bytes);
        SkReadBuffer reader(storage.data(), bytes);
        sk_sp<SkColorTable> decoded = SkColorTable::Deserialize(reader);
        if (!decoded || !reader.isValid()) return false;
        const std::array<const uint8_t*, 4> recovered{
            (*decoded).alphaTable(), (*decoded).redTable(), (*decoded).greenTable(), (*decoded).blueTable()};
        for (unsigned int channel = 0; channel < 4; ++channel) {
            for (int index = 0; index < 256; ++index) {
                if (stored[channel][index] != recovered[channel][index]) return false;
            }
        }
        SkReadBuffer truncated(storage.data(), bytes - 4);
        if (SkColorTable::Deserialize(truncated) || truncated.isValid()) return false;
        sk_sp<SkColorFilter> filter = SkColorFilters::Table(table);
        table.reset(); // The filter retains the rows used by its pipeline.
        if (!filter) return false;
        for (int sample = 0; sample < 256; ++sample) {
            const float component = static_cast<float>(sample) / 255.0f;
            const std::array<int, 3> alphaBytes{64, 128, 255};
            const int alphaByte = alphaBytes[static_cast<size_t>(sample % 3)];
            const float alpha = static_cast<float>(alphaByte) / 255.0f;
            const SkColor4f input{component, component, component, alpha};
            const SkColor4f result = (*filter).filterColor4f(input, nullptr, nullptr);
            AppendBytes(output, &result, sizeof(result));
            const std::array<float, 4> channels{result.fA, result.fR, result.fG, result.fB};
            for (unsigned int channel = 0; channel < 4; ++channel) {
                const int inputByte = channel == 0 ? alphaByte : sample;
                const float expected = static_cast<float>(recovered[channel][inputByte]) / 255.0f;
                if (std::fabs(channels[channel] - expected) > 0.00001f) return false;
            }
        }
    }
    return true;
}
}

int main(int argc, char** argv) {
    std::vector<uint8_t> output{};
    if (!matrix_operations(output)) return 1;
    if (!matrix_filters(output)) return 2;
    if (!table_filters(output)) return 3;
    if (argc == 2) {
        FILE* file = std::fopen(argv[1], "wb");
        if (file == nullptr) return 4;
        const size_t written = std::fwrite(output.data(), 1, output.size(), file);
        const int closed = std::fclose(file);
        if (written != output.size() || closed != 0) return 4;
    }
    std::printf("color matrix/table: 1024 matrix colors, 3840 table colors, %zu bytes PASS\n", output.size());
    return 0;
}
