/*
 * Copyright 2009 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkEdgeClipper_DEFINED
#define SkEdgeClipper_DEFINED

#include "include/core/SkPathTypes.h"
#include "include/core/SkPoint.h"
#include "include/core/SkScalar.h"

#include <optional>

struct SkPathRaw;
struct SkRect;

/** This is basically an iterator. It is initialized with an edge and a clip,
    and then next() is called until it returns no verb.
 */
class SkEdgeClipper {
public:
    explicit SkEdgeClipper(bool canCullToTheRight) : fCanCullToTheRight(canCullToTheRight) {}

    bool clipLine(SkPoint p0, SkPoint p1, const SkRect& clip);
    bool clipQuad(const SkPoint pts[3], const SkRect& clip);
    bool clipCubic(const SkPoint pts[4], const SkRect& clip);

    // Copies the next clipped segment's points into pts (room for 4 points).
    std::optional<SkPathVerb> next(SkPoint pts[]);

    bool canCullToTheRight() const { return fCanCullToTheRight; }

    /**
     *  Clips each segment from the path, and passes the result (in a clipper) to the
     *  consume proc. The clipper passed to consume is valid only during that call.
     */
    static void ClipPath(const SkPathRaw&, const SkRect& clip, bool canCullToTheRight,
                         void (*consume)(SkEdgeClipper*, bool newCtr, void* ctx), void* ctx);

private:
    enum {
        kMaxVerbs = 18,  // max curvature in X and Y split cubic into 9 pieces, * (line + cubic)
        kMaxPoints = 54  // 2 lines + 1 cubic require 6 points; times 9 pieces
    };

    // Recording appends at the cursors; finishing rewinds them so next() reads the
    // recorded segments up to fVerbStop.
    int         fPointIndex = 0;
    int         fVerbIndex = 0;
    int         fVerbStop = 0;
    const bool  fCanCullToTheRight;

    SkPoint     fPoints[kMaxPoints] = {};
    SkPathVerb  fVerbs[kMaxVerbs] = {};

    void beginRecording();
    bool finishRecording();
    // Records verb and reserves pointCount points; returns the first reserved index.
    int appendVerb(SkPathVerb verb, int pointCount);
    void clipMonoQuad(const SkPoint srcPts[3], const SkRect& clip);
    void clipMonoCubic(const SkPoint srcPts[4], const SkRect& clip);
    void appendLine(SkPoint p0, SkPoint p1);
    void appendVLine(SkScalar x, SkScalar y0, SkScalar y1, bool reverse);
    void appendQuad(const SkPoint pts[3], bool reverse);
    void appendCubic(const SkPoint pts[4], bool reverse);
};

#endif
