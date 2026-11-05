// Differential corpus for path clipping ahead of scan conversion: line intersection and
// scan-line clipping against rectangles in both culling modes, edge clipping of lines,
// quadratics and cubics with every recorded segment, and whole-path clipping.

#include "differential_corpus.h"

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "src/core/SkEdgeClipper.h"
#include "src/core/SkLineClipper.h"
#include "src/core/SkPathPriv.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

const std::array<SkRect, 7> kClips{
    SkRect::MakeLTRB(0, 0, 64, 64),    SkRect::MakeLTRB(-10, -20, 10, 20),
    SkRect::MakeLTRB(5, 5, 5, 40),     SkRect::MakeLTRB(-1e7f, -1e7f, 1e7f, 1e7f),
    SkRect::MakeLTRB(0.5f, 0.25f, 31.75f, 15.5f), SkRect::MakeLTRB(3, 3, 4, 4),
    SkRect::MakeLTRB(-64, 0, 0, 64),
};

float pick_coordinate(Sequence& sequence) {
    const uint32_t choice = sequence.next() % 24;
    if (choice == 0) {
        return 1e8f;
    }
    if (choice == 1) {
        return -6e6f;
    }
    if (choice == 2) {
        return 1e-6f;
    }
    if (choice == 3) {
        // Exactly on a common clip edge.
        const float edge = (sequence.next() % 2 == 0) ? 0.0f : 64.0f;
        return edge;
    }
    const int32_t whole = static_cast<int32_t>(sequence.next() % 161) - 48;
    const float fraction = static_cast<float>(sequence.next() % 8) * 0.125f;
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

void fill_points(Sequence& sequence, SkPoint* points, int count) {
    for (int i = 0; i < count; ++i) {
        const uint32_t choice = sequence.next() % 10;
        if (i > 0 && choice == 0) {
            points[i] = points[i - 1];
        } else if (i > 0 && choice == 1) {
            points[i] = SkPoint{points[i - 1].fX, pick_coordinate(sequence)};
        } else if (i > 0 && choice == 2) {
            points[i] = SkPoint{pick_coordinate(sequence), points[i - 1].fY};
        } else {
            points[i] = SkPoint{pick_coordinate(sequence), pick_coordinate(sequence)};
        }
    }
}

void append_point(Output& output, const SkPoint& point) {
    append(output, point.fX);
    append(output, point.fY);
}

void append_points(Output& output, const SkPoint* points, int count) {
    for (int i = 0; i < count; ++i) {
        append_point(output, points[i]);
    }
}

int verb_point_count(SkPathVerb verb) {
    int count = 0;
    switch (verb) {
        case SkPathVerb::kLine:
            count = 2;
            break;
        case SkPathVerb::kQuad:
            count = 3;
            break;
        case SkPathVerb::kCubic:
            count = 4;
            break;
        default:
            count = 0;
            break;
    }
    return count;
}

void append_segments(Output& output, SkEdgeClipper* clipper) {
    int segments = 0;
    SkPoint pts[4] = {};
    for (std::optional<SkPathVerb> verb = (*clipper).next(pts); verb.has_value();
         verb = (*clipper).next(pts)) {
        append(output, static_cast<uint8_t>(verb.value()));
        append_points(output, pts, verb_point_count(verb.value()));
        ++segments;
    }
    append(output, segments);
}

void line_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 6000; ++i) {
        SkPoint line[2] = {};
        fill_points(sequence, line, 2);
        const SkRect& clip = kClips[sequence.pick(kClips.size())];

        SkPoint intersected[2] = {{-7, -7}, {-7, -7}};
        append_bool(output, SkLineClipper::IntersectLine(line, clip, intersected));
        append_points(output, intersected, 2);
        SkPoint inPlace[2] = {line[0], line[1]};
        append_bool(output, SkLineClipper::IntersectLine(inPlace, clip, inPlace));
        append_points(output, inPlace, 2);

        for (int cull = 0; cull < 2; ++cull) {
            SkPoint lines[SkLineClipper::kMaxPoints] = {};
            const int count = SkLineClipper::ClipLine(line, clip, lines, cull == 1);
            append(output, count);
            append_points(output, lines, count + 1);

            SkEdgeClipper clipper(cull == 1);
            append_bool(output, clipper.clipLine(line[0], line[1], clip));
            append_segments(output, &clipper);
        }
        ++output.cases;
    }
}

void curve_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 6000; ++i) {
        SkPoint pts[4] = {};
        fill_points(sequence, pts, 4);
        const SkRect& clip = kClips[sequence.pick(kClips.size())];
        const bool cull = sequence.next() % 2 == 0;
        SkEdgeClipper clipper(cull);
        if (i % 2 == 0) {
            append_bool(output, clipper.clipQuad(pts, clip));
        } else {
            append_bool(output, clipper.clipCubic(pts, clip));
        }
        append_segments(output, &clipper);
        // A second clip on the same clipper starts a fresh recording.
        append_bool(output, clipper.clipLine(pts[3], pts[0], clip));
        append_segments(output, &clipper);
        ++output.cases;
    }
}

struct ConsumeState {
    Output* fOutput = nullptr;
    int fCalls = 0;
};

void consume_segments(SkEdgeClipper* clipper, bool newContour, void* ctx) {
    ConsumeState& state = *static_cast<ConsumeState*>(ctx);
    append_bool(*state.fOutput, newContour);
    append_bool(*state.fOutput, (*clipper).canCullToTheRight());
    append_segments(*state.fOutput, clipper);
    ++state.fCalls;
}

void path_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 1500; ++i) {
        SkPathBuilder builder{};
        const int contours = 1 + static_cast<int>(sequence.next() % 3);
        for (int contour = 0; contour < contours; ++contour) {
            SkPoint pts[4] = {};
            fill_points(sequence, pts, 4);
            builder.moveTo(pts[0]);
            const int verbs = 1 + static_cast<int>(sequence.next() % 4);
            for (int verb = 0; verb < verbs; ++verb) {
                fill_points(sequence, pts, 4);
                const uint32_t kind = sequence.next() % 4;
                if (kind == 0) {
                    builder.lineTo(pts[1]);
                } else if (kind == 1) {
                    builder.quadTo(pts[1], pts[2]);
                } else if (kind == 2) {
                    builder.conicTo(pts[1], pts[2], (sequence.next() % 2 == 0) ? 0.5f : 3.0f);
                } else {
                    builder.cubicTo(pts[1], pts[2], pts[3]);
                }
            }
            if (sequence.next() % 2 == 0) {
                builder.close();
            }
        }
        const SkPath path = builder.detach();
        const std::optional<SkPathRaw> raw = SkPathPriv::Raw(path, SkResolveConvexity::kNo);
        append_bool(output, raw.has_value());
        if (raw.has_value()) {
            const SkRect& clip = kClips[sequence.pick(kClips.size())];
            ConsumeState state{};
            state.fOutput = &output;
            SkEdgeClipper::ClipPath(raw.value(), clip, sequence.next() % 2 == 0,
                                    consume_segments, &state);
            append(output, state.fCalls);
        }
        ++output.cases;
    }
}

void clipping_corpus(Output& output) {
    Sequence sequence{};
    line_corpus(output, sequence);
    curve_corpus(output, sequence);
    path_corpus(output, sequence);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    clipping_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("path clipping: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
