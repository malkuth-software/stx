// Coarser levels by exact integer Douglas-Peucker (levels.hpp).
//
// A vertex's distance to a chord a-b is compared as distance^2 * |b - a|^2,
// which for a vertex projecting inside the chord is cross^2 and otherwise
// the squared distance to the nearer end times |b - a|^2. Positions are
// within 2^28 of the origin, so differences stay under 2^29, squares and
// cross products under 2^59, and the compared products under 2^118: they
// are formed in 128 bits from two 64-bit halves.

#include "stx_map/levels.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_map {
namespace {

constexpr std::int32_t position_limit = 1 << 28;

// An unsigned 128-bit value.
struct Wide final {
    std::uint64_t high = 0U;
    std::uint64_t low = 0U;
};

Wide multiply(std::uint64_t a, std::uint64_t b) noexcept {
    const std::uint64_t a_low = a & 0xFFFFFFFFU;
    const std::uint64_t a_high = a >> 32U;
    const std::uint64_t b_low = b & 0xFFFFFFFFU;
    const std::uint64_t b_high = b >> 32U;
    const std::uint64_t low_low = a_low * b_low;
    const std::uint64_t high_low = a_high * b_low;
    const std::uint64_t low_high = a_low * b_high;
    const std::uint64_t high_high = a_high * b_high;
    const std::uint64_t middle = (low_low >> 32U) + (high_low & 0xFFFFFFFFU) + (low_high & 0xFFFFFFFFU);
    Wide product;
    product.low = (middle << 32U) | (low_low & 0xFFFFFFFFU);
    product.high = high_high + (high_low >> 32U) + (low_high >> 32U) + (middle >> 32U);
    return product;
}

bool less(const Wide& a, const Wide& b) noexcept {
    const bool smaller = a.high != b.high ? a.high < b.high : a.low < b.low;
    return smaller;
}

std::uint64_t magnitude(std::int64_t value) noexcept {
    const std::uint64_t result = value < 0 ? static_cast<std::uint64_t>(-value) : static_cast<std::uint64_t>(value);
    return result;
}

struct Point final {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

std::uint64_t squared_distance(Point a, Point b) noexcept {
    const std::int64_t dx = b.x - a.x;
    const std::int64_t dy = b.y - a.y;
    const std::uint64_t squared = static_cast<std::uint64_t>(dx * dx) + static_cast<std::uint64_t>(dy * dy);
    return squared;
}

// distance(p, chord a-b)^2 * |b - a|^2 (|b - a|^2 taken as 1 for a = b).
Wide scaled_distance(Point p, Point a, Point b) noexcept {
    const std::uint64_t chord = squared_distance(a, b);
    if (chord == 0U) {
        const Wide plain = multiply(squared_distance(p, a), 1U);
        return plain;
    }
    const std::int64_t dx = b.x - a.x;
    const std::int64_t dy = b.y - a.y;
    const std::int64_t along = (p.x - a.x) * dx + (p.y - a.y) * dy;
    if (along <= 0) {
        const Wide before = multiply(squared_distance(p, a), chord);
        return before;
    }
    if (static_cast<std::uint64_t>(along) >= chord) {
        const Wide after = multiply(squared_distance(p, b), chord);
        return after;
    }
    const std::int64_t cross = (p.x - a.x) * dy - (p.y - a.y) * dx;
    const std::uint64_t size = magnitude(cross);
    const Wide inside = multiply(size, size);
    return inside;
}

Point point_at(const LayerView& input, std::uint32_t vertex) noexcept {
    Point point;
    point.x = input.x[vertex];
    point.y = input.y[vertex];
    return point;
}

// Marks the vertices Douglas-Peucker keeps between first and last (both
// kept). The stack holds pairs; it never holds more than the span's
// vertex count of them.
void simplify_span(const LayerView& input, std::uint32_t first, std::uint32_t last, std::uint64_t tolerance_squared,
                   SimplifyScratch scratch) noexcept {
    scratch.keep[first] = 1U;
    scratch.keep[last] = 1U;
    std::size_t depth = 0U;
    scratch.stack[0] = first;
    scratch.stack[1] = last;
    depth = 1U;
    while (depth > 0U) {
        --depth;
        const std::uint32_t from = scratch.stack[depth * 2U];
        const std::uint32_t to = scratch.stack[depth * 2U + 1U];
        if (to <= from + 1U) continue;
        const Point a = point_at(input, from);
        const Point b = point_at(input, to);
        const std::uint64_t chord = squared_distance(a, b);
        const std::uint64_t scale = chord == 0U ? 1U : chord;
        const Wide limit = multiply(tolerance_squared, scale);
        Wide farthest;
        std::uint32_t index = from;
        for (std::uint32_t v = from + 1U; v < to; ++v) {
            const Wide distance = scaled_distance(point_at(input, v), a, b);
            if (index == from || less(farthest, distance)) {
                farthest = distance;
                index = v;
            }
        }
        if (!less(limit, farthest)) continue;
        scratch.keep[index] = 1U;
        scratch.stack[depth * 2U] = from;
        scratch.stack[depth * 2U + 1U] = index;
        ++depth;
        scratch.stack[depth * 2U] = index;
        scratch.stack[depth * 2U + 1U] = to;
        ++depth;
    }
}

bool view_valid(const LayerView& input) noexcept {
    if (input.x.size() != input.y.size()) return false;
    if (input.starts.empty()) return input.x.empty();
    if (input.starts[0] != 0U || input.starts[input.starts.size() - 1U] != input.x.size()) return false;
    for (std::size_t k = 0U; k + 1U < input.starts.size(); ++k) {
        if (input.starts[k + 1U] <= input.starts[k]) return false;
    }
    // The products above assume positions within 2^28 of the origin, as
    // decode_map guarantees.
    for (std::size_t v = 0U; v < input.x.size(); ++v) {
        const bool near = input.x[v] <= position_limit && input.x[v] >= -position_limit &&
            input.y[v] <= position_limit && input.y[v] >= -position_limit;
        if (!near) return false;
    }
    return true;
}

} // namespace

std::size_t simplify_stack_entries(const LayerView& input) noexcept {
    std::size_t longest = 0U;
    for (std::size_t k = 0U; k + 1U < input.starts.size(); ++k) {
        const std::size_t count = static_cast<std::size_t>(input.starts[k + 1U] - input.starts[k]);
        if (count > longest) longest = count;
    }
    return longest;
}

Status simplify_layer(const LayerView& input, std::uint32_t tolerance_units, SimplifyScratch scratch,
                      LayerStorage output, SimplifiedCounts& counts) noexcept {
    counts = SimplifiedCounts{};
    if (tolerance_units == 0U || !view_valid(input)) return Status::invalid_argument;
    const std::size_t vertices = input.x.size();
    const std::size_t lines = line_count(input);
    const std::size_t stack_entries = simplify_stack_entries(input);
    if (scratch.keep.size() < vertices || scratch.stack.size() < stack_entries * 2U) return Status::storage_too_small;
    if (output.x.size() < vertices || output.y.size() < vertices || output.starts.size() < lines + 1U) {
        return Status::storage_too_small;
    }
    const std::uint64_t tolerance_squared = static_cast<std::uint64_t>(tolerance_units) * tolerance_units;
    const std::int64_t tolerance = static_cast<std::int64_t>(tolerance_units);
    std::uint32_t written = 0U;
    std::uint32_t kept_lines = 0U;
    for (std::size_t line = 0U; line < lines; ++line) {
        const std::uint32_t first = input.starts[line];
        const std::uint32_t last = input.starts[line + 1U] - 1U;
        for (std::uint32_t v = first; v <= last; ++v) scratch.keep[v] = 0U;
        if (input.kind == LayerKind::rings && last > first) {
            // Split at the vertex farthest from the start, so both halves
            // have distinct ends.
            const Point start = point_at(input, first);
            std::uint32_t far = first;
            std::uint64_t far_distance = 0U;
            for (std::uint32_t v = first + 1U; v <= last; ++v) {
                const std::uint64_t distance = squared_distance(start, point_at(input, v));
                if (distance > far_distance) {
                    far_distance = distance;
                    far = v;
                }
            }
            if (far == first) far = last;
            simplify_span(input, first, far, tolerance_squared, scratch);
            simplify_span(input, far, last, tolerance_squared, scratch);
        } else {
            simplify_span(input, first, last, tolerance_squared, scratch);
        }
        std::uint32_t kept = 0U;
        std::int64_t min_x = input.x[first];
        std::int64_t max_x = min_x;
        std::int64_t min_y = input.y[first];
        std::int64_t max_y = min_y;
        for (std::uint32_t v = first; v <= last; ++v) {
            if (scratch.keep[v] == 0U) continue;
            ++kept;
            const std::int64_t x = input.x[v];
            const std::int64_t y = input.y[v];
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
        if (input.kind == LayerKind::rings) {
            const bool small = max_x - min_x < tolerance && max_y - min_y < tolerance;
            if (kept < 3U || small) continue;
        }
        output.starts[kept_lines] = written;
        for (std::uint32_t v = first; v <= last; ++v) {
            if (scratch.keep[v] == 0U) continue;
            output.x[written] = input.x[v];
            output.y[written] = input.y[v];
            ++written;
        }
        ++kept_lines;
    }
    output.starts[kept_lines] = written;
    counts.lines = kept_lines;
    counts.vertices = written;
    return Status::ok;
}

} // namespace stx_map
