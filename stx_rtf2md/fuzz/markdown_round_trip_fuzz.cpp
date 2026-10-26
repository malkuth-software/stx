// stx_rtf2md markdown contract fuzzer. The first byte chooses a mode:
// - even: the remaining bytes are dialect text. An accepted document must
//   be in normal form, re-read equal from its canonical text, and write the
//   same canonical text twice.
// - odd: the remaining bytes drive a generator of normal-form documents
//   (every block kind, every style, links, line breaks, blanks and markup
//   characters in text). Each must survive write then read unchanged.
// Every document checked must also survive write_rtf then read_rtf (rule 2).
// Any contract break aborts. A build check for CI smoke runs, not a
// certificate (owner revision of decision 6A).
#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"
#include "stx_rtf2md/markdown.hpp"
#include "stx_rtf2md/rtf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace stx_rtf2md;

constexpr std::size_t input_limit = 65536;

struct Workspace final {
    Limits limits{};
    MarkdownReader reader{limits};
    MarkdownWriter writer{limits};
    Document document{};
    Document reread{};
    LossReport loss{};
    std::vector<char> canonical = std::vector<char>(input_limit * 64U);
    std::vector<char> second = std::vector<char>(input_limit * 64U);
    RtfWriter rtf_writer{limits};
    RtfReader rtf_reader{limits};
    Document rtf_reread{};
    std::vector<std::byte> rtf = std::vector<std::byte>(input_limit * 256U);
};

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

// Writes document, re-reads it, and checks equality and idempotence.
void check_round_trip(Workspace& workspace, const Document& document) {
    const Result written = workspace.writer.write(document, std::span<char>(workspace.canonical));
    require(written.status == Status::ok);
    if (written.produced > 0) {
        char one = 0;
        const Result small = workspace.writer.write(document, std::span<char>(&one, 1));
        require(written.produced == 1U || (small.status == Status::output_too_small && small.required == written.produced));
    }
    const std::span<const char> text(workspace.canonical.data(), written.produced);
    const Result reread = workspace.reader.read(text, workspace.reread, workspace.loss);
    require(reread.status == Status::ok);
    require(workspace.reread == document);
    const Result again = workspace.writer.write(workspace.reread, std::span<char>(workspace.second));
    require(again.status == Status::ok && again.produced == written.produced);
    for (std::size_t index = 0; index < written.produced; ++index) {
        require(workspace.canonical[index] == workspace.second[index]);
    }
    const Result rtf_written = workspace.rtf_writer.write(document, std::span<std::byte>(workspace.rtf));
    require(rtf_written.status == Status::ok);
    const std::span<const std::byte> rtf(workspace.rtf.data(), rtf_written.produced);
    const Result rtf_read = workspace.rtf_reader.read(rtf, workspace.rtf_reread, workspace.loss);
    require(rtf_read.status == Status::ok);
    require(workspace.rtf_reread == document);
}

// Reads bytes from the fuzz input; zero once exhausted.
class ByteSource final {
public:
    ByteSource(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t next() {
        if (offset_ >= size_) {
            return 0;
        }
        const std::uint8_t value = data_[offset_];
        offset_ += 1U;
        return value;
    }

    bool exhausted() const {
        return offset_ >= size_;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

// Text pieces chosen to provoke the writer: blanks, markup characters,
// digits before punctuation, backticks and a multi-byte scalar.
const char* const pieces[] = {
    "a", "word", " ", "  ", "\t", "*", "_", "~", "`", "``", "[", "]", "{", "}", "<", ">", "&", "&#32;", "\\",
    "#", "-", "+", "=", "|", "1.", "2)", ".", "!", "(", ")", "\xC3\xA9", "x y", ":", "\"",
};
constexpr std::size_t piece_count = sizeof(pieces) / sizeof(pieces[0]);

const char* const targets[] = {"https://example.com/a", "a b", "(x)", "<y>", "", "u&v", "back\\slash"};
constexpr std::size_t target_count = sizeof(targets) / sizeof(targets[0]);

void append_run(Document& document, Block& block, RunKind kind, const std::string& text, const RunStyle& style) {
    if (kind == RunKind::text && block.run_count > 0) {
        Run& last = document.runs.back();
        if (last.kind == RunKind::text && last.style == style) {
            document.text += text;
            last.length.value += static_cast<std::uint32_t>(text.size());
            return;
        }
    }
    Run run{};
    run.kind = kind;
    run.start = TextOffset{static_cast<std::uint32_t>(document.text.size())};
    run.length = TextLength{static_cast<std::uint32_t>(text.size())};
    run.style = style;
    document.text += text;
    document.runs.push_back(run);
    block.run_count += 1U;
}

// Builds a document in normal form from the byte source.
void generate(ByteSource& source, Document& document) {
    document.clear();
    std::vector<std::string> link_targets;
    std::uint8_t previous_kind = 255;
    std::uint8_t previous_level = 0;
    std::uint8_t previous_quote = 0;
    const std::size_t block_count = 1U + source.next() % 6U;
    for (std::size_t block_index = 0; block_index < block_count && !source.exhausted(); ++block_index) {
        Block block{};
        const std::uint8_t choice = source.next();
        block.first_run = RunIndex{static_cast<std::uint32_t>(document.runs.size())};
        block.quote_depth = static_cast<std::uint8_t>((choice >> 4U) % 3U);
        const std::uint8_t kind = choice % 4U;
        block.kind = static_cast<BlockKind>(kind);
        if (block.kind == BlockKind::heading) {
            block.level = static_cast<std::uint8_t>(1U + source.next() % 6U);
        } else if (block.kind == BlockKind::list_item) {
            std::uint8_t level = static_cast<std::uint8_t>(source.next() % 3U);
            const bool chain = previous_kind == static_cast<std::uint8_t>(BlockKind::list_item)
                && previous_quote == block.quote_depth;
            const std::uint8_t allowed = chain ? static_cast<std::uint8_t>(previous_level + 1U) : 0;
            if (level > allowed) {
                level = allowed;
            }
            block.level = level;
            if ((source.next() & 1U) != 0) {
                block.marker = ListMarker::ordered;
                block.ordinal = source.next();
            }
        }
        if (block.kind != BlockKind::code_block) {
            const std::uint8_t align = source.next() % 5U;
            block.alignment = align == 3U ? Alignment::center : (align == 4U ? Alignment::right : Alignment::left);
        }
        if (block.kind == BlockKind::code_block) {
            std::string text;
            const std::size_t count = 1U + source.next() % 6U;
            for (std::size_t index = 0; index < count; ++index) {
                const std::uint8_t byte = source.next();
                text += (byte % 7U == 0) ? std::string("\n") : std::string(pieces[byte % piece_count]);
            }
            append_run(document, block, RunKind::text, text, RunStyle{});
        } else {
            const std::size_t count = 1U + source.next() % 8U;
            for (std::size_t index = 0; index < count; ++index) {
                const std::uint8_t byte = source.next();
                const bool line_break = block.kind != BlockKind::heading && byte % 11U == 0 && block.run_count > 0;
                if (line_break) {
                    append_run(document, block, RunKind::line_break, std::string(), RunStyle{});
                    continue;
                }
                const std::uint8_t bits = source.next();
                const std::uint8_t more = source.next();
                RunStyle style{};
                style.bold = (bits & 1U) != 0;
                style.italic = (bits & 2U) != 0;
                style.underline = (bits & 4U) != 0;
                style.strikethrough = (bits & 8U) != 0;
                style.code = (bits & 16U) != 0;
                style.superscript = (bits & 32U) != 0 && (bits & 64U) == 0;
                style.subscript = (bits & 64U) != 0 && (bits & 32U) == 0;
                if ((more & 1U) != 0) {
                    style.colour = Rgb{more, 0x20, 0x40};
                }
                if ((more & 2U) != 0) {
                    style.background = Rgb{0x10, more, 0x30};
                }
                if (block.kind != BlockKind::heading) {
                    style.size_step = static_cast<std::int8_t>(static_cast<int>((more >> 2U) % 7U) - 2);
                }
                if ((more & 0x80U) != 0) {
                    // A new link for each linked run keeps links in first-use
                    // order and each link one span.
                    style.link = LinkIndex{static_cast<std::uint32_t>(link_targets.size())};
                    link_targets.push_back(targets[more % target_count]);
                }
                append_run(document, block, RunKind::text, pieces[byte % piece_count], style);
            }
            // A block may not end in a line break.
            while (block.run_count > 0 && document.runs.back().kind == RunKind::line_break) {
                document.runs.pop_back();
                block.run_count -= 1U;
            }
            if (block.run_count == 0) {
                append_run(document, block, RunKind::text, "x", RunStyle{});
            }
        }
        document.blocks.push_back(block);
        previous_kind = static_cast<std::uint8_t>(block.kind);
        previous_level = block.level;
        previous_quote = block.quote_depth;
    }
    // Merging a linked run into an equal neighbour can leave a link unused;
    // renumber links in first-use order and drop unused ones.
    std::vector<std::int64_t> renumber(link_targets.size(), -1);
    std::vector<std::string> used;
    for (std::size_t index = 0; index < document.runs.size(); ++index) {
        RunStyle& style = document.runs[index].style;
        if (!style.link.has_value()) {
            continue;
        }
        const std::uint32_t old_index = (*style.link).value;
        if (renumber[old_index] < 0) {
            renumber[old_index] = static_cast<std::int64_t>(used.size());
            used.push_back(link_targets[old_index]);
        }
        style.link = LinkIndex{static_cast<std::uint32_t>(renumber[old_index])};
    }
    for (std::size_t index = 0; index < used.size(); ++index) {
        Link link{};
        link.target_start = TextOffset{static_cast<std::uint32_t>(document.text.size())};
        link.target_length = TextLength{static_cast<std::uint32_t>(used[index].size())};
        document.text += used[index];
        document.links.push_back(link);
    }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0 || size > input_limit) {
        return 0;
    }
    static Workspace* workspace = new Workspace();
    if ((data[0] & 1U) == 0) {
        const std::span<const char> text(reinterpret_cast<const char*>(data + 1), size - 1U);
        const Result result = (*workspace).reader.read(text, (*workspace).document, (*workspace).loss);
        if (result.status != Status::ok) {
            require((*workspace).document.blocks.empty() && (*workspace).document.text.empty());
            return 0;
        }
        require(check_normal_form((*workspace).document).violation == NormalFormViolation::none);
        const Document accepted = (*workspace).document;
        check_round_trip(*workspace, accepted);
        return 0;
    }
    ByteSource source(data + 1, size - 1U);
    Document generated{};
    generate(source, generated);
    if (check_normal_form(generated).violation != NormalFormViolation::none) {
        // The generator must only produce normal form.
        std::abort();
    }
    check_round_trip(*workspace, generated);
    return 0;
}

#if defined(STX_RTF2MD_FUZZ_STANDALONE)
int main() {
    // Fixed inputs so the standalone build exercises both modes.
    const char* const texts[] = {
        "# T\n\n- a\n  - b\n\n> q *e* **s** [u]{u}\n", "**foo.**bar", "`` ` `` [a](<b c>) &#32;x&#32;",
        "1. x\n2) y\n\n{align=center}\npara\\\nnext", "```\ncode\n\n```\n> > deep\n>\n> > - item",
    };
    for (const char* const text : texts) {
        std::vector<std::uint8_t> input;
        input.push_back(0);
        for (const char* cursor = text; *cursor != '\0'; ++cursor) {
            input.push_back(static_cast<std::uint8_t>(*cursor));
        }
        LLVMFuzzerTestOneInput(input.data(), input.size());
    }
    std::vector<std::uint8_t> input(512);
    for (std::uint32_t seed = 0; seed < 4000U; ++seed) {
        std::uint32_t state = seed * 2654435761U + 1U;
        input[0] = 1;
        for (std::size_t index = 1; index < input.size(); ++index) {
            state = state * 1103515245U + 12345U;
            input[index] = static_cast<std::uint8_t>(state >> 16U);
        }
        LLVMFuzzerTestOneInput(input.data(), input.size());
    }
    return 0;
}
#endif
