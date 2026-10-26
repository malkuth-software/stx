#pragma once

#include "stx_rtf2md/document.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace stx_rtf2md {

// Implementation-specific effort units, not CPU cycles: one per input byte
// read, output byte written, run or block created, and emphasis delimiter
// step. Work is linear in the input, so a stated multiple of it bounds a call.
struct WorkCount final { std::uint64_t value = 0; };

// Every limit is a count and is checked before the work it bounds. The
// defaults are conservative; a caller may lower or raise them.
struct Limits final {
    std::uint64_t input_bytes = 16U * 1024U * 1024U;
    std::uint32_t group_depth = 256;
    std::uint32_t font_entries = 4096;
    std::uint32_t colour_entries = 4096;
    std::uint32_t style_entries = 4096;
    std::uint32_t list_entries = 4096;
    std::uint32_t blocks = 1048576;
    std::uint32_t runs = 4194304;
    std::uint32_t links = 65536;
    // A reader may spend work_per_input_byte units for each input byte; a
    // writer the same for each byte of Document::text, plus a fixed
    // work_per_element for each block, run and link.
    std::uint32_t work_per_input_byte = 32;
    std::uint32_t work_per_element = 64;
};

enum class Status : std::uint8_t {
    ok,
    malformed,
    limit_exceeded,
    unsupported_encoding,
    effort_exceeded,
    output_too_small,
    invalid_document,
};

// On any status other than ok a reader leaves its Document cleared and a
// writer reports zero bytes produced; the output span's contents are then
// unspecified. output_too_small reports the exact size required.
struct Result final {
    Status status = Status::invalid_document;
    SourceOffset stop{};
    std::size_t produced = 0;
    std::size_t required = 0;
    WorkCount work{};
};

enum class LossKind : std::uint8_t {
    // RTF input
    destination_skipped,
    picture_skipped,
    object_skipped,
    control_word_ignored,
    field_result_kept,
    table_flattened,
    justify_read_as_left,
    underline_style_collapsed,
    font_dropped,
    size_quantised,
    indent_dropped,
    spacing_dropped,
    control_character_dropped,
    // Markdown input: constructs outside the dialect, kept as literal text
    html_kept_as_text,
    image_kept_as_text,
    reference_link_kept_as_text,
    table_kept_as_text,
    task_marker_kept_as_text,
    footnote_kept_as_text,
    entity_kept_as_text,
};

constexpr std::size_t loss_kind_count = 20;

struct LossEntry final {
    // Saturates at the largest std::uint32_t rather than wrapping.
    std::uint32_t count = 0;
    // Where the first occurrence starts; meaningful only when count > 0.
    SourceOffset first{};
};

// Counts what a read dropped or kept as text, by kind. Fixed storage: no
// allocation, so a reader can fill it on every call.
class LossReport final {
public:
    void clear() noexcept;
    void record(LossKind kind, SourceOffset offset) noexcept;
    [[nodiscard]] LossEntry entry(LossKind kind) const noexcept;
    // True when no kind has been recorded since the last clear.
    [[nodiscard]] bool empty() const noexcept;

private:
    std::array<LossEntry, loss_kind_count> entries_{};
};

}
