// Markdown reader and writer tests: the dialect's tables in README.md, the
// loss report, flattening, the canonical form, both round-trip rules and
// every failure status. Documents are compared through a compact dump.
#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"
#include "stx_rtf2md/markdown.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using stx_rtf2md::Alignment;
using stx_rtf2md::Block;
using stx_rtf2md::BlockKind;
using stx_rtf2md::Document;
using stx_rtf2md::Limits;
using stx_rtf2md::ListMarker;
using stx_rtf2md::LossKind;
using stx_rtf2md::LossReport;
using stx_rtf2md::MarkdownReader;
using stx_rtf2md::MarkdownWriter;
using stx_rtf2md::Result;
using stx_rtf2md::Rgb;
using stx_rtf2md::Run;
using stx_rtf2md::RunKind;
using stx_rtf2md::RunStyle;
using stx_rtf2md::Status;

void fail(const std::string& message) {
    std::fprintf(stderr, "stx_rtf2md: %s\n", message.c_str());
    std::abort();
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::span<const char> bytes(std::string_view text) {
    const std::span<const char> span(text.data(), text.size());
    return span;
}

void append_hex(std::string& out, std::uint8_t value) {
    const char* const digits = "0123456789abcdef";
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 0x0FU]);
}

// One line per block: a header (p, hN, liN-b or liN-oK, code; qN for quote
// depth; @c or @r for alignment), then its runs: quoted text followed by
// style flags, or / for a line break.
std::string dump(const Document& document) {
    std::string out;
    for (std::size_t index = 0; index < document.blocks.size(); ++index) {
        const Block& block = document.blocks[index];
        if (block.kind == BlockKind::paragraph) {
            out.append("p");
        } else if (block.kind == BlockKind::heading) {
            out.append("h");
            out.append(std::to_string(block.level));
        } else if (block.kind == BlockKind::list_item) {
            out.append("li");
            out.append(std::to_string(block.level));
            if (block.marker == ListMarker::bullet) {
                out.append("-b");
            } else {
                out.append("-o");
                out.append(std::to_string(block.ordinal));
            }
        } else {
            out.append("code");
        }
        if (block.quote_depth > 0) {
            out.append(" q");
            out.append(std::to_string(block.quote_depth));
        }
        if (block.alignment == Alignment::center) {
            out.append(" @c");
        } else if (block.alignment == Alignment::right) {
            out.append(" @r");
        }
        out.append(":");
        for (std::uint32_t offset = 0; offset < block.run_count; ++offset) {
            const Run& run = document.runs[block.first_run.value + offset];
            out.push_back(' ');
            if (run.kind == RunKind::line_break) {
                out.push_back('/');
                continue;
            }
            out.push_back('"');
            out.append(document.text, run.start.value, run.length.value);
            out.push_back('"');
            const RunStyle& style = run.style;
            if (style.bold) {
                out.append("b");
            }
            if (style.italic) {
                out.append("i");
            }
            if (style.underline) {
                out.append("u");
            }
            if (style.strikethrough) {
                out.append("s");
            }
            if (style.code) {
                out.append("c");
            }
            if (style.superscript) {
                out.append("^");
            }
            if (style.subscript) {
                out.append("_");
            }
            if (style.size_step != 0) {
                out.append(style.size_step > 0 ? "+" : "-");
                out.append(std::to_string(style.size_step > 0 ? style.size_step : -style.size_step));
            }
            if (style.colour.has_value()) {
                out.append("#");
                append_hex(out, (*style.colour).red);
                append_hex(out, (*style.colour).green);
                append_hex(out, (*style.colour).blue);
            }
            if (style.background.has_value()) {
                out.append("%");
                append_hex(out, (*style.background).red);
                append_hex(out, (*style.background).green);
                append_hex(out, (*style.background).blue);
            }
            if (style.link.has_value()) {
                const stx_rtf2md::Link& link = document.links[(*style.link).value];
                out.append("->");
                out.append(document.text, link.target_start.value, link.target_length.value);
            }
        }
        out.push_back('\n');
    }
    return out;
}

struct Harness final {
    Limits limits{};
    MarkdownReader reader{limits};
    MarkdownWriter writer{limits};
    Document document{};
    LossReport loss{};
    // The loss report of the first read in reads_as or flatten checks.
    LossReport first_loss{};
    std::vector<char> output = std::vector<char>(1U << 20U);
};

Harness& harness() {
    static Harness instance{};
    return instance;
}

Document read_ok(std::string_view markdown) {
    Harness& h = harness();
    const Result result = h.reader.read(bytes(markdown), h.document, h.loss);
    if (result.status != Status::ok) {
        fail("read failed (status " + std::to_string(static_cast<int>(result.status)) + ") for: " + std::string(markdown));
    }
    Document document = h.document;
    return document;
}

std::string write_ok(const Document& document) {
    Harness& h = harness();
    const Result result = h.writer.write(document, std::span<char>(h.output.data(), h.output.size()));
    if (result.status != Status::ok) {
        fail("write failed (status " + std::to_string(static_cast<int>(result.status)) + ")");
    }
    const std::string text(h.output.data(), result.produced);
    return text;
}

// Reads markdown, compares the dump, then checks both round-trip rules.
void reads_as(std::string_view markdown, std::string_view expected) {
    const Document document = read_ok(markdown);
    harness().first_loss = harness().loss;
    const std::string actual = dump(document);
    if (actual != expected) {
        fail("read of [" + std::string(markdown) + "]\n  expected:\n" + std::string(expected) + "  actual:\n" + actual);
    }
    const std::string canonical = write_ok(document);
    const Document again = read_ok(canonical);
    if (!(again == document)) {
        fail("read(write(d)) != d for [" + std::string(markdown) + "]\n  canonical:\n" + canonical + "  reread:\n"
             + dump(again));
    }
    const std::string twice = write_ok(again);
    require(twice == canonical, "writing is not idempotent for [" + std::string(markdown) + "]");
}

void writes_as(std::string_view markdown, std::string_view expected) {
    const Document document = read_ok(markdown);
    const std::string canonical = write_ok(document);
    if (canonical != expected) {
        fail("canonical form of [" + std::string(markdown) + "]\n  expected:\n" + std::string(expected) + "  actual:\n"
             + canonical);
    }
}

std::uint32_t loss_count(LossKind kind) {
    const std::uint32_t count = harness().first_loss.entry(kind).count;
    return count;
}

void blocks() {
    reads_as("Hello world\n", "p: \"Hello world\"\n");
    reads_as("one\ntwo\n", "p: \"one two\"\n");
    reads_as("# Title\n\n## Sub ##\n", "h1: \"Title\"\nh2: \"Sub\"\n");
    reads_as("Title\n=====\n\nSub\n---\n", "h1: \"Title\"\nh2: \"Sub\"\n");
    reads_as("- a\n- b\n  - c\n    - d\n- e\n",
             "li0-b: \"a\"\nli0-b: \"b\"\nli1-b: \"c\"\nli2-b: \"d\"\nli0-b: \"e\"\n");
    reads_as("3. three\n4) four\n\n10. ten\n    - nested\n",
             "li0-o3: \"three\"\nli0-o4: \"four\"\nli0-o10: \"ten\"\nli1-b: \"nested\"\n");
    reads_as("> quoted\n> > deeper\n", "p q1: \"quoted\"\np q2: \"deeper\"\n");
    reads_as("> - in a quote\n>   - nested\n", "li0-b q1: \"in a quote\"\nli1-b q1: \"nested\"\n");
    reads_as("```\nint x;\n\n  return;\n```\n", "code: \"int x;\n\n  return;\"\n");
    reads_as("    indented\n      more\n\n", "code: \"indented\n  more\"\n");
    reads_as("{align=center}\nCentred\n\n{align=right}\n## Right\n", "p @c: \"Centred\"\nh2 @r: \"Right\"\n");
    reads_as("- a\n\n  {align=center}\n  - b\n", "li0-b: \"a\"\nli1-b @c: \"b\"\n");
    reads_as("> {align=right}\n> quoted\n", "p q1 @r: \"quoted\"\n");
    // An attribute line followed by a blank line is a paragraph of its text.
    reads_as("{align=center}\n\ntext\n", "p: \"{align=center}\"\np: \"text\"\n");
    // Tabs: a tab after a list marker is content indentation.
    reads_as("-\tTabbed\n", "li0-b: \"Tabbed\"\n");
}

void inlines() {
    reads_as("*i* **b** ***bi*** _u_ __B__\n",
             "p: \"i\"i \" \" \"b\"b \" \" \"bi\"bi \" \" \"u\"i \" \" \"B\"b\n");
    reads_as("~~gone~~ ~not~\n", "p: \"gone\"s \" ~not~\"\n");
    reads_as("a `code` b `` a`b `` ` ``x`` `\n", "p: \"a \" \"code\"c \" b \" \"a`b\"c \" \" \"``x``\"c\n");
    reads_as("[link](https://example.com) [b](<a b> \"title\")\n",
             "p: \"link\"->https://example.com \" \" \"b\"->a b\n");
    // A tab ends a raw destination, so the writer brackets a target holding one.
    reads_as("[t](<a\tb>)\n", "p: \"t\"->a\tb\n");
    writes_as("[t](<a\tb>)\n", "[t](<a\tb>)\n");
    reads_as("<https://x.org/p> <me@example.com>\n",
             "p: \"https://x.org/p\"->https://x.org/p \" \" \"me@example.com\"->mailto:me@example.com\n");
    reads_as("hard\\\nbreak  \nagain\n", "p: \"hard\" / \"break\" / \"again\"\n");
    reads_as("\\*not\\* \\a &#65;&#x42; &#32;x\n", "p: \"*not* \\a AB  x\"\n");
    reads_as("[under]{u} [hi]{color=#C00000 bg=#00ff00} [x]{size=+2} [2]{sup}[n]{sub}\n",
             "p: \"under\"u \" \" \"hi\"#c00000%00ff00 \" \" \"x\"+2 \" \" \"2\"^ \"n\"_\n");
    reads_as("[a [b]{size=-1} c]{u size=+1}\n", "p: \"a \"u+1 \"b\"u-1 \" c\"u+1\n");
    reads_as("[**bold**]{u}\n", "p: \"bold\"bu\n");
    reads_as("[x]{b i s}\n", "p: \"x\"bis\n");
    reads_as("[[t]{u}](https://u)\n", "p: \"t\"u->https://u\n");
    reads_as("**`code`**\n", "p: \"code\"bc\n");
    // Not spans: unknown, repeated or contradictory items.
    reads_as("[a]{zz} [b]{u u} [c]{sup sub}\n", "p: \"[a]{zz} [b]{u u} [c]{sup sub}\"\n");
}

void outside_the_dialect() {
    reads_as("<div>raw</div>\n", "p: \"<div>raw</div>\"\n");
    require(loss_count(LossKind::html_kept_as_text) == 2, "two HTML tags counted");
    reads_as("![alt](img.png)\n", "p: \"![alt](img.png)\"\n");
    require(loss_count(LossKind::image_kept_as_text) == 1, "an image counted");
    reads_as("[ref]: https://x\n", "p: \"[ref]: https://x\"\n");
    require(loss_count(LossKind::reference_link_kept_as_text) == 1, "a definition counted");
    reads_as("| a | b |\n|---|---|\n", "p: \"| a | b | |---|---|\"\n");
    require(loss_count(LossKind::table_kept_as_text) == 1, "a table counted");
    reads_as("- [ ] task\n", "li0-b: \"[ ] task\"\n");
    require(loss_count(LossKind::task_marker_kept_as_text) == 1, "a task marker counted");
    reads_as("note[^1]\n", "p: \"note[^1]\"\n");
    require(loss_count(LossKind::footnote_kept_as_text) == 1, "a footnote counted");
    reads_as("&amp; &#0;\n", "p: \"&amp; &#0;\"\n");
    require(loss_count(LossKind::entity_kept_as_text) == 2, "two references counted");
    reads_as("***\n", "p: \"***\"\n");
    require(loss_count(LossKind::thematic_break_kept_as_text) == 1, "a thematic break counted");
    reads_as("```c\nx\n```\n", "code: \"x\"\n");
    require(loss_count(LossKind::code_language_dropped) == 1, "an info string counted");
    reads_as("[t](u \"title\")\n", "p: \"t\"->u\n");
    require(loss_count(LossKind::link_title_dropped) == 1, "a title counted");
}

void flattening() {
    reads_as("- a\n\n  second\n", "li0-b: \"a\"\np: \"second\"\n");
    require(loss_count(LossKind::structure_flattened) == 1, "a second paragraph counted");
    reads_as("- a\n\n  ```\n  code\n  ```\n", "li0-b: \"a\"\ncode: \"code\"\n");
    reads_as("- > quoted\n", "p: \"quoted\"\n");
    reads_as("- # heading\n", "h1: \"heading\"\n");
    reads_as("-\n\nafter\n", "p: \"after\"\n");
    require(loss_count(LossKind::empty_block_dropped) == 1, "an empty item counted");
    reads_as("#\n\nx\n", "p: \"x\"\n");
    require(loss_count(LossKind::empty_block_dropped) == 1, "an empty heading counted");
    // An empty outer item: its nested item becomes level 0.
    reads_as("- - x\n", "li0-b: \"x\"\n");
}

void canonical_form() {
    writes_as("Hello\n", "Hello\n");
    writes_as("# T\n\npara\n", "# T\n\npara\n");
    writes_as("- a\n  - b\n1. one\n", "- a\n\n  - b\n\n1. one\n");
    writes_as("10. ten\n    - nested\n", "10. ten\n\n    - nested\n");
    writes_as("> q\n", "> q\n");
    writes_as("{align=center}\nc\n", "{align=center}\nc\n");
    writes_as("__b__ _i_\n", "**b** *i*\n");
    writes_as("a\\\nb\n", "a\\\nb\n");
    writes_as("[x]{bg=#00FF00 u}\n", "[x]{u bg=#00ff00}\n");
    writes_as("&#32;lead and trail&#32;\n", "&#32;lead and trail&#32;\n");
    writes_as("\\# not a heading, 1\\. not a list\n", "\\# not a heading, 1. not a list\n");
    writes_as("1\\. at a line start\n", "1\\. at a line start\n");
    writes_as("a & b < c * d _ e\n", "a \\& b \\< c \\* d \\_ e\n");
    writes_as("```\nhas ``` inside\n```\n", "````\nhas ``` inside\n````\n");
    writes_as("> ```\n> a\n>\n> b\n> ```\n", "> ```\n> a\n>\n> b\n> ```\n");
    writes_as("[t](a(b))\n", "[t](<a(b)>)\n");
}

Document single(const std::string& text, const RunStyle& style) {
    Document document{};
    Block block{};
    block.run_count = 1;
    document.blocks.push_back(block);
    Run run{};
    run.length = stx_rtf2md::TextLength{static_cast<std::uint32_t>(text.size())};
    run.style = style;
    document.runs.push_back(run);
    document.text = text;
    return document;
}

void append_run(Document& document, const std::string& text, const RunStyle& style) {
    Run run{};
    run.start = stx_rtf2md::TextOffset{static_cast<std::uint32_t>(document.text.size())};
    run.length = stx_rtf2md::TextLength{static_cast<std::uint32_t>(text.size())};
    run.style = style;
    document.text += text;
    document.runs.push_back(run);
    document.blocks.back().run_count += 1U;
}

void document_round_trip(const Document& document, const char* message) {
    require(stx_rtf2md::check_normal_form(document).violation == stx_rtf2md::NormalFormViolation::none,
            std::string("fixture not in normal form: ") + message);
    const std::string canonical = write_ok(document);
    const Document again = read_ok(canonical);
    if (!(again == document)) {
        fail(std::string("read(write(d)) != d: ") + message + "\n  canonical:\n" + canonical + "  expected:\n"
             + dump(document) + "  actual:\n" + dump(again));
    }
}

void emphasis_fallback() {
    RunStyle bold{};
    bold.bold = true;
    // Bold "foo." then plain "bar" cannot be written **foo.**bar.
    Document document = single("foo.", bold);
    append_run(document, "bar", RunStyle{});
    document_round_trip(document, "bold ending in punctuation before a letter");
    const std::string text = write_ok(document);
    require(text == "[foo.]{b}bar\n", "the fallback spelling: " + text);
    // Emphasis around blanks.
    Document blank = single("a", RunStyle{});
    append_run(blank, " spaced ", bold);
    append_run(blank, "b", RunStyle{});
    document_round_trip(blank, "bold with edge blanks");
    // Adjacent differently emphasised runs.
    RunStyle italic{};
    italic.italic = true;
    RunStyle both{};
    both.bold = true;
    both.italic = true;
    Document mixed = single("a", bold);
    append_run(mixed, "b", both);
    append_run(mixed, "c", italic);
    append_run(mixed, "d", RunStyle{});
    document_round_trip(mixed, "bold, both, italic, plain");
    RunStyle strike{};
    strike.strikethrough = true;
    Document struck = single("x", strike);
    append_run(struck, "y", bold);
    document_round_trip(struck, "strike then bold");
}

void document_round_trips() {
    // Every style, a link spanning a line break, blanks everywhere.
    RunStyle styled{};
    styled.underline = true;
    styled.colour = Rgb{1, 2, 3};
    styled.background = Rgb{250, 251, 252};
    styled.size_step = -2;
    styled.superscript = true;
    Document document = single("  lead", RunStyle{});
    append_run(document, "styled", styled);
    RunStyle code{};
    code.code = true;
    code.bold = true;
    append_run(document, "` tick", code);
    document_round_trip(document, "styles and code");

    Document breaks{};
    Block block{};
    breaks.blocks.push_back(block);
    Run line_break{};
    line_break.kind = RunKind::line_break;
    breaks.runs.push_back(line_break);
    breaks.blocks.back().run_count = 1;
    append_run(breaks, "\tafter", RunStyle{});
    breaks.runs.push_back(line_break);
    breaks.runs.back().start = stx_rtf2md::TextOffset{static_cast<std::uint32_t>(breaks.text.size())};
    breaks.blocks.back().run_count += 1U;
    breaks.runs.push_back(line_break);
    breaks.runs.back().start = stx_rtf2md::TextOffset{static_cast<std::uint32_t>(breaks.text.size())};
    breaks.blocks.back().run_count += 1U;
    append_run(breaks, "end ", RunStyle{});
    document_round_trip(breaks, "leading and adjacent line breaks, edge blanks");

    const Document markers = read_ok("# Heading #1\n\n- -dash\n\n> > >quote\n\n2023. year\n");
    document_round_trip(markers, "text that looks like markup");
}

void failures() {
    Harness& h = harness();
    const std::string invalid = std::string("ok\n") + static_cast<char>(0xC3) + "(";
    Result result = h.reader.read(bytes(invalid), h.document, h.loss);
    require(result.status == Status::malformed && result.stop.value == 3, "invalid UTF-8 stops at its offset");
    require(h.document.blocks.empty(), "a failed read clears the document");

    Limits small{};
    small.input_bytes = 4;
    MarkdownReader limited(small);
    result = limited.read(bytes("too long"), h.document, h.loss);
    require(result.status == Status::limit_exceeded, "input limit");

    Limits tight{};
    tight.work_per_input_byte = 1;
    MarkdownReader starved(tight);
    result = starved.read(bytes("*a* *b* *c* *d* *e*\n"), h.document, h.loss);
    require(result.status == Status::effort_exceeded, "effort limit");

    const Document document = read_ok("Hello\n");
    std::vector<char> tiny(3);
    result = h.writer.write(document, std::span<char>(tiny.data(), tiny.size()));
    require(result.status == Status::output_too_small && result.required == 6 && result.produced == 0,
            "output too small reports the exact size");

    Document broken = document;
    broken.runs[0].length.value = 0;
    result = h.writer.write(broken, std::span<char>(h.output.data(), h.output.size()));
    require(result.status == Status::invalid_document, "a document outside normal form is refused");

    // Control characters are dropped and counted; CRLF and CR end lines.
    const std::string controls = std::string("a") + static_cast<char>(0x01) + "b\r\nc\rd";
    const Document cleaned = read_ok(controls);
    require(dump(cleaned) == "p: \"ab c d\"\n", "controls dropped, line endings read");
    require(h.loss.entry(LossKind::control_character_dropped).count == 1, "a control character counted");
    require(h.loss.entry(LossKind::control_character_dropped).first.value == 1, "at its input offset");
}

void idempotence_corpus() {
    const char* const corpus[] = {
        "*a **b** c*", "**a *b* c**", "*a*b*c*", "a*b*c", "**foo**bar", "__foo__bar", "_foo_bar_", "*foo**bar**baz*",
        "***a** b*", "*(**a**)*", "**a\\\nb**", "~~a **b** c~~", "[a *b](c)*", "*[a](b)*", "[*a*]{u}", "x [y]{u} z",
        "`a` *`b`*", "<https://a>*b*", "> *a*\n> **b**", "- *a*\n  - **b**", "1. a\n2. b\n\n3) c", "  lead\ttab",
        "a  \nb", "#hash", "x\n===\n", "- \\- not nested", "[]{u} [](x) ![](i)", "a &copy; &#65; &#xFFFF0;",
    };
    for (const char* const markdown : corpus) {
        const Document document = read_ok(markdown);
        const std::string canonical = write_ok(document);
        const Document again = read_ok(canonical);
        if (!(again == document)) {
            fail(std::string("read(write(read(m))) != read(m) for: ") + markdown + "\n  canonical:\n" + canonical
                 + "  first:\n" + dump(document) + "  second:\n" + dump(again));
        }
        require(write_ok(again) == canonical, std::string("not idempotent: ") + markdown);
    }
}

}

int main() {
    blocks();
    inlines();
    outside_the_dialect();
    flattening();
    canonical_form();
    emphasis_fallback();
    document_round_trips();
    failures();
    idempotence_corpus();
    return 0;
}
