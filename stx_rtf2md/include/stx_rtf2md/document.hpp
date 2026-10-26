#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stx_rtf2md {

// Byte offset into Document::text. Never a character or scalar position.
struct TextOffset final { std::uint32_t value = 0; };
// UTF-8 byte count within Document::text.
struct TextLength final { std::uint32_t value = 0; };
// Byte offset into a reader's input (RTF bytes or dialect text).
struct SourceOffset final { std::uint64_t value = 0; };
// Index into Document::runs.
struct RunIndex final { std::uint32_t value = 0; };
// Index into Document::links.
struct LinkIndex final { std::uint32_t value = 0; };

bool operator==(const LinkIndex& left, const LinkIndex& right) noexcept;

struct Rgb final {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

bool operator==(const Rgb& left, const Rgb& right) noexcept;

enum class BlockKind : std::uint8_t { paragraph, heading, list_item, code_block };
enum class ListMarker : std::uint8_t { bullet, ordered };
enum class Alignment : std::uint8_t { left, center, right };
enum class RunKind : std::uint8_t { text, line_break };

constexpr std::uint8_t maximum_heading_level = 6;
constexpr std::uint8_t maximum_list_level = 7;
constexpr std::uint8_t maximum_quote_depth = 7;
constexpr std::int8_t minimum_size_step = -2;
constexpr std::int8_t maximum_size_step = 4;
// CommonMark admits at most nine digits in an ordered list marker.
constexpr std::uint32_t maximum_ordinal = 999999999U;

struct RunStyle final {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    // Inline code excludes every other attribute except link.
    bool code = false;
    // Superscript and subscript exclude each other.
    bool superscript = false;
    bool subscript = false;
    std::optional<LinkIndex> link;
    std::optional<Rgb> colour;
    std::optional<Rgb> background;
    // 0 is body size; minimum_size_step to maximum_size_step otherwise.
    std::int8_t size_step = 0;
};

bool operator==(const RunStyle& left, const RunStyle& right) noexcept;

// A text run names a non-empty range of Document::text; a line break names
// an empty range at the end of the text before it and carries the default
// style. Text never contains a line feed, except in a code block, whose one
// text run holds its lines joined by line feeds.
struct Run final {
    RunKind kind = RunKind::text;
    TextOffset start{};
    TextLength length{};
    RunStyle style{};
};

bool operator==(const Run& left, const Run& right) noexcept;

struct Block final {
    BlockKind kind = BlockKind::paragraph;
    // Heading: 1 to maximum_heading_level. List item: nesting 0 to
    // maximum_list_level. Paragraph and code block: 0.
    std::uint8_t level = 0;
    // Any kind may be quoted: 0 to maximum_quote_depth.
    std::uint8_t quote_depth = 0;
    // List items only; other kinds keep the defaults. An ordered item's
    // ordinal is the number written on its marker, 0 to maximum_ordinal:
    // readers keep the number they read, so consecutive numbering is the
    // consumer's presentation choice, not part of the model. Bullet items
    // and other kinds have ordinal 0.
    ListMarker marker = ListMarker::bullet;
    std::uint32_t ordinal = 0;
    Alignment alignment = Alignment::left;
    RunIndex first_run{};
    std::uint32_t run_count = 0;
};

bool operator==(const Block& left, const Block& right) noexcept;

struct Link final {
    TextOffset target_start{};
    TextLength target_length{};
};

bool operator==(const Link& left, const Link& right) noexcept;

// A rich-text document. It owns all of its storage: run text and link
// targets live in one UTF-8 string, and runs and links name byte ranges of
// it, so building a document allocates per growth of a vector, never per
// run. Readers produce documents in normal form (check_normal_form) and
// writers refuse any other. Equal content has exactly one normal-form
// representation, so operator== compares field by field.
class Document final {
public:
    std::string text;
    std::vector<Block> blocks;
    std::vector<Run> runs;
    std::vector<Link> links;

    // Ends every element of the four collections and keeps their capacity.
    void clear() noexcept;
};

bool operator==(const Document& left, const Document& right) noexcept;

enum class NormalFormViolation : std::uint8_t {
    none,
    // Sizes and indices
    size_exceeds_offsets,          // text, run or link count beyond 32-bit offsets
    blocks_not_contiguous,         // block runs do not tile runs in order
    run_range_outside_text,
    link_index_out_of_range,
    link_range_outside_text,
    text_layout_not_canonical,     // text is not run texts then link targets, in order
    // Blocks
    block_without_text_run,
    level_out_of_range,
    quote_depth_out_of_range,
    list_fields_on_other_kind,     // marker or ordinal set on a non-list block or a bullet item
    ordinal_out_of_range,
    alignment_on_code_block,
    code_block_not_one_run,        // index: the block's first run
    list_level_without_parent,     // level L > 0 not preceded by a list item at level L - 1 or deeper
                                   // at the same quote depth
    // Runs
    empty_text_run,
    line_break_with_text,
    line_break_styled,
    line_break_at_block_end,
    line_break_in_heading,
    adjacent_equal_runs,
    code_with_other_attribute,
    superscript_with_subscript,
    size_step_out_of_range,
    size_step_in_heading,
    styled_run_in_code_block,
    // Text
    invalid_utf8,
    forbidden_character,           // C0 control other than tab (and line feed in code blocks),
                                   // DEL, U+2028, U+2029, U+FEFF
    // Links
    link_unused,
    links_not_in_first_use_order,
    link_split,                    // a link's runs are not one span within one block
};

struct NormalFormCheck final {
    NormalFormViolation violation = NormalFormViolation::none;
    // Which element the violation concerns: a block index for block and
    // tiling violations, a run index for run violations, a link index for
    // link range and link_unused violations, a byte offset into text for
    // text_layout_not_canonical, invalid_utf8 and forbidden_character, and 0
    // for size_exceeds_offsets.
    std::uint64_t index = 0;
};

// Checks every normal-form rule and reports the first violation in this
// order: sizes, block tiling, text layout with run and link ranges, text
// encoding, then each block with its runs in document order, then unused
// links. Reads only;
// work is linear in the document. A document is in normal form exactly when
// violation is none.
[[nodiscard]] NormalFormCheck check_normal_form(const Document& document) noexcept;

}
