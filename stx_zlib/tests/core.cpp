#include "stx_zlib/inflate.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "stx_zlib: %s\n", message);
    std::abort();
}
} // namespace

int main() {
    std::unique_ptr<stx_zlib::InflateStream> stream = std::make_unique<stx_zlib::InflateStream>();
    stx_zlib::InflateOptions options{};
    options.limits.input_bytes = stx_zlib::ByteCount{64};
    options.limits.output_bytes = stx_zlib::ByteCount{64};
    options.limits.work = stx_zlib::WorkCount{10000};
    require((*stream).reset(options) == stx_zlib::InflateError::none, "zlib reset failed");
    constexpr std::array<std::byte, 9> zlib_a{
        std::byte{0x78}, std::byte{0x9c}, std::byte{0x4b}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x62}, std::byte{0x00}, std::byte{0x62}};
    std::array<std::byte, 8> output{};
    const stx_zlib::InflateResult result = (*stream).process(zlib_a, output, true);
    require(result.status == stx_zlib::InflateStatus::stream_end, "zlib stream did not finish");
    require(result.input_consumed == zlib_a.size() && result.output_produced == 1,
            "zlib progress mismatch");
    require(output[0] == std::byte{'a'}, "zlib output mismatch");
    options.format = stx_zlib::InflateFormat::raw_deflate;
    require((*stream).reset(options) == stx_zlib::InflateError::none, "raw reset failed");
    constexpr std::array<std::byte, 3> raw_a{
        std::byte{0x4b}, std::byte{0x04}, std::byte{0x00}};
    const stx_zlib::InflateResult raw_result = (*stream).process(raw_a, output, true);
    require(raw_result.status == stx_zlib::InflateStatus::stream_end && output[0] == std::byte{'a'},
            "raw stream failed");
    return 0;
}
