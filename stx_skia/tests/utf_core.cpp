// Differential corpus for SkUTF: counting, decoding (with and without replacement), encoding and
// conversion between UTF-8 and UTF-16, over valid text built from every class of code point,
// structurally malformed text (bad lead bytes, stray or missing continuations, truncation,
// unpaired UTF-16 surrogates, odd lengths, misaligned pointers) and conversion into buffers of
// every capacity. Both builds must produce identical bytes (utf-core.raw).
//
// A second artifact (utf-core-strict.raw) records the same functions on input that is well formed
// but not Unicode: overlong UTF-8, UTF-8 and UTF-32 encodings of surrogates, values past U+10FFFF,
// and lone surrogates given to the encoders. The intake code accepts these; the corpus keeps them
// apart so that a stricter build can differ there and only there.
//
// The candidate alone checks that it rejects all of that input, and that it refuses a null UTF-32
// pointer with a length and a negative conversion capacity.

#include "differential_corpus.h"

#include "src/core/SkUTF.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

// Independent encoders for valid scalar values, so the corpus does not build its input with the
// code under test.
void encode_utf8(std::vector<uint8_t>& out, uint32_t value) {
    if (value < 0x80) {
        out.push_back(static_cast<uint8_t>(value));
    } else if (value < 0x800) {
        out.push_back(static_cast<uint8_t>(0xC0 | (value >> 6)));
        out.push_back(static_cast<uint8_t>(0x80 | (value & 0x3F)));
    } else if (value < 0x10000) {
        out.push_back(static_cast<uint8_t>(0xE0 | (value >> 12)));
        out.push_back(static_cast<uint8_t>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<uint8_t>(0x80 | (value & 0x3F)));
    } else {
        out.push_back(static_cast<uint8_t>(0xF0 | (value >> 18)));
        out.push_back(static_cast<uint8_t>(0x80 | ((value >> 12) & 0x3F)));
        out.push_back(static_cast<uint8_t>(0x80 | ((value >> 6) & 0x3F)));
        out.push_back(static_cast<uint8_t>(0x80 | (value & 0x3F)));
    }
}

void encode_utf16(std::vector<uint16_t>& out, uint32_t value) {
    if (value < 0x10000) {
        out.push_back(static_cast<uint16_t>(value));
    } else {
        const uint32_t offset = value - 0x10000;
        out.push_back(static_cast<uint16_t>(0xD800 | (offset >> 10)));
        out.push_back(static_cast<uint16_t>(0xDC00 | (offset & 0x3FF)));
    }
}

constexpr std::array<uint32_t, 18> kScalars{0x0,    0x1,    0x41,   0x7F,    0x80,    0x7FF,
                                            0x800,  0xFFF,  0x1000, 0xD7FF,  0xE000,  0xFFFD,
                                            0xFFFE, 0xFFFF, 0x10000, 0x1F600, 0x10FFFE, 0x10FFFF};

uint32_t random_scalar(Sequence& sequence) {
    for (;;) {
        const uint32_t range = sequence.pick(4);
        uint32_t value = 0;
        if (range == 0) value = sequence.next() % 0x80;
        if (range == 1) value = 0x80 + sequence.next() % 0x780;
        if (range == 2) value = 0x800 + sequence.next() % 0xF800;
        if (range == 3) value = 0x10000 + sequence.next() % 0x100000;
        if (value < 0xD800 || value > 0xDFFF) return value;
    }
}

// Records every UTF-8 function on 'bytes'.
void record_utf8(Output& output, const std::vector<uint8_t>& bytes) {
    const char* text = reinterpret_cast<const char*>(bytes.data());
    const size_t length = bytes.size();
    append(output, static_cast<int32_t>(SkUTF::CountUTF8(text, length)));
    const char* cursor = text;
    const char* end = text + length;
    int steps = 0;
    while (cursor < end && steps < 64) {
        const SkUnichar value = SkUTF::NextUTF8(&cursor, end);
        append(output, static_cast<int32_t>(value));
        append(output, static_cast<int64_t>(cursor - text));
        ++steps;
    }
    cursor = text;
    steps = 0;
    while (cursor < end && steps < 64) {
        append(output, static_cast<int32_t>(SkUTF::NextUTF8WithReplacement(&cursor, end)));
        ++steps;
    }
    // Conversion into buffers of every capacity up to one more than needed, with guards.
    const int needed = SkUTF::UTF8ToUTF16(nullptr, 0, text, length);
    append(output, static_cast<int32_t>(needed));
    const int top = needed > 0 ? needed + 1 : 3;
    for (int capacity = 0; capacity <= top && capacity <= 40; ++capacity) {
        std::array<uint16_t, 48> buffer{};
        buffer.fill(0xBEEF);
        const int written = SkUTF::UTF8ToUTF16(buffer.data(), capacity, text, length);
        append(output, static_cast<int32_t>(written));
        for (const uint16_t unit : buffer) append(output, unit);
    }
    ++output.cases;
}

void record_utf16(Output& output, const std::vector<uint16_t>& units) {
    const size_t length = units.size();
    append(output, static_cast<int32_t>(SkUTF::CountUTF16(units.data(), length * 2)));
    const uint16_t* cursor = units.data();
    const uint16_t* end = units.data() + length;
    int steps = 0;
    while (cursor < end && steps < 64) {
        append(output, static_cast<int32_t>(SkUTF::NextUTF16(&cursor, end)));
        append(output, static_cast<int64_t>(cursor - units.data()));
        ++steps;
    }
    const int needed = SkUTF::UTF16ToUTF8(nullptr, 0, units.data(), length);
    append(output, static_cast<int32_t>(needed));
    const int top = needed > 0 ? needed + 1 : 5;
    for (int capacity = 0; capacity <= top && capacity <= 80; ++capacity) {
        std::array<char, 96> buffer{};
        buffer.fill('#');
        const int written = SkUTF::UTF16ToUTF8(buffer.data(), capacity, units.data(), length);
        append(output, static_cast<int32_t>(written));
        for (const char byte : buffer) append(output, byte);
    }
    ++output.cases;
}

void record_utf32(Output& output, const std::vector<int32_t>& values) {
    const size_t length = values.size();
    append(output, static_cast<int32_t>(SkUTF::CountUTF32(values.data(), length * 4)));
    const int32_t* cursor = values.data();
    const int32_t* end = values.data() + length;
    int steps = 0;
    while (cursor < end && steps < 64) {
        append(output, static_cast<int32_t>(SkUTF::NextUTF32(&cursor, end)));
        append(output, static_cast<int64_t>(cursor - values.data()));
        ++steps;
    }
    ++output.cases;
}

void record_encoders(Output& output, SkUnichar value) {
    std::array<char, SkUTF::kMaxBytesInUTF8Sequence> utf8{};
    utf8.fill('#');
    append(output, static_cast<uint64_t>(SkUTF::ToUTF8(value)));
    append(output, static_cast<uint64_t>(SkUTF::ToUTF8(value, utf8.data())));
    for (const char byte : utf8) append(output, byte);
    std::array<uint16_t, 2> utf16{0xBEEF, 0xBEEF};
    append(output, static_cast<uint64_t>(SkUTF::ToUTF16(value)));
    append(output, static_cast<uint64_t>(SkUTF::ToUTF16(value, utf16.data())));
    append(output, utf16[0]);
    append(output, utf16[1]);
    ++output.cases;
}

void valid_cases(Output& output) {
    for (const uint32_t value : kScalars) {
        record_encoders(output, static_cast<SkUnichar>(value));
        std::vector<uint8_t> bytes{};
        encode_utf8(bytes, value);
        record_utf8(output, bytes);
        std::vector<uint16_t> units{};
        encode_utf16(units, value);
        record_utf16(output, units);
        record_utf32(output, std::vector<int32_t>{static_cast<int32_t>(value)});
    }
    // Values no encoding can hold.
    for (const SkUnichar value : {-1, -0x7FFFFFFF - 1, 0x110000, 0x7FFFFFFF}) {
        record_encoders(output, value);
    }
    Sequence sequence{};
    for (int round = 0; round < 300; ++round) {
        const size_t count = sequence.pick(24);
        std::vector<uint8_t> bytes{};
        std::vector<uint16_t> units{};
        std::vector<int32_t> values{};
        for (size_t index = 0; index < count; ++index) {
            const uint32_t value = random_scalar(sequence);
            encode_utf8(bytes, value);
            encode_utf16(units, value);
            values.push_back(static_cast<int32_t>(value));
        }
        record_utf8(output, bytes);
        record_utf16(output, units);
        record_utf32(output, values);
    }
    // Empty and null input.
    append(output, static_cast<int32_t>(SkUTF::CountUTF8(nullptr, 0)));
    append(output, static_cast<int32_t>(SkUTF::CountUTF8(nullptr, 3)));
    append(output, static_cast<int32_t>(SkUTF::CountUTF16(nullptr, 0)));
    append(output, static_cast<int32_t>(SkUTF::CountUTF32(nullptr, 0)));
    const char* nothing = nullptr;
    append(output, static_cast<int32_t>(SkUTF::NextUTF8(&nothing, nullptr)));
    append(output, static_cast<int32_t>(SkUTF::UTF8ToUTF16(nullptr, 0, nullptr, 0)));
    append(output, static_cast<int32_t>(SkUTF::UTF16ToUTF8(nullptr, 0, nullptr, 0)));
    ++output.cases;
}

// Malformed structure, which the intake code already rejects.
void malformed_cases(Output& output) {
    const std::vector<std::vector<uint8_t>> utf8{
            {0x80},             {0xBF},          {0xC0, 0x80},       {0xC1, 0xBF},
            {0xF5, 0x80, 0x80, 0x80},            {0xF8},             {0xFF},
            {0xC3},             {0xE2, 0x82},    {0xF0, 0x9F, 0x98}, {0xC3, 0x41},
            {0xE2, 0x41, 0x82}, {0x41, 0x80, 0x42},                  {0xE2, 0x82, 0xAC, 0x80},
            {0x41, 0xC3},       {0xF0, 0x9F, 0x98, 0x41},            {0xFE, 0xFF}};
    for (const std::vector<uint8_t>& bytes : utf8) record_utf8(output, bytes);
    const std::vector<std::vector<uint16_t>> utf16{
            {0xDC00},         {0xDFFF, 0x41},   {0xD800},         {0xD800, 0x41},
            {0xD83D, 0xD83D}, {0x41, 0xD800},   {0x41, 0xDC00, 0x42}};
    for (const std::vector<uint16_t>& units : utf16) record_utf16(output, units);
    const std::vector<std::vector<int32_t>> utf32{{-1}, {0x41, -0x7FFFFFFF - 1}, {0x7F000000}};
    for (const std::vector<int32_t>& values : utf32) record_utf32(output, values);
    // Odd UTF-16 and unaligned UTF-32 byte lengths, and misaligned pointers.
    alignas(8) std::array<uint8_t, 16> raw{};
    raw.fill(0x41);
    append(output, static_cast<int32_t>(SkUTF::CountUTF16(
                           reinterpret_cast<const uint16_t*>(raw.data()), 3)));
    append(output, static_cast<int32_t>(SkUTF::CountUTF16(
                           reinterpret_cast<const uint16_t*>(raw.data() + 1), 4)));
    append(output, static_cast<int32_t>(SkUTF::CountUTF32(
                           reinterpret_cast<const int32_t*>(raw.data()), 6)));
    append(output, static_cast<int32_t>(SkUTF::CountUTF32(
                           reinterpret_cast<const int32_t*>(raw.data() + 2), 8)));
    ++output.cases;
}

// Well-formed input that is not Unicode; see the header comment.
void strict_cases(Output& output) {
    const std::vector<std::vector<uint8_t>> utf8{
            {0xE0, 0x80, 0x80},       {0xE0, 0x9F, 0xBF},       {0xF0, 0x80, 0x80, 0x80},
            {0xF0, 0x8F, 0xBF, 0xBF}, {0xED, 0xA0, 0x80},       {0xED, 0xBF, 0xBF},
            {0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80},                {0xF4, 0x90, 0x80, 0x80},
            {0xF4, 0xBF, 0xBF, 0xBF}, {0x41, 0xED, 0xA0, 0x80, 0x42}, {0x41, 0xE0, 0x80, 0xAF}};
    for (const std::vector<uint8_t>& bytes : utf8) record_utf8(output, bytes);
    const std::vector<std::vector<int32_t>> utf32{
            {0xD800}, {0xDFFF}, {0x41, 0xDBFF}, {0x110000}, {0x41, 0xFFFFFF}};
    for (const std::vector<int32_t>& values : utf32) record_utf32(output, values);
    for (const SkUnichar value : {0xD800, 0xDBFF, 0xDC00, 0xDFFF}) record_encoders(output, value);
}

#ifndef STX_SKIA_REFERENCE
bool rejects_utf8(const std::vector<uint8_t>& bytes) {
    const char* text = reinterpret_cast<const char*>(bytes.data());
    const char* cursor = text;
    const char* end = text + bytes.size();
    bool decodedInvalid = false;
    while (cursor < end) {
        if (SkUTF::NextUTF8(&cursor, end) < 0) decodedInvalid = true;
    }
    return SkUTF::CountUTF8(text, bytes.size()) == -1 && decodedInvalid &&
           SkUTF::UTF8ToUTF16(nullptr, 0, text, bytes.size()) == -1;
}

// Overlong forms, encoded surrogates and values past U+10FFFF are rejected by every decoder;
// lone surrogates are rejected by the encoders; and the boundary values on either side are kept.
bool strict_unicode() {
    const std::vector<std::vector<uint8_t>> rejected{
            {0xE0, 0x80, 0x80}, {0xE0, 0x9F, 0xBF}, {0xF0, 0x80, 0x80, 0x80},
            {0xF0, 0x8F, 0xBF, 0xBF}, {0xED, 0xA0, 0x80}, {0xED, 0xBF, 0xBF},
            {0xF4, 0x90, 0x80, 0x80}, {0xF4, 0xBF, 0xBF, 0xBF}};
    for (const std::vector<uint8_t>& bytes : rejected) {
        if (!rejects_utf8(bytes)) return false;
    }
    // The nearest accepted neighbours: U+0800, U+10000, U+D7FF, U+E000 and U+10FFFF.
    const std::vector<std::vector<uint8_t>> accepted{
            {0xE0, 0xA0, 0x80}, {0xF0, 0x90, 0x80, 0x80}, {0xED, 0x9F, 0xBF},
            {0xEE, 0x80, 0x80}, {0xF4, 0x8F, 0xBF, 0xBF}};
    for (const std::vector<uint8_t>& bytes : accepted) {
        const char* text = reinterpret_cast<const char*>(bytes.data());
        if (SkUTF::CountUTF8(text, bytes.size()) != 1) return false;
    }
    for (const int32_t value : {0xD800, 0xDFFF, 0x110000, 0xFFFFFF}) {
        const int32_t* cursor = &value;
        if (SkUTF::CountUTF32(&value, 4) != -1) return false;
        if (SkUTF::NextUTF32(&cursor, &value + 1) != -1) return false;
    }
    for (const SkUnichar value : {0xD800, 0xDBFF, 0xDC00, 0xDFFF}) {
        if (SkUTF::ToUTF8(value) != 0 || SkUTF::ToUTF16(value) != 0) return false;
    }
    return SkUTF::ToUTF8(0xD7FF) == 3 && SkUTF::ToUTF16(0xE000) == 1;
}

bool refuses_bad_arguments() {
    if (SkUTF::CountUTF32(nullptr, 4) != -1) return false;
    const char text[] = "abc";
    std::array<uint16_t, 4> units{};
    if (SkUTF::UTF8ToUTF16(units.data(), -1, text, 3) != -1) return false;
    const std::array<uint16_t, 3> source{0x61, 0x62, 0x63};
    std::array<char, 4> bytes{};
    return SkUTF::UTF16ToUTF8(bytes.data(), -2, source.data(), 3) == -1;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 3) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!strict_unicode()) {
        std::puts("utf core: text that is not Unicode was accepted");
        return 1;
    }
    if (!refuses_bad_arguments()) {
        std::puts("utf core: a null source or negative capacity was not refused");
        return 1;
    }
#endif
    Output output{};
    valid_cases(output);
    malformed_cases(output);
    Output strict{};
    strict_cases(strict);
    if (argc >= 2 && !corpus::write_file(argv[1], output)) return 2;
    if (argc == 3 && !corpus::write_file(argv[2], strict)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    const unsigned long long strictHash = corpus::fnv1a(strict.bytes);
    std::printf("utf core: %zu cases, %zu bytes, hash=%llu; strict %zu cases, hash=%llu PASS\n",
                output.cases, output.bytes.size(), hash, strict.cases, strictHash);
    return 0;
}
