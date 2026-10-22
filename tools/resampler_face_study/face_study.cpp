// The image-face resampler study (2026-10-22): Mitchell-Netravali
// (B = C = 1/3), as the T2 engine's material image fill implements it
// provisionally, against stx_conv's CONV* resampler, on premultiplied RGBA
// icon and face images of 16 to 256 pixels.
//
// The Mitchell-Netravali path reproduces the stretch mode of
// src/core/engine/primitives/material/material_image.cpp at PR #255 head
// 3138398b (mitchell_netravali, append_taps, plan_resample_axis for one
// segment, resample_material_image, valid_premultiplied); it is a copy for
// measurement, not a second implementation for use.
//
// Every scene is defined in the unit square and rendered at any size by
// averaging 8 x 8 subsamples per pixel, so the reference for a resample to
// D pixels is the same scene rendered directly at D pixels. Scene geometry
// uses no transcendental functions, and neither resampler does, so the same
// build gives the same bytes on every host; the digests show whether it did.
//
//   face_study <output-directory> [--timing]
//
// writes quality.tsv, digests.txt, (with --timing) timing.tsv and memory.tsv,
// and raw/ RGBA8 images for the montage script beside this file.

#include <stx_conv/conv.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {

// A premultiplied colour in [0, 1] per channel (the engine's Unit).
struct Unit final {
    double red{};
    double green{};
    double blue{};
    double alpha{};
};

// An image of premultiplied RGBA8 pixels, rows packed.
struct Image8 final {
    std::vector<std::uint8_t> bytes;
    std::size_t width{};
    std::size_t height{};
};

// A resampled image before clamping and quantization: premultiplied
// channels in [0, 1] when the filter does not overshoot. `values` holds
// width x height x 4 doubles.
struct ImageUnit final {
    std::vector<double> values;
    std::size_t width{};
    std::size_t height{};
};

// ---------------------------------------------------------------------------
// Scenes

enum class Scene : std::uint8_t { icon, edges, gradient, checker, alpha_ring };

constexpr std::array<Scene, 5> all_scenes{Scene::icon, Scene::edges, Scene::gradient, Scene::checker,
                                         Scene::alpha_ring};

const char* scene_name(Scene scene) {
    switch (scene) {
    case Scene::icon: return "icon";
    case Scene::edges: return "edges";
    case Scene::gradient: return "gradient";
    case Scene::checker: return "checker";
    case Scene::alpha_ring: return "alpha_ring";
    }
    return "unknown";
}

Unit premultiplied(double red, double green, double blue, double alpha) {
    const Unit colour{red * alpha, green * alpha, blue * alpha, alpha};
    return colour;
}

// Source-over of `top` onto `bottom`, both premultiplied.
Unit over(Unit top, Unit bottom) {
    const double keep = 1.0 - top.alpha;
    const Unit result{top.red + bottom.red * keep, top.green + bottom.green * keep, top.blue + bottom.blue * keep,
                      top.alpha + bottom.alpha * keep};
    return result;
}

double squared_distance(double x, double y, double centre_x, double centre_y) {
    const double dx = x - centre_x;
    const double dy = y - centre_y;
    const double distance = dx * dx + dy * dy;
    return distance;
}

// The scene's premultiplied colour at (x, y) in the unit square.
Unit scene_colour(Scene scene, double x, double y) {
    switch (scene) {
    case Scene::icon: {
        // A blue disc with a white diagonal bar and a translucent red
        // square, on a transparent ground: hard edges on flat colour.
        Unit colour{};
        const double r2 = squared_distance(x, y, 0.5, 0.5);
        if (r2 < 0.42 * 0.42) colour = premultiplied(0.16, 0.47, 0.86, 1.0);
        const double across = x - y;
        if (r2 < 0.30 * 0.30 && across > -0.07 && across < 0.07) colour = over(premultiplied(1.0, 1.0, 1.0, 1.0), colour);
        if (x > 0.58 && x < 0.80 && y > 0.18 && y < 0.38) colour = over(premultiplied(0.90, 0.12, 0.10, 0.8), colour);
        return colour;
    }
    case Scene::edges: {
        // Opaque white with a black half-plane (edge at x = 0.37), a thin
        // black line and a grey rectangle.
        Unit colour = premultiplied(1.0, 1.0, 1.0, 1.0);
        if (x < 0.37) colour = premultiplied(0.0, 0.0, 0.0, 1.0);
        if (y > 0.595 && y < 0.625) colour = premultiplied(0.0, 0.0, 0.0, 1.0);
        if (x > 0.55 && x < 0.80 && y > 0.12 && y < 0.40) colour = premultiplied(0.35, 0.35, 0.35, 1.0);
        return colour;
    }
    case Scene::gradient: {
        // Opaque smooth colour: red ramps across, green down, blue a
        // radial quadratic.
        const double r2 = squared_distance(x, y, 0.5, 0.5);
        const double blue = std::clamp(1.0 - 2.0 * r2, 0.0, 1.0);
        const Unit colour = premultiplied(x, y, blue, 1.0);
        return colour;
    }
    case Scene::checker: {
        // Opaque black and white squares, 12 to the unit: finer than a
        // 16-pixel image can hold, so shrinking it tests aliasing.
        const std::int64_t column = static_cast<std::int64_t>(std::floor(x * 12.0));
        const std::int64_t row = static_cast<std::int64_t>(std::floor(y * 12.0));
        const bool dark = ((column + row) % 2) == 0;
        const double level = dark ? 0.0 : 1.0;
        const Unit colour = premultiplied(level, level, level, 1.0);
        return colour;
    }
    case Scene::alpha_ring: {
        // A translucent dark ring and a half-transparent orange disc on a
        // transparent ground: premultiplied edges at partial alpha.
        Unit colour{};
        const double r2 = squared_distance(x, y, 0.5, 0.5);
        if (r2 < 0.22 * 0.22) colour = premultiplied(0.95, 0.55, 0.10, 0.5);
        if (r2 > 0.30 * 0.30 && r2 < 0.40 * 0.40) colour = premultiplied(0.10, 0.10, 0.12, 0.85);
        return colour;
    }
    }
    return Unit{};
}

std::uint8_t quantize(double value) {
    const double scaled = std::floor(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5);
    const std::uint8_t byte = static_cast<std::uint8_t>(scaled);
    return byte;
}

// The scene at size x size pixels, each the mean of 8 x 8 subsamples.
Image8 render(Scene scene, std::size_t size) {
    constexpr std::size_t subsamples = 8;
    Image8 image{};
    image.width = size;
    image.height = size;
    image.bytes.assign(size * size * 4, 0);
    const double inverse_size = 1.0 / static_cast<double>(size);
    const double inverse_count = 1.0 / static_cast<double>(subsamples * subsamples);
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t column = 0; column < size; ++column) {
            Unit sum{};
            for (std::size_t sy = 0; sy < subsamples; ++sy) {
                for (std::size_t sx = 0; sx < subsamples; ++sx) {
                    const double offset_x = (static_cast<double>(sx) + 0.5) / static_cast<double>(subsamples);
                    const double offset_y = (static_cast<double>(sy) + 0.5) / static_cast<double>(subsamples);
                    const double x = (static_cast<double>(column) + offset_x) * inverse_size;
                    const double y = (static_cast<double>(row) + offset_y) * inverse_size;
                    const Unit colour = scene_colour(scene, x, y);
                    sum.red = sum.red + colour.red;
                    sum.green = sum.green + colour.green;
                    sum.blue = sum.blue + colour.blue;
                    sum.alpha = sum.alpha + colour.alpha;
                }
            }
            const std::size_t at = (row * size + column) * 4;
            image.bytes[at] = quantize(sum.red * inverse_count);
            image.bytes[at + 1] = quantize(sum.green * inverse_count);
            image.bytes[at + 2] = quantize(sum.blue * inverse_count);
            image.bytes[at + 3] = quantize(sum.alpha * inverse_count);
        }
    }
    return image;
}

// ---------------------------------------------------------------------------
// Mitchell-Netravali, reproduced from material_image.cpp (#255, 3138398b),
// stretch mode only.

namespace mn {

struct ResampleAxis final {
    std::vector<std::uint32_t> first;
    std::vector<std::uint32_t> source;
    std::vector<double> weight;
};

double mitchell_netravali(double x) noexcept {
    const double a = std::abs(x);
    if (a < 1.0) {
        const double inner = ((7.0 * a - 12.0) * a * a + 16.0 / 3.0) / 6.0;
        return inner;
    }
    if (a < 2.0) {
        const double outer = (((-7.0 / 3.0 * a + 12.0) * a - 20.0) * a + 32.0 / 3.0) / 6.0;
        return outer;
    }
    return 0.0;
}

double shrink_factor(double source_length, double destination_length) noexcept {
    if (!(destination_length > 0.0)) return 1.0;
    const double factor = std::max(1.0, source_length / destination_length);
    return factor;
}

void append_taps(double u, double factor, std::uint32_t low, std::uint32_t high, ResampleAxis& axis) {
    const std::size_t begin = axis.weight.size();
    const double reach = 2.0 * factor;
    const std::int64_t first = static_cast<std::int64_t>(std::floor(u - reach - 0.5));
    const std::int64_t last = static_cast<std::int64_t>(std::ceil(u + reach - 0.5));
    const std::int64_t length = static_cast<std::int64_t>(high);
    double sum = 0.0;
    for (std::int64_t position = first; position <= last; ++position) {
        const double weight = mitchell_netravali((static_cast<double>(position) + 0.5 - u) / factor);
        if (weight == 0.0) continue;
        const std::int64_t index = std::clamp<std::int64_t>(position, static_cast<std::int64_t>(low), length - 1);
        axis.source.push_back(static_cast<std::uint32_t>(index));
        axis.weight.push_back(weight);
        sum = sum + weight;
    }
    if (!(sum > 0.0)) return;
    for (std::size_t tap = begin; tap < axis.weight.size(); ++tap) axis.weight[tap] = axis.weight[tap] / sum;
}

// plan_resample_axis for a stretch of `source_length` pixels over `outputs`
// device pixels starting at 0.
void plan_stretch_axis(double source_length, std::uint32_t outputs, ResampleAxis& axis) {
    axis.first.assign(static_cast<std::size_t>(outputs) + 1U, 0U);
    axis.source.clear();
    axis.weight.clear();
    const double destination_length = static_cast<double>(outputs);
    for (std::uint32_t output = 0U; output < outputs; ++output) {
        const double centre = static_cast<double>(output) + 0.5;
        const double u = centre * source_length / destination_length;
        const double factor = shrink_factor(source_length, destination_length);
        append_taps(u, factor, 0U, static_cast<std::uint32_t>(source_length), axis);
        axis.first[output + 1U] = static_cast<std::uint32_t>(axis.source.size());
    }
}

double unit_channel(std::uint8_t value) noexcept {
    const double channel = static_cast<double>(value) / 255.0;
    return channel;
}

// resample_material_image without its final valid_premultiplied, which the
// study applies separately so that overshoot can be measured first.
void resample(const Image8& pixels, const ResampleAxis& across, const ResampleAxis& down, std::vector<Unit>& scratch,
              ImageUnit& resampled) {
    const std::size_t columns = across.first.size() - 1U;
    const std::size_t rows = down.first.size() - 1U;
    const std::size_t source_rows = pixels.height;
    const std::size_t source_row_bytes = pixels.width * 4U;
    scratch.assign(source_rows * columns, Unit{});
    resampled.values.assign(rows * columns * 4U, 0.0);
    resampled.width = columns;
    resampled.height = rows;
    for (std::size_t source_row = 0U; source_row < source_rows; ++source_row) {
        const std::size_t row_start = source_row * source_row_bytes;
        for (std::size_t column = 0U; column < columns; ++column) {
            Unit sum{};
            for (std::uint32_t tap = across.first[column]; tap < across.first[column + 1U]; ++tap) {
                const std::size_t offset = row_start + static_cast<std::size_t>(across.source[tap]) * 4U;
                const double weight = across.weight[tap];
                sum.red = sum.red + unit_channel(pixels.bytes[offset]) * weight;
                sum.green = sum.green + unit_channel(pixels.bytes[offset + 1U]) * weight;
                sum.blue = sum.blue + unit_channel(pixels.bytes[offset + 2U]) * weight;
                sum.alpha = sum.alpha + unit_channel(pixels.bytes[offset + 3U]) * weight;
            }
            scratch[source_row * columns + column] = sum;
        }
    }
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = 0U; column < columns; ++column) {
            Unit sum{};
            for (std::uint32_t tap = down.first[row]; tap < down.first[row + 1U]; ++tap) {
                const Unit& part = scratch[static_cast<std::size_t>(down.source[tap]) * columns + column];
                const double weight = down.weight[tap];
                sum.red = sum.red + part.red * weight;
                sum.green = sum.green + part.green * weight;
                sum.blue = sum.blue + part.blue * weight;
                sum.alpha = sum.alpha + part.alpha * weight;
            }
            const std::size_t at = (row * columns + column) * 4U;
            resampled.values[at] = sum.red;
            resampled.values[at + 1U] = sum.green;
            resampled.values[at + 2U] = sum.blue;
            resampled.values[at + 3U] = sum.alpha;
        }
    }
}

// The storage one resample holds: both axes' plans and the scratch rows.
std::size_t storage_bytes(const ResampleAxis& across, const ResampleAxis& down, const std::vector<Unit>& scratch) {
    std::size_t bytes = 0;
    bytes += across.first.capacity() * sizeof(std::uint32_t) + across.source.capacity() * sizeof(std::uint32_t);
    bytes += across.weight.capacity() * sizeof(double);
    bytes += down.first.capacity() * sizeof(std::uint32_t) + down.source.capacity() * sizeof(std::uint32_t);
    bytes += down.weight.capacity() * sizeof(double);
    bytes += scratch.capacity() * sizeof(Unit);
    return bytes;
}

} // namespace mn

// ---------------------------------------------------------------------------
// The methods under study.

enum class Method : std::uint8_t {
    // Mitchell-Netravali as #255.
    mitchell_netravali,
    // CONV* binary64, pixel-area sampling on both axes.
    conv64_area,
    // CONV* binary64, nodal sampling (endpoint-aligned points on
    // enlargement, nodal basins on reduction).
    conv64_nodal,
    // CONV* binary64: pixel-centre points on enlargement, pixel-area
    // footprints on reduction (composed from line operations).
    conv64_centre,
    // CONV* binary32, pixel-area sampling.
    conv32_area,
};

constexpr std::array<Method, 5> all_methods{Method::mitchell_netravali, Method::conv64_area, Method::conv64_nodal,
                                           Method::conv64_centre, Method::conv32_area};

const char* method_name(Method method) {
    switch (method) {
    case Method::mitchell_netravali: return "mitchell_netravali";
    case Method::conv64_area: return "conv64_area";
    case Method::conv64_nodal: return "conv64_nodal";
    case Method::conv64_centre: return "conv64_centre";
    case Method::conv32_area: return "conv32_area";
    }
    return "unknown";
}

template<stx_conv::Precision Scalar>
void to_scalars(const Image8& image, std::vector<Scalar>& values) {
    values.resize(image.bytes.size());
    for (std::size_t index = 0; index < image.bytes.size(); ++index) {
        values[index] = static_cast<Scalar>(image.bytes[index]) / static_cast<Scalar>(255);
    }
}

void require_ok(stx_conv::Status status, const char* what) {
    if (status != stx_conv::Status::ok) {
        std::fprintf(stderr, "FAIL: %s returned status %d\n", what, static_cast<int>(status));
        std::exit(1);
    }
}

// The pixel-centre line plan: centre-aligned points when enlarging (clamped
// to the end nodes), pixel-area footprints otherwise.
stx_conv::LinePlan centre_line_plan(std::size_t source, std::size_t target, std::vector<double>& positions) {
    if (target < source) {
        const stx_conv::LinePlan area(source, target, stx_conv::Sampling::pixel_area);
        return area;
    }
    positions.resize(target);
    const double scale = static_cast<double>(source) / static_cast<double>(target);
    const double last = static_cast<double>(source - 1);
    for (std::size_t output = 0; output < target; ++output) {
        const double centre = (static_cast<double>(output) + 0.5) * scale - 0.5;
        positions[output] = std::clamp(centre, 0.0, last);
    }
    const stx_conv::LinePlan points = stx_conv::LinePlan::points(source, positions);
    return points;
}

// One axis of the centre composition: every row (horizontal) or column
// (vertical) of `source` (width x height x 4) through `plan` into `output`.
void conv_axis(const stx_conv::LinePlan& plan, const std::vector<double>& source, std::size_t width,
               std::size_t height, bool vertical, std::vector<double>& output, std::size_t output_width) {
    const std::size_t length = plan.source_length();
    stx_conv::Profile profile(length, 4);
    stx_conv::LineWorkspace work(length, 4);
    const std::size_t lines = vertical ? width : height;
    for (std::size_t line = 0; line < lines; ++line) {
        const std::size_t source_offset = vertical ? line * 4 : line * width * 4;
        const std::size_t output_offset = vertical ? line * 4 : line * output_width * 4;
        const std::size_t source_stride = vertical ? width * 4 : 4;
        const std::size_t output_stride = vertical ? output_width * 4 : 4;
        const std::span<const double> input(source.data() + source_offset, source.size() - source_offset);
        const std::span<double> destination(output.data() + output_offset, output.size() - output_offset);
        require_ok(stx_conv::compile({input, source_stride}, profile, work), "centre compile");
        require_ok(stx_conv::evaluate(profile, plan, {destination, output_stride}), "centre evaluate");
    }
}

// The library's axis order: vertical first when the height shrinks.
void conv_centre(const Image8& source, std::size_t target, ImageUnit& result) {
    std::vector<double> input;
    to_scalars<double>(source, input);
    std::vector<double> across_positions;
    std::vector<double> down_positions;
    const stx_conv::LinePlan across = centre_line_plan(source.width, target, across_positions);
    const stx_conv::LinePlan down = centre_line_plan(source.height, target, down_positions);
    result.width = target;
    result.height = target;
    result.values.assign(target * target * 4, 0.0);
    if (target < source.height) {
        std::vector<double> middle(source.width * target * 4, 0.0);
        conv_axis(down, input, source.width, source.height, true, middle, source.width);
        conv_axis(across, middle, source.width, target, false, result.values, target);
    } else {
        std::vector<double> middle(target * source.height * 4, 0.0);
        conv_axis(across, input, source.width, source.height, false, middle, target);
        conv_axis(down, middle, target, source.height, true, result.values, target);
    }
}

template<stx_conv::Precision Scalar>
void conv_image(const Image8& source, std::size_t target, stx_conv::Sampling sampling, ImageUnit& result) {
    std::vector<Scalar> input;
    to_scalars<Scalar>(source, input);
    const stx_conv::BasicImagePlan<Scalar> plan(source.width, source.height, target, target, sampling);
    stx_conv::BasicImageWorkspace<Scalar> work(plan, 4);
    std::vector<Scalar> output(target * target * 4, Scalar(0));
    require_ok(stx_conv::resize(plan, {input, source.width * 4}, {output, target * 4}, work), "image resize");
    result.width = target;
    result.height = target;
    result.values.resize(output.size());
    for (std::size_t index = 0; index < output.size(); ++index) result.values[index] = static_cast<double>(output[index]);
}

void run_method(Method method, const Image8& source, std::size_t target, ImageUnit& result) {
    switch (method) {
    case Method::mitchell_netravali: {
        mn::ResampleAxis across{};
        mn::ResampleAxis down{};
        std::vector<Unit> scratch;
        mn::plan_stretch_axis(static_cast<double>(source.width), static_cast<std::uint32_t>(target), across);
        mn::plan_stretch_axis(static_cast<double>(source.height), static_cast<std::uint32_t>(target), down);
        mn::resample(source, across, down, scratch, result);
        return;
    }
    case Method::conv64_area: conv_image<double>(source, target, stx_conv::Sampling::pixel_area, result); return;
    case Method::conv64_nodal: conv_image<double>(source, target, stx_conv::Sampling::nodal, result); return;
    case Method::conv64_centre: conv_centre(source, target, result); return;
    case Method::conv32_area: conv_image<float>(source, target, stx_conv::Sampling::pixel_area, result); return;
    }
    std::fprintf(stderr, "FAIL: unknown method\n");
    std::exit(1);
}

// valid_premultiplied then quantization, as the material does before its
// raster stores the colour.
Image8 finish(const ImageUnit& values) {
    Image8 image{};
    image.width = values.width;
    image.height = values.height;
    image.bytes.assign(values.values.size(), 0);
    for (std::size_t pixel = 0; pixel < values.width * values.height; ++pixel) {
        const std::size_t at = pixel * 4;
        const double alpha = std::clamp(values.values[at + 3], 0.0, 1.0);
        image.bytes[at] = quantize(std::clamp(values.values[at], 0.0, alpha));
        image.bytes[at + 1] = quantize(std::clamp(values.values[at + 1], 0.0, alpha));
        image.bytes[at + 2] = quantize(std::clamp(values.values[at + 2], 0.0, alpha));
        image.bytes[at + 3] = quantize(alpha);
    }
    return image;
}

// ---------------------------------------------------------------------------
// Measurements.

std::uint64_t fnv1a(const std::uint8_t* bytes, std::size_t count, std::uint64_t hash) {
    for (std::size_t index = 0; index < count; ++index) {
        hash = hash ^ static_cast<std::uint64_t>(bytes[index]);
        hash = hash * 1099511628211ULL;
    }
    return hash;
}

constexpr std::uint64_t fnv_basis = 14695981039346656037ULL;

std::uint64_t digest_values(const ImageUnit& values, std::uint64_t hash) {
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(values.values.data());
    const std::uint64_t result = fnv1a(bytes, values.values.size() * sizeof(double), hash);
    return result;
}

struct Quality final {
    // Against the scene rendered at the target size, over all channels of
    // the finished bytes.
    double rmse{};
    double psnr{};
    int max_difference{};
    // Before clamping: how far a channel leaves the range of the source
    // pixels around its footprint (ringing and overshoot), in [0, 1] units.
    double max_overshoot{};
    // The share of channel values that leave that range by more than one
    // 8-bit level.
    double visible_overshoot_share{};
    // The share of pixels that are not valid premultiplied colours before
    // clamping (a channel above alpha or below 0, alpha outside [0, 1]).
    double invalid_share{};
};

Quality measure(const Image8& source, const ImageUnit& values, const Image8& finished, const Image8& reference) {
    Quality quality{};
    double squared = 0.0;
    int worst = 0;
    for (std::size_t index = 0; index < finished.bytes.size(); ++index) {
        const int difference = std::abs(static_cast<int>(finished.bytes[index]) - static_cast<int>(reference.bytes[index]));
        worst = std::max(worst, difference);
        squared = squared + static_cast<double>(difference) * static_cast<double>(difference);
    }
    const double mean = squared / static_cast<double>(finished.bytes.size());
    quality.rmse = std::sqrt(mean);
    quality.psnr = mean > 0.0 ? 10.0 * std::log10(255.0 * 255.0 / mean) : 999.0;
    quality.max_difference = worst;
    const double ratio = static_cast<double>(source.width) / static_cast<double>(values.width);
    const double half = 0.5 * std::max(ratio, 1.0) + 1.0;
    std::size_t over_count = 0;
    std::size_t invalid_count = 0;
    double worst_over = 0.0;
    for (std::size_t row = 0; row < values.height; ++row) {
        const double v = (static_cast<double>(row) + 0.5) * ratio;
        const std::int64_t top = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(v - half)));
        const std::int64_t bottom = std::min<std::int64_t>(static_cast<std::int64_t>(source.height) - 1,
                                                           static_cast<std::int64_t>(std::floor(v + half)));
        for (std::size_t column = 0; column < values.width; ++column) {
            const double u = (static_cast<double>(column) + 0.5) * ratio;
            const std::int64_t left = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(u - half)));
            const std::int64_t right = std::min<std::int64_t>(static_cast<std::int64_t>(source.width) - 1,
                                                              static_cast<std::int64_t>(std::floor(u + half)));
            const std::size_t at = (row * values.width + column) * 4;
            const double alpha = values.values[at + 3];
            bool invalid = alpha < -1e-12 || alpha > 1.0 + 1e-12;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                double low = 1.0;
                double high = 0.0;
                for (std::int64_t y = top; y <= bottom; ++y) {
                    for (std::int64_t x = left; x <= right; ++x) {
                        const std::size_t from = (static_cast<std::size_t>(y) * source.width + static_cast<std::size_t>(x)) * 4 + channel;
                        const double level = static_cast<double>(source.bytes[from]) / 255.0;
                        low = std::min(low, level);
                        high = std::max(high, level);
                    }
                }
                const double value = values.values[at + channel];
                const double beyond = std::max(value - high, low - value);
                if (beyond > worst_over) worst_over = beyond;
                if (beyond > 1.0 / 255.0) ++over_count;
                if (channel < 3 && (value < -1e-12 || value > alpha + 1e-12)) invalid = true;
            }
            if (invalid) ++invalid_count;
        }
    }
    const double channels = static_cast<double>(values.width * values.height * 4);
    const double pixels = static_cast<double>(values.width * values.height);
    quality.max_overshoot = std::max(0.0, worst_over);
    quality.visible_overshoot_share = static_cast<double>(over_count) / channels;
    quality.invalid_share = static_cast<double>(invalid_count) / pixels;
    return quality;
}

// The 10-90% rise of the red channel across the edges scene's vertical
// edge, along the row at y = 0.9, in source pixels; and the overshoot
// above white and below black along that row.
struct EdgeProfile final {
    double rise_source_pixels{};
    double above{};
    double below{};
};

EdgeProfile edge_profile(const ImageUnit& values, std::size_t source_size) {
    EdgeProfile profile{};
    const std::size_t row = static_cast<std::size_t>(0.9 * static_cast<double>(values.height));
    double first = -1.0;
    double second = -1.0;
    double previous = values.values[(row * values.width) * 4];
    for (std::size_t column = 0; column < values.width; ++column) {
        const double value = values.values[(row * values.width + column) * 4];
        profile.above = std::max(profile.above, value - 1.0);
        profile.below = std::max(profile.below, -value);
        if (column > 0 && column < values.width * 3 / 4) {
            if (first < 0.0 && previous < 0.1 && value >= 0.1) {
                first = static_cast<double>(column - 1) + (0.1 - previous) / (value - previous);
            }
            if (second < 0.0 && previous < 0.9 && value >= 0.9) {
                second = static_cast<double>(column - 1) + (0.9 - previous) / (value - previous);
            }
        }
        previous = value;
    }
    const double scale = static_cast<double>(values.width) / static_cast<double>(source_size);
    profile.rise_source_pixels = (second - first) / scale;
    return profile;
}

// Steps against the gradient's rising red ramp along the middle row, before
// quantization: a resampler that reverses a monotone ramp bands.
std::size_t ramp_reversals(const ImageUnit& values) {
    const std::size_t row = values.height / 2;
    std::size_t reversals = 0;
    for (std::size_t column = 1; column < values.width; ++column) {
        const double here = values.values[(row * values.width + column) * 4];
        const double before = values.values[(row * values.width + column - 1) * 4];
        if (here < before - 1e-12) ++reversals;
    }
    return reversals;
}

void write_raw(const std::filesystem::path& directory, const std::string& name, const Image8& image) {
    const std::filesystem::path path = directory / (name + "_" + std::to_string(image.width) + "x" +
                                                    std::to_string(image.height) + ".rgba");
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", path.string().c_str());
        std::exit(1);
    }
    const std::size_t written = std::fwrite(image.bytes.data(), 1, image.bytes.size(), file);
    std::fclose(file);
    if (written != image.bytes.size()) {
        std::fprintf(stderr, "FAIL: short write %s\n", path.string().c_str());
        std::exit(1);
    }
}

constexpr std::array<std::size_t, 7> face_sizes{16, 24, 32, 48, 64, 128, 256};

// The cases written out for the montage: scene, source size, target size.
struct MontageCase final {
    Scene scene{};
    std::size_t source{};
    std::size_t target{};
};

constexpr std::array<MontageCase, 11> montage_cases{{
    {Scene::checker, 24, 128}, {Scene::edges, 32, 256},
    {Scene::icon, 16, 64}, {Scene::icon, 32, 48}, {Scene::icon, 256, 32}, {Scene::icon, 48, 32},
    {Scene::edges, 16, 128}, {Scene::checker, 256, 24}, {Scene::gradient, 16, 128},
    {Scene::alpha_ring, 24, 64}, {Scene::alpha_ring, 256, 24},
}};

bool is_montage_case(Scene scene, std::size_t source, std::size_t target) {
    for (const MontageCase& montage : montage_cases) {
        if (montage.scene == scene && montage.source == source && montage.target == target) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Timing: one bake's resample, from the decoded premultiplied bytes to the
// finished bytes, with every plan, workspace and buffer allocated inside the
// timed region (as a bake that keeps nothing between bakes would), and the
// same with plans and workspaces prepared beforehand.

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start).count();
    return ms;
}

double timed_cold(Method method, const Image8& source, std::size_t target) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ImageUnit values{};
    run_method(method, source, target, values);
    const Image8 finished = finish(values);
    const double ms = milliseconds_since(start);
    if (finished.bytes.empty()) std::exit(1);
    return ms;
}

// Prepared state for one method and one size pair.
struct Prepared final {
    mn::ResampleAxis across{};
    mn::ResampleAxis down{};
    std::vector<Unit> scratch;
    std::vector<double> input64;
    std::vector<float> input32;
    std::vector<double> output64;
    std::vector<float> output32;
    ImageUnit values{};
};

template<stx_conv::Precision Scalar>
double timed_conv_warm(const stx_conv::BasicImagePlan<Scalar>& plan, stx_conv::BasicImageWorkspace<Scalar>& work,
                       const Image8& source, std::vector<Scalar>& input, std::vector<Scalar>& output,
                       std::size_t target, ImageUnit& values) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < source.bytes.size(); ++index) {
        input[index] = static_cast<Scalar>(source.bytes[index]) / static_cast<Scalar>(255);
    }
    require_ok(stx_conv::resize(plan, {input, source.width * 4}, {output, target * 4}, work), "warm resize");
    for (std::size_t index = 0; index < output.size(); ++index) values.values[index] = static_cast<double>(output[index]);
    const Image8 finished = finish(values);
    const double ms = milliseconds_since(start);
    if (finished.bytes.empty()) std::exit(1);
    return ms;
}

double timed_mn_warm(Prepared& prepared, const Image8& source) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    mn::resample(source, prepared.across, prepared.down, prepared.scratch, prepared.values);
    const Image8 finished = finish(prepared.values);
    const double ms = milliseconds_since(start);
    if (finished.bytes.empty()) std::exit(1);
    return ms;
}

double median(std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    const double middle = samples[samples.size() / 2];
    return middle;
}

void timing(const std::filesystem::path& directory) {
    std::FILE* table = std::fopen((directory / "timing.tsv").string().c_str(), "wb");
    std::FILE* memory = std::fopen((directory / "memory.tsv").string().c_str(), "wb");
    if (table == nullptr || memory == nullptr) std::exit(1);
    std::fprintf(table, "method\tsource\ttarget\tcold_median_ms\tcold_max_ms\twarm_median_ms\twarm_max_ms\n");
    std::fprintf(memory, "method\tsource\ttarget\tplan_and_workspace_bytes\tconversion_buffer_bytes\n");
    constexpr std::size_t repeats = 21;
    std::vector<std::array<std::size_t, 2>> pairs;
    for (std::size_t source : face_sizes) {
        for (std::size_t target : face_sizes) {
            if (source != target) pairs.push_back({source, target});
        }
    }
    // The two image workloads of #255's measured costs.
    pairs.push_back({64, 256});
    pairs.push_back({1024, 256});
    for (const std::array<std::size_t, 2>& pair : pairs) {
        const std::size_t source_size = pair[0];
        const std::size_t target = pair[1];
        const Image8 source = render(Scene::icon, source_size);
        for (Method method : all_methods) {
            std::vector<double> cold;
            std::vector<double> warm;
            Prepared prepared{};
            prepared.values.width = target;
            prepared.values.height = target;
            prepared.values.values.assign(target * target * 4, 0.0);
            std::size_t plan_bytes = 0;
            std::size_t conversion_bytes = 0;
            if (method == Method::mitchell_netravali) {
                mn::plan_stretch_axis(static_cast<double>(source_size), static_cast<std::uint32_t>(target), prepared.across);
                mn::plan_stretch_axis(static_cast<double>(source_size), static_cast<std::uint32_t>(target), prepared.down);
                mn::resample(source, prepared.across, prepared.down, prepared.scratch, prepared.values);
                plan_bytes = mn::storage_bytes(prepared.across, prepared.down, prepared.scratch);
                for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
                    cold.push_back(timed_cold(method, source, target));
                    warm.push_back(timed_mn_warm(prepared, source));
                }
            } else if (method == Method::conv32_area) {
                const stx_conv::ImagePlan32 plan(source_size, source_size, target, target, stx_conv::Sampling::pixel_area);
                stx_conv::ImageWorkspace32 work(plan, 4);
                prepared.input32.assign(source.bytes.size(), 0.0F);
                prepared.output32.assign(target * target * 4, 0.0F);
                plan_bytes = plan.storage_bytes() + work.storage_bytes();
                conversion_bytes = (prepared.input32.capacity() + prepared.output32.capacity()) * sizeof(float);
                for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
                    cold.push_back(timed_cold(method, source, target));
                    warm.push_back(timed_conv_warm<float>(plan, work, source, prepared.input32, prepared.output32,
                                                          target, prepared.values));
                }
            } else if (method == Method::conv64_centre) {
                // Composed from line operations; only the cold path exists.
                for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
                    cold.push_back(timed_cold(method, source, target));
                }
            } else {
                const stx_conv::Sampling sampling =
                    method == Method::conv64_area ? stx_conv::Sampling::pixel_area : stx_conv::Sampling::nodal;
                const stx_conv::ImagePlan plan(source_size, source_size, target, target, sampling);
                stx_conv::ImageWorkspace work(plan, 4);
                prepared.input64.assign(source.bytes.size(), 0.0);
                prepared.output64.assign(target * target * 4, 0.0);
                plan_bytes = plan.storage_bytes() + work.storage_bytes();
                conversion_bytes = (prepared.input64.capacity() + prepared.output64.capacity()) * sizeof(double);
                for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
                    cold.push_back(timed_cold(method, source, target));
                    warm.push_back(timed_conv_warm<double>(plan, work, source, prepared.input64, prepared.output64,
                                                           target, prepared.values));
                }
            }
            const double cold_median = median(cold);
            const double cold_max = cold.back();
            double warm_median = 0.0;
            double warm_max = 0.0;
            if (!warm.empty()) {
                warm_median = median(warm);
                warm_max = warm.back();
            }
            std::fprintf(table, "%s\t%zu\t%zu\t%.4f\t%.4f\t%.4f\t%.4f\n", method_name(method), source_size, target,
                         cold_median, cold_max, warm_median, warm_max);
            if (method != Method::conv64_centre) {
                std::fprintf(memory, "%s\t%zu\t%zu\t%zu\t%zu\n", method_name(method), source_size, target, plan_bytes,
                             conversion_bytes);
            }
        }
    }
    std::fclose(table);
    std::fclose(memory);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: face_study <output-directory> [--timing]\n");
        return 2;
    }
    const std::filesystem::path directory(argv[1]);
    const bool with_timing = argc > 2 && std::string_view(argv[2]) == "--timing";
    std::filesystem::create_directories(directory / "raw");
    std::FILE* table = std::fopen((directory / "quality.tsv").string().c_str(), "wb");
    std::FILE* digests = std::fopen((directory / "digests.txt").string().c_str(), "wb");
    if (table == nullptr || digests == nullptr) return 1;
    std::fprintf(table, "scene\tsource\ttarget\tmethod\trmse\tpsnr\tmax_difference\tmax_overshoot\t"
                        "visible_overshoot_share\tinvalid_share\tedge_rise_source_px\tedge_above\tedge_below\t"
                        "ramp_reversals\n");
    std::array<std::uint64_t, all_methods.size()> byte_digests{};
    std::array<std::uint64_t, all_methods.size()> value_digests{};
    byte_digests.fill(fnv_basis);
    value_digests.fill(fnv_basis);
    for (Scene scene : all_scenes) {
        for (std::size_t source_size : face_sizes) {
            const Image8 source = render(scene, source_size);
            for (std::size_t target : face_sizes) {
                if (source_size == target) continue;
                const Image8 reference = render(scene, target);
                const bool montage = is_montage_case(scene, source_size, target);
                const std::string stem = std::string(scene_name(scene)) + "_" + std::to_string(source_size) + "_to_" +
                                         std::to_string(target);
                if (montage) {
                    write_raw(directory / "raw", stem + "_source", source);
                    write_raw(directory / "raw", stem + "_reference", reference);
                }
                for (std::size_t index = 0; index < all_methods.size(); ++index) {
                    const Method method = all_methods[index];
                    ImageUnit values{};
                    run_method(method, source, target, values);
                    const Image8 finished = finish(values);
                    const Quality quality = measure(source, values, finished, reference);
                    EdgeProfile edge{};
                    std::size_t reversals = 0;
                    if (scene == Scene::edges) edge = edge_profile(values, source_size);
                    if (scene == Scene::gradient) reversals = ramp_reversals(values);
                    std::fprintf(table, "%s\t%zu\t%zu\t%s\t%.4f\t%.3f\t%d\t%.5f\t%.5f\t%.5f\t%.4f\t%.5f\t%.5f\t%zu\n",
                                 scene_name(scene), source_size, target, method_name(method), quality.rmse,
                                 quality.psnr, quality.max_difference, quality.max_overshoot,
                                 quality.visible_overshoot_share, quality.invalid_share, edge.rise_source_pixels,
                                 edge.above, edge.below, reversals);
                    byte_digests[index] = fnv1a(finished.bytes.data(), finished.bytes.size(), byte_digests[index]);
                    value_digests[index] = digest_values(values, value_digests[index]);
                    if (montage) write_raw(directory / "raw", stem + "_" + method_name(method), finished);
                }
            }
        }
    }
    for (std::size_t index = 0; index < all_methods.size(); ++index) {
        std::fprintf(digests, "%s bytes=%016llx values=%016llx\n", method_name(all_methods[index]),
                     static_cast<unsigned long long>(byte_digests[index]),
                     static_cast<unsigned long long>(value_digests[index]));
        std::printf("%s bytes=%016llx values=%016llx\n", method_name(all_methods[index]),
                    static_cast<unsigned long long>(byte_digests[index]),
                    static_cast<unsigned long long>(value_digests[index]));
    }
    std::fclose(table);
    std::fclose(digests);
    if (with_timing) timing(directory);
    return 0;
}
