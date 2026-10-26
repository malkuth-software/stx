#include "stx_rtf2md/markdown.hpp"

#include "markdown_inline.hpp"
#include "text_scan.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stx_rtf2md {

namespace {

constexpr std::uint64_t offset_capacity = std::numeric_limits<std::uint32_t>::max();

// From clean_offset on, a clean offset c came from input offset
// input_offset + (c - clean_offset).
struct Shift final {
    std::size_t clean_offset = 0;
    std::uint64_t input_offset = 0;
};

struct Line final {
    std::size_t start = 0;
    std::size_t end = 0;
};

// A position within a line. When partial_tab is set the cursor sits on a tab
// of which some columns are already consumed; the rest count as spaces.
struct Cursor final {
    std::size_t position = 0;
    std::size_t column = 0;
    bool partial_tab = false;
};

struct IndentScan final {
    std::size_t columns = 0;
    std::size_t position = 0;
    std::size_t column = 0;
};

enum class ContainerKind : std::uint8_t { quote, list_item };

struct Container final {
    ContainerKind kind = ContainerKind::quote;
    std::uint64_t serial = 0;
    // List items: the columns a continuation line must be indented by.
    std::size_t required_indent = 0;
    ListMarker marker = ListMarker::bullet;
    std::uint32_t ordinal = 0;
    // The item has opened a leaf (its first block, emitted or flattened).
    bool has_leaf = false;
    // The item's first leaf was emitted as a list item block.
    bool emitted = false;
    // A non-blank line has been seen inside the item.
    bool content_seen = false;
    std::uint64_t source = 0;
};

enum class LeafKind : std::uint8_t { none, paragraph, fenced_code, indented_code };

struct LeafLine final {
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t prefix_spaces = 0;
};

// How a leaf maps onto the model, fixed when the leaf opens.
struct BlockPlan final {
    BlockKind kind = BlockKind::paragraph;
    std::uint8_t level = 0;
    std::uint8_t quote_depth = 0;
    ListMarker marker = ListMarker::bullet;
    std::uint32_t ordinal = 0;
    Alignment alignment = Alignment::left;
    bool flattened = false;
    // The list item container whose first block this is, as an index into
    // the container stack, or -1.
    std::ptrdiff_t item = -1;
};

struct OpenLeaf final {
    LeafKind kind = LeafKind::none;
    BlockPlan plan{};
    char fence_character = '`';
    std::size_t fence_length = 0;
    std::size_t fence_indent = 0;
    std::uint64_t source = 0;
};

struct PendingAttribute final {
    Alignment alignment = Alignment::left;
    std::size_t line_index = 0;
    std::size_t depth = 0;
    std::uint64_t parent_serial = 0;
    std::uint64_t serial_mark = 0;
    LeafLine text{};
    BlockPlan plan{};
    std::uint64_t source = 0;
};

enum class ReadState : std::uint8_t { ok, malformed, limit_exceeded, effort_exceeded };

bool is_blank(char character) noexcept {
    return character == ' ' || character == '\t';
}

bool is_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

}

struct MarkdownReader::Workspace final {
    std::string clean;
    std::vector<Shift> shifts;
    std::vector<Line> lines;
    std::vector<Container> containers;
    std::uint64_t next_serial = 1;
    OpenLeaf leaf{};
    std::vector<LeafLine> leaf_lines;
    std::optional<PendingAttribute> pending;
    std::string content;
    std::vector<detail::ContentOrigin> origins;
    detail::InlineParser inlines;
    detail::LinkTargets links;
    detail::EffortMeter effort;
};

namespace {

// One read: owns nothing, borrows the reader's workspace and the caller's
// document and loss report for the duration of MarkdownReader::read.
class BlockReader final {
public:
    BlockReader(MarkdownReader::Workspace& workspace, Document& document, LossReport& loss, const Limits& limits)
        : workspace_(workspace), document_(document), loss_(loss), limits_(limits) {}

    ReadState run(std::string_view input, SourceOffset& stop);

private:
    ReadState clean_input(std::string_view input, SourceOffset& stop);
    void split_lines();
    ReadState process_line(std::size_t line_index);
    ReadState finish();

    std::uint64_t source_of(std::size_t clean_offset) const;
    void record_loss(LossKind kind, std::size_t clean_offset);
    IndentScan peek_indent(const Cursor& cursor, std::size_t end) const;
    void advance_columns(Cursor& cursor, std::size_t columns, std::size_t end) const;
    bool rest_is_blank(const Cursor& cursor, std::size_t end) const;
    LeafLine line_from(const Cursor& cursor, std::size_t end) const;

    BlockPlan plan_for(LeafKind kind, BlockKind block_kind, bool mark);
    ReadState close_leaf();
    ReadState close_containers(std::size_t depth);
    ReadState open_leaf(LeafKind kind, BlockKind block_kind, const Cursor& cursor, std::size_t end,
                        std::size_t line_index);
    bool consume_pending(std::size_t line_index, BlockPlan& plan);
    ReadState flush_pending();
    ReadState emit_inline(BlockPlan plan, std::uint64_t source);
    ReadState emit_code(const BlockPlan& plan, bool indented, std::uint64_t source);
    ReadState begin_block(const BlockPlan& plan, std::uint64_t source, Block& block);
    void note_paragraph_constructs();

    bool parse_atx(const IndentScan& scan, std::size_t end, std::size_t& level, LeafLine& content) const;
    bool parse_fence_open(const IndentScan& scan, std::size_t end, char& character, std::size_t& length,
                          bool& has_info) const;
    bool parse_fence_close(const IndentScan& scan, std::size_t end) const;
    bool parse_setext(const IndentScan& scan, std::size_t end, std::size_t& level) const;
    bool parse_thematic(const IndentScan& scan, std::size_t end) const;
    bool parse_list_marker(const IndentScan& scan, std::size_t end, ListMarker& marker, std::uint32_t& ordinal,
                           std::size_t& marker_end) const;
    bool parse_attribute_line(const IndentScan& scan, std::size_t end, Alignment& alignment) const;

    MarkdownReader::Workspace& workspace_;
    Document& document_;
    LossReport& loss_;
    const Limits& limits_;
};

ReadState BlockReader::run(std::string_view input, SourceOffset& stop) {
    ReadState state = clean_input(input, stop);
    if (state != ReadState::ok) {
        return state;
    }
    split_lines();
    for (std::size_t index = 0; index < workspace_.lines.size(); ++index) {
        state = process_line(index);
        if (state != ReadState::ok) {
            stop = SourceOffset{source_of(workspace_.lines[index].start)};
            return state;
        }
    }
    state = finish();
    if (state != ReadState::ok) {
        stop = SourceOffset{input.size()};
        return state;
    }
    stop = SourceOffset{input.size()};
    return ReadState::ok;
}

// Validates UTF-8 and drops forbidden characters (counted), keeping line
// feeds and carriage returns as line endings. A leading U+FEFF is ignored.
ReadState BlockReader::clean_input(std::string_view input, SourceOffset& stop) {
    std::string& clean = workspace_.clean;
    clean.clear();
    clean.reserve(input.size());
    workspace_.shifts.clear();
    workspace_.shifts.push_back(Shift{});
    std::size_t offset = 0;
    while (offset < input.size()) {
        const detail::ScalarRead read = detail::read_scalar(input, offset, input.size());
        if (!read.valid) {
            stop = SourceOffset{offset};
            return ReadState::malformed;
        }
        const bool leading_mark = offset == 0 && read.scalar == 0xFEFFU;
        const bool forbidden = read.scalar != 0x0DU && detail::forbidden_scalar(read.scalar, true);
        if (leading_mark || forbidden) {
            if (forbidden) {
                loss_.record(LossKind::control_character_dropped, SourceOffset{offset});
            }
            Shift shift{};
            shift.clean_offset = clean.size();
            shift.input_offset = offset + read.length;
            workspace_.shifts.push_back(shift);
        } else {
            clean.append(input.substr(offset, read.length));
        }
        offset += read.length;
    }
    return ReadState::ok;
}

void BlockReader::split_lines() {
    workspace_.lines.clear();
    const std::string& clean = workspace_.clean;
    std::size_t start = 0;
    std::size_t index = 0;
    while (index < clean.size()) {
        const char character = clean[index];
        if (character == '\n' || character == '\r') {
            Line line{};
            line.start = start;
            line.end = index;
            workspace_.lines.push_back(line);
            const bool pair = character == '\r' && index + 1U < clean.size() && clean[index + 1U] == '\n';
            index += pair ? 2U : 1U;
            start = index;
            continue;
        }
        index += 1U;
    }
    if (start < clean.size()) {
        Line line{};
        line.start = start;
        line.end = clean.size();
        workspace_.lines.push_back(line);
    }
}

std::uint64_t BlockReader::source_of(std::size_t clean_offset) const {
    const std::vector<Shift>& shifts = workspace_.shifts;
    std::size_t low = 0;
    std::size_t high = shifts.size();
    // The last shift whose clean_offset is at most clean_offset.
    while (high - low > 1U) {
        const std::size_t middle = low + (high - low) / 2U;
        if (shifts[middle].clean_offset <= clean_offset) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const Shift& shift = shifts[low];
    const std::uint64_t source = shift.input_offset + (clean_offset - shift.clean_offset);
    return source;
}

void BlockReader::record_loss(LossKind kind, std::size_t clean_offset) {
    loss_.record(kind, SourceOffset{source_of(clean_offset)});
}

IndentScan BlockReader::peek_indent(const Cursor& cursor, std::size_t end) const {
    IndentScan scan{};
    std::size_t position = cursor.position;
    std::size_t column = cursor.column;
    const std::string& clean = workspace_.clean;
    while (position < end) {
        const char character = clean[position];
        if (character == ' ') {
            column += 1U;
        } else if (character == '\t') {
            column += 4U - column % 4U;
        } else {
            break;
        }
        position += 1U;
    }
    scan.columns = column - cursor.column;
    scan.position = position;
    scan.column = column;
    return scan;
}

void BlockReader::advance_columns(Cursor& cursor, std::size_t columns, std::size_t end) const {
    const std::string& clean = workspace_.clean;
    std::size_t remaining = columns;
    while (remaining > 0 && cursor.position < end) {
        const char character = clean[cursor.position];
        if (character == ' ') {
            cursor.position += 1U;
            cursor.column += 1U;
            cursor.partial_tab = false;
            remaining -= 1U;
        } else if (character == '\t') {
            const std::size_t width = 4U - cursor.column % 4U;
            if (width <= remaining) {
                cursor.position += 1U;
                cursor.column += width;
                cursor.partial_tab = false;
                remaining -= width;
            } else {
                cursor.column += remaining;
                cursor.partial_tab = true;
                remaining = 0;
            }
        } else {
            break;
        }
    }
}

bool BlockReader::rest_is_blank(const Cursor& cursor, std::size_t end) const {
    const IndentScan scan = peek_indent(cursor, end);
    return scan.position >= end;
}

// The rest of the line from cursor; a partly consumed tab becomes spaces.
LeafLine BlockReader::line_from(const Cursor& cursor, std::size_t end) const {
    LeafLine line{};
    line.start = cursor.position;
    line.end = end;
    if (cursor.partial_tab && cursor.position < end) {
        line.prefix_spaces = 4U - cursor.column % 4U;
        line.start = cursor.position + 1U;
    }
    return line;
}

// Fixes how a leaf maps onto the model. A paragraph that is a list item's
// first leaf is that item's block; any other leaf inside a list item, or
// inside a quote inside one, is flattened to the nearest representable
// position. When mark is set, the item records that it has a leaf.
BlockPlan BlockReader::plan_for(LeafKind kind, BlockKind block_kind, bool mark) {
    BlockPlan plan{};
    plan.kind = block_kind;
    std::vector<Container>& containers = workspace_.containers;
    std::size_t outer_quotes = 0;
    bool list_seen = false;
    bool quote_inside_list = false;
    std::ptrdiff_t innermost = -1;
    for (std::size_t index = 0; index < containers.size(); ++index) {
        if (containers[index].kind == ContainerKind::quote) {
            if (list_seen) {
                quote_inside_list = true;
            } else {
                outer_quotes += 1U;
            }
        } else {
            list_seen = true;
            innermost = static_cast<std::ptrdiff_t>(index);
        }
    }
    if (outer_quotes > maximum_quote_depth) {
        outer_quotes = maximum_quote_depth;
        plan.flattened = true;
    }
    plan.quote_depth = static_cast<std::uint8_t>(outer_quotes);
    if (innermost < 0) {
        return plan;
    }
    Container& item = containers[static_cast<std::size_t>(innermost)];
    const bool first_paragraph = kind == LeafKind::paragraph && block_kind == BlockKind::paragraph
        && !item.has_leaf && !quote_inside_list;
    if (first_paragraph) {
        std::size_t emitted_ancestors = 0;
        for (std::ptrdiff_t index = 0; index < innermost; ++index) {
            const Container& ancestor = containers[static_cast<std::size_t>(index)];
            if (ancestor.kind == ContainerKind::list_item && ancestor.emitted) {
                emitted_ancestors += 1U;
            }
        }
        plan.kind = BlockKind::list_item;
        plan.level = static_cast<std::uint8_t>(std::min<std::size_t>(emitted_ancestors, maximum_list_level));
        plan.marker = item.marker;
        plan.ordinal = item.marker == ListMarker::ordered ? item.ordinal : 0U;
        plan.item = innermost;
    } else {
        plan.flattened = true;
    }
    if (mark) {
        item.has_leaf = true;
    }
    return plan;
}

ReadState BlockReader::begin_block(const BlockPlan& plan, std::uint64_t source, Block& block) {
    if (document_.blocks.size() >= limits_.blocks) {
        return ReadState::limit_exceeded;
    }
    block.kind = plan.kind;
    block.level = plan.kind == BlockKind::heading ? plan.level : 0;
    block.quote_depth = plan.quote_depth;
    block.alignment = plan.kind == BlockKind::code_block ? Alignment::left : plan.alignment;
    if (plan.kind == BlockKind::list_item) {
        // Nesting is written under a parent, so a level can exceed the
        // previous block's list level by at most one.
        std::size_t allowed = 0;
        if (!document_.blocks.empty()) {
            const Block& previous = document_.blocks.back();
            if (previous.kind == BlockKind::list_item && previous.quote_depth == plan.quote_depth) {
                allowed = previous.level + 1U;
            }
        }
        std::size_t level = plan.level;
        if (level > allowed) {
            level = allowed;
            loss_.record(LossKind::structure_flattened, SourceOffset{source});
        }
        block.level = static_cast<std::uint8_t>(level);
        block.marker = plan.marker;
        block.ordinal = plan.ordinal;
    }
    block.first_run = RunIndex{static_cast<std::uint32_t>(document_.runs.size())};
    return ReadState::ok;
}

ReadState BlockReader::emit_inline(BlockPlan plan, std::uint64_t source) {
    Block block{};
    ReadState state = begin_block(plan, source, block);
    if (state != ReadState::ok) {
        return state;
    }
    detail::InlineRequest request{};
    request.content = workspace_.content;
    request.origins = workspace_.origins;
    request.kind = plan.kind;
    request.list_item = plan.kind == BlockKind::list_item;
    const detail::InlineStatus status =
        workspace_.inlines.parse(request, document_, workspace_.links, loss_, workspace_.effort, limits_);
    if (status == detail::InlineStatus::effort_exceeded) {
        return ReadState::effort_exceeded;
    }
    if (status == detail::InlineStatus::limit_exceeded) {
        return ReadState::limit_exceeded;
    }
    const std::size_t run_count = document_.runs.size() - block.first_run.value;
    if (run_count == 0) {
        loss_.record(LossKind::empty_block_dropped, SourceOffset{source});
        return ReadState::ok;
    }
    block.run_count = static_cast<std::uint32_t>(run_count);
    document_.blocks.push_back(block);
    if (plan.flattened) {
        loss_.record(LossKind::structure_flattened, SourceOffset{source});
    }
    if (plan.item >= 0 && static_cast<std::size_t>(plan.item) < workspace_.containers.size()) {
        workspace_.containers[static_cast<std::size_t>(plan.item)].emitted = true;
    }
    return ReadState::ok;
}

ReadState BlockReader::emit_code(const BlockPlan& plan, bool indented, std::uint64_t source) {
    std::vector<LeafLine>& lines = workspace_.leaf_lines;
    std::size_t count = lines.size();
    if (indented) {
        // Indented code drops trailing blank lines.
        while (count > 0) {
            const LeafLine& line = lines[count - 1U];
            bool blank = true;
            for (std::size_t index = line.start; index < line.end; ++index) {
                if (!is_blank(workspace_.clean[index])) {
                    blank = false;
                    break;
                }
            }
            if (!blank) {
                break;
            }
            count -= 1U;
        }
    }
    std::string& content = workspace_.content;
    content.clear();
    for (std::size_t index = 0; index < count; ++index) {
        if (index > 0) {
            content.push_back('\n');
        }
        const LeafLine& line = lines[index];
        content.append(line.prefix_spaces, ' ');
        content.append(workspace_.clean, line.start, line.end - line.start);
    }
    if (content.empty()) {
        loss_.record(LossKind::empty_block_dropped, SourceOffset{source});
        return ReadState::ok;
    }
    BlockPlan code_plan = plan;
    code_plan.kind = BlockKind::code_block;
    code_plan.alignment = Alignment::left;
    Block block{};
    const ReadState state = begin_block(code_plan, source, block);
    if (state != ReadState::ok) {
        return state;
    }
    if (document_.runs.size() >= limits_.runs || document_.text.size() + content.size() > offset_capacity) {
        return ReadState::limit_exceeded;
    }
    Run run{};
    run.kind = RunKind::text;
    run.start = TextOffset{static_cast<std::uint32_t>(document_.text.size())};
    run.length = TextLength{static_cast<std::uint32_t>(content.size())};
    document_.text.append(content);
    document_.runs.push_back(run);
    block.run_count = 1;
    document_.blocks.push_back(block);
    if (plan.flattened) {
        loss_.record(LossKind::structure_flattened, SourceOffset{source});
    }
    return ReadState::ok;
}

// Counts constructs outside the dialect that a paragraph's text keeps
// literally: a link reference definition at its start, and a table (a
// header line with '|' followed by a delimiter row).
void BlockReader::note_paragraph_constructs() {
    const std::string& content = workspace_.content;
    if (!content.empty() && content[0] == '[') {
        std::size_t index = 1;
        while (index < content.size() && index < 1000U && content[index] != ']' && content[index] != '[') {
            if (content[index] == '\\') {
                index += 1U;
            }
            index += 1U;
        }
        if (index + 1U < content.size() && content[index] == ']' && content[index + 1U] == ':' && index > 1U) {
            loss_.record(LossKind::reference_link_kept_as_text, SourceOffset{workspace_.origins.front().source_start});
        }
    }
    const std::size_t first_end = content.find('\n');
    if (first_end == std::string::npos) {
        return;
    }
    const std::string_view header(content.data(), first_end);
    std::size_t second_end = content.find('\n', first_end + 1U);
    if (second_end == std::string::npos) {
        second_end = content.size();
    }
    const std::string_view row(content.data() + first_end + 1U, second_end - first_end - 1U);
    if (header.find('|') == std::string_view::npos) {
        return;
    }
    bool dash = false;
    bool valid = !row.empty();
    for (std::size_t index = 0; index < row.size(); ++index) {
        const char character = row[index];
        if (character == '-') {
            dash = true;
        } else if (character != '|' && character != ':' && !is_blank(character)) {
            valid = false;
            break;
        }
    }
    if (valid && dash && row.find('|') != std::string_view::npos) {
        loss_.record(LossKind::table_kept_as_text, SourceOffset{workspace_.origins.front().source_start});
    }
}

ReadState BlockReader::close_leaf() {
    OpenLeaf& leaf = workspace_.leaf;
    const LeafKind kind = leaf.kind;
    leaf.kind = LeafKind::none;
    if (kind == LeafKind::none) {
        return ReadState::ok;
    }
    if (kind == LeafKind::fenced_code || kind == LeafKind::indented_code) {
        const ReadState state = emit_code(leaf.plan, kind == LeafKind::indented_code, leaf.source);
        workspace_.leaf_lines.clear();
        return state;
    }
    // A paragraph: strip each line's leading blanks and the last line's
    // trailing blanks, joining lines with line feeds.
    std::string& content = workspace_.content;
    content.clear();
    workspace_.origins.clear();
    const std::vector<LeafLine>& lines = workspace_.leaf_lines;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            content.push_back('\n');
        }
        std::size_t start = lines[index].start;
        while (start < lines[index].end && is_blank(workspace_.clean[start])) {
            start += 1U;
        }
        detail::ContentOrigin origin{};
        origin.content_start = content.size();
        origin.source_start = source_of(start);
        workspace_.origins.push_back(origin);
        content.append(workspace_.clean, start, lines[index].end - start);
    }
    while (!content.empty() && is_blank(content.back())) {
        content.pop_back();
    }
    workspace_.leaf_lines.clear();
    if (leaf.plan.kind == BlockKind::paragraph || leaf.plan.kind == BlockKind::list_item) {
        note_paragraph_constructs();
    }
    const ReadState state = emit_inline(leaf.plan, leaf.source);
    return state;
}

ReadState BlockReader::close_containers(std::size_t depth) {
    std::vector<Container>& containers = workspace_.containers;
    while (containers.size() > depth) {
        const Container& container = containers.back();
        if (container.kind == ContainerKind::list_item && !container.has_leaf && !container.content_seen) {
            loss_.record(LossKind::empty_block_dropped, SourceOffset{container.source});
        }
        containers.pop_back();
    }
    return ReadState::ok;
}

// A pending attribute line applies to a paragraph or heading opened on the
// very next line, in the attribute's container or in list items newly
// opened inside it.
bool BlockReader::consume_pending(std::size_t line_index, BlockPlan& plan) {
    if (!workspace_.pending.has_value()) {
        return false;
    }
    const PendingAttribute& pending = *workspace_.pending;
    const std::vector<Container>& containers = workspace_.containers;
    bool applies = pending.line_index + 1U == line_index && containers.size() >= pending.depth;
    if (applies && pending.depth > 0) {
        applies = containers[pending.depth - 1U].serial == pending.parent_serial;
    }
    for (std::size_t index = pending.depth; applies && index < containers.size(); ++index) {
        const bool new_item = containers[index].kind == ContainerKind::list_item
            && containers[index].serial >= pending.serial_mark;
        applies = new_item;
    }
    if (!applies) {
        return false;
    }
    plan.alignment = pending.alignment;
    workspace_.pending.reset();
    return true;
}

// An attribute line that applied to nothing is a paragraph of its own text.
ReadState BlockReader::flush_pending() {
    if (!workspace_.pending.has_value()) {
        return ReadState::ok;
    }
    const PendingAttribute pending = *workspace_.pending;
    workspace_.pending.reset();
    std::string& content = workspace_.content;
    content.assign(workspace_.clean, pending.text.start, pending.text.end - pending.text.start);
    while (!content.empty() && is_blank(content.back())) {
        content.pop_back();
    }
    workspace_.origins.clear();
    detail::ContentOrigin origin{};
    origin.source_start = pending.source;
    workspace_.origins.push_back(origin);
    const ReadState state = emit_inline(pending.plan, pending.source);
    return state;
}

ReadState BlockReader::open_leaf(LeafKind kind, BlockKind block_kind, const Cursor& cursor, std::size_t end,
                                 std::size_t line_index) {
    BlockPlan plan = plan_for(kind, block_kind, true);
    const bool consumed = kind == LeafKind::paragraph && consume_pending(line_index, plan);
    if (!consumed) {
        const ReadState state = flush_pending();
        if (state != ReadState::ok) {
            return state;
        }
    }
    OpenLeaf& leaf = workspace_.leaf;
    leaf.kind = kind;
    leaf.plan = plan;
    leaf.source = source_of(cursor.position);
    workspace_.leaf_lines.clear();
    workspace_.leaf_lines.push_back(line_from(cursor, end));
    return ReadState::ok;
}

bool BlockReader::parse_atx(const IndentScan& scan, std::size_t end, std::size_t& level, LeafLine& content) const {
    if (scan.columns > 3U) {
        return false;
    }
    const std::string& clean = workspace_.clean;
    std::size_t index = scan.position;
    while (index < end && clean[index] == '#' && index - scan.position < 7U) {
        index += 1U;
    }
    const std::size_t hashes = index - scan.position;
    if (hashes == 0 || hashes > 6U || (index < end && !is_blank(clean[index]))) {
        return false;
    }
    while (index < end && is_blank(clean[index])) {
        index += 1U;
    }
    std::size_t stop = end;
    while (stop > index && is_blank(clean[stop - 1U])) {
        stop -= 1U;
    }
    // An optional closing sequence: '#'s preceded by a blank, or the whole
    // content.
    std::size_t run = stop;
    while (run > index && clean[run - 1U] == '#') {
        run -= 1U;
    }
    if (run < stop && (run == index || is_blank(clean[run - 1U]))) {
        stop = run;
        while (stop > index && is_blank(clean[stop - 1U])) {
            stop -= 1U;
        }
    }
    level = hashes;
    content.start = index;
    content.end = stop;
    content.prefix_spaces = 0;
    return true;
}

bool BlockReader::parse_fence_open(const IndentScan& scan, std::size_t end, char& character, std::size_t& length,
                                   bool& has_info) const {
    if (scan.columns > 3U || scan.position >= end) {
        return false;
    }
    const std::string& clean = workspace_.clean;
    const char first = clean[scan.position];
    if (first != '`' && first != '~') {
        return false;
    }
    std::size_t index = scan.position;
    while (index < end && clean[index] == first) {
        index += 1U;
    }
    const std::size_t run = index - scan.position;
    if (run < 3U) {
        return false;
    }
    has_info = false;
    for (std::size_t info = index; info < end; ++info) {
        if (first == '`' && clean[info] == '`') {
            return false;
        }
        if (!is_blank(clean[info])) {
            has_info = true;
        }
    }
    character = first;
    length = run;
    return true;
}

bool BlockReader::parse_fence_close(const IndentScan& scan, std::size_t end) const {
    if (scan.columns > 3U) {
        return false;
    }
    const OpenLeaf& leaf = workspace_.leaf;
    const std::string& clean = workspace_.clean;
    std::size_t index = scan.position;
    while (index < end && clean[index] == leaf.fence_character) {
        index += 1U;
    }
    if (index - scan.position < leaf.fence_length) {
        return false;
    }
    while (index < end && is_blank(clean[index])) {
        index += 1U;
    }
    return index == end;
}

bool BlockReader::parse_setext(const IndentScan& scan, std::size_t end, std::size_t& level) const {
    if (scan.columns > 3U || scan.position >= end) {
        return false;
    }
    const std::string& clean = workspace_.clean;
    const char first = clean[scan.position];
    if (first != '=' && first != '-') {
        return false;
    }
    std::size_t index = scan.position;
    while (index < end && clean[index] == first) {
        index += 1U;
    }
    while (index < end && is_blank(clean[index])) {
        index += 1U;
    }
    if (index != end) {
        return false;
    }
    level = first == '=' ? 1U : 2U;
    return true;
}

bool BlockReader::parse_thematic(const IndentScan& scan, std::size_t end) const {
    if (scan.columns > 3U || scan.position >= end) {
        return false;
    }
    const std::string& clean = workspace_.clean;
    const char first = clean[scan.position];
    if (first != '*' && first != '-' && first != '_') {
        return false;
    }
    std::size_t count = 0;
    for (std::size_t index = scan.position; index < end; ++index) {
        if (clean[index] == first) {
            count += 1U;
        } else if (!is_blank(clean[index])) {
            return false;
        }
    }
    return count >= 3U;
}

bool BlockReader::parse_list_marker(const IndentScan& scan, std::size_t end, ListMarker& marker,
                                    std::uint32_t& ordinal, std::size_t& marker_end) const {
    if (scan.columns > 3U || scan.position >= end) {
        return false;
    }
    const std::string& clean = workspace_.clean;
    const char first = clean[scan.position];
    std::size_t index = scan.position;
    if (first == '-' || first == '+' || first == '*') {
        marker = ListMarker::bullet;
        ordinal = 0;
        index += 1U;
    } else if (is_digit(first)) {
        std::uint32_t value = 0;
        while (index < end && is_digit(clean[index]) && index - scan.position < 9U) {
            value = value * 10U + static_cast<std::uint32_t>(clean[index] - '0');
            index += 1U;
        }
        if (index >= end || (clean[index] != '.' && clean[index] != ')')) {
            return false;
        }
        marker = ListMarker::ordered;
        ordinal = value;
        index += 1U;
    } else {
        return false;
    }
    if (index < end && !is_blank(clean[index])) {
        return false;
    }
    marker_end = index;
    return true;
}

bool BlockReader::parse_attribute_line(const IndentScan& scan, std::size_t end, Alignment& alignment) const {
    if (scan.columns > 3U) {
        return false;
    }
    const std::string& clean = workspace_.clean;
    std::size_t stop = end;
    while (stop > scan.position && is_blank(clean[stop - 1U])) {
        stop -= 1U;
    }
    const std::string_view text(clean.data() + scan.position, stop - scan.position);
    if (text == "{align=left}") {
        alignment = Alignment::left;
        return true;
    }
    if (text == "{align=center}") {
        alignment = Alignment::center;
        return true;
    }
    if (text == "{align=right}") {
        alignment = Alignment::right;
        return true;
    }
    return false;
}

ReadState BlockReader::process_line(std::size_t line_index) {
    const Line line = workspace_.lines[line_index];
    const std::size_t end = line.end;
    if (!workspace_.effort.charge(line.end - line.start + 1U)) {
        return ReadState::effort_exceeded;
    }
    std::vector<Container>& containers = workspace_.containers;
    Cursor cursor{};
    cursor.position = line.start;
    // Blankness is judged after the matched containers: ">" alone is a
    // blank line inside a quote.

    // 1. Continue open containers.
    std::size_t matched = 0;
    while (matched < containers.size()) {
        Container& container = containers[matched];
        if (container.kind == ContainerKind::quote) {
            const IndentScan scan = peek_indent(cursor, end);
            if (scan.columns > 3U || scan.position >= end || workspace_.clean[scan.position] != '>') {
                break;
            }
            cursor.position = scan.position + 1U;
            cursor.column = scan.column + 1U;
            cursor.partial_tab = false;
            if (cursor.position < end && is_blank(workspace_.clean[cursor.position])) {
                advance_columns(cursor, 1, end);
            }
        } else {
            if (rest_is_blank(cursor, end)) {
                if (!container.content_seen) {
                    break;
                }
            } else {
                const IndentScan scan = peek_indent(cursor, end);
                if (scan.columns < container.required_indent) {
                    break;
                }
                advance_columns(cursor, container.required_indent, end);
            }
        }
        matched += 1U;
    }
    bool all_matched = matched == containers.size();
    if (!workspace_.effort.charge(matched + 1U)) {
        return ReadState::effort_exceeded;
    }
    // A matched non-blank line gives its list items content, so a later
    // blank line continues them.
    const bool rest_blank = rest_is_blank(cursor, end);
    if (!rest_blank) {
        for (std::size_t index = 0; index < matched; ++index) {
            if (containers[index].kind == ContainerKind::list_item) {
                containers[index].content_seen = true;
            }
        }
    }

    // 2. Continue an open code block.
    OpenLeaf& leaf = workspace_.leaf;
    ReadState state = ReadState::ok;
    if (leaf.kind == LeafKind::fenced_code) {
        if (all_matched) {
            const IndentScan scan = peek_indent(cursor, end);
            if (parse_fence_close(scan, end)) {
                return close_leaf();
            }
            const std::size_t strip = std::min(scan.columns, leaf.fence_indent);
            advance_columns(cursor, strip, end);
            workspace_.leaf_lines.push_back(line_from(cursor, end));
            return ReadState::ok;
        }
        state = close_leaf();
    } else if (leaf.kind == LeafKind::indented_code) {
        if (all_matched) {
            const IndentScan scan = peek_indent(cursor, end);
            if (rest_blank || scan.columns >= 4U) {
                advance_columns(cursor, 4, end);
                workspace_.leaf_lines.push_back(line_from(cursor, end));
                return ReadState::ok;
            }
        }
        state = close_leaf();
    }
    if (state != ReadState::ok) {
        return state;
    }

    // 3. Start new containers and leaves.
    std::size_t depth = matched;
    bool paragraph_open = leaf.kind == LeafKind::paragraph;
    while (true) {
        const IndentScan scan = peek_indent(cursor, end);
        if (scan.position >= end) {
            break;
        }
        const char first = workspace_.clean[scan.position];
        if (scan.columns >= 4U) {
            if (paragraph_open) {
                break;
            }
            state = close_containers(depth);
            if (state != ReadState::ok) {
                return state;
            }
            advance_columns(cursor, 4, end);
            state = open_leaf(LeafKind::indented_code, BlockKind::code_block, cursor, end, line_index);
            return state;
        }
        if (first == '>') {
            state = close_leaf();
            if (state == ReadState::ok) {
                state = close_containers(depth);
            }
            if (state != ReadState::ok) {
                return state;
            }
            if (containers.size() >= limits_.group_depth) {
                break;
            }
            Container quote{};
            quote.kind = ContainerKind::quote;
            quote.serial = workspace_.next_serial;
            workspace_.next_serial += 1U;
            quote.source = source_of(scan.position);
            containers.push_back(quote);
            depth = containers.size();
            cursor.position = scan.position + 1U;
            cursor.column = scan.column + 1U;
            cursor.partial_tab = false;
            if (cursor.position < end && is_blank(workspace_.clean[cursor.position])) {
                advance_columns(cursor, 1, end);
            }
            paragraph_open = false;
            all_matched = true;
            continue;
        }
        std::size_t heading_level = 0;
        LeafLine heading_text{};
        if (parse_atx(scan, end, heading_level, heading_text)) {
            state = close_leaf();
            if (state == ReadState::ok) {
                state = close_containers(depth);
            }
            if (state != ReadState::ok) {
                return state;
            }
            BlockPlan plan = plan_for(LeafKind::paragraph, BlockKind::heading, true);
            plan.level = static_cast<std::uint8_t>(heading_level);
            if (!consume_pending(line_index, plan)) {
                state = flush_pending();
                if (state != ReadState::ok) {
                    return state;
                }
            }
            workspace_.content.assign(workspace_.clean, heading_text.start, heading_text.end - heading_text.start);
            workspace_.origins.clear();
            detail::ContentOrigin origin{};
            origin.source_start = source_of(heading_text.start);
            workspace_.origins.push_back(origin);
            state = emit_inline(plan, source_of(scan.position));
            return state;
        }
        char fence_character = '`';
        std::size_t fence_length = 0;
        bool has_info = false;
        if (parse_fence_open(scan, end, fence_character, fence_length, has_info)) {
            state = close_leaf();
            if (state == ReadState::ok) {
                state = close_containers(depth);
            }
            if (state == ReadState::ok) {
                state = flush_pending();
            }
            if (state != ReadState::ok) {
                return state;
            }
            if (has_info) {
                record_loss(LossKind::code_language_dropped, scan.position);
            }
            leaf.kind = LeafKind::fenced_code;
            leaf.plan = plan_for(LeafKind::fenced_code, BlockKind::code_block, true);
            leaf.fence_character = fence_character;
            leaf.fence_length = fence_length;
            leaf.fence_indent = scan.columns;
            leaf.source = source_of(scan.position);
            workspace_.leaf_lines.clear();
            return ReadState::ok;
        }
        std::size_t setext_level = 0;
        if (paragraph_open && all_matched && parse_setext(scan, end, setext_level)) {
            BlockPlan& plan = leaf.plan;
            if (plan.kind == BlockKind::list_item) {
                if (plan.item >= 0) {
                    containers[static_cast<std::size_t>(plan.item)].emitted = false;
                }
                plan.item = -1;
                plan.flattened = true;
                plan.marker = ListMarker::bullet;
                plan.ordinal = 0;
            }
            plan.kind = BlockKind::heading;
            plan.level = static_cast<std::uint8_t>(setext_level);
            return close_leaf();
        }
        if (parse_thematic(scan, end)) {
            state = close_leaf();
            if (state == ReadState::ok) {
                state = close_containers(depth);
            }
            if (state == ReadState::ok) {
                state = flush_pending();
            }
            if (state != ReadState::ok) {
                return state;
            }
            record_loss(LossKind::thematic_break_kept_as_text, scan.position);
            Cursor text_cursor = cursor;
            text_cursor.position = scan.position;
            text_cursor.partial_tab = false;
            state = open_leaf(LeafKind::paragraph, BlockKind::paragraph, text_cursor, end, line_index);
            if (state == ReadState::ok) {
                state = close_leaf();
            }
            return state;
        }
        ListMarker marker = ListMarker::bullet;
        std::uint32_t ordinal = 0;
        std::size_t marker_end = 0;
        if (parse_list_marker(scan, end, marker, ordinal, marker_end)) {
            Cursor after_marker{};
            after_marker.position = marker_end;
            after_marker.column = scan.column + (marker_end - scan.position);
            const bool content_blank = rest_is_blank(after_marker, end);
            bool admitted = containers.size() < limits_.group_depth;
            // The interruption rule applies only to a paragraph in the
            // matched container; a lazy line may start any list item.
            if (paragraph_open && all_matched) {
                const bool starts_at_one = marker == ListMarker::bullet || ordinal == 1U;
                admitted = admitted && !content_blank && starts_at_one;
            }
            if (admitted) {
                state = close_leaf();
                if (state == ReadState::ok) {
                    state = close_containers(depth);
                }
                if (state != ReadState::ok) {
                    return state;
                }
                const IndentScan spacing = peek_indent(after_marker, end);
                std::size_t padding = spacing.columns;
                if (content_blank || padding > 4U) {
                    padding = 1U;
                }
                Container item{};
                item.kind = ContainerKind::list_item;
                item.serial = workspace_.next_serial;
                workspace_.next_serial += 1U;
                item.required_indent = scan.columns + (marker_end - scan.position) + padding;
                item.marker = marker;
                item.ordinal = ordinal;
                item.content_seen = !content_blank;
                item.source = source_of(scan.position);
                containers.push_back(item);
                depth = containers.size();
                cursor = after_marker;
                if (content_blank) {
                    return ReadState::ok;
                }
                advance_columns(cursor, padding, end);
                paragraph_open = false;
                all_matched = true;
                continue;
            }
        }
        Alignment alignment = Alignment::left;
        bool inside_empty_item = false;
        for (std::size_t index = 0; index < containers.size(); ++index) {
            if (containers[index].kind == ContainerKind::list_item) {
                inside_empty_item = !containers[index].has_leaf;
            }
        }
        if (!paragraph_open && !inside_empty_item && parse_attribute_line(scan, end, alignment)) {
            state = close_containers(depth);
            if (state == ReadState::ok) {
                state = flush_pending();
            }
            if (state != ReadState::ok) {
                return state;
            }
            PendingAttribute pending{};
            pending.alignment = alignment;
            pending.line_index = line_index;
            pending.depth = containers.size();
            pending.parent_serial = containers.empty() ? 0 : containers.back().serial;
            pending.serial_mark = workspace_.next_serial;
            pending.text.start = scan.position;
            pending.text.end = end;
            pending.plan = plan_for(LeafKind::paragraph, BlockKind::paragraph, false);
            pending.source = source_of(scan.position);
            workspace_.pending = pending;
            return ReadState::ok;
        }
        break;
    }

    // 4. Text or a blank line.
    if (rest_is_blank(cursor, end)) {
        state = close_leaf();
        if (state == ReadState::ok) {
            state = close_containers(depth);
        }
        if (state == ReadState::ok && workspace_.pending.has_value() && (*workspace_.pending).line_index < line_index) {
            state = flush_pending();
        }
        return state;
    }
    if (paragraph_open) {
        // Continuation, including a lazy one when containers did not match.
        workspace_.leaf_lines.push_back(line_from(cursor, end));
        return ReadState::ok;
    }
    state = close_leaf();
    if (state == ReadState::ok) {
        state = close_containers(depth);
    }
    if (state != ReadState::ok) {
        return state;
    }
    state = open_leaf(LeafKind::paragraph, BlockKind::paragraph, cursor, end, line_index);
    if (state == ReadState::ok && workspace_.pending.has_value() && (*workspace_.pending).line_index < line_index) {
        state = flush_pending();
    }
    return state;
}

ReadState BlockReader::finish() {
    ReadState state = close_leaf();
    if (state == ReadState::ok) {
        state = flush_pending();
    }
    if (state == ReadState::ok) {
        state = close_containers(0);
    }
    if (state != ReadState::ok) {
        return state;
    }
    if (workspace_.links.count() > limits_.links) {
        return ReadState::limit_exceeded;
    }
    if (document_.text.size() > offset_capacity) {
        return ReadState::limit_exceeded;
    }
    workspace_.links.append_to(document_);
    if (document_.text.size() > offset_capacity) {
        return ReadState::limit_exceeded;
    }
    return ReadState::ok;
}

}

MarkdownReader::MarkdownReader(const Limits& limits) : limits_(limits), workspace_(std::make_unique<Workspace>()) {}

MarkdownReader::~MarkdownReader() {}

Result MarkdownReader::read(std::span<const char> text, Document& document, LossReport& loss) {
    Result result{};
    document.clear();
    loss.clear();
    if (text.size() > limits_.input_bytes) {
        result.status = Status::limit_exceeded;
        return result;
    }
    Workspace& workspace = *workspace_;
    const std::uint64_t allowance = static_cast<std::uint64_t>(limits_.work_per_input_byte) * (text.size() + 1U);
    workspace.effort.reset(allowance);
    workspace.containers.clear();
    workspace.next_serial = 1;
    workspace.leaf = OpenLeaf{};
    workspace.leaf_lines.clear();
    workspace.pending.reset();
    workspace.links.clear();
    ReadState state = ReadState::ok;
    SourceOffset stop{};
    try {
        BlockReader reader(workspace, document, loss, limits_);
        const std::string_view input(text.data(), text.size());
        state = reader.run(input, stop);
    } catch (const std::bad_alloc&) {
        state = ReadState::limit_exceeded;
    }
    result.stop = stop;
    result.work = WorkCount{workspace.effort.used()};
    if (state == ReadState::ok) {
        const NormalFormCheck check = check_normal_form(document);
        if (check.violation != NormalFormViolation::none) {
            // A reader defect: never present a document outside normal form.
            document.clear();
            result.status = Status::invalid_document;
            return result;
        }
        result.status = Status::ok;
        return result;
    }
    document.clear();
    if (state == ReadState::malformed) {
        result.status = Status::malformed;
    } else if (state == ReadState::effort_exceeded) {
        result.status = Status::effort_exceeded;
    } else {
        result.status = Status::limit_exceeded;
    }
    return result;
}

}
