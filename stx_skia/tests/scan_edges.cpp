// Differential corpus for scan-conversion edges: line, quadratic and cubic edges (with and
// without a clip) stepped through every segment, the analytic (anti-aliased) edges stepped
// through every update, and both edge builders over random paths with and without a clip.

#include "differential_corpus.h"

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "src/core/SkAnalyticEdge.h"
#include "src/core/SkEdge.h"
#include "src/core/SkEdgeBuilder.h"
#include "src/core/SkGeometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

// Coordinates in [-200, 600) at sixteenth steps, sometimes exactly on a half pixel.
float pick_coordinate(Sequence& sequence) {
    const int32_t whole = static_cast<int32_t>(sequence.next() % 800) - 200;
    const uint32_t choice = sequence.next() % 6;
    float fraction = static_cast<float>(sequence.next() % 16) * 0.0625f;
    if (choice == 0) {
        fraction = 0.5f;
    } else if (choice == 1) {
        fraction = 0.0f;
    }
    const float value = static_cast<float>(whole) + fraction;
    return value;
}

SkPoint pick_point(Sequence& sequence, const SkPoint& previous) {
    const uint32_t choice = sequence.next() % 10;
    if (choice == 0) {
        const SkPoint vertical{previous.fX, pick_coordinate(sequence)};
        return vertical;
    }
    if (choice == 1) {
        const SkPoint horizontal{pick_coordinate(sequence), previous.fY};
        return horizontal;
    }
    if (choice == 2) {
        const SkPoint near{previous.fX + 0.25f, previous.fY + 0.75f};
        return near;
    }
    const SkPoint point{pick_coordinate(sequence), pick_coordinate(sequence)};
    return point;
}

void append_edge(Output& output, const SkEdge& edge) {
    append(output, edge.fX);
    append(output, edge.fDxDy);
    append(output, edge.fFirstY);
    append(output, edge.fLastY);
    append(output, static_cast<int8_t>(edge.fEdgeType));
    append(output, static_cast<int8_t>(edge.fWinding));
    append(output, edge.segmentsLeft());
}

// Records the edge and every following segment.
void append_edge_segments(Output& output, SkEdge* edge) {
    append_edge(output, *edge);
    int steps = 0;
    while ((*edge).hasNextSegment() && steps < 200) {
        append_bool(output, (*edge).nextSegment());
        append_edge(output, *edge);
        ++steps;
    }
    append(output, steps);
}

void append_analytic_edge(Output& output, const SkAnalyticEdge& edge) {
    append(output, edge.fX);
    append(output, edge.fDX);
    append(output, edge.fUpperX);
    append(output, edge.fY);
    append(output, edge.fUpperY);
    append(output, edge.fLowerY);
    append(output, edge.fDY);
    append(output, static_cast<int8_t>(edge.fEdgeType));
    append(output, edge.fCurveCount);
    append(output, edge.fCurveShift);
    append(output, static_cast<int8_t>(edge.fWinding));
}

// Records the edge and every following update.
void append_analytic_updates(Output& output, SkAnalyticEdge* edge) {
    append_analytic_edge(output, *edge);
    int steps = 0;
    while ((*edge).fCurveCount != 0 && steps < 200) {
        append_bool(output, (*edge).update((*edge).fLowerY));
        append_analytic_edge(output, *edge);
        ++steps;
    }
    append(output, steps);
}

const std::array<SkIRect, 3> kClips{
    SkIRect::MakeLTRB(0, 0, 64, 64), SkIRect::MakeLTRB(-50, 10, 300, 200),
    SkIRect::MakeLTRB(100, 100, 101, 400),
};

void line_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 4000; ++i) {
        const SkPoint p0{pick_coordinate(sequence), pick_coordinate(sequence)};
        const SkPoint p1 = pick_point(sequence, p0);
        // Edge fields are read only after a successful setup; the original leaves them
        // uninitialized otherwise.
        SkEdge edge{};
        const bool set = edge.setLine(p0, p1);
        append_bool(output, set);
        if (set) {
            append_edge(output, edge);
        }
        for (const SkIRect& clip : kClips) {
            SkEdge clipped{};
            const bool clippedSet = clipped.setLine(p0, p1, &clip);
            append_bool(output, clippedSet);
            if (clippedSet) {
                append_edge(output, clipped);
            }
        }
        SkEdge unclipped{};
        const bool unclippedSet = unclipped.setLine(p0, p1, nullptr);
        append_bool(output, unclippedSet);
        if (unclippedSet) {
            append_edge(output, unclipped);
        }

        SkAnalyticEdge analytic{};
        const bool analyticSet = analytic.setLine(p0, p1);
        append_bool(output, analyticSet);
        if (analyticSet) {
            append_analytic_edge(output, analytic);
        }
        if (analyticSet && analytic.fLowerY > analytic.fUpperY) {
            analytic.goY(analytic.fUpperY + SK_Fixed1);
            append(output, analytic.fX);
            append(output, analytic.fY);
        }
        ++output.cases;
    }
}

void curve_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 3000; ++i) {
        SkPoint pts[4] = {};
        pts[0] = SkPoint{pick_coordinate(sequence), pick_coordinate(sequence)};
        for (int k = 1; k < 4; ++k) {
            pts[k] = pick_point(sequence, pts[k - 1]);
        }
        SkPoint quads[5] = {};
        const int quadPieces = SkChopQuadAtYExtrema(pts, quads);
        for (int piece = 0; piece <= quadPieces; ++piece) {
            SkQuadraticEdge edge{};
            const bool set = edge.setQuadratic(&quads[piece * 2]);
            append_bool(output, set);
            if (set) {
                append_edge_segments(output, &edge);
            }
            SkAnalyticQuadraticEdge analytic{};
            const bool analyticSet = analytic.setQuadratic(&quads[piece * 2]);
            append_bool(output, analyticSet);
            if (analyticSet) {
                append(output, analytic.fQLastX);
                append(output, analytic.fQLastY);
                append_analytic_updates(output, &analytic);
            }
        }
        SkPoint cubics[10] = {};
        const int cubicPieces = SkChopCubicAtYExtrema(pts, cubics);
        for (int piece = 0; piece <= cubicPieces; ++piece) {
            SkCubicEdge edge{};
            const bool set = edge.setCubic(&cubics[piece * 3]);
            append_bool(output, set);
            if (set) {
                append_edge_segments(output, &edge);
            }
            SkAnalyticCubicEdge analytic{};
            const bool analyticSet = analytic.setCubic(&cubics[piece * 3]);
            append_bool(output, analyticSet);
            if (analyticSet) {
                append(output, analytic.fCLastX);
                append(output, analytic.fCLastY);
                append_analytic_updates(output, &analytic);
            }
        }
        ++output.cases;
    }
}

SkPath random_path(Sequence& sequence, bool linesOnly) {
    SkPathBuilder builder{};
    const int contours = 1 + static_cast<int>(sequence.next() % 3);
    SkPoint last{0, 0};
    for (int contour = 0; contour < contours; ++contour) {
        last = SkPoint{pick_coordinate(sequence), pick_coordinate(sequence)};
        builder.moveTo(last);
        const int verbs = 1 + static_cast<int>(sequence.next() % 6);
        for (int verb = 0; verb < verbs; ++verb) {
            const SkPoint p1 = pick_point(sequence, last);
            const SkPoint p2 = pick_point(sequence, p1);
            const SkPoint p3 = pick_point(sequence, p2);
            const uint32_t kind = linesOnly ? 0 : sequence.next() % 4;
            if (kind == 0) {
                builder.lineTo(p1);
                last = p1;
            } else if (kind == 1) {
                builder.quadTo(p1, p2);
                last = p2;
            } else if (kind == 2) {
                builder.conicTo(p1, p2, (sequence.next() % 2 == 0) ? 0.70710678f : 2.5f);
                last = p2;
            } else {
                builder.cubicTo(p1, p2, p3);
                last = p3;
            }
        }
        if (sequence.next() % 2 == 0) {
            builder.close();
        }
    }
    const SkPath path = builder.detach();
    return path;
}

void builder_corpus(Output& output, Sequence& sequence) {
    for (int i = 0; i < 2500; ++i) {
        const SkPath path = random_path(sequence, i % 3 == 0);
        const SkIRect* clip = nullptr;
        if (sequence.next() % 2 == 0) {
            clip = &kClips[sequence.pick(kClips.size())];
        }

        SkBasicEdgeBuilder basic;
        const int basicCount = basic.buildEdges(path, clip);
        append(output, basicCount);
        SkEdge** edges = basic.edgeList();
        for (int e = 0; e < basicCount; ++e) {
            append_edge_segments(output, edges[e]);
        }

        SkAnalyticEdgeBuilder analytic;
        const int analyticCount = analytic.buildEdges(path, clip);
        append(output, analyticCount);
        SkAnalyticEdge** analyticEdges = analytic.analyticEdgeList();
        for (int e = 0; e < analyticCount; ++e) {
            append_analytic_updates(output, analyticEdges[e]);
        }
        ++output.cases;
    }
}

void edge_corpus(Output& output) {
    Sequence sequence{};
    line_corpus(output, sequence);
    curve_corpus(output, sequence);
    builder_corpus(output, sequence);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2) return 2;
    Output output{};
    edge_corpus(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("scan edges: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
