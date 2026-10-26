#include "markdown_inline.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stx_rtf2md::detail {

namespace {

constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();
constexpr std::uint64_t offset_capacity = std::numeric_limits<std::uint32_t>::max();
// CommonMark bounds a raw destination's parenthesis nesting; 32 is the
// reference parsers' value.
constexpr std::uint32_t maximum_destination_depth = 32;
// An attribute list longer than this is ordinary text.
constexpr std::size_t maximum_attribute_length = 128;

bool is_special(char character) noexcept {
    switch (character) {
    case '\\': case '\n': case '`': case '*': case '_': case '~':
    case '[': case '!': case ']': case '<': case '&':
        return true;
    default:
        return false;
    }
}

bool is_ascii_letter(char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

bool is_ascii_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

bool is_hex_digit(char character) noexcept {
    const bool lower = character >= 'a' && character <= 'f';
    const bool upper = character >= 'A' && character <= 'F';
    return is_ascii_digit(character) || lower || upper;
}

std::uint32_t hex_value(char character) noexcept {
    if (is_ascii_digit(character)) {
        return static_cast<std::uint32_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint32_t>(character - 'a') + 10U;
    }
    return static_cast<std::uint32_t>(character - 'A') + 10U;
}

bool is_blank(char character) noexcept {
    return character == ' ' || character == '\t';
}

std::uint32_t character_value(char character) noexcept {
    const unsigned char byte = static_cast<unsigned char>(character);
    return static_cast<std::uint32_t>(byte);
}

bool parse_rgb(std::string_view text, Rgb& colour) noexcept {
    if (text.size() != 7U || text[0] != '#') {
        return false;
    }
    for (std::size_t index = 1; index < 7U; ++index) {
        if (!is_hex_digit(text[index])) {
            return false;
        }
    }
    colour.red = static_cast<std::uint8_t>(hex_value(text[1]) * 16U + hex_value(text[2]));
    colour.green = static_cast<std::uint8_t>(hex_value(text[3]) * 16U + hex_value(text[4]));
    colour.blue = static_cast<std::uint8_t>(hex_value(text[5]) * 16U + hex_value(text[6]));
    return true;
}

}

// --- LinkTargets ---------------------------------------------------------

void LinkTargets::clear() noexcept {
    text_.clear();
    entries_.clear();
}

std::uint32_t LinkTargets::add(std::string_view target) {
    Entry entry{};
    entry.start = text_.size();
    entry.length = target.size();
    text_.append(target);
    entries_.push_back(entry);
    const std::uint32_t index = static_cast<std::uint32_t>(entries_.size() - 1U);
    return index;
}

std::size_t LinkTargets::count() const noexcept {
    return entries_.size();
}

void LinkTargets::append_to(Document& document) const {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const Entry& entry = entries_[index];
        Link link{};
        link.target_start = TextOffset{static_cast<std::uint32_t>(document.text.size())};
        link.target_length = TextLength{static_cast<std::uint32_t>(entry.length)};
        document.text.append(text_, entry.start, entry.length);
        document.links.push_back(link);
    }
}

// --- Orderings -----------------------------------------------------------

bool InlineParser::BacktickOrder::operator()(const BacktickRun& left, const BacktickRun& right) const noexcept {
    if (left.length != right.length) {
        return left.length < right.length;
    }
    return left.position < right.position;
}

// Outer spans first: by first node, then the longer range.
bool InlineParser::SpanOrder::operator()(const SpanRange& left, const SpanRange& right) const noexcept {
    if (left.first_node != right.first_node) {
        return left.first_node < right.first_node;
    }
    return left.end_node > right.end_node;
}

// --- Entry ---------------------------------------------------------------

InlineStatus InlineParser::parse(const InlineRequest& request, Document& document, LinkTargets& links,
                                 LossReport& loss, EffortMeter& effort, const Limits& limits) {
    document_ = &document;
    loss_ = &loss;
    effort_ = &effort;
    reset(request);
    if (!(*effort_).charge(content_.size() + 1U)) {
        return InlineStatus::effort_exceeded;
    }
    if (request.list_item) {
        const bool unchecked = content_.substr(0, 3) == "[ ]";
        const bool checked = content_.substr(0, 3) == "[x]" || content_.substr(0, 3) == "[X]";
        const bool separated = content_.size() == 3U || (content_.size() > 3U && is_blank(content_[3]));
        if ((unchecked || checked) && separated) {
            record_loss(LossKind::task_marker_kept_as_text, 0);
        }
    }
    collect_backtick_runs();
    if (!scan()) {
        return InlineStatus::effort_exceeded;
    }
    if (!process_emphasis(-1)) {
        return InlineStatus::effort_exceeded;
    }
    const InlineStatus status = flatten(document, links, limits);
    return status;
}

void InlineParser::reset(const InlineRequest& request) {
    request_ = request;
    content_ = request.content;
    block_first_run_ = (*document_).runs.size();
    nodes_.clear();
    node_text_.clear();
    delimiters_.clear();
    delimiter_head_ = -1;
    delimiter_top_ = -1;
    brackets_.clear();
    link_floor_ = 0;
    link_ranges_.clear();
    target_text_.clear();
    span_ranges_.clear();
    bold_delta_.clear();
    italic_delta_.clear();
    strike_delta_.clear();
    backtick_runs_.clear();
    double_quote_memo_ = ScanMemo{};
    single_quote_memo_ = ScanMemo{};
    parenthesis_memo_ = ScanMemo{};
}

void InlineParser::record_loss(LossKind kind, std::size_t content_offset) {
    std::uint64_t source = 0;
    const std::span<const ContentOrigin> origins = request_.origins;
    for (std::size_t index = 0; index < origins.size(); ++index) {
        if (origins[index].content_start > content_offset) {
            break;
        }
        source = origins[index].source_start + (content_offset - origins[index].content_start);
    }
    (*loss_).record(kind, SourceOffset{source});
}

// --- Scanning ------------------------------------------------------------

// Records every maximal backtick string, ordered by (length, position), so
// a code span's closer is one binary search away.
void InlineParser::collect_backtick_runs() {
    std::size_t position = 0;
    while (position < content_.size()) {
        if (content_[position] != '`') {
            position += 1U;
            continue;
        }
        BacktickRun run{};
        run.position = position;
        while (position < content_.size() && content_[position] == '`') {
            position += 1U;
        }
        run.length = position - run.position;
        backtick_runs_.push_back(run);
    }
    const BacktickOrder order{};
    std::sort(backtick_runs_.begin(), backtick_runs_.end(), order);
}

// The first backtick string of exactly length starting at or after after,
// or npos.
std::size_t InlineParser::find_backtick_closer(std::size_t length, std::size_t after) const {
    BacktickRun probe{};
    probe.length = length;
    probe.position = after;
    const BacktickOrder order{};
    const std::vector<BacktickRun>::const_iterator found =
        std::lower_bound(backtick_runs_.begin(), backtick_runs_.end(), probe, order);
    if (found == backtick_runs_.end() || (*found).length != length) {
        return npos;
    }
    return (*found).position;
}

void InlineParser::append_node(NodeKind kind, std::string_view text) {
    Node node{};
    node.kind = kind;
    node.text_start = node_text_.size();
    node.text_length = text.size();
    node_text_.append(text);
    nodes_.push_back(node);
}

// Literal text joins the previous text node when that node ends the store.
void InlineParser::append_text(std::string_view text) {
    if (!nodes_.empty()) {
        Node& last = nodes_.back();
        const bool contiguous = last.text_start + last.text_length == node_text_.size();
        if (last.kind == NodeKind::text && !last.sealed && contiguous) {
            node_text_.append(text);
            last.text_length += text.size();
            return;
        }
    }
    append_node(NodeKind::text, text);
}

bool InlineParser::scan() {
    std::size_t position = 0;
    while (position < content_.size()) {
        const char character = content_[position];
        std::size_t next = position + 1U;
        switch (character) {
        case '\\':
            next = scan_backslash(position);
            break;
        case '\n':
            next = scan_line_end(position);
            break;
        case '`':
            next = scan_code(position);
            break;
        case '*': case '_': case '~':
            next = scan_delimiter_run(position);
            break;
        case '[': {
            Bracket bracket{};
            bracket.node = nodes_.size();
            bracket.content_start = position + 1U;
            bracket.delimiter_bottom = delimiter_top_;
            append_node(NodeKind::bracket, "[");
            brackets_.push_back(bracket);
            next = position + 1U;
            break;
        }
        case '!':
            if (position + 1U < content_.size() && content_[position + 1U] == '[') {
                Bracket bracket{};
                bracket.node = nodes_.size();
                bracket.image = true;
                bracket.content_start = position + 2U;
                bracket.delimiter_bottom = delimiter_top_;
                append_node(NodeKind::bracket, "![");
                brackets_.push_back(bracket);
                next = position + 2U;
            } else {
                append_text("!");
            }
            break;
        case ']':
            next = close_bracket(position);
            break;
        case '<':
            next = scan_autolink_or_html(position);
            break;
        case '&':
            scratch_.clear();
            next = scan_entity(position, scratch_, true);
            append_text(scratch_);
            break;
        default:
            next = scan_plain(position);
            break;
        }
        if ((*effort_).exhausted()) {
            return false;
        }
        position = next;
    }
    return true;
}

std::size_t InlineParser::scan_plain(std::size_t position) {
    std::size_t end = position;
    while (end < content_.size() && !is_special(content_[end])) {
        end += 1U;
    }
    append_text(content_.substr(position, end - position));
    return end;
}

std::size_t InlineParser::scan_backslash(std::size_t position) {
    const std::size_t next = position + 1U;
    if (next < content_.size()) {
        const char escaped = content_[next];
        if (escaped == '\n') {
            append_node(NodeKind::line_break, std::string_view{});
            return next + 1U;
        }
        if (is_ascii_punctuation(character_value(escaped))) {
            append_text(content_.substr(next, 1));
            return next + 1U;
        }
    }
    append_text("\\");
    return next;
}

// Two or more spaces before a line end make a hard break; otherwise the
// line end is a soft break, read as one space. Spaces before it are removed
// from the preceding text either way.
std::size_t InlineParser::scan_line_end(std::size_t position) {
    std::size_t spaces = 0;
    while (spaces < position && content_[position - 1U - spaces] == ' ') {
        spaces += 1U;
    }
    if (!nodes_.empty() && spaces > 0) {
        Node& last = nodes_.back();
        const bool contiguous = last.text_start + last.text_length == node_text_.size();
        if (last.kind == NodeKind::text && !last.sealed && contiguous) {
            const std::size_t trimmed = std::min(spaces, last.text_length);
            last.text_length -= trimmed;
            node_text_.resize(node_text_.size() - trimmed);
        }
    }
    if (spaces >= 2U) {
        append_node(NodeKind::line_break, std::string_view{});
    } else {
        append_text(" ");
    }
    return position + 1U;
}

std::size_t InlineParser::scan_code(std::size_t position) {
    std::size_t end = position;
    while (end < content_.size() && content_[end] == '`') {
        end += 1U;
    }
    const std::size_t length = end - position;
    const std::size_t closer = find_backtick_closer(length, end);
    if (closer == npos) {
        append_text(content_.substr(position, length));
        return end;
    }
    scratch_.assign(content_.substr(end, closer - end));
    for (std::size_t index = 0; index < scratch_.size(); ++index) {
        if (scratch_[index] == '\n') {
            scratch_[index] = ' ';
        }
    }
    bool all_spaces = true;
    for (std::size_t index = 0; index < scratch_.size(); ++index) {
        if (scratch_[index] != ' ') {
            all_spaces = false;
            break;
        }
    }
    const bool padded = scratch_.size() >= 2U && scratch_.front() == ' ' && scratch_.back() == ' ';
    if (padded && !all_spaces) {
        scratch_ = scratch_.substr(1, scratch_.size() - 2U);
    }
    if (scratch_.empty()) {
        // An empty code span cannot be a run; keep its backticks as text.
        append_text(content_.substr(position, closer + length - position));
    } else {
        append_node(NodeKind::code, scratch_);
    }
    return closer + length;
}

std::size_t InlineParser::scan_delimiter_run(std::size_t position) {
    const char character = content_[position];
    std::size_t end = position;
    while (end < content_.size() && content_[end] == character) {
        end += 1U;
    }
    const std::size_t length = end - position;
    if (character == '~' && length != 2U) {
        append_text(content_.substr(position, length));
        return end;
    }
    const std::uint32_t before = scalar_before(content_, position);
    const std::uint32_t after = scalar_at(content_, end);
    const bool before_space = is_unicode_whitespace(before);
    const bool after_space = is_unicode_whitespace(after);
    const bool before_punctuation = is_unicode_punctuation(before);
    const bool after_punctuation = is_unicode_punctuation(after);
    const bool left_flanking = !after_space && (!after_punctuation || before_space || before_punctuation);
    const bool right_flanking = !before_space && (!before_punctuation || after_space || after_punctuation);
    Delimiter delimiter{};
    delimiter.character = character;
    delimiter.original = static_cast<std::uint32_t>(length);
    if (character == '_') {
        delimiter.can_open = left_flanking && (!right_flanking || before_punctuation);
        delimiter.can_close = right_flanking && (!left_flanking || after_punctuation);
    } else {
        delimiter.can_open = left_flanking;
        delimiter.can_close = right_flanking;
    }
    delimiter.node = nodes_.size();
    append_node(NodeKind::delimiter, content_.substr(position, length));
    nodes_.back().remaining = static_cast<std::uint32_t>(length);
    if (delimiter.can_open || delimiter.can_close) {
        push_delimiter(delimiter);
    }
    return end;
}

// <scheme:...> and <user@host> autolinks; otherwise '<' is text, counted as
// raw HTML when it opens a tag, comment, declaration or instruction.
std::size_t InlineParser::scan_autolink_or_html(std::size_t position) {
    const std::size_t start = position + 1U;
    std::size_t index = start;
    // Scheme: a letter, then 1 to 31 letters, digits, '+', '.' or '-'.
    if (index < content_.size() && is_ascii_letter(content_[index])) {
        index += 1U;
        while (index < content_.size() && index - start < 32U) {
            const char character = content_[index];
            const bool scheme_character = is_ascii_letter(character) || is_ascii_digit(character)
                || character == '+' || character == '.' || character == '-';
            if (!scheme_character) {
                break;
            }
            index += 1U;
        }
        const std::size_t scheme_length = index - start;
        if (scheme_length >= 2U && index < content_.size() && content_[index] == ':') {
            std::size_t end = index + 1U;
            while (end < content_.size()) {
                const char character = content_[end];
                const std::uint32_t value = character_value(character);
                if (character == '>' || character == '<' || character == ' ' || value < 0x20U || value == 0x7FU) {
                    break;
                }
                end += 1U;
            }
            if (end < content_.size() && content_[end] == '>') {
                const std::string_view uri = content_.substr(start, end - start);
                LinkRange range{};
                range.first_node = nodes_.size();
                append_node(NodeKind::text, uri);
                nodes_.back().sealed = true;
                range.end_node = nodes_.size();
                range.target_start = target_text_.size();
                range.target_length = uri.size();
                target_text_.append(uri);
                link_ranges_.push_back(range);
                return end + 1U;
            }
        }
    }
    // Email: local part, '@', then labels of letters, digits and '-'.
    std::size_t end = start;
    while (end < content_.size()) {
        const char character = content_[end];
        const bool local = is_ascii_letter(character) || is_ascii_digit(character)
            || std::string_view(".!#$%&'*+/=?^_`{|}~-").find(character) != std::string_view::npos;
        if (!local) {
            break;
        }
        end += 1U;
    }
    if (end > start && end < content_.size() && content_[end] == '@') {
        std::size_t host = end + 1U;
        std::size_t label_length = 0;
        bool valid = true;
        while (host < content_.size() && content_[host] != '>') {
            const char character = content_[host];
            if (is_ascii_letter(character) || is_ascii_digit(character) || character == '-') {
                label_length += 1U;
            } else if (character == '.' && label_length > 0) {
                label_length = 0;
            } else {
                valid = false;
                break;
            }
            if (label_length > 63U) {
                valid = false;
                break;
            }
            host += 1U;
        }
        if (valid && label_length > 0 && host < content_.size() && content_[host] == '>') {
            const std::string_view address = content_.substr(start, host - start);
            LinkRange range{};
            range.first_node = nodes_.size();
            append_node(NodeKind::text, address);
            nodes_.back().sealed = true;
            range.end_node = nodes_.size();
            range.target_start = target_text_.size();
            target_text_.append("mailto:");
            target_text_.append(address);
            range.target_length = target_text_.size() - range.target_start;
            link_ranges_.push_back(range);
            return host + 1U;
        }
    }
    if (start < content_.size()) {
        const char next = content_[start];
        const bool closing_tag = next == '/' && start + 1U < content_.size() && is_ascii_letter(content_[start + 1U]);
        if (is_ascii_letter(next) || closing_tag || next == '!' || next == '?') {
            record_loss(LossKind::html_kept_as_text, position);
        }
    }
    append_text("<");
    return start;
}

// Decodes a numeric character reference into target, or copies the source
// text when it is not one the model admits. Named references are kept as
// text. Losses are recorded only when count_loss is set.
std::size_t InlineParser::scan_entity(std::size_t position, std::string& target, bool count_loss) {
    std::size_t index = position + 1U;
    if (index < content_.size() && content_[index] == '#') {
        index += 1U;
        const bool hexadecimal = index < content_.size() && (content_[index] == 'x' || content_[index] == 'X');
        if (hexadecimal) {
            index += 1U;
        }
        const std::size_t digits_start = index;
        const std::size_t maximum_digits = hexadecimal ? 6U : 7U;
        std::uint32_t value = 0;
        while (index < content_.size() && index - digits_start < maximum_digits) {
            const char character = content_[index];
            const bool digit = hexadecimal ? is_hex_digit(character) : is_ascii_digit(character);
            if (!digit) {
                break;
            }
            value = value * (hexadecimal ? 16U : 10U) + hex_value(character);
            index += 1U;
        }
        const bool has_digits = index > digits_start;
        if (has_digits && index < content_.size() && content_[index] == ';') {
            const bool scalar = value <= 0x10FFFFU && !(value >= 0xD800U && value <= 0xDFFFU);
            if (scalar && !forbidden_scalar(value, false)) {
                append_scalar(target, value);
                return index + 1U;
            }
            if (count_loss) {
                record_loss(LossKind::entity_kept_as_text, position);
            }
            target.append(content_.substr(position, index + 1U - position));
            return index + 1U;
        }
        target.push_back('&');
        return position + 1U;
    }
    // A named reference: '&', a letter, 1 to 31 letters or digits, ';'.
    if (index < content_.size() && is_ascii_letter(content_[index])) {
        std::size_t end = index + 1U;
        while (end < content_.size() && end - index < 32U
               && (is_ascii_letter(content_[end]) || is_ascii_digit(content_[end]))) {
            end += 1U;
        }
        if (end - index >= 2U && end < content_.size() && content_[end] == ';') {
            if (count_loss) {
                record_loss(LossKind::entity_kept_as_text, position);
            }
            target.append(content_.substr(position, end + 1U - position));
            return end + 1U;
        }
    }
    target.push_back('&');
    return position + 1U;
}

std::size_t InlineParser::close_bracket(std::size_t position) {
    if (brackets_.empty()) {
        append_text("]");
        return position + 1U;
    }
    const Bracket bracket = brackets_.back();
    const std::size_t after = position + 1U;
    if (after < content_.size() && content_[after] == '(') {
        const bool active = bracket.image || bracket.node >= link_floor_;
        if (active) {
            const std::size_t target_start = target_text_.size();
            const LinkParse parse = parse_inline_link(after + 1U);
            if (parse.ok) {
                brackets_.pop_back();
                const std::size_t source_start = bracket.image ? bracket.content_start - 2U : bracket.content_start - 1U;
                if (bracket.image) {
                    target_text_.resize(target_start);
                    record_loss(LossKind::image_kept_as_text, source_start);
                    make_literal(bracket, source_start, parse.end);
                    return parse.end;
                }
                if (!process_emphasis(bracket.delimiter_bottom)) {
                    return parse.end;
                }
                const std::size_t first = bracket.node + 1U;
                if (first == nodes_.size()) {
                    target_text_.resize(target_start);
                    record_loss(LossKind::structure_flattened, source_start);
                    make_literal(bracket, source_start, parse.end);
                    return parse.end;
                }
                LinkRange range{};
                range.first_node = first;
                range.end_node = nodes_.size();
                range.target_start = target_start;
                range.target_length = target_text_.size() - target_start;
                link_ranges_.push_back(range);
                nodes_[bracket.node].kind = NodeKind::dead;
                nodes_.back().sealed = true;
                link_floor_ = bracket.node;
                if (parse.has_title) {
                    record_loss(LossKind::link_title_dropped, source_start);
                }
                return parse.end;
            }
            target_text_.resize(target_start);
        }
    } else if (after < content_.size() && content_[after] == '{' && !bracket.image) {
        const AttributeParse parse = parse_attributes(after + 1U);
        if (parse.ok) {
            brackets_.pop_back();
            if (!process_emphasis(bracket.delimiter_bottom)) {
                return parse.end;
            }
            const std::size_t first = bracket.node + 1U;
            if (first == nodes_.size()) {
                make_literal(bracket, bracket.content_start - 1U, parse.end);
                return parse.end;
            }
            SpanRange range{};
            range.first_node = first;
            range.end_node = nodes_.size();
            range.attributes = parse.attributes;
            span_ranges_.push_back(range);
            nodes_[bracket.node].kind = NodeKind::dead;
            nodes_.back().sealed = true;
            return parse.end;
        }
    }
    if (!bracket.image && bracket.content_start < content_.size() && content_[bracket.content_start] == '^') {
        record_loss(LossKind::footnote_kept_as_text, bracket.content_start - 1U);
    }
    brackets_.pop_back();
    append_text("]");
    return after;
}

// Turns a bracket's whole construct into literal source text: the bracket
// node holds the text, every later node is dead, and delimiters inside are
// dropped so they cannot pair with delimiters outside.
void InlineParser::make_literal(const Bracket& bracket, std::size_t source_start, std::size_t source_end) {
    remove_delimiters_above(bracket.delimiter_bottom);
    Node& node = nodes_[bracket.node];
    node.kind = NodeKind::text;
    node.text_start = node_text_.size();
    node.text_length = source_end - source_start;
    node_text_.append(content_.substr(source_start, source_end - source_start));
    std::size_t index = bracket.node + 1U;
    while (index < nodes_.size()) {
        if (nodes_[index].skip_to != 0) {
            const std::size_t skip = nodes_[index].skip_to;
            nodes_[index].kind = NodeKind::dead;
            index = skip;
            continue;
        }
        nodes_[index].kind = NodeKind::dead;
        index += 1U;
    }
    node.skip_to = nodes_.size();
    node.sealed = true;
}

InlineParser::LinkParse InlineParser::parse_inline_link(std::size_t position) {
    LinkParse parse{};
    std::size_t index = position;
    bool newline_seen = false;
    while (index < content_.size() && (is_blank(content_[index]) || (content_[index] == '\n' && !newline_seen))) {
        if (content_[index] == '\n') {
            newline_seen = true;
        }
        index += 1U;
    }
    if (index < content_.size() && content_[index] == '<') {
        index += 1U;
        while (index < content_.size()) {
            const char character = content_[index];
            if (character == '\n' || character == '<') {
                return parse;
            }
            if (character == '>') {
                break;
            }
            if (character == '\\' && index + 1U < content_.size()
                && is_ascii_punctuation(character_value(content_[index + 1U]))) {
                target_text_.push_back(content_[index + 1U]);
                index += 2U;
                continue;
            }
            if (character == '&') {
                index = scan_entity(index, target_text_, true);
                continue;
            }
            target_text_.push_back(character);
            index += 1U;
        }
        if (index >= content_.size()) {
            return parse;
        }
        index += 1U;
    } else {
        std::uint32_t depth = 0;
        while (index < content_.size()) {
            const char character = content_[index];
            const std::uint32_t value = character_value(character);
            if (character == ' ' || character == '\t' || character == '\n' || value < 0x20U || value == 0x7FU) {
                break;
            }
            if (character == '\\' && index + 1U < content_.size()
                && is_ascii_punctuation(character_value(content_[index + 1U]))) {
                target_text_.push_back(content_[index + 1U]);
                index += 2U;
                continue;
            }
            if (character == '(') {
                depth += 1U;
                if (depth > maximum_destination_depth) {
                    return parse;
                }
            } else if (character == ')') {
                if (depth == 0) {
                    break;
                }
                depth -= 1U;
            }
            if (character == '&') {
                index = scan_entity(index, target_text_, true);
                continue;
            }
            target_text_.push_back(character);
            index += 1U;
        }
        if (depth != 0) {
            return parse;
        }
    }
    const std::size_t destination_end = index;
    newline_seen = false;
    while (index < content_.size() && (is_blank(content_[index]) || (content_[index] == '\n' && !newline_seen))) {
        if (content_[index] == '\n') {
            newline_seen = true;
        }
        index += 1U;
    }
    const bool separated = index > destination_end;
    if (separated && index < content_.size()) {
        const char opener = content_[index];
        if (opener == '"' || opener == '\'' || opener == '(') {
            const char closer = opener == '(' ? ')' : opener;
            ScanMemo& memo = opener == '"' ? double_quote_memo_ : (opener == '\'' ? single_quote_memo_ : parenthesis_memo_);
            const std::size_t close = next_unescaped(closer, index + 1U, memo);
            if (close == npos) {
                return parse;
            }
            parse.has_title = true;
            index = close + 1U;
            newline_seen = false;
            while (index < content_.size()
                   && (is_blank(content_[index]) || (content_[index] == '\n' && !newline_seen))) {
                if (content_[index] == '\n') {
                    newline_seen = true;
                }
                index += 1U;
            }
        }
    }
    if (index < content_.size() && content_[index] == ')') {
        parse.ok = true;
        parse.end = index + 1U;
    }
    return parse;
}

// The first unescaped character at or after from, or npos. Answers for
// increasing positions reuse the last scan, so repeated title searches stay
// linear in the content.
std::size_t InlineParser::next_unescaped(char character, std::size_t from, ScanMemo& memo) {
    if (memo.valid && from >= memo.from && (memo.result == npos || from <= memo.result)) {
        return memo.result;
    }
    std::size_t backslashes = 0;
    std::size_t back = from;
    while (back > 0 && content_[back - 1U] == '\\') {
        backslashes += 1U;
        back -= 1U;
    }
    std::size_t result = npos;
    std::size_t index = from;
    while (index < content_.size()) {
        const char current = content_[index];
        if (current == character && backslashes % 2U == 0) {
            result = index;
            break;
        }
        backslashes = current == '\\' ? backslashes + 1U : 0;
        index += 1U;
    }
    const bool charged = (*effort_).charge(index - from + 1U);
    static_cast<void>(charged);
    memo.valid = true;
    memo.from = from;
    memo.result = result;
    return result;
}

InlineParser::AttributeParse InlineParser::parse_attributes(std::size_t position) const {
    AttributeParse parse{};
    std::size_t close = position;
    while (close < content_.size() && content_[close] != '}' && content_[close] != '\n'
           && close - position <= maximum_attribute_length) {
        close += 1U;
    }
    if (close >= content_.size() || content_[close] != '}' || close - position > maximum_attribute_length) {
        return parse;
    }
    SpanAttributes attributes{};
    bool underline_seen = false;
    bool position_seen = false;
    bool colour_seen = false;
    bool background_seen = false;
    bool size_seen = false;
    bool bold_seen = false;
    bool italic_seen = false;
    bool strike_seen = false;
    bool any = false;
    std::size_t index = position;
    while (index < close) {
        if (content_[index] == ' ') {
            index += 1U;
            continue;
        }
        std::size_t end = index;
        while (end < close && content_[end] != ' ') {
            end += 1U;
        }
        const std::string_view item = content_.substr(index, end - index);
        index = end;
        any = true;
        if (item == "u" && !underline_seen) {
            underline_seen = true;
            attributes.underline = true;
        } else if (item == "sup" && !position_seen) {
            position_seen = true;
            attributes.position = Position::superscript;
        } else if (item == "sub" && !position_seen) {
            position_seen = true;
            attributes.position = Position::subscript;
        } else if (item == "b" && !bold_seen) {
            bold_seen = true;
            attributes.bold = true;
        } else if (item == "i" && !italic_seen) {
            italic_seen = true;
            attributes.italic = true;
        } else if (item == "s" && !strike_seen) {
            strike_seen = true;
            attributes.strikethrough = true;
        } else if (item.substr(0, 6) == "color=" && !colour_seen) {
            Rgb colour{};
            if (!parse_rgb(item.substr(6), colour)) {
                return parse;
            }
            colour_seen = true;
            attributes.colour = colour;
        } else if (item.substr(0, 3) == "bg=" && !background_seen) {
            Rgb colour{};
            if (!parse_rgb(item.substr(3), colour)) {
                return parse;
            }
            background_seen = true;
            attributes.background = colour;
        } else if (item.size() == 7U && item.substr(0, 5) == "size=" && !size_seen) {
            const char sign = item[5];
            const char digit = item[6];
            const bool raise = sign == '+' && digit >= '1' && digit <= '4';
            const bool lower = sign == '-' && digit >= '1' && digit <= '2';
            if (!raise && !lower) {
                return parse;
            }
            size_seen = true;
            const std::int8_t magnitude = static_cast<std::int8_t>(digit - '0');
            attributes.size_step = raise ? magnitude : static_cast<std::int8_t>(-magnitude);
        } else {
            return parse;
        }
    }
    if (!any) {
        return parse;
    }
    parse.ok = true;
    parse.end = close + 1U;
    parse.attributes = attributes;
    return parse;
}

// --- Delimiters ----------------------------------------------------------

void InlineParser::push_delimiter(const Delimiter& delimiter) {
    const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(delimiters_.size());
    delimiters_.push_back(delimiter);
    Delimiter& pushed = delimiters_.back();
    pushed.previous = delimiter_top_;
    pushed.next = -1;
    if (delimiter_top_ >= 0) {
        delimiters_[static_cast<std::size_t>(delimiter_top_)].next = index;
    } else {
        delimiter_head_ = index;
    }
    delimiter_top_ = index;
}

void InlineParser::remove_delimiter(std::ptrdiff_t index) {
    Delimiter& delimiter = delimiters_[static_cast<std::size_t>(index)];
    if (delimiter.previous >= 0) {
        delimiters_[static_cast<std::size_t>(delimiter.previous)].next = delimiter.next;
    } else {
        delimiter_head_ = delimiter.next;
    }
    if (delimiter.next >= 0) {
        delimiters_[static_cast<std::size_t>(delimiter.next)].previous = delimiter.previous;
    } else {
        delimiter_top_ = delimiter.previous;
    }
    delimiter.previous = -1;
    delimiter.next = -1;
}

// A bracket's bottom delimiter stays on the stack while the bracket is open
// (removals happen only above some open bracket's bottom), so its next link
// is current.
std::ptrdiff_t InlineParser::first_above(std::ptrdiff_t bottom) const {
    if (bottom < 0) {
        return delimiter_head_;
    }
    return delimiters_[static_cast<std::size_t>(bottom)].next;
}

void InlineParser::remove_delimiters_above(std::ptrdiff_t bottom) {
    std::ptrdiff_t current = first_above(bottom);
    while (current >= 0) {
        const std::ptrdiff_t next = delimiters_[static_cast<std::size_t>(current)].next;
        remove_delimiter(current);
        current = next;
    }
}

// CommonMark's process-emphasis procedure over the delimiters above bottom,
// with the openers-bottom table indexed by character, whether the closer
// can also open, and the closer's original length modulo 3.
bool InlineParser::process_emphasis(std::ptrdiff_t bottom) {
    std::ptrdiff_t openers_bottom[3][2][3];
    for (std::size_t character = 0; character < 3U; ++character) {
        for (std::size_t opens = 0; opens < 2U; ++opens) {
            for (std::size_t modulo = 0; modulo < 3U; ++modulo) {
                openers_bottom[character][opens][modulo] = bottom;
            }
        }
    }
    std::ptrdiff_t current = first_above(bottom);
    while (current >= 0) {
        if (!(*effort_).charge(1)) {
            return false;
        }
        const Delimiter closer = delimiters_[static_cast<std::size_t>(current)];
        if (!closer.can_close) {
            current = closer.next;
            continue;
        }
        const std::size_t character_index = closer.character == '*' ? 0U : (closer.character == '_' ? 1U : 2U);
        const std::size_t opens_index = closer.can_open ? 1U : 0U;
        const std::size_t modulo_index = closer.original % 3U;
        const std::ptrdiff_t limit = openers_bottom[character_index][opens_index][modulo_index];
        std::ptrdiff_t opener = closer.previous;
        bool found = false;
        while (opener >= 0 && opener != bottom && opener != limit) {
            if (!(*effort_).charge(1)) {
                return false;
            }
            const Delimiter& candidate = delimiters_[static_cast<std::size_t>(opener)];
            if (candidate.character == closer.character && candidate.can_open) {
                bool odd_match = false;
                if (closer.character != '~' && (candidate.can_close || closer.can_open)) {
                    const bool sum_multiple = (candidate.original + closer.original) % 3U == 0;
                    const bool both_multiple = candidate.original % 3U == 0 && closer.original % 3U == 0;
                    odd_match = sum_multiple && !both_multiple;
                }
                if (!odd_match) {
                    found = true;
                    break;
                }
            }
            opener = candidate.previous;
        }
        if (!found) {
            openers_bottom[character_index][opens_index][modulo_index] = closer.previous;
            const std::ptrdiff_t next = closer.next;
            if (!closer.can_open) {
                remove_delimiter(current);
            }
            current = next;
            continue;
        }
        const std::size_t opener_node = delimiters_[static_cast<std::size_t>(opener)].node;
        const std::size_t closer_node = closer.node;
        std::uint32_t used = 1;
        if (closer.character == '~') {
            used = 2;
        } else if (nodes_[opener_node].remaining >= 2U && nodes_[closer_node].remaining >= 2U) {
            used = 2;
        }
        std::vector<std::int32_t>* delta = &italic_delta_;
        if (closer.character == '~') {
            delta = &strike_delta_;
        } else if (used == 2U) {
            delta = &bold_delta_;
        }
        if ((*delta).size() < nodes_.size() + 1U) {
            (*delta).resize(nodes_.size() + 1U, 0);
        }
        (*delta)[opener_node + 1U] += 1;
        (*delta)[closer_node] -= 1;
        nodes_[opener_node].remaining -= used;
        nodes_[closer_node].remaining -= used;
        std::ptrdiff_t between = delimiters_[static_cast<std::size_t>(opener)].next;
        while (between >= 0 && between != current) {
            const std::ptrdiff_t next = delimiters_[static_cast<std::size_t>(between)].next;
            remove_delimiter(between);
            between = next;
        }
        if (nodes_[opener_node].remaining == 0) {
            remove_delimiter(opener);
        }
        if (nodes_[closer_node].remaining == 0) {
            const std::ptrdiff_t next = delimiters_[static_cast<std::size_t>(current)].next;
            remove_delimiter(current);
            current = next;
        }
    }
    remove_delimiters_above(bottom);
    return true;
}

// --- Output --------------------------------------------------------------

InlineStatus InlineParser::append_run(Document& document, RunKind kind, std::string_view text, const RunStyle& style,
                                      const Limits& limits) {
    if (document.text.size() + text.size() > offset_capacity) {
        return InlineStatus::limit_exceeded;
    }
    if (kind == RunKind::text && document.runs.size() > block_first_run_) {
        Run& last = document.runs.back();
        if (last.kind == RunKind::text && last.style == style) {
            document.text.append(text);
            last.length.value += static_cast<std::uint32_t>(text.size());
            return InlineStatus::ok;
        }
    }
    if (document.runs.size() >= limits.runs) {
        return InlineStatus::limit_exceeded;
    }
    Run run{};
    run.kind = kind;
    run.start = TextOffset{static_cast<std::uint32_t>(document.text.size())};
    run.length = TextLength{static_cast<std::uint32_t>(text.size())};
    run.style = style;
    document.text.append(text);
    document.runs.push_back(run);
    return InlineStatus::ok;
}

InlineStatus InlineParser::flatten(Document& document, LinkTargets& links, const Limits& limits) {
    if (!(*effort_).charge(nodes_.size() + 1U)) {
        return InlineStatus::effort_exceeded;
    }
    bold_delta_.resize(nodes_.size() + 1U, 0);
    italic_delta_.resize(nodes_.size() + 1U, 0);
    strike_delta_.resize(nodes_.size() + 1U, 0);
    const SpanOrder order{};
    std::sort(span_ranges_.begin(), span_ranges_.end(), order);
    link_document_index_.assign(link_ranges_.size(), -1);
    span_stack_.clear();
    span_end_stack_.clear();
    std::int32_t bold = 0;
    std::int32_t italic = 0;
    std::int32_t strike = 0;
    std::size_t next_span = 0;
    std::size_t next_link = 0;
    bool heading_size_dropped = false;
    InlineStatus status = InlineStatus::ok;
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        bold += bold_delta_[index];
        italic += italic_delta_[index];
        strike += strike_delta_[index];
        while (!span_end_stack_.empty() && span_end_stack_.back() <= index) {
            span_end_stack_.pop_back();
            span_stack_.pop_back();
        }
        while (next_span < span_ranges_.size() && span_ranges_[next_span].first_node <= index) {
            const SpanRange& range = span_ranges_[next_span];
            SpanAttributes effective = span_stack_.empty() ? SpanAttributes{} : span_stack_.back();
            const SpanAttributes& own = range.attributes;
            effective.underline = effective.underline || own.underline;
            effective.bold = effective.bold || own.bold;
            effective.italic = effective.italic || own.italic;
            effective.strikethrough = effective.strikethrough || own.strikethrough;
            if (own.position != Position::none) {
                effective.position = own.position;
            }
            if (own.colour.has_value()) {
                effective.colour = own.colour;
            }
            if (own.background.has_value()) {
                effective.background = own.background;
            }
            if (own.size_step != 0) {
                effective.size_step = own.size_step;
            }
            span_stack_.push_back(effective);
            span_end_stack_.push_back(range.end_node);
            next_span += 1U;
        }
        while (next_link < link_ranges_.size() && link_ranges_[next_link].end_node <= index) {
            next_link += 1U;
        }
        const Node& node = nodes_[index];
        if (node.kind == NodeKind::dead) {
            continue;
        }
        RunStyle style{};
        const SpanAttributes span = span_stack_.empty() ? SpanAttributes{} : span_stack_.back();
        style.bold = bold > 0 || span.bold;
        style.italic = italic > 0 || span.italic;
        style.strikethrough = strike > 0 || span.strikethrough;
        style.underline = span.underline;
        style.superscript = span.position == Position::superscript;
        style.subscript = span.position == Position::subscript;
        style.colour = span.colour;
        style.background = span.background;
        style.size_step = span.size_step;
        if (request_.kind == BlockKind::heading && style.size_step != 0) {
            style.size_step = 0;
            heading_size_dropped = true;
        }
        const bool linked = next_link < link_ranges_.size() && link_ranges_[next_link].first_node <= index;
        if (linked && node.kind != NodeKind::line_break) {
            if (link_document_index_[next_link] < 0) {
                const LinkRange& range = link_ranges_[next_link];
                const std::string_view target(target_text_.data() + range.target_start, range.target_length);
                if (links.count() >= limits.links) {
                    return InlineStatus::limit_exceeded;
                }
                link_document_index_[next_link] = links.add(target);
            }
            style.link = LinkIndex{static_cast<std::uint32_t>(link_document_index_[next_link])};
        }
        const std::string_view text(node_text_.data() + node.text_start, node.text_length);
        if (node.kind == NodeKind::line_break) {
            status = append_run(document, RunKind::line_break, std::string_view{}, RunStyle{}, limits);
        } else if (node.kind == NodeKind::delimiter) {
            const std::string_view remaining = text.substr(0, node.remaining);
            if (!remaining.empty()) {
                status = append_run(document, RunKind::text, remaining, style, limits);
            }
        } else {
            style.code = node.kind == NodeKind::code;
            if (!text.empty()) {
                status = append_run(document, RunKind::text, text, style, limits);
            }
        }
        if (status != InlineStatus::ok) {
            return status;
        }
    }
    if (heading_size_dropped) {
        record_loss(LossKind::structure_flattened, 0);
    }
    return status;
}

}
