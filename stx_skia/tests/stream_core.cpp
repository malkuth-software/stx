// Differential corpus for the stream interfaces the candidate keeps: SkStreamPriv's
// RemainingLengthIsBelow for every combination of known or unknown length and position, with
// lengths, positions and requests around the boundaries; SkStream::skip; and SkWStream::writeText
// into a recording stream. Both builds must produce identical bytes (stream-core.raw). A position
// past the length and a null text are checked by the candidate alone: the intake code wraps around
// at the first and reads through a null pointer at the second.

#include "differential_corpus.h"
#include "stream_sink.h"

#include "include/core/SkStream.h"
#include "src/core/SkStreamPriv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace {

using corpus::Output;
using corpus::append;
using corpus::append_bool;

// Reports whatever length and position it is given, and reads up to its length.
class ReportingStream final : public SkStream {
public:
    ReportingStream(bool knowsLength, size_t length, bool knowsPosition, size_t position)
            : fKnowsLength(knowsLength), fLength(length), fKnowsPosition(knowsPosition),
              fPosition(position) {}

    size_t read(void*, size_t size) override {
        const size_t remaining = fPosition < fLength ? fLength - fPosition : 0;
        const size_t count = size < remaining ? size : remaining;
        fPosition += count;
        return count;
    }
    bool isAtEnd() const override { return fPosition >= fLength; }
    bool hasPosition() const override { return fKnowsPosition; }
    size_t getPosition() const override { return fPosition; }
    bool hasLength() const override { return fKnowsLength; }
    size_t getLength() const override { return fLength; }

private:
    bool fKnowsLength;
    size_t fLength;
    bool fKnowsPosition;
    size_t fPosition;
};

// The intake helper takes a pointer; the candidate's takes a reference.
bool below(SkStream& stream, size_t length) {
#ifdef STX_SKIA_REFERENCE
    const bool result = SkStreamPriv::RemainingLengthIsBelow(&stream, length);
#else
    const bool result = SkStreamPriv::RemainingLengthIsBelow(stream, length);
#endif
    return result;
}

constexpr size_t kMax = std::numeric_limits<size_t>::max();

void remaining_cases(Output& output) {
    const std::array<size_t, 6> lengths{0, 1, 7, 64, kMax - 1, kMax};
    const std::array<size_t, 8> requests{0, 1, 6, 7, 8, 63, 64, kMax};
    for (const bool knowsLength : {false, true}) {
        for (const bool knowsPosition : {false, true}) {
            for (const size_t length : lengths) {
                // Positions from the start to the end, never past it (see the header comment).
                const std::array<size_t, 4> positions{0, length / 2, length > 0 ? length - 1 : 0,
                                                      length};
                for (const size_t position : positions) {
                    for (const size_t request : requests) {
                        ReportingStream stream(knowsLength, length, knowsPosition, position);
                        append_bool(output, below(stream, request));
                        ++output.cases;
                    }
                }
            }
        }
    }
}

void skip_cases(Output& output) {
    const std::array<size_t, 5> requests{0, 1, 5, 9, kMax};
    for (const size_t request : requests) {
        ReportingStream stream(true, 9, true, 2);
        const size_t skipped = stream.skip(request);
        append(output, static_cast<uint64_t>(skipped));
        append(output, static_cast<uint64_t>(stream.getPosition()));
        append_bool(output, stream.isAtEnd());
        ++output.cases;
    }
}

void text_cases(Output& output) {
    const std::array<const char*, 5> texts{"", "a", "move 1 2\n", "\xc3\xa9t\xc3\xa9",
                                           "a longer line of path text, 0x3f800000\n"};
    ByteSink sink{};
    for (const char* text : texts) {
        append_bool(output, sink.writeText(text));
        append(output, static_cast<uint64_t>(sink.bytesWritten()));
        ++output.cases;
    }
    output.bytes.insert(output.bytes.end(), sink.bytes().begin(), sink.bytes().end());
}

#ifndef STX_SKIA_REFERENCE
// A stream reporting a position past its length has nothing left, so any positive request is
// refused, and so is an empty one.
bool past_end_refused() {
    for (const size_t request : {size_t{0}, size_t{1}, kMax}) {
        ReportingStream stream(true, 4, true, 5);
        if (!below(stream, request)) return false;
    }
    return true;
}

// A null text writes nothing and is refused, and the stream stays usable.
bool null_text_refused() {
    ByteSink sink{};
    if (sink.writeText(nullptr)) return false;
    if (sink.bytesWritten() != 0) return false;
    return sink.writeText("ok") && sink.bytesWritten() == 2;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (!null_text_refused()) {
        std::puts("stream core: a null text was not refused");
        return 1;
    }
    if (!past_end_refused()) {
        std::puts("stream core: a position past the length was not refused");
        return 1;
    }
#endif
    Output output{};
    remaining_cases(output);
    skip_cases(output);
    text_cases(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("stream core: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
