#pragma once

// Copied from GUI.Forms feacf6d4 (src/core/image/png_writer.hpp) for the benchmarks in tools/codec_bench,
// which compare against historical GUI.Forms decoders written against these
// names. Benchmark-only; no stx package includes it.

// Source-compatibility seam: the PNG encoder is stx_png's
// (stx_png/encoder.hpp). GUI.Forms code keeps the gui_forms::image names.
#include "stx_png/encoder.hpp"

namespace gui_forms::image {

using stx_png::PngChannelOrder;
using stx_png::PngEncodeInput;
using stx_png::PngSourceAlpha;
using stx_png::default_png_compression_level;
using stx_png::encode_png;

} // namespace gui_forms::image
