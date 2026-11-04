#pragma once

// Copied from GUI.Forms feacf6d4 (include/gui_forms/compression/inflate.hpp) for the benchmarks in tools/codec_bench,
// which compare against historical GUI.Forms decoders written against these
// names. Benchmark-only; no stx package includes it.

#include "stx_zlib/inflate.hpp"

namespace gui_forms::compression {

using stx_zlib::ByteCount;
using stx_zlib::WorkCount;
using stx_zlib::InflateFormat;
using stx_zlib::InflateStatus;
using stx_zlib::InflateError;
using stx_zlib::InflateLimits;
using stx_zlib::InflateOptions;
using stx_zlib::InflateResult;
using stx_zlib::InflateStream;

} // namespace gui_forms::compression
