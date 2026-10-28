#include "src/core/SkRasterPipeline.h"
#include "src/core/SkRasterPipelineOpList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>

extern bool gForceHighPrecisionRasterPipeline;

namespace {
constexpr size_t stride = 211;
constexpr size_t rows = 5;
constexpr uint32_t sentinel = 0x3f7a9521U;
using Pixels = std::array<uint32_t, stride * rows>;

uint32_t swap_red_blue(uint32_t pixel) {
    const uint32_t kept = pixel & 0xff00ff00U;
    const uint32_t red = (pixel & 0x000000ffU) << 16;
    const uint32_t blue = (pixel & 0x00ff0000U) >> 16;
    const uint32_t swapped = kept | red | blue;
    return swapped;
}

bool check_pixels(const Pixels& source, const Pixels& destination, size_t x, size_t y,
                  size_t width, size_t height) {
    for (size_t row = 0; row < rows; ++row) {
        for (size_t column = 0; column < stride; ++column) {
            uint32_t expected = sentinel;
            const bool live = row >= y && row < y + height && column >= x && column < x + width;
            if (live) expected = swap_red_blue(source[row * stride + column]);
            if (destination[row * stride + column] != expected) return false;
        }
    }
    return true;
}

bool row_case(size_t width, size_t height, size_t x, bool compile) {
    Pixels source{};
    Pixels destination{};
    for (size_t index = 0; index < source.size(); ++index) {
        const uint32_t value = static_cast<uint32_t>(index);
        source[index] = 0xff000000U | ((value * 17U) & 255U) |
                        (((value * 31U) & 255U) << 8) | (((value * 67U) & 255U) << 16);
    }
    destination.fill(sentinel);
    SkRasterPipelineContexts::MemoryCtx input{source.data(), static_cast<int>(stride)};
    SkRasterPipelineContexts::MemoryCtx output{destination.data(), static_cast<int>(stride)};
    // Arenas and context storage outlive every pipeline and compiled borrower.
    SkSTArenaAlloc<1024> inputArena;
    SkSTArenaAlloc<1024> outputArena;
    SkSTArenaAlloc<1024> joinedArena;
    SkRasterPipeline inputPipeline(&inputArena);
    SkRasterPipeline outputPipeline(&outputArena);
    SkRasterPipeline joined(&joinedArena);
    inputPipeline.append(SkRasterPipelineOp::load_8888, &input);
    inputPipeline.append(SkRasterPipelineOp::swap_rb);
    outputPipeline.append(SkRasterPipelineOp::store_8888, &output);
    joined.extend(inputPipeline);
    joined.extend(outputPipeline);
    if (joined.getNumStages() != 3) return false;
    constexpr size_t y = 1;
    if (compile) {
        const std::function<void(size_t, size_t, size_t, size_t)> run = joined.compile();
        run(x, y, width, height);
    } else {
        joined.run(x, y, width, height);
    }
    if (!check_pixels(source, destination, x, y, width, height)) return false;
    if (input.pixels != source.data() || output.pixels != destination.data()) return false;

    joined.reset();
    if (!joined.empty() || joined.getNumStages() != 0) return false;
    destination.fill(sentinel);
    joined.run(x, y, width, height);
    {
        const std::function<void(size_t, size_t, size_t, size_t)> run = joined.compile();
        run(x, y, width, height);
    }
    for (uint32_t pixel : destination) if (pixel != sentinel) return false;
    joined.extend(inputPipeline);
    joined.extend(outputPipeline);
    joined.run(x, y, width, height);
    const bool restored = check_pixels(source, destination, x, y, width, height);
    return restored;
}
}

int main() {
    size_t cases = 0;
    for (int highPrecision = 0; highPrecision < 2; ++highPrecision) {
        gForceHighPrecisionRasterPipeline = highPrecision != 0;
        for (size_t width = 0; width <= 65; ++width) {
            for (size_t height = 0; height <= 3; ++height) {
                for (const size_t x : std::array<size_t, 4>{1, 3, 67, 129}) {
                    for (int compiled = 0; compiled < 2; ++compiled) {
                        if (!row_case(width, height, x, compiled != 0)) return 1;
                        ++cases;
                    }
                }
            }
        }
    }
    gForceHighPrecisionRasterPipeline = false;
    std::printf("raster pipeline: %zu padded-row, extension and reset cases PASS\n", cases);
    return 0;
}
