#include "stx_rtf2md/markdown.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stx_rtf2md {

namespace {

// The span attributes a run carries; in the emphasis fallback bold, italic
// and strikethrough join them.
struct SpanKey final {
    bool underline = false;
    bool superscript = false;
    bool subscript = false;
    std::optional<Rgb> colour;
    std::optional<Rgb> background;
    std::int8_t size_step = 0;
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
};

bool operator==(const SpanKey& left, const SpanKey& right) noexcept {
    return left.underline == right.underline && left.superscript == right.superscript
        && left.subscript == right.subscript && left.colour == right.colour && left.background == right.background
        && left.size_step == right.size_step && left.bold == right.bold && left.italic == right.italic
        && left.strikethrough == right.strikethrough;
}

bool is_default(const SpanKey& key) noexcept {
    const SpanKey unstyled{};
    return key == unstyled;
}

SpanKey span_key(const RunStyle& style, bool fallback) {
    SpanKey key{};
    key.underline = style.underline;
    key.superscript = style.superscript;
    key.subscript = style.subscript;
    key.colour = style.colour;
    key.background = style.background;
    key.size_step = style.size_step;
    if (fallback) {
        key.bold = style.bold;
        key.italic = style.italic;
        key.strikethrough = style.strikethrough;
    }
    return key;
}

void append_hex(std::string& out, std::uint8_t value) {
    const char* const digits = "0123456789abcdef";
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 0x0FU]);
}

void append_rgb(std::string& out, const Rgb& colour) {
    out.push_back('#');
    append_hex(out, colour.red);
    append_hex(out, colour.green);
    append_hex(out, colour.blue);
}

void append_items(std::string& out, const SpanKey& key) {
    bool first = true;
    if (key.underline) {
        out.append("u");
        first = false;
    }
    if (key.superscript || key.subscript) {
        if (!first) {
            out.push_back(' ');
        }
        out.append(key.superscript ? "sup" : "sub");
        first = false;
    }
    if (key.colour.has_value()) {
        if (!first) {
            out.push_back(' ');
        }
        out.append("color=");
        append_rgb(out, *key.colour);
        first = false;
    }
    if (key.background.has_value()) {
        if (!first) {
            out.push_back(' ');
        }
        out.append("bg=");
        append_rgb(out, *key.background);
        first = false;
    }
    if (key.size_step != 0) {
        if (!first) {
            out.push_back(' ');
        }
        out.append("size=");
        out.push_back(key.size_step > 0 ? '+' : '-');
        const int magnitude = key.size_step > 0 ? key.size_step : -key.size_step;
        out.push_back(static_cast<char>('0' + magnitude));
        first = false;
    }
    if (key.bold) {
        if (!first) {
            out.push_back(' ');
        }
        out.append("b");
        first = false;
    }
    if (key.italic) {
        if (!first) {
            out.push_back(' ');
        }
        out.append("i");
        first = false;
    }
    if (key.strikethrough) {
        if (!first) {
            out.push_back(' ');
        }
        out.append("s");
    }
}

bool is_blank(char character) noexcept {
    return character == ' ' || character == '\t';
}

bool is_digit(char character) noexcept {
    return character >= '0' && character <= '9';
}

std::string_view run_text(const Document& document, const Run& run) {
    const std::string_view text(document.text.data() + run.start.value, run.length.value);
    return text;
}

std::string_view link_target(const Document& document, LinkIndex index) {
    const Link& link = document.links[index.value];
    const std::string_view text(document.text.data() + link.target_start.value, link.target_length.value);
    return text;
}

// Writes one block's inline content. Line breaks become a backslash and a
// line feed; the caller adds the block's prefixes to each line.
class InlineWriter final {
public:
    // next_text is reusable storage for each run's nearest following text
    // run; the writer fills it.
    InlineWriter(const Document& document, const Block& block, bool fallback, std::string& out,
                 std::vector<std::size_t>& next_text)
        : document_(document), block_(block), fallback_(fallback), out_(out), next_text_(next_text) {}

    void write();

private:
    std::optional<std::uint32_t> link_context(std::size_t index) const;
    SpanKey span_context(std::size_t index) const;
    void close_emphasis();
    void transition_emphasis(const RunStyle& style);
    void close_span();
    void close_link();
    void write_text(std::string_view text, bool followed_by_text);
    void write_code(std::string_view text);
    void write_destination(std::string_view target);

    const Document& document_;
    const Block& block_;
    bool fallback_ = false;
    std::string& out_;
    bool at_line_start_ = true;
    std::optional<std::uint32_t> current_link_;
    bool span_open_ = false;
    SpanKey current_key_{};
    std::vector<char> emphasis_;
    std::vector<std::size_t>& next_text_;
    // The nearest text run before the current one, or npos.
    std::size_t previous_text_ = static_cast<std::size_t>(-1);
};

std::optional<std::uint32_t> InlineWriter::link_context(std::size_t index) const {
    const Run& run = document_.runs[index];
    std::optional<std::uint32_t> context;
    if (run.kind == RunKind::text) {
        if (run.style.link.has_value()) {
            context = (*run.style.link).value;
        }
        return context;
    }
    // A line break stays inside a link whose text runs surround it, however
    // many line breaks lie between them.
    const std::size_t after_index = next_text_[index - block_.first_run.value];
    if (previous_text_ == static_cast<std::size_t>(-1) || after_index == static_cast<std::size_t>(-1)) {
        return context;
    }
    const std::optional<LinkIndex> before = document_.runs[previous_text_].style.link;
    const std::optional<LinkIndex> after = document_.runs[after_index].style.link;
    if (before.has_value() && after.has_value() && (*before).value == (*after).value) {
        context = (*before).value;
    }
    return context;
}

SpanKey InlineWriter::span_context(std::size_t index) const {
    const Run& run = document_.runs[index];
    if (run.kind == RunKind::text) {
        const SpanKey key = span_key(run.style, fallback_);
        return key;
    }
    SpanKey key{};
    const std::size_t after_index = next_text_[index - block_.first_run.value];
    if (previous_text_ == static_cast<std::size_t>(-1) || after_index == static_cast<std::size_t>(-1)) {
        return key;
    }
    const SpanKey before_key = span_key(document_.runs[previous_text_].style, fallback_);
    const SpanKey after_key = span_key(document_.runs[after_index].style, fallback_);
    if (before_key == after_key) {
        key = before_key;
    }
    return key;
}

void InlineWriter::close_emphasis() {
    while (!emphasis_.empty()) {
        const char marker = emphasis_.back();
        emphasis_.pop_back();
        out_.append(marker == 'b' ? "**" : (marker == 'i' ? "*" : "~~"));
    }
}

// Closes markers from the top until every open marker is wanted, then opens
// the missing ones in the order bold, italic, strikethrough.
void InlineWriter::transition_emphasis(const RunStyle& style) {
    while (!emphasis_.empty()) {
        bool unwanted = false;
        for (std::size_t index = 0; index < emphasis_.size(); ++index) {
            const char marker = emphasis_[index];
            const bool wanted = (marker == 'b' && style.bold) || (marker == 'i' && style.italic)
                || (marker == 's' && style.strikethrough);
            if (!wanted) {
                unwanted = true;
                break;
            }
        }
        if (!unwanted) {
            break;
        }
        const char marker = emphasis_.back();
        emphasis_.pop_back();
        out_.append(marker == 'b' ? "**" : (marker == 'i' ? "*" : "~~"));
    }
    const bool bold_open = std::find(emphasis_.begin(), emphasis_.end(), 'b') != emphasis_.end();
    const bool italic_open = std::find(emphasis_.begin(), emphasis_.end(), 'i') != emphasis_.end();
    const bool strike_open = std::find(emphasis_.begin(), emphasis_.end(), 's') != emphasis_.end();
    if (style.bold && !bold_open) {
        out_.append("**");
        emphasis_.push_back('b');
        at_line_start_ = false;
    }
    if (style.italic && !italic_open) {
        out_.append("*");
        emphasis_.push_back('i');
        at_line_start_ = false;
    }
    if (style.strikethrough && !strike_open) {
        out_.append("~~");
        emphasis_.push_back('s');
        at_line_start_ = false;
    }
}

void InlineWriter::close_span() {
    if (!span_open_) {
        return;
    }
    out_.append("]{");
    append_items(out_, current_key_);
    out_.push_back('}');
    span_open_ = false;
    current_key_ = SpanKey{};
}

void InlineWriter::close_link() {
    if (!current_link_.has_value()) {
        return;
    }
    out_.append("](");
    write_destination(link_target(document_, LinkIndex{*current_link_}));
    out_.push_back(')');
    current_link_.reset();
}

void InlineWriter::write_destination(std::string_view target) {
    // A raw destination ends at a space, a control character or DEL (the
    // reader's raw scan), so a target holding any of them is bracketed.
    bool bracketed = false;
    for (std::size_t index = 0; index < target.size(); ++index) {
        const char character = target[index];
        const std::uint32_t value = static_cast<unsigned char>(character);
        if (character == ' ' || character == '(' || character == ')' || character == '<' || character == '>'
            || value < 0x20U || value == 0x7FU) {
            bracketed = true;
            break;
        }
    }
    if (bracketed) {
        out_.push_back('<');
    }
    for (std::size_t index = 0; index < target.size(); ++index) {
        const char character = target[index];
        const bool escape = character == '\\' || character == '&' || (bracketed && (character == '<' || character == '>'));
        if (escape) {
            out_.push_back('\\');
        }
        out_.push_back(character);
    }
    if (bracketed) {
        out_.push_back('>');
    }
}

void InlineWriter::write_text(std::string_view text, bool followed_by_text) {
    const bool heading = block_.kind == BlockKind::heading;
    std::size_t escape_at = text.size();
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (at_line_start_) {
            at_line_start_ = false;
            if (character == ' ') {
                out_.append("&#32;");
                continue;
            }
            if (character == '\t') {
                out_.append("&#9;");
                continue;
            }
            const bool block_marker = character == '#' || character == '>' || character == '-' || character == '+'
                || character == '=' || character == '|';
            if (block_marker) {
                out_.push_back('\\');
                out_.push_back(character);
                continue;
            }
            if (is_digit(character)) {
                std::size_t digits_end = index;
                while (digits_end < text.size() && is_digit(text[digits_end])) {
                    digits_end += 1U;
                }
                if (digits_end < text.size() && (text[digits_end] == '.' || text[digits_end] == ')')) {
                    escape_at = digits_end;
                }
            }
        }
        const bool always = character == '\\' || character == '`' || character == '*' || character == '_'
            || character == '[' || character == ']' || character == '~' || character == '<' || character == '{'
            || character == '&';
        // A '!' ending a run may meet the '[' that opens the next run's link
        // or span, which would read as an image.
        const bool bang_before_run = character == '!' && index + 1U == text.size() && followed_by_text;
        if (always || index == escape_at || (heading && character == '#') || bang_before_run) {
            out_.push_back('\\');
        }
        out_.push_back(character);
    }
}

void InlineWriter::write_code(std::string_view text) {
    std::size_t longest = 0;
    std::size_t current = 0;
    bool all_spaces = true;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '`') {
            current += 1U;
            longest = std::max(longest, current);
        } else {
            current = 0;
        }
        if (text[index] != ' ') {
            all_spaces = false;
        }
    }
    const std::size_t fence = longest + 1U;
    const bool edge = text.front() == '`' || text.back() == '`' || text.front() == ' ' || text.back() == ' ';
    const bool padded = edge && !all_spaces;
    out_.append(fence, '`');
    if (padded) {
        out_.push_back(' ');
    }
    out_.append(text);
    if (padded) {
        out_.push_back(' ');
    }
    out_.append(fence, '`');
    at_line_start_ = false;
}

void InlineWriter::write() {
    const std::size_t first = block_.first_run.value;
    const std::size_t end = first + block_.run_count;
    // One backward pass gives each run its nearest following text run.
    next_text_.assign(block_.run_count, static_cast<std::size_t>(-1));
    std::size_t following = static_cast<std::size_t>(-1);
    for (std::size_t index = end; index > first; --index) {
        next_text_[index - 1U - first] = following;
        if (document_.runs[index - 1U].kind == RunKind::text) {
            following = index - 1U;
        }
    }
    previous_text_ = static_cast<std::size_t>(-1);
    for (std::size_t index = first; index < end; ++index) {
        const Run& run = document_.runs[index];
        const std::optional<std::uint32_t> link = link_context(index);
        const SpanKey key = span_context(index);
        if (link != current_link_) {
            close_emphasis();
            close_span();
            close_link();
            if (link.has_value()) {
                out_.push_back('[');
                at_line_start_ = false;
                current_link_ = link;
            }
        }
        const bool span_changed = span_open_ ? !(key == current_key_) : !is_default(key);
        if (span_changed) {
            close_emphasis();
            close_span();
            if (!is_default(key)) {
                out_.push_back('[');
                at_line_start_ = false;
                span_open_ = true;
                current_key_ = key;
            }
        }
        if (run.kind == RunKind::line_break) {
            out_.append("\\\n");
            at_line_start_ = true;
            continue;
        }
        previous_text_ = index;
        if (!fallback_) {
            transition_emphasis(run.style);
        }
        const std::string_view text = run_text(document_, run);
        if (run.style.code) {
            write_code(text);
        } else {
            const bool followed_by_text = index + 1U < end && document_.runs[index + 1U].kind == RunKind::text;
            write_text(text, followed_by_text);
        }
    }
    close_emphasis();
    close_span();
    close_link();
    // CommonMark strips a block's trailing blanks; a literal one is written
    // as a reference. Code spans, delimiters and brackets never end in one.
    if (!out_.empty() && is_blank(out_.back())) {
        const char blank = out_.back();
        out_.pop_back();
        out_.append(blank == ' ' ? "&#32;" : "&#9;");
    }
}

bool block_has_emphasis(const Document& document, const Block& block) {
    const std::size_t first = block.first_run.value;
    for (std::size_t index = first; index < first + block.run_count; ++index) {
        const RunStyle& style = document.runs[index].style;
        if (style.bold || style.italic || style.strikethrough) {
            return true;
        }
    }
    return false;
}

// Equal runs: kinds, text, styles, and links compared by target.
bool same_runs(const Document& left, const Block& left_block, const Document& right, const Block& right_block) {
    if (left_block.run_count != right_block.run_count) {
        return false;
    }
    for (std::uint32_t offset = 0; offset < left_block.run_count; ++offset) {
        const Run& a = left.runs[left_block.first_run.value + offset];
        const Run& b = right.runs[right_block.first_run.value + offset];
        if (a.kind != b.kind || run_text(left, a) != run_text(right, b)) {
            return false;
        }
        RunStyle a_style = a.style;
        RunStyle b_style = b.style;
        if (a_style.link.has_value() != b_style.link.has_value()) {
            return false;
        }
        if (a_style.link.has_value()) {
            if (link_target(left, *a_style.link) != link_target(right, *b_style.link)) {
                return false;
            }
            a_style.link.reset();
            b_style.link.reset();
        }
        if (!(a_style == b_style)) {
            return false;
        }
    }
    return true;
}

}

struct MarkdownWriter::Workspace final {
    explicit Workspace(const Limits& limits) : verifier(limits) {}

    std::string out;
    std::string inline_text;
    MarkdownReader verifier;
    Document verify_document;
    LossReport verify_loss;
    std::array<std::size_t, maximum_list_level + 1U> list_widths{};
    std::vector<std::size_t> next_text;
};

MarkdownWriter::MarkdownWriter(const Limits& limits) : limits_(limits), workspace_(std::make_unique<Workspace>(limits)) {}

MarkdownWriter::~MarkdownWriter() {}

Result MarkdownWriter::write(const Document& document, std::span<char> output) {
    Result result{};
    const NormalFormCheck check = check_normal_form(document);
    if (check.violation != NormalFormViolation::none) {
        result.status = Status::invalid_document;
        return result;
    }
    Workspace& workspace = *workspace_;
    try {
        std::string& out = workspace.out;
        out.clear();
        std::string& inline_text = workspace.inline_text;
        for (std::size_t block_index = 0; block_index < document.blocks.size(); ++block_index) {
            const Block& block = document.blocks[block_index];
            if (block_index > 0) {
                // A bare blank line would end the quotes the two blocks
                // share, so the separator is quoted to their common depth.
                const std::uint8_t previous_depth = document.blocks[block_index - 1U].quote_depth;
                const std::uint8_t shared = std::min(previous_depth, block.quote_depth);
                for (std::uint8_t depth = 0; depth < shared; ++depth) {
                    out.append(depth == 0 ? ">" : " >");
                }
                out.push_back('\n');
            }
            std::string quote;
            for (std::uint8_t depth = 0; depth < block.quote_depth; ++depth) {
                quote.append("> ");
            }
            std::size_t indent = 0;
            std::string lead;
            std::size_t continuation = 0;
            if (block.kind == BlockKind::list_item) {
                for (std::uint8_t level = 0; level < block.level; ++level) {
                    indent += workspace.list_widths[level];
                }
                if (block.marker == ListMarker::bullet) {
                    lead = "- ";
                } else {
                    lead = std::to_string(block.ordinal);
                    lead.append(". ");
                }
                workspace.list_widths[block.level] = lead.size();
                continuation = lead.size();
            } else if (block.kind == BlockKind::heading) {
                lead.assign(block.level, '#');
                lead.push_back(' ');
            }
            const std::string indentation(indent, ' ');
            if (block.alignment != Alignment::left) {
                out.append(quote);
                out.append(indentation);
                out.append(block.alignment == Alignment::center ? "{align=center}" : "{align=right}");
                out.push_back('\n');
            }
            if (block.kind == BlockKind::code_block) {
                const std::string_view text = run_text(document, document.runs[block.first_run.value]);
                std::size_t longest = 0;
                std::size_t current = 0;
                for (std::size_t index = 0; index < text.size(); ++index) {
                    current = text[index] == '`' ? current + 1U : 0U;
                    longest = std::max(longest, current);
                }
                const std::string fence(std::max<std::size_t>(3U, longest + 1U), '`');
                std::string quote_trimmed = quote;
                while (!quote_trimmed.empty() && quote_trimmed.back() == ' ') {
                    quote_trimmed.pop_back();
                }
                out.append(quote);
                out.append(fence);
                out.push_back('\n');
                std::size_t line_start = 0;
                while (line_start <= text.size()) {
                    std::size_t line_end = text.find('\n', line_start);
                    if (line_end == std::string_view::npos) {
                        line_end = text.size();
                    }
                    const std::string_view line = text.substr(line_start, line_end - line_start);
                    out.append(line.empty() ? quote_trimmed : quote);
                    out.append(line);
                    out.push_back('\n');
                    line_start = line_end + 1U;
                }
                out.append(quote);
                out.append(fence);
                out.push_back('\n');
                continue;
            }
            inline_text.clear();
            bool fallback = false;
            {
                InlineWriter writer(document, block, false, inline_text, workspace.next_text);
                writer.write();
            }
            if (block_has_emphasis(document, block)) {
                const Result verify = workspace.verifier.read(std::span<const char>(inline_text.data(), inline_text.size()),
                                                              workspace.verify_document, workspace.verify_loss);
                const bool one_block = verify.status == Status::ok && workspace.verify_document.blocks.size() == 1U;
                fallback = !one_block
                    || !same_runs(document, block, workspace.verify_document, workspace.verify_document.blocks[0]);
            }
            if (fallback) {
                inline_text.clear();
                InlineWriter writer(document, block, true, inline_text, workspace.next_text);
                writer.write();
            }
            const std::string continuation_indent(continuation, ' ');
            std::size_t line_start = 0;
            bool first_line = true;
            while (line_start <= inline_text.size()) {
                std::size_t line_end = inline_text.find('\n', line_start);
                if (line_end == std::string::npos) {
                    line_end = inline_text.size();
                }
                out.append(quote);
                out.append(indentation);
                out.append(first_line ? lead : continuation_indent);
                out.append(inline_text, line_start, line_end - line_start);
                out.push_back('\n');
                first_line = false;
                if (line_end == inline_text.size()) {
                    break;
                }
                line_start = line_end + 1U;
            }
        }
        // Writing is linear in the document; the allowance bounds it like a
        // read of the same size.
        const std::uint64_t elements = document.blocks.size() + document.runs.size() + document.links.size();
        const std::uint64_t allowance = static_cast<std::uint64_t>(limits_.work_per_input_byte) * (document.text.size() + 1U)
            + static_cast<std::uint64_t>(limits_.work_per_element) * elements;
        result.work = WorkCount{out.size()};
        if (out.size() > allowance) {
            result.status = Status::effort_exceeded;
            return result;
        }
        result.required = out.size();
        if (output.size() < out.size()) {
            result.status = Status::output_too_small;
            return result;
        }
        std::copy(out.begin(), out.end(), output.begin());
        result.produced = out.size();
        result.status = Status::ok;
    } catch (const std::bad_alloc&) {
        result.status = Status::limit_exceeded;
        result.produced = 0;
    }
    return result;
}

}
