#include "stx_rtf2md/rtf.hpp"

#include "text_scan.hpp"

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

// Twips per quote level and per list level of indentation.
constexpr std::int32_t twips_per_level = 720;
// Canonical half-point sizes for steps -2 to +4 against the 12-point body
// declared in \*\defchp (plan section 6).
constexpr std::array<std::int32_t, 7> step_half_points{16, 20, 24, 28, 36, 48, 64};
// Style numbers in the written style sheet.
constexpr std::int32_t quote_style = 7;
constexpr std::int32_t code_style = 8;
// List override numbers: bullets and numbers.
constexpr std::int32_t bullet_list = 1;
constexpr std::int32_t ordered_list = 2;

// A colour's place in the written colour table (1-based; 0 is auto).
struct ColourSlot final {
    std::uint32_t key = 0;
    std::uint32_t index = 0;
};

struct ColourSlotOrder final {
    bool operator()(const ColourSlot& left, std::uint32_t right) const noexcept {
        return left.key < right;
    }
};

std::uint32_t colour_key(const Rgb& colour) noexcept {
    return (static_cast<std::uint32_t>(colour.red) << 16U) | (static_cast<std::uint32_t>(colour.green) << 8U)
        | static_cast<std::uint32_t>(colour.blue);
}

std::string_view run_text(const Document& document, const Run& run) {
    const std::string_view text(document.text.data() + run.start.value, run.length.value);
    return text;
}

}

struct RtfWriter::Workspace final {
    std::string out;
    // Colours in first-use order, and a sorted index of them.
    std::vector<Rgb> colours;
    std::vector<ColourSlot> slots;
};

namespace {

// Writes one document. Borrows the writer's workspace and the document for
// the duration of RtfWriter::write.
class Emitter final {
public:
    Emitter(RtfWriter::Workspace& workspace, const Document& document, const RtfWriteOptions& options)
        : workspace_(workspace), out_(workspace.out), document_(document), options_(options) {}

    void write();

private:
    void collect_colours();
    std::uint32_t colour_index(const Rgb& colour) const;
    void write_header();
    void write_text(std::string_view text);
    void write_number(std::int64_t value);
    void write_word(std::string_view word, std::int64_t value);
    void write_paragraph_start(const Block& block, bool code_start);
    void write_block(const Block& block);
    void write_code_block(const Block& block);
    void write_run_group(const Run& run);
    void write_target(std::string_view target);

    RtfWriter::Workspace& workspace_;
    std::string& out_;
    const Document& document_;
    const RtfWriteOptions& options_;
};

void Emitter::write_number(std::int64_t value) {
    out_.append(std::to_string(value));
}

void Emitter::write_word(std::string_view word, std::int64_t value) {
    out_.push_back('\\');
    out_.append(word);
    write_number(value);
}

// Colours and backgrounds share one table, in order of first use.
void Emitter::collect_colours() {
    workspace_.colours.clear();
    workspace_.slots.clear();
    const ColourSlotOrder order{};
    for (std::size_t index = 0; index < document_.runs.size(); ++index) {
        const RunStyle& style = document_.runs[index].style;
        const std::array<std::optional<Rgb>, 2> used{style.colour, style.background};
        for (std::size_t which = 0; which < used.size(); ++which) {
            if (!used[which].has_value()) {
                continue;
            }
            const std::uint32_t key = colour_key(*used[which]);
            const std::vector<ColourSlot>::iterator found =
                std::lower_bound(workspace_.slots.begin(), workspace_.slots.end(), key, order);
            if (found != workspace_.slots.end() && (*found).key == key) {
                continue;
            }
            workspace_.colours.push_back(*used[which]);
            ColourSlot slot{};
            slot.key = key;
            slot.index = static_cast<std::uint32_t>(workspace_.colours.size());
            workspace_.slots.insert(found, slot);
        }
    }
}

std::uint32_t Emitter::colour_index(const Rgb& colour) const {
    const std::uint32_t key = colour_key(colour);
    const ColourSlotOrder order{};
    const std::vector<ColourSlot>::const_iterator found =
        std::lower_bound(workspace_.slots.begin(), workspace_.slots.end(), key, order);
    return (*found).index;
}

// Text as ASCII: \, { and } escaped, tab as \tab, everything else above
// 0x7E as \uN with '?' as its one-byte fallback (\uc1), as UTF-16 pairs
// beyond the BMP.
void Emitter::write_text(std::string_view text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const detail::ScalarRead read = detail::read_scalar(text, offset, text.size());
        offset += read.length;
        const std::uint32_t scalar = read.scalar;
        if (scalar == '\\' || scalar == '{' || scalar == '}') {
            out_.push_back('\\');
            out_.push_back(static_cast<char>(scalar));
        } else if (scalar == '\t') {
            out_.append("\\tab ");
        } else if (scalar >= 0x20U && scalar < 0x7FU) {
            out_.push_back(static_cast<char>(scalar));
        } else if (scalar < 0x10000U) {
            const std::int64_t value = scalar < 0x8000U ? static_cast<std::int64_t>(scalar)
                                                        : static_cast<std::int64_t>(scalar) - 65536;
            write_word("u", value);
            out_.push_back('?');
        } else {
            const std::uint32_t reduced = scalar - 0x10000U;
            const std::uint32_t high = 0xD800U + (reduced >> 10U);
            const std::uint32_t low = 0xDC00U + (reduced & 0x3FFU);
            write_word("u", static_cast<std::int64_t>(high) - 65536);
            out_.push_back('?');
            write_word("u", static_cast<std::int64_t>(low) - 65536);
            out_.push_back('?');
        }
    }
}

void Emitter::write_header() {
    out_.append("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl{\\f0\\fswiss ");
    write_text(options_.body_font);
    out_.append(";}{\\f1\\fmodern ");
    write_text(options_.code_font);
    out_.append(";}}\n{\\colortbl;");
    for (std::size_t index = 0; index < workspace_.colours.size(); ++index) {
        const Rgb& colour = workspace_.colours[index];
        write_word("red", colour.red);
        write_word("green", colour.green);
        write_word("blue", colour.blue);
        out_.push_back(';');
    }
    out_.append("}\n{\\*\\defchp\\fs24}\n");
    out_.append("{\\stylesheet{\\s0 Normal;}{\\s1\\fs40 heading 1;}{\\s2\\fs32 heading 2;}{\\s3\\fs28 heading 3;}"
                "{\\s4\\fs24 heading 4;}{\\s5\\fs24 heading 5;}{\\s6\\fs24 heading 6;}{\\s7\\i Quote;}"
                "{\\s8\\f1 Code;}}\n");
    // One bullet list and one numbered list, nine levels each.
    out_.append("{\\*\\listtable");
    for (std::int32_t list = bullet_list; list <= ordered_list; ++list) {
        out_.append("{\\list\\listtemplateid");
        write_number(list);
        for (std::int32_t level = 0; level < 9; ++level) {
            out_.append("{\\listlevel");
            if (list == bullet_list) {
                out_.append("\\levelnfc23\\levelstartat1{\\leveltext\\'01\\u8226 ?;}{\\levelnumbers;}");
            } else {
                out_.append("\\levelnfc0\\levelstartat1{\\leveltext\\'02\\'");
                const char* const digits = "0123456789abcdef";
                out_.push_back('0');
                out_.push_back(digits[level]);
                out_.append(".;}{\\levelnumbers\\'01;}");
            }
            write_word("fi", -360);
            write_word("li", static_cast<std::int64_t>(twips_per_level) * (level + 1));
            out_.push_back('}');
        }
        write_word("listid", list);
        out_.push_back('}');
    }
    out_.append("}\n{\\*\\listoverridetable");
    for (std::int32_t list = bullet_list; list <= ordered_list; ++list) {
        out_.append("{\\listoverride");
        write_word("listid", list);
        out_.append("\\listoverridecount0");
        write_word("ls", list);
        out_.push_back('}');
    }
    out_.append("}\n");
}

// Standard properties for other readers, then the exact block fields.
void Emitter::write_paragraph_start(const Block& block, bool code_start) {
    out_.append("\\pard\\plain");
    const std::int64_t quote_indent = static_cast<std::int64_t>(twips_per_level) * block.quote_depth;
    if (block.kind == BlockKind::code_block) {
        write_word("s", code_style);
        if (block.quote_depth > 0) {
            write_word("li", quote_indent);
        }
    } else if (block.quote_depth > 0) {
        write_word("s", quote_style);
        if (block.kind == BlockKind::list_item) {
            write_word("li", quote_indent + static_cast<std::int64_t>(twips_per_level) * (block.level + 1));
        } else {
            write_word("li", quote_indent);
        }
    } else if (block.kind == BlockKind::heading) {
        write_word("s", block.level);
    }
    if (block.kind == BlockKind::heading) {
        write_word("outlinelevel", block.level - 1);
    }
    if (block.kind == BlockKind::list_item) {
        write_word("ls", block.marker == ListMarker::bullet ? bullet_list : ordered_list);
        write_word("ilvl", block.level);
        if (block.quote_depth == 0) {
            write_word("fi", -360);
            write_word("li", static_cast<std::int64_t>(twips_per_level) * (block.level + 1));
        }
    }
    if (block.alignment == Alignment::center) {
        out_.append("\\qc");
    } else if (block.alignment == Alignment::right) {
        out_.append("\\qr");
    }
    out_.append("{\\*\\stxblock");
    write_word("stxkind", static_cast<std::int64_t>(block.kind));
    write_word("stxlevel", block.level);
    write_word("stxquote", block.quote_depth);
    write_word("stxmarker", static_cast<std::int64_t>(block.marker));
    write_word("stxordinal", block.ordinal);
    if (code_start) {
        out_.append("\\stxstart");
    }
    out_.push_back('}');
    if (block.kind == BlockKind::list_item) {
        // The marker for readers without list support.
        out_.append("{\\listtext\\pard\\plain ");
        if (block.marker == ListMarker::bullet) {
            out_.append("\\u8226 ?");
        } else {
            write_number(block.ordinal);
            out_.push_back('.');
        }
        out_.append("\\tab}");
    }
    // The paragraph's properties end in a group: no delimiter space follows,
    // since a space after '}' would be text.
}

void Emitter::write_run_group(const Run& run) {
    const RunStyle& style = run.style;
    out_.push_back('{');
    if (style.bold) {
        out_.append("\\b");
    }
    if (style.italic) {
        out_.append("\\i");
    }
    if (style.underline) {
        out_.append("\\ul");
    }
    if (style.strikethrough) {
        out_.append("\\strike");
    }
    if (style.superscript) {
        out_.append("\\super");
    }
    if (style.subscript) {
        out_.append("\\sub");
    }
    if (style.code) {
        out_.append("\\f1");
    }
    if (style.colour.has_value()) {
        write_word("cf", colour_index(*style.colour));
    }
    if (style.background.has_value()) {
        write_word("highlight", colour_index(*style.background));
    }
    if (style.size_step != 0) {
        const std::size_t step = static_cast<std::size_t>(style.size_step - minimum_size_step);
        write_word("fs", step_half_points[step]);
    }
    // A control word needs a delimiting space; directly after '{' a space
    // would be text.
    if (out_.back() != '{') {
        out_.push_back(' ');
    }
    write_text(run_text(document_, run));
    out_.push_back('}');
}

// A field code's quoted target escapes '"' and '\' with a backslash; the RTF
// text then escapes every backslash again.
void Emitter::write_target(std::string_view target) {
    std::string field;
    for (std::size_t index = 0; index < target.size(); ++index) {
        const char character = target[index];
        if (character == '"' || character == '\\') {
            field.push_back('\\');
        }
        field.push_back(character);
    }
    out_.append("{\\field{\\*\\fldinst HYPERLINK \"");
    write_text(field);
    out_.append("\"}{\\fldrslt ");
}

void Emitter::write_block(const Block& block) {
    write_paragraph_start(block, false);
    const std::size_t first = block.first_run.value;
    const std::size_t end = first + block.run_count;
    std::optional<std::uint32_t> open_link;
    for (std::size_t index = first; index < end; ++index) {
        const Run& run = document_.runs[index];
        if (run.kind == RunKind::line_break) {
            // A link continues across a line break only if its runs resume.
            bool continues = false;
            if (open_link.has_value()) {
                std::size_t next = index + 1U;
                while (next < end && document_.runs[next].kind == RunKind::line_break) {
                    next += 1U;
                }
                continues = next < end && document_.runs[next].style.link.has_value()
                    && (*document_.runs[next].style.link).value == *open_link;
            }
            if (open_link.has_value() && !continues) {
                out_.append("}}");
                open_link.reset();
            }
            out_.append("\\line ");
            continue;
        }
        const std::optional<LinkIndex> link = run.style.link;
        const bool same_link = link.has_value() && open_link.has_value() && (*link).value == *open_link;
        if (open_link.has_value() && !same_link) {
            out_.append("}}");
            open_link.reset();
        }
        if (link.has_value() && !open_link.has_value()) {
            const Link& target = document_.links[(*link).value];
            const std::string_view text(document_.text.data() + target.target_start.value, target.target_length.value);
            write_target(text);
            open_link = (*link).value;
        }
        write_run_group(run);
    }
    if (open_link.has_value()) {
        out_.append("}}");
    }
    out_.append("\\par\n");
}

// One Code-style paragraph per line, the first marked as the block's start
// so adjacent code blocks stay apart.
void Emitter::write_code_block(const Block& block) {
    const std::string_view text = run_text(document_, document_.runs[block.first_run.value]);
    std::size_t line_start = 0;
    bool first = true;
    while (line_start <= text.size()) {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }
        write_paragraph_start(block, first);
        first = false;
        const std::string_view line = text.substr(line_start, line_end - line_start);
        if (!line.empty()) {
            out_.append("{\\f1 ");
            write_text(line);
            out_.push_back('}');
        }
        out_.append("\\par\n");
        if (line_end == text.size()) {
            break;
        }
        line_start = line_end + 1U;
    }
}

void Emitter::write() {
    out_.clear();
    collect_colours();
    write_header();
    for (std::size_t index = 0; index < document_.blocks.size(); ++index) {
        const Block& block = document_.blocks[index];
        if (block.kind == BlockKind::code_block) {
            write_code_block(block);
        } else {
            write_block(block);
        }
    }
    out_.append("}\n");
}

}

RtfWriter::RtfWriter(const Limits& limits, const RtfWriteOptions& options)
    : limits_(limits), options_(options), workspace_(std::make_unique<Workspace>()) {}

RtfWriter::~RtfWriter() {}

Result RtfWriter::write(const Document& document, std::span<std::byte> output) {
    Result result{};
    const NormalFormCheck check = check_normal_form(document);
    if (check.violation != NormalFormViolation::none) {
        result.status = Status::invalid_document;
        return result;
    }
    Workspace& workspace = *workspace_;
    try {
        Emitter emitter(workspace, document, options_);
        emitter.write();
    } catch (const std::bad_alloc&) {
        result.status = Status::limit_exceeded;
        return result;
    }
    const std::string& out = workspace.out;
    // Writing is linear in the document: each byte of text becomes at most
    // a \uN escape, and each run or block a fixed amount of markup.
    const std::uint64_t elements = document.blocks.size() + document.runs.size() + document.links.size();
    const std::uint64_t allowance = static_cast<std::uint64_t>(limits_.work_per_input_byte) * (document.text.size() + 1U)
        + static_cast<std::uint64_t>(limits_.work_per_element) * (elements + 64U);
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
    for (std::size_t index = 0; index < out.size(); ++index) {
        output[index] = static_cast<std::byte>(static_cast<unsigned char>(out[index]));
    }
    result.produced = out.size();
    result.status = Status::ok;
    return result;
}

}
