#include "stx_rtf2md/document.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace stx_rtf2md {

bool operator==(const LinkIndex& left, const LinkIndex& right) noexcept {
    return left.value == right.value;
}

bool operator==(const Rgb& left, const Rgb& right) noexcept {
    return left.red == right.red && left.green == right.green && left.blue == right.blue;
}

bool operator==(const RunStyle& left, const RunStyle& right) noexcept {
    return left.bold == right.bold && left.italic == right.italic && left.underline == right.underline
        && left.strikethrough == right.strikethrough && left.code == right.code
        && left.superscript == right.superscript && left.subscript == right.subscript
        && left.link == right.link && left.colour == right.colour && left.background == right.background
        && left.size_step == right.size_step;
}

bool operator==(const Run& left, const Run& right) noexcept {
    return left.kind == right.kind && left.start.value == right.start.value
        && left.length.value == right.length.value && left.style == right.style;
}

bool operator==(const Block& left, const Block& right) noexcept {
    return left.kind == right.kind && left.level == right.level && left.quote_depth == right.quote_depth
        && left.marker == right.marker && left.ordinal == right.ordinal
        && left.alignment == right.alignment && left.first_run.value == right.first_run.value
        && left.run_count == right.run_count;
}

bool operator==(const Link& left, const Link& right) noexcept {
    return left.target_start.value == right.target_start.value
        && left.target_length.value == right.target_length.value;
}

bool operator==(const Document& left, const Document& right) noexcept {
    return left.text == right.text && left.blocks == right.blocks && left.runs == right.runs
        && left.links == right.links;
}

void Document::clear() noexcept {
    text.clear();
    blocks.clear();
    runs.clear();
    links.clear();
}

namespace {

constexpr std::uint64_t offset_capacity = std::numeric_limits<std::uint32_t>::max();

struct ScalarRead final {
    bool valid = false;
    std::uint32_t scalar = 0;
    std::size_t length = 0;
};

std::uint32_t byte_at(const std::string& text, std::size_t offset) noexcept {
    const unsigned char byte = static_cast<unsigned char>(text[offset]);
    return static_cast<std::uint32_t>(byte);
}

bool continuation(std::uint32_t byte) noexcept {
    return (byte & 0xC0U) == 0x80U;
}

// Decodes the scalar at offset within [offset, end); end is at most
// text.size() and greater than offset. Rejects overlong forms, surrogates,
// values above U+10FFFF and sequences cut by end.
ScalarRead read_scalar(const std::string& text, std::size_t offset, std::size_t end) noexcept {
    ScalarRead read{};
    const std::uint32_t lead = byte_at(text, offset);
    const std::size_t available = end - offset;
    if (lead < 0x80U) {
        read.valid = true;
        read.scalar = lead;
        read.length = 1;
        return read;
    }
    std::size_t length = 0;
    std::uint32_t scalar = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
        scalar = lead & 0x1FU;
        minimum = 0x80U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
        scalar = lead & 0x0FU;
        minimum = 0x800U;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
        scalar = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return read;
    }
    if (available < length) {
        return read;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const std::uint32_t byte = byte_at(text, offset + index);
        if (!continuation(byte)) {
            return read;
        }
        scalar = (scalar << 6U) | (byte & 0x3FU);
    }
    const bool surrogate = scalar >= 0xD800U && scalar <= 0xDFFFU;
    if (scalar < minimum || surrogate || scalar > 0x10FFFFU) {
        return read;
    }
    read.valid = true;
    read.scalar = scalar;
    read.length = length;
    return read;
}

// Tab is the only admitted control, and line feed inside a code block;
// line and paragraph separators would split a block, and U+FEFF is a
// byte-order mark, not text.
bool forbidden_scalar(std::uint32_t scalar, bool line_feed_admitted) noexcept {
    const bool admitted_control = scalar == 0x09U || (line_feed_admitted && scalar == 0x0AU);
    const bool control = (scalar < 0x20U && !admitted_control) || scalar == 0x7FU;
    const bool separator = scalar == 0x2028U || scalar == 0x2029U;
    return control || separator || scalar == 0xFEFFU;
}

NormalFormCheck found(NormalFormViolation violation, std::uint64_t index) noexcept {
    NormalFormCheck check{};
    check.violation = violation;
    check.index = index;
    return check;
}

NormalFormCheck check_sizes(const Document& document) noexcept {
    NormalFormCheck check{};
    const bool text_too_long = document.text.size() > offset_capacity;
    const bool too_many_runs = document.runs.size() > offset_capacity;
    const bool too_many_links = document.links.size() > offset_capacity;
    const bool too_many_blocks = document.blocks.size() > offset_capacity;
    if (text_too_long || too_many_runs || too_many_links || too_many_blocks) {
        check = found(NormalFormViolation::size_exceeds_offsets, 0);
    }
    return check;
}

NormalFormCheck check_tiling(const Document& document) noexcept {
    NormalFormCheck check{};
    std::uint64_t expected_run = 0;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const Block& block = document.blocks[index];
        if (block.run_count == 0) {
            check = found(NormalFormViolation::block_without_text_run, index);
            return check;
        }
        if (block.first_run.value != expected_run) {
            check = found(NormalFormViolation::blocks_not_contiguous, index);
            return check;
        }
        expected_run += block.run_count;
    }
    if (expected_run != document.runs.size()) {
        check = found(NormalFormViolation::blocks_not_contiguous, document.blocks.size());
    }
    return check;
}

// Run texts, then link targets, tile Document::text in order.
NormalFormCheck check_layout(const Document& document) noexcept {
    NormalFormCheck check{};
    const std::uint64_t text_size = document.text.size();
    std::uint64_t cursor = 0;
    for (std::size_t index = 0; index < document.runs.size(); ++index) {
        const Run& run = document.runs[index];
        const std::uint64_t start = run.start.value;
        const std::uint64_t end = start + run.length.value;
        if (end > text_size) {
            check = found(NormalFormViolation::run_range_outside_text, index);
            return check;
        }
        if (start != cursor) {
            check = found(NormalFormViolation::text_layout_not_canonical, cursor);
            return check;
        }
        if (run.kind == RunKind::line_break && run.length.value != 0) {
            check = found(NormalFormViolation::line_break_with_text, index);
            return check;
        }
        if (run.kind == RunKind::text && run.length.value == 0) {
            check = found(NormalFormViolation::empty_text_run, index);
            return check;
        }
        cursor = end;
    }
    for (std::size_t index = 0; index < document.links.size(); ++index) {
        const Link& link = document.links[index];
        const std::uint64_t start = link.target_start.value;
        const std::uint64_t end = start + link.target_length.value;
        if (end > text_size) {
            check = found(NormalFormViolation::link_range_outside_text, index);
            return check;
        }
        if (start != cursor) {
            check = found(NormalFormViolation::text_layout_not_canonical, cursor);
            return check;
        }
        cursor = end;
    }
    if (cursor != text_size) {
        check = found(NormalFormViolation::text_layout_not_canonical, cursor);
    }
    return check;
}

// Decodes [start, end) alone, so a scalar cut by a run or link boundary is
// invalid even when the whole text would decode.
NormalFormCheck check_range_encoding(const std::string& text, std::size_t start, std::size_t end,
                                     bool line_feed_admitted) noexcept {
    NormalFormCheck check{};
    std::size_t offset = start;
    while (offset < end) {
        const ScalarRead read = read_scalar(text, offset, end);
        if (!read.valid) {
            check = found(NormalFormViolation::invalid_utf8, offset);
            return check;
        }
        if (forbidden_scalar(read.scalar, line_feed_admitted)) {
            check = found(NormalFormViolation::forbidden_character, offset);
            return check;
        }
        offset += read.length;
    }
    return check;
}

// Requires check_tiling and check_layout to have passed: blocks tile the
// runs, and every range lies inside the text.
NormalFormCheck check_encoding(const Document& document) noexcept {
    NormalFormCheck check{};
    for (std::size_t block_index = 0; block_index < document.blocks.size(); ++block_index) {
        const Block& block = document.blocks[block_index];
        const bool line_feed_admitted = block.kind == BlockKind::code_block;
        const std::size_t first = block.first_run.value;
        const std::size_t last = first + block.run_count;
        for (std::size_t index = first; index < last; ++index) {
            const Run& run = document.runs[index];
            const std::size_t start = run.start.value;
            const std::size_t end = start + run.length.value;
            check = check_range_encoding(document.text, start, end, line_feed_admitted);
            if (check.violation != NormalFormViolation::none) {
                return check;
            }
        }
    }
    for (std::size_t index = 0; index < document.links.size(); ++index) {
        const Link& link = document.links[index];
        const std::size_t start = link.target_start.value;
        const std::size_t end = start + link.target_length.value;
        check = check_range_encoding(document.text, start, end, false);
        if (check.violation != NormalFormViolation::none) {
            return check;
        }
    }
    return check;
}

NormalFormCheck check_block_fields(const Block& block, std::size_t index) noexcept {
    NormalFormCheck check{};
    bool level_valid = block.level == 0;
    if (block.kind == BlockKind::heading) {
        level_valid = block.level >= 1 && block.level <= maximum_heading_level;
    } else if (block.kind == BlockKind::list_item) {
        level_valid = block.level <= maximum_list_level;
    }
    if (!level_valid) {
        check = found(NormalFormViolation::level_out_of_range, index);
        return check;
    }
    if (block.quote_depth > maximum_quote_depth) {
        check = found(NormalFormViolation::quote_depth_out_of_range, index);
        return check;
    }
    const bool ordered_item = block.kind == BlockKind::list_item && block.marker == ListMarker::ordered;
    if (!ordered_item && block.kind != BlockKind::list_item && block.marker != ListMarker::bullet) {
        check = found(NormalFormViolation::list_fields_on_other_kind, index);
        return check;
    }
    if (!ordered_item && block.ordinal != 0) {
        check = found(NormalFormViolation::list_fields_on_other_kind, index);
        return check;
    }
    if (ordered_item && block.ordinal > maximum_ordinal) {
        check = found(NormalFormViolation::ordinal_out_of_range, index);
        return check;
    }
    if (block.kind == BlockKind::code_block && block.alignment != Alignment::left) {
        check = found(NormalFormViolation::alignment_on_code_block, index);
    }
    return check;
}

NormalFormCheck check_run_style(const RunStyle& style, BlockKind kind, std::size_t index) noexcept {
    NormalFormCheck check{};
    const bool formatted = style.bold || style.italic || style.underline || style.strikethrough
        || style.superscript || style.subscript || style.colour.has_value()
        || style.background.has_value() || style.size_step != 0;
    if (style.code && formatted) {
        check = found(NormalFormViolation::code_with_other_attribute, index);
        return check;
    }
    if (style.superscript && style.subscript) {
        check = found(NormalFormViolation::superscript_with_subscript, index);
        return check;
    }
    if (style.size_step < minimum_size_step || style.size_step > maximum_size_step) {
        check = found(NormalFormViolation::size_step_out_of_range, index);
        return check;
    }
    if (kind == BlockKind::heading && style.size_step != 0) {
        check = found(NormalFormViolation::size_step_in_heading, index);
        return check;
    }
    const RunStyle unstyled{};
    if (kind == BlockKind::code_block && !(style == unstyled)) {
        check = found(NormalFormViolation::styled_run_in_code_block, index);
    }
    return check;
}

// Links are numbered in order of first use, and each link's text runs form
// one span inside one block; only line breaks may interrupt that span.
struct LinkTracker final {
    std::uint64_t link_count = 0;
    std::uint64_t next_link = 0;
    std::optional<std::uint32_t> open_link;
};

NormalFormCheck track_link(LinkTracker& tracker, const RunStyle& style, std::size_t index) noexcept {
    NormalFormCheck check{};
    if (!style.link.has_value()) {
        tracker.open_link.reset();
        return check;
    }
    const std::uint32_t link = (*style.link).value;
    if (link >= tracker.link_count) {
        check = found(NormalFormViolation::link_index_out_of_range, index);
        return check;
    }
    if (link == tracker.next_link) {
        tracker.open_link = link;
        tracker.next_link += 1;
        return check;
    }
    const bool continues_open = tracker.open_link.has_value() && *tracker.open_link == link;
    if (continues_open) {
        return check;
    }
    if (link < tracker.next_link) {
        check = found(NormalFormViolation::link_split, index);
        return check;
    }
    check = found(NormalFormViolation::links_not_in_first_use_order, index);
    return check;
}

NormalFormCheck check_block_runs(const Document& document, const Block& block, LinkTracker& tracker) noexcept {
    NormalFormCheck check{};
    const std::size_t first = block.first_run.value;
    const std::size_t last = first + block.run_count - 1U;
    const bool code_block = block.kind == BlockKind::code_block;
    if (code_block && (block.run_count != 1U || document.runs[first].kind != RunKind::text)) {
        check = found(NormalFormViolation::code_block_not_one_run, first);
        return check;
    }
    if (document.runs[first].kind == RunKind::line_break) {
        check = found(NormalFormViolation::line_break_at_block_edge, first);
        return check;
    }
    if (document.runs[last].kind == RunKind::line_break) {
        check = found(NormalFormViolation::line_break_at_block_edge, last);
        return check;
    }
    const RunStyle unstyled{};
    for (std::size_t index = first; index <= last; ++index) {
        const Run& run = document.runs[index];
        if (run.kind == RunKind::line_break) {
            if (!(run.style == unstyled)) {
                check = found(NormalFormViolation::line_break_styled, index);
                return check;
            }
            if (document.runs[index - 1U].kind == RunKind::line_break) {
                check = found(NormalFormViolation::adjacent_line_breaks, index);
                return check;
            }
            continue;
        }
        check = check_run_style(run.style, block.kind, index);
        if (check.violation != NormalFormViolation::none) {
            return check;
        }
        if (index > first) {
            const Run& previous = document.runs[index - 1U];
            if (previous.kind == RunKind::text && previous.style == run.style) {
                check = found(NormalFormViolation::adjacent_equal_runs, index);
                return check;
            }
        }
        check = track_link(tracker, run.style, index);
        if (check.violation != NormalFormViolation::none) {
            return check;
        }
    }
    tracker.open_link.reset();
    return check;
}

// Requires check_tiling to have passed: every block names at least one run
// and the blocks tile Document::runs.
NormalFormCheck check_blocks(const Document& document) noexcept {
    NormalFormCheck check{};
    LinkTracker tracker{};
    tracker.link_count = document.links.size();
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const Block& block = document.blocks[index];
        check = check_block_fields(block, index);
        if (check.violation != NormalFormViolation::none) {
            return check;
        }
        check = check_block_runs(document, block, tracker);
        if (check.violation != NormalFormViolation::none) {
            return check;
        }
    }
    if (tracker.next_link != tracker.link_count) {
        check = found(NormalFormViolation::link_unused, tracker.next_link);
    }
    return check;
}

}

NormalFormCheck check_normal_form(const Document& document) noexcept {
    NormalFormCheck check = check_sizes(document);
    if (check.violation != NormalFormViolation::none) {
        return check;
    }
    check = check_tiling(document);
    if (check.violation != NormalFormViolation::none) {
        return check;
    }
    check = check_layout(document);
    if (check.violation != NormalFormViolation::none) {
        return check;
    }
    check = check_encoding(document);
    if (check.violation != NormalFormViolation::none) {
        return check;
    }
    check = check_blocks(document);
    return check;
}

}
