#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint3.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkString.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
std::vector<uint32_t> contents(const SkBinaryWriteBuffer& writer) {
    const size_t bytes = writer.bytesWritten();
    std::vector<uint32_t> storage(bytes / sizeof(uint32_t));
    writer.writeToMemory(storage.data());
    return storage;
}

bool primitives() {
    const std::array<uint32_t, 8> bits{
        0, 1, 0xffffffffU, 0x80000000U, 0x7fffffffU, 0x3f800000U, 0x7fc00001U, 0xff800000U};
    for (const uint32_t pattern : bits) {
        const std::array<uint32_t, 3> input{pattern, pattern, pattern};
        SkReadBuffer reader(input.data(), sizeof(input));
        if (reader.getArrayCount() != pattern || reader.offset() != 0) return false;
        if (reader.readUInt() != pattern) return false;
        const int32_t integer = reader.readInt();
        if (std::bit_cast<uint32_t>(integer) != pattern) return false;
        const float scalar = reader.readScalar();
        if (std::bit_cast<uint32_t>(scalar) != pattern) return false;
        if (!reader.eof() || !reader.isValid()) return false;
    }
    const std::array<uint32_t, 3> booleans{0, 1, 2};
    SkReadBuffer booleanReader(booleans.data(), sizeof(booleans));
    if (booleanReader.readBool() || !booleanReader.readBool()) return false;
    if (!booleanReader.readBool() || booleanReader.isValid()) return false;
    return true;
}

bool arrays_and_strings() {
    const SkSerialProcs procs{};
    for (size_t count = 0; count <= 257; ++count) {
        std::vector<uint8_t> bytes(count);
        for (size_t index = 0; index < count; ++index) bytes[index] = static_cast<uint8_t>(index * 37);
        SkBinaryWriteBuffer writer(procs);
        writer.writeByteArray(bytes.data(), count);
        writer.writeString("padding");
        writer.writeUInt(0x12345678U);
        std::vector<uint32_t> storage = contents(writer);
        SkReadBuffer reader(storage.data(), writer.bytesWritten());
        if (reader.getArrayCount() != count || reader.offset() != 0) return false;
        sk_sp<SkData> owned = reader.readByteArrayAsData();
        if (!owned || (*owned).size() != count) return false;
        if (count != 0 && std::memcmp((*owned).data(), bytes.data(), count) != 0) return false;
        SkString text{};
        reader.readString(&text);
        if (!text.equals("padding") || reader.readUInt() != 0x12345678U) return false;
        if (!reader.isValid() || !reader.eof()) return false;
        // Returned data must remain independent of the input storage.
        for (uint32_t& word : storage) word = 0;
        if (count != 0 && std::memcmp((*owned).data(), bytes.data(), count) != 0) return false;
    }
    const std::array<int32_t, 4> integers{-10, 0, 1, 2147483647};
    const std::array<float, 4> scalars{-1.5f, 0.0f, 1.25f, 123.5f};
    SkBinaryWriteBuffer writer(procs);
    writer.writeIntArray(SkSpan<const int32_t>(integers.data(), integers.size()));
    writer.writeScalarArray(SkSpan<const float>(scalars.data(), scalars.size()));
    std::vector<uint32_t> storage = contents(writer);
    SkReadBuffer reader(storage.data(), writer.bytesWritten());
    std::array<int32_t, 4> decodedIntegers{};
    std::array<float, 4> decodedScalars{};
    if (!reader.readIntArray(SkSpan<int32_t>(decodedIntegers.data(), decodedIntegers.size()))) return false;
    if (!reader.readScalarArray(SkSpan<float>(decodedScalars.data(), decodedScalars.size()))) return false;
    if (decodedIntegers != integers || decodedScalars != scalars) return false;
    return true;
}

bool structures() {
    const SkSerialProcs procs{};
    const SkPoint point{1.25f, -7.5f};
    const SkPoint3 point3{2.0f, -3.0f, 4.0f};
    const SkRect rect = SkRect::MakeLTRB(-1.0f, 2.0f, 14.0f, 22.0f);
    const SkIRect integerRect = SkIRect::MakeLTRB(-2, 3, 21, 31);
    const SkColor4f color{0.25f, 0.5f, 0.75f, 1.0f};
    const SkMatrix matrix = SkMatrix::MakeAll(2, 1, 3, 0, 4, 5, 0, 0, 1);
    const SkM44 matrix44(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    const SkPath path = SkPath::Rect(rect);
    const SkRegion region(integerRect);
    const std::array<SkSamplingOptions, 4> sampling{
        SkSamplingOptions(), SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear),
        SkSamplingOptions(SkCubicResampler{0.25f, 0.5f}), SkSamplingOptions::Aniso(4)};
    SkBinaryWriteBuffer writer(procs);
    writer.writePoint(point);
    writer.writePoint3(point3);
    writer.writeRect(rect);
    writer.writeIRect(integerRect);
    writer.writeColor4f(color);
    writer.writeMatrix(matrix);
    writer.write(matrix44);
    writer.writePath(path);
    writer.writeRegion(region);
    for (const SkSamplingOptions& options : sampling) writer.writeSampling(options);
    std::vector<uint32_t> storage = contents(writer);
    SkReadBuffer reader(storage.data(), writer.bytesWritten());
    if (reader.readPoint() != point) return false;
    SkPoint3 decodedPoint3{};
    reader.readPoint3(&decodedPoint3);
    if (decodedPoint3 != point3 || reader.readRect() != rect) return false;
    SkIRect decodedIRect{};
    reader.readIRect(&decodedIRect);
    SkColor4f decodedColor{};
    reader.readColor4f(&decodedColor);
    SkMatrix decodedMatrix{};
    reader.readMatrix(&decodedMatrix);
    SkM44 decoded44{};
    reader.read(&decoded44);
    if (decodedIRect != integerRect || decodedColor != color) return false;
    if (decodedMatrix != matrix || decoded44 != matrix44) return false;
    const std::optional<SkPath> decodedPath = reader.readPath();
    if (!decodedPath || *decodedPath != path) return false;
    SkRegion decodedRegion{};
    reader.readRegion(&decodedRegion);
    if (decodedRegion != region) return false;
    for (const SkSamplingOptions& options : sampling) {
        if (reader.readSampling() != options) return false;
    }
    if (!reader.isValid() || !reader.eof()) return false;
    return true;
}

bool region_input_bounds() {
#ifndef STX_SKIA_REFERENCE
    SkRegion source(SkIRect::MakeXYWH(2, 3, 5, 7));
    source.op(SkIRect::MakeXYWH(20, 3, 4, 7), SkRegion::kUnion_Op);
    if (!source.isComplex()) return false;
    const size_t bytes = source.writeToMemory(nullptr);
    std::vector<uint8_t> unaligned(bytes + 1);
    if (source.writeToMemory(unaligned.data() + 1) != bytes) return false;
    SkRegion decoded;
    if (decoded.readFromMemory(unaligned.data() + 1, bytes) != bytes) return false;
    if (decoded != source) return false;

    const SkRegion retained(SkIRect::MakeXYWH(4, 5, 6, 7));
    decoded = retained;
    const std::array<int32_t, 5> sentinelRect{0, 0, 0,
                                              std::numeric_limits<int32_t>::max(), 1};
    if (decoded.readFromMemory(sentinelRect.data(), sizeof(sentinelRect)) != 0) return false;
    if (decoded != retained) return false;

    const std::array<int32_t, 7> wrappedCount{
        0x40000007, 0, 0, 10, 10, 1, 536870913};
    if (decoded.readFromMemory(wrappedCount.data(), sizeof(wrappedCount)) != 0) return false;
    if (decoded != retained) return false;
#endif
    return true;
}

struct ImageContext {
    int calls = 0;
    bool alpha = false;
};

sk_sp<SkImage> image_bytes(const void* bytes, size_t size, void* context) {
    ImageContext& state = *static_cast<ImageContext*>(context);
    ++state.calls;
    if (size != 8) return nullptr;
    std::array<uint32_t, 2> dimensions{};
    std::memcpy(dimensions.data(), bytes, size);
    if (dimensions[0] == 0 || dimensions[0] > 4 || dimensions[1] != dimensions[0]) return nullptr;
    const int width = static_cast<int>(dimensions[0]);
    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, width);
    const size_t count = dimensions[0] * dimensions[1];
    const std::vector<uint32_t> pixels(count, 0xff123456U);
    sk_sp<SkData> data = SkData::MakeWithCopy(pixels.data(), count * sizeof(uint32_t));
    sk_sp<SkImage> image = SkImages::RasterFromData(info, data, info.minRowBytes());
    return image;
}

sk_sp<SkImage> image_data(sk_sp<SkData> data, std::optional<SkAlphaType> alpha, void* context) {
    ImageContext& state = *static_cast<ImageContext*>(context);
    state.alpha = alpha == kUnpremul_SkAlphaType;
    const void* bytes = (*data).data();
    const size_t size = (*data).size();
    sk_sp<SkImage> image = image_bytes(bytes, size, context);
    return image;
}

sk_sp<const SkData> image_encode(SkImage* image, void* context) {
    ImageContext& state = *static_cast<ImageContext*>(context);
    ++state.calls;
    const std::array<uint32_t, 2> dimensions{
        static_cast<uint32_t>((*image).width()), static_cast<uint32_t>((*image).height())};
    sk_sp<const SkData> data = SkData::MakeWithCopy(dimensions.data(), sizeof(dimensions));
    return data;
}

bool images() {
    const SkSerialProcs procs{};
    const std::array<uint32_t, 2> base{4, 4};
    const std::array<uint32_t, 2> middle{2, 2};
    const std::array<uint32_t, 2> last{1, 1};
    SkBinaryWriteBuffer mips(procs);
    mips.writeInt(2);
    mips.writeByteArray(middle.data(), sizeof(middle));
    mips.writeByteArray(last.data(), sizeof(last));
    const std::vector<uint32_t> mipStorage = contents(mips);
    SkBinaryWriteBuffer writer(procs);
    writer.writeUInt(SkWriteBufferImageFlags::kHasMipmap | SkWriteBufferImageFlags::kUnpremul);
    writer.writeByteArray(base.data(), sizeof(base));
    writer.writeByteArray(mipStorage.data(), mips.bytesWritten());
    const std::vector<uint32_t> storage = contents(writer);
    for (int mode = 0; mode < 3; ++mode) {
        ImageContext context{};
        SkDeserialProcs callbacks{};
        callbacks.fImageCtx = &context;
        if (mode == 1) callbacks.fImageProc = image_bytes;
        if (mode == 2) callbacks.fImageDataProc = image_data;
        SkReadBuffer reader(storage.data(), writer.bytesWritten());
        reader.setDeserialProcs(callbacks);
        sk_sp<SkImage> image = reader.readImage();
        if (!image || !reader.isValid() || !reader.eof()) return false;
        if (mode == 0) {
            if ((*image).width() != 1 || context.calls != 0) return false;
        } else {
            if ((*image).width() != 4 || context.calls != 3 || !(*image).hasMipmaps()) return false;
            if (mode == 2 && !context.alpha) return false;
        }
    }
    ImageContext creation{};
    sk_sp<SkImage> source = image_bytes(base.data(), sizeof(base), &creation);
    if (!source) return false;
    source = (*source).withDefaultMipmaps();
    if (!source) return false;
    ImageContext encoding{};
    const SkSerialProcs encoder{.fImageProc = image_encode, .fImageCtx = &encoding};
    SkBinaryWriteBuffer imageWriter(encoder);
    imageWriter.writeImage(source.get());
    if (encoding.calls != 3) return false;
    sk_sp<SkData> encoded = imageWriter.snapshotAsData();
    ImageContext decoding{};
    const SkDeserialProcs decoder{.fImageDataProc = image_data, .fImageCtx = &decoding};
    SkReadBuffer imageReader((*encoded).data(), (*encoded).size());
    imageReader.setDeserialProcs(decoder);
    sk_sp<SkImage> restored = imageReader.readImage();
    if (!restored || decoding.calls != 3 || !imageReader.isValid()) return false;
    if (!imageReader.eof() || (*restored).dimensions() != (*source).dimensions()) return false;
    if (!(*restored).hasMipmaps()) return false;
    return true;
}

bool boundaries() {
    const std::array<uint32_t, 4> input{0xffffffffU, 1, 2, 3};
    SkReadBuffer largeArray(input.data(), sizeof(input));
    if (largeArray.readByteArrayAsData() || largeArray.isValid()) return false;
    SkReadBuffer largeSkip(input.data(), sizeof(input));
    if (largeSkip.skip(std::numeric_limits<size_t>::max()) != nullptr || largeSkip.isValid()) return false;
    SkReadBuffer product(input.data(), sizeof(input));
    if (product.skip(std::numeric_limits<size_t>::max(), 2) != nullptr || product.isValid()) return false;
    const std::array<uint32_t, 2> badString{1, 0x61616161U};
    SkReadBuffer strings(badString.data(), sizeof(badString));
    SkString text("before");
    strings.readString(&text);
    if (strings.isValid() || !text.isEmpty()) return false;
#ifndef STX_SKIA_REFERENCE
    SkReadBuffer empty{};
    if (empty.size() != 0 || empty.offset() != 0 || empty.available() != 0 || !empty.eof()) return false;
    if (!empty.readPad32(nullptr, 0) || !empty.isValid()) return false;
    if (empty.skip(0) != nullptr || !empty.isValid()) return false;
    empty.setMemory(input.data(), sizeof(input));
    if (!empty.isValid() || empty.readUInt() != input[0]) return false;
    empty.setMemory(nullptr, 0);
    if (!empty.isValid() || empty.size() != 0) return false;
    if (empty.readUInt() != 0 || empty.isValid()) return false;
    empty.setMemory(input.data(), sizeof(input));
    if (empty.isValid() || empty.size() != 0) return false;
    SkReadBuffer nullExtent(nullptr, 4);
    if (nullExtent.isValid() || nullExtent.size() != 0) return false;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.data());
    SkReadBuffer unaligned(bytes + 1, 4);
    if (unaligned.isValid()) return false;
    SkReadBuffer tooLarge(input.data(), std::numeric_limits<size_t>::max() - 3);
    if (tooLarge.isValid()) return false;
    SkReadBuffer noDestination(input.data(), sizeof(input));
    if (noDestination.readPad32(nullptr, 4) || noDestination.isValid()) return false;
#endif
    return true;
}
}

int benchmark() {
    const SkSerialProcs procs{};
    const std::array<size_t, 3> sizes{16, 256, 4096};
    for (const size_t bytes : sizes) {
        const std::vector<uint32_t> input(bytes / sizeof(uint32_t), 0x12345678U);
        SkBinaryWriteBuffer writer(procs);
        writer.writeByteArray(input.data(), bytes);
        const std::vector<uint32_t> storage = contents(writer);
        const size_t extent = writer.bytesWritten();
        const size_t iterations = 131072;
        uint64_t digest = 0;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (size_t iteration = 0; iteration < iterations; ++iteration) {
            SkReadBuffer reader(storage.data(), extent);
            sk_sp<SkData> data = reader.readByteArrayAsData();
            if (!data || (*data).size() != bytes) return 1;
            const uint8_t* payload = (*data).bytes();
            digest += payload[0];
            digest += payload[bytes - 1];
        }
        const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(stop - start).count();
        std::printf("owned_bytes_%zu,%zu,%.9f,%llu\n", bytes, iterations, seconds,
                    static_cast<unsigned long long>(digest));
    }
    const std::vector<uint32_t> words(4096, 0x12345678U);
    const size_t passes = 8192;
    const size_t reads = passes * words.size();
    uint64_t digest = 0;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (size_t pass = 0; pass < passes; ++pass) {
        SkReadBuffer reader(words.data(), words.size() * sizeof(uint32_t));
        for (size_t index = 0; index < words.size(); ++index) digest += reader.readUInt();
    }
    const std::chrono::steady_clock::time_point stop = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(stop - start).count();
    std::printf("words,%zu,%.9f,%llu\n", reads, seconds, static_cast<unsigned long long>(digest));
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) {
        const int result = benchmark();
        return result;
    }
    if (!primitives()) { std::puts("primitive mismatch"); return 1; }
    if (!arrays_and_strings()) { std::puts("array/string mismatch"); return 1; }
    if (!structures()) { std::puts("structure mismatch"); return 1; }
    if (!region_input_bounds()) { std::puts("region input mismatch"); return 1; }
    if (!images()) { std::puts("image callback mismatch"); return 1; }
    if (!boundaries()) { std::puts("boundary mismatch"); return 1; }
    std::printf("read buffer: primitives, 258 byte arrays, structures, callbacks, boundaries PASS; size=%zu\n",
                sizeof(SkReadBuffer));
    return 0;
}
