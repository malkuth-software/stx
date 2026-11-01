"""Binary64 plan and gather model of stx_conv's line factor (src/plan.cpp).

Extracted from the 2026-10-29 CONV study's conv_model.py (not retained): only the
regular plan (tail weights, point and basin segments) and the production
gather are kept. Operation order follows src/plan.cpp and the stream's
synthesis exactly; the stream regression's bitwise claims depend on it.
"""
import numpy as np

GL_NODES = (-0.7745966692414834, 0.0, 0.7745966692414834)
GL_WEIGHTS = (5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0)


class Profile:
    def __init__(self, anchors, currents):
        self.anchors = anchors
        self.currents = currents


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def cmod(a, b):
    return a - b * cdiv(a, b)


def tail_weights(u):
    v = 1.0 - u
    u2 = u * u
    u3 = u2 * u
    v2 = v * v
    v3 = v2 * v
    basis = [5.0 * v3 * v * u, 10.0 * v3 * u2, 10.0 * v2 * u3, 5.0 * v * u3 * u, u3 * u2]
    weights = [0.0] * 5
    suffix = 0.0
    for index in range(5, 0, -1):
        suffix += basis[index - 1]
        weights[index - 1] = suffix
    return weights


def regular_sample(target, source, targets, sampling):
    intervals = source - 1
    if source == targets:
        return ("point", target, target, 1)
    if source == 1:
        return ("point", 0, 0, 1)
    if sampling == "pixel_area":
        return ("basin", 2 * target * source - targets, 2 * (target + 1) * source - targets, 2 * targets)
    if targets >= source:
        return ("point", target * intervals, target * intervals, targets - 1)
    if targets == 1:
        return ("basin", 0, intervals, 1)
    den = 2 * (targets - 1)
    left = 0 if target == 0 else (2 * target - 1) * intervals
    right = intervals * den if target + 1 == targets else (2 * target + 1) * intervals
    return ("basin", left, right, den)


def first_basin_cell(start, den):
    first = cdiv(start, den)
    if cmod(start, den) < 0:
        first -= 1
    return first


def end_basin_cell(end, den):
    return cdiv(end, den) + (1 if cmod(end, den) > 0 else 0)


class Segment:
    __slots__ = ("anchor_weight", "cell", "anchor", "weights")

    def __init__(self):
        self.anchor_weight = 0.0
        self.cell = 0
        self.anchor = 0
        self.weights = [0.0] * 5


def point_segment(numerator, den, n):
    cell = cdiv(numerator, den)
    rem = cmod(numerator, den)
    if rem < 0:
        cell -= 1
        rem += den
    fraction = float(rem) / float(den)
    if fraction == 1.0:
        cell += 1
        fraction = 0.0
    seg = Segment()
    seg.anchor_weight = 1.0
    seg.cell = cell
    seg.anchor = cell
    if n < 5:
        seg.weights[0] = fraction
    else:
        seg.weights = tail_weights(fraction)
    return seg


def basin_segments(left, right, den, n):
    out = []
    for cell in range(first_basin_cell(left, den), end_basin_cell(right, den)):
        origin = cell * den
        low = max(left, origin)
        high = min(right, origin + den)
        a = float(low - origin) / float(den)
        extent = float(high - low) / float(den)
        seg = Segment()
        seg.anchor_weight = float(high - low) / float(right - left)
        if cell < 0:
            seg.anchor = 0
        elif cell >= n - 1:
            seg.anchor = n - 1
        else:
            seg.cell = cell
            seg.anchor = cell
            if n < 5:
                seg.weights[0] = seg.anchor_weight * (a + 0.5 * extent)
            else:
                middle = a + 0.5 * extent
                half = 0.5 * extent
                for node in range(3):
                    tail = tail_weights(middle + half * GL_NODES[node])
                    factor = 0.5 * seg.anchor_weight * GL_WEIGHTS[node]
                    for k in range(5):
                        seg.weights[k] += factor * tail[k]
        out.append(seg)
    return out


def make_plan(n, m, sampling="nodal"):
    """Per target: (sample tuple, list of Segment)."""
    plan = []
    for t in range(m):
        sample = regular_sample(t, n, m, sampling)
        if sample[0] == "point":
            plan.append((sample, [point_segment(sample[1], sample[3], n)]))
        else:
            plan.append((sample, basin_segments(sample[1], sample[2], sample[3], n)))
    return plan


def segment_value(seg, anchors, currents, dtype=np.float64):
    f = dtype
    sample = f(seg.anchor_weight) * f(anchors[seg.anchor])
    if seg.weights[0] != 0.0:
        c = currents[seg.cell]
        for k in range(5):
            sample = sample + f(seg.weights[k]) * f(c[k])
    return sample


def evaluate(profile, plan, dtype=np.float64):
    """Production gather: each target accumulates its segments serially."""
    out = np.empty(len(plan), dtype=dtype)
    for t, (_, segments) in enumerate(plan):
        acc = dtype(0.0)
        for index, seg in enumerate(segments):
            value = segment_value(seg, profile.anchors, profile.currents, dtype)
            acc = value if index == 0 else acc + value
        out[t] = acc
    return out
