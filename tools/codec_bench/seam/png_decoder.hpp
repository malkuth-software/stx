#pragma once

// Copied from GUI.Forms feacf6d4 (src/core/image/png_decoder.hpp) for the benchmarks in tools/codec_bench,
// which compare against historical GUI.Forms decoders written against these
// names. Benchmark-only; no stx package includes it.

// Source-compatibility seam: the PNG sample decoder is stx_png's
// (stx_png/decoder.hpp). GUI.Forms code keeps the gui_forms::image names,
// and the compression seam it used comes with it as before.
#include "gui_forms/compression/inflate.hpp"
#include "stx_png/decoder.hpp"

namespace gui_forms::image {

using stx_png::PngDecodeError;
using stx_png::PngDecodeLimits;
using stx_png::PngDecodeResult;
using stx_png::PngMetadataRecord;
using stx_png::PngSampleLayout;
using stx_png::decode_png_samples;
using stx_png::decode_png_samples_discarding_metadata;
using stx_png::inspect_png_samples;
using stx_png::measure_png_samples;

} // namespace gui_forms::image
