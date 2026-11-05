// Differential corpus for the fill kernels behind solid rectangle drawing: SkOpts::memset16/32/64
// and rect_memset16/32/64 for every count from 0 to 80 and longer rows, start offsets 0-3,
// padded row strides and several heights. Each fill is written between sentinel guards and the
// whole buffer is recorded, so a write outside the requested pixels changes the bytes. Both
// builds must produce identical bytes (memset.raw). "--benchmark" times the kernel and canvas
// solid fills instead (tools/measure_fill.py).

#include "differential_corpus.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "src/core/SkMemset.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;

template <typename T>
constexpr T sentinel() {
    return static_cast<T>(0xA5A5A5A5A5A5A5A5ull);
}

template <typename T>
void append_buffer(Output& output, const std::vector<T>& buffer) {
    for (const T value : buffer) append(output, value);
}

template <typename T>
void row_cases(Output& output, void (*fill)(T[], T, int), Sequence& sequence) {
    std::vector<int> counts{};
    for (int count = 0; count <= 80; ++count) counts.push_back(count);
    for (const int count : {127, 128, 129, 1000, 1023}) counts.push_back(count);
    for (const int count : counts) {
        for (int offset = 0; offset < 4; ++offset) {
            std::vector<T> buffer(static_cast<size_t>(count + 8), sentinel<T>());
            const T value = static_cast<T>(sequence.next() * 0x9E3779B97F4A7C15ull);
            fill(buffer.data() + offset, value, count);
            append_buffer(output, buffer);
            ++output.cases;
        }
    }
}

template <typename T>
void rect_cases(Output& output, void (*fill)(T[], T, int, size_t, int), Sequence& sequence) {
    const std::array<int, 9> widths{0, 1, 3, 4, 5, 15, 16, 17, 67};
    const std::array<int, 4> heights{0, 1, 2, 7};
    const std::array<int, 3> paddings{0, 1, 9};
    for (const int width : widths) {
        for (const int height : heights) {
            for (const int padding : paddings) {
                for (int offset = 0; offset < 3; ++offset) {
                    const int stride = width + padding;
                    std::vector<T> buffer(static_cast<size_t>(stride * (height + 1) + 8),
                                          sentinel<T>());
                    const T value = static_cast<T>(sequence.next() * 0xD1B54A32D192ED03ull);
                    fill(buffer.data() + offset, value, width,
                         static_cast<size_t>(stride) * sizeof(T), height);
                    append_buffer(output, buffer);
                    ++output.cases;
                }
            }
        }
    }
}

void shared_corpus(Output& output) {
    Sequence sequence{};
    row_cases<uint16_t>(output, SkOpts::memset16, sequence);
    row_cases<uint32_t>(output, SkOpts::memset32, sequence);
    row_cases<uint64_t>(output, SkOpts::memset64, sequence);
    rect_cases<uint16_t>(output, SkOpts::rect_memset16, sequence);
    rect_cases<uint32_t>(output, SkOpts::rect_memset32, sequence);
    rect_cases<uint64_t>(output, SkOpts::rect_memset64, sequence);
}

// ---- Benchmark ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

struct FillCase {
    const char* name = "";
    int width = 0;
    int height = 0;
};

// Best of five timed batches, each about 1 GiB of fill, in seconds per fill.
double time_kernel(void (*fill)(uint32_t[], uint32_t, int, size_t, int), const FillCase& shape) {
    const size_t stride = static_cast<size_t>(shape.width) + 8;
    std::vector<uint32_t> buffer(stride * static_cast<size_t>(shape.height) + 16);
    const double bytes = static_cast<double>(shape.width) * shape.height * 4;
    const int repetitions = static_cast<int>((1u << 30) / bytes) + 1;
    double best = 1e30;
    for (int trial = 0; trial < 5; ++trial) {
        const Clock::time_point start = Clock::now();
        for (int i = 0; i < repetitions; ++i) {
            fill(buffer.data() + 1, 0xFF336699u + static_cast<uint32_t>(i), shape.width,
                 stride * 4, shape.height);
        }
        const std::chrono::duration<double> elapsed = Clock::now() - start;
        const double seconds = elapsed.count() / repetitions;
        if (seconds < best) best = seconds;
    }
    return best;
}

// Solid rectangles drawn through the canvas: opaque, antialiased at fractional edges and
// not, on a 1920x1080 N32 surface. Best of five, in seconds per frame of 64 rectangles.
double time_canvas(bool antialias) {
    const SkImageInfo info = SkImageInfo::MakeN32Premul(1920, 1080);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return -1;
    SkCanvas& canvas = *(*surface).getCanvas();
    SkPaint paint;
    paint.setAntiAlias(antialias);
    Sequence sequence{};
    std::vector<SkRect> rects{};
    std::vector<SkColor> colors{};
    for (int i = 0; i < 64; ++i) {
        const float x = static_cast<float>(sequence.pick(1600)) + 0.25f;
        const float y = static_cast<float>(sequence.pick(900)) + 0.5f;
        const float w = 40.0f + static_cast<float>(sequence.pick(300));
        const float h = 10.0f + static_cast<float>(sequence.pick(160));
        rects.push_back(SkRect::MakeXYWH(x, y, w, h));
        colors.push_back(static_cast<SkColor>(sequence.next() | 0xFF000000u));
    }
    const int frames = 200;
    double best = 1e30;
    for (int trial = 0; trial < 5; ++trial) {
        const Clock::time_point start = Clock::now();
        for (int frame = 0; frame < frames; ++frame) {
            paint.setColor(SK_ColorWHITE);
            canvas.drawRect(SkRect::MakeWH(1920, 1080), paint);
            for (size_t i = 0; i < rects.size(); ++i) {
                paint.setColor(colors[i]);
                canvas.drawRect(rects[i], paint);
            }
        }
        const std::chrono::duration<double> elapsed = Clock::now() - start;
        const double seconds = elapsed.count() / frames;
        if (seconds < best) best = seconds;
    }
    return best;
}

// The kernel as compiled for the build's baseline, captured before SkGraphics::Init's run-time
// dispatch can replace it (on x86 the reference may then choose AVX or ERMS variants).
void (*gDefaultRect32)(uint32_t[], uint32_t, int, size_t, int) = nullptr;

int benchmark() {
    const std::array<FillCase, 5> cases{FillCase{"37x300", 37, 300}, FillCase{"100x20", 100, 20},
                                        FillCase{"1000x500", 1000, 500},
                                        FillCase{"1920x1080", 1920, 1080},
                                        FillCase{"4000x2000", 4000, 2000}};
    std::puts("kind,case,seconds,gigabytes_per_second");
    for (const FillCase& shape : cases) {
        const double bytes = static_cast<double>(shape.width) * shape.height * 4;
        const double compiled = time_kernel(gDefaultRect32, shape);
        std::printf("kernel_default,%s,%.9g,%.3f\n", shape.name, compiled, bytes / compiled / 1e9);
        const double dispatched = time_kernel(SkOpts::rect_memset32, shape);
        std::printf("kernel_dispatched,%s,%.9g,%.3f\n", shape.name, dispatched,
                    bytes / dispatched / 1e9);
    }
    std::printf("canvas_aa,1920x1080+64,%.9g,0\n", time_canvas(true));
    std::printf("canvas_bw,1920x1080+64,%.9g,0\n", time_canvas(false));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    gDefaultRect32 = SkOpts::rect_memset32;
    SkGraphics::Init();
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) return benchmark();
    if (argc < 1 || argc > 2) return 2;
    Output output{};
    shared_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("memset: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases, output.bytes.size(),
                hash);
    return 0;
}
