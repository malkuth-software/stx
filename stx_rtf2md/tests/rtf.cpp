// RTF reader tests: each row of the stage plan's section 5, the loss
// report, every failure status, and RTF to markdown end to end. All RTF here
// is authored for this repository (PROVENANCE.md).
#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"
#include "stx_rtf2md/markdown.hpp"
#include "stx_rtf2md/rtf.hpp"

#include <cstddef>
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
using stx_rtf2md::MarkdownWriter;
using stx_rtf2md::Result;
using stx_rtf2md::RtfReader;
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

std::span<const std::byte> bytes(std::string_view text) {
    const std::span<const std::byte> span(reinterpret_cast<const std::byte*>(text.data()), text.size());
    return span;
}

void append_hex(std::string& out, std::uint8_t value) {
    const char* const digits = "0123456789abcdef";
    out.push_back(digits[value >> 4U]);
    out.push_back(digits[value & 0x0FU]);
}

// The markdown tests' dump format: one line per block, runs with flags.
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
    RtfReader reader{limits};
    MarkdownWriter writer{limits};
    Document document{};
    LossReport loss{};
};

Harness& harness() {
    static Harness instance{};
    return instance;
}

void reads_as(std::string_view rtf, std::string_view expected) {
    Harness& h = harness();
    const Result result = h.reader.read(bytes(rtf), h.document, h.loss);
    if (result.status != Status::ok) {
        fail("read failed (status " + std::to_string(static_cast<int>(result.status)) + " at "
             + std::to_string(result.stop.value) + ") for: " + std::string(rtf));
    }
    const std::string actual = dump(h.document);
    if (actual != expected) {
        fail("read of [" + std::string(rtf) + "]\n  expected:\n" + std::string(expected) + "  actual:\n" + actual);
    }
}

std::uint32_t loss_count(LossKind kind) {
    const std::uint32_t count = harness().loss.entry(kind).count;
    return count;
}

Status status_of(std::string_view rtf, std::uint64_t& stop) {
    Harness& h = harness();
    const Result result = h.reader.read(bytes(rtf), h.document, h.loss);
    stop = result.stop.value;
    require(result.status == Status::ok || h.document.blocks.empty(), "a failed read clears the document");
    return result.status;
}

void characters() {
    reads_as("{\\rtf1\\ansi Hello\\par}", "p: \"Hello\"\n");
    reads_as("{\\rtf1\\ansi {\\b bold} {\\i it} \\ul u\\ulnone  \\strike s\\strike0  \\super 2\\nosupersub  "
             "\\sub x\\nosupersub  \\b\\i both\\plain  plain\\par}",
             "p: \"bold\"b \" \" \"it\"i \" \" \"u\"u \" \" \"s\"s \" \" \"2\"^ \" \" \"x\"_ \" \" \"both\"bi \" plain\"\n");
    reads_as("{\\rtf1 \\uldb double\\ulnone\\par}", "p: \"double\"u\n");
    require(loss_count(LossKind::underline_style_collapsed) == 1, "an underline style collapsed");
    reads_as("{\\rtf1{\\colortbl;\\red255\\green0\\blue0;\\red0\\green255\\blue0;}"
             "{\\cf1 red} {\\highlight2 bg} {\\cb2\\cf1 both}\\par}",
             "p: \"red\"#ff0000 \" \" \"bg\"%00ff00 \" \" \"both\"#ff0000%00ff00\n");
    reads_as("{\\rtf1{\\fonttbl{\\f0\\fswiss Arial;}{\\f1\\fmodern\\fcharset0 Courier New;}}\\f0 say \\f1 code\\f0  "
             "now\\par}",
             "p: \"say \" \"code\"c \" now\"\n");
    // The default font is monospace: monospace marks nothing.
    reads_as("{\\rtf1\\deff1{\\fonttbl{\\f0\\fswiss Arial;}{\\f1 Consolas;}}typed\\par}", "p: \"typed\"\n");
    reads_as("{\\rtf1 seen{\\v hidden}\\par}", "p: \"seen\"\n");
}

// Size steps are relative to the document's body size (plan section 5).
void sizes() {
    Harness& h = harness();
    // Most characters are in 12 points, so that is the body.
    reads_as("{\\rtf1 \\fs48 big\\fs16  small\\fs24  and the body text\\par}",
             "p: \"big\"+3 \" small\"-2 \" and the body text\"\n");
    require(h.reader.last_body().rule == stx_rtf2md::BodyRule::most_common, "the most common size chose the body");
    // Windows Forms' RichTextBox writes its 8.5-point default: plain body text.
    reads_as("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Microsoft Sans Serif;}}\\viewkind4\\uc1\\pard\\f0\\fs17 "
             "plain text as the control writes it\\par \\fs20 bigger\\par}",
             "p: \"plain text as the control writes it\"\np: \"bigger\"+1\n");
    require(h.reader.last_body().half_points == 17, "an 8.5-point body");
    require(loss_count(LossKind::size_quantised) == 0, "20 at a 17 body reads as the canonical 28");
    // Word states an 11-point default in \defchp: plain body text.
    reads_as("{\\rtf1{\\*\\defchp \\f0\\fs22 }\\pard\\plain\\fs22 body text\\fs44  double\\par}",
             "p: \"body text\" \" double\"+3\n");
    require(h.reader.last_body().rule == stx_rtf2md::BodyRule::default_character_format, "defchp chose the body");
    require(h.reader.last_body().half_points == 22, "an 11-point body");
    // The Normal style's size, from an entry without \s.
    reads_as("{\\rtf1{\\stylesheet{\\fs20 Normal;}{\\s1 heading 1;}}\\fs20 x\\fs40 y\\par}", "p: \"x\" \"y\"+3\n");
    require(h.reader.last_body().rule == stx_rtf2md::BodyRule::normal_style, "the Normal style chose the body");
    reads_as("{\\rtf1}", "");
    require(h.reader.last_body().rule == stx_rtf2md::BodyRule::none, "no text, no rule");
}

void paragraphs() {
    reads_as("{\\rtf1 \\qc centred\\par\\pard\\qr right\\par\\pard\\qj just\\par}",
             "p @c: \"centred\"\np @r: \"right\"\np: \"just\"\n");
    require(loss_count(LossKind::justify_read_as_left) == 1, "justify read as left");
    reads_as("{\\rtf1 one\\line two\\tab three\\line\\par}", "p: \"one\" / \"two\tthree\"\n");
    reads_as("{\\rtf1 a\\par\\par b\\par}", "p: \"a\"\np: \"b\"\n");
    require(loss_count(LossKind::spacing_dropped) == 1, "an empty paragraph counted");
    reads_as("{\\rtf1 last paragraph without par}", "p: \"last paragraph without par\"\n");
    reads_as("{\\rtf1 \\li720\\fi-360 indented\\par}", "p: \"indented\"\n");
    require(loss_count(LossKind::indent_dropped) == 1, "an indent counted");
}

void headings_and_styles() {
    reads_as("{\\rtf1 \\outlinelevel0 Title\\par\\pard\\outlinelevel2 Third\\par}", "h1: \"Title\"\nh3: \"Third\"\n");
    reads_as("{\\rtf1{\\stylesheet{\\s0 Normal;}{\\s1\\fs32 heading 1;}{\\s2 Heading 2;}{\\s3 Quote;}{\\s4 Code;}}"
             "\\s1\\fs32 Big\\par\\pard\\plain\\s2 Sub\\par\\pard\\s3\\li1440 quoted\\par\\pard\\s4 int x;\\par\\s4\\par"
             "\\s4   return;\\par\\pard after\\par}",
             "h1: \"Big\"\nh2: \"Sub\"\np q2: \"quoted\"\ncode: \"int x;\n\n  return;\"\np: \"after\"\n");
    reads_as("{\\rtf1 \\outlinelevel0 A\\line B\\par}", "h1: \"A B\"\n");
}

void lists() {
    const std::string tables =
        "{\\*\\listtable{\\list\\listtemplateid1{\\listlevel\\levelnfc23{\\leveltext\\'01\\'95;}}"
        "{\\listlevel\\levelnfc0\\levelstartat3{\\leveltext\\'02\\'01.;}}\\listid10}"
        "{\\list{\\listlevel\\levelnfc0\\levelstartat1}\\listid20}}"
        "{\\*\\listoverridetable{\\listoverride\\listid10\\listoverridecount0\\ls1}"
        "{\\listoverride\\listid20\\listoverridecount0\\ls2}}";
    reads_as("{\\rtf1" + tables
                 + "\\ls1\\ilvl0{\\listtext\\'95\\tab}first\\par\\ls1\\ilvl1{\\listtext 3.\\tab}nested\\par"
                   "\\ls1\\ilvl1 again\\par\\ls1\\ilvl0 second\\par\\pard\\ls2 one\\par\\ls2 two\\par}",
             "li0-b: \"first\"\nli1-o3: \"nested\"\nli1-o4: \"again\"\nli0-b: \"second\"\nli0-o1: \"one\"\n"
             "li0-o2: \"two\"\n");
    reads_as("{\\rtf1{\\pntext\\f2\\'b7\\tab}{\\*\\pn\\pnlvlblt\\pnf2{\\pntxtb\\'b7}}old style\\par}",
             "li0-b: \"old style\"\n");
    reads_as("{\\rtf1{\\*\\pn\\pnlvlbody\\pndec\\pnstart5}five\\par}", "li0-o5: \"five\"\n");
    // A skipped level is clamped under its parent; level 2 of list 10 is not
    // defined, so it is a bullet.
    reads_as("{\\rtf1" + tables + "\\ls1\\ilvl2 deep\\par}", "li0-b: \"deep\"\n");
    require(loss_count(LossKind::structure_flattened) == 1, "a clamped level counted");
}

void fields() {
    reads_as("{\\rtf1 see {\\field{\\*\\fldinst{HYPERLINK \"https://example.com/a b\"}}{\\fldrslt{\\ul click}}} "
             "here\\par}",
             "p: \"see \" \"click\"u->https://example.com/a b \" here\"\n");
    reads_as("{\\rtf1 {\\field{\\*\\fldinst HYPERLINK \\\\l \"top\"}{\\fldrslt up}}\\par}", "p: \"up\"->#top\n");
    reads_as("{\\rtf1 page {\\field{\\*\\fldinst PAGE}{\\fldrslt 7}}\\par}", "p: \"page 7\"\n");
    require(loss_count(LossKind::field_result_kept) == 1, "a field result kept");
}

void characters_and_encoding() {
    reads_as("{\\rtf1 \\emdash\\endash\\bullet\\lquote\\rquote\\ldblquote\\rdblquote\\~x\\-y\\_z\\{\\}\\\\\\par}",
             "p: \"\xE2\x80\x94\xE2\x80\x93\xE2\x80\xA2\xE2\x80\x98\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x9D\xC2\xA0xy"
             "\xE2\x80\x91z{}\\\"\n");
    reads_as("{\\rtf1\\ansi\\ansicpg1252 caf\\'e9 \\'80\\par}", "p: \"caf\xC3\xA9 \xE2\x82\xAC\"\n");
    reads_as("{\\rtf1\\uc1 \\u8364?\\uc2\\u20320\\'c4\\'e3!\\par}", "p: \"\xE2\x82\xAC\xE4\xBD\xA0!\"\n");
    reads_as("{\\rtf1 \\u-10179?\\u-8704?\\par}", "p: \"\xF0\x9F\x98\x80\"\n");
    reads_as("{\\rtf1\\ansi\\ansicpg1251 \\u1087\\'ef\\par}", "p: \"\xD0\xBF\"\n");
    std::uint64_t stop = 0;
    require(status_of("{\\rtf1\\ansi\\ansicpg1251 \\'ef\\par}", stop) == Status::unsupported_encoding,
            "another code page is refused when a byte needs it");
    require(status_of("{\\rtf1{\\fonttbl{\\f1\\fcharset128 MS Mincho;}}\\f1 \\'82\\'a0\\par}", stop)
                == Status::unsupported_encoding,
            "a font's other charset is refused when a byte needs it");
    reads_as("{\\rtf1 a\\'01b\\par}", "p: \"ab\"\n");
    require(loss_count(LossKind::control_character_dropped) == 1, "a control character counted");
}

void skipped() {
    reads_as("{\\rtf1 a{\\pict\\pngblip 89504e47}b{\\object x}c{\\header head}d{\\*\\unknowndest x}e"
             "{\\info{\\title T}}f{\\*\\generator g}\\par}",
             "p: \"abcdef\"\n");
    require(loss_count(LossKind::picture_skipped) == 1, "a picture counted");
    require(loss_count(LossKind::object_skipped) == 1, "an object counted");
    require(loss_count(LossKind::destination_skipped) == 2, "a header and an unknown destination counted");
    // Three binary bytes that look like braces, then the picture's own close.
    reads_as("{\\rtf1 x{\\pict\\bin3 }}}}y\\par}", "p: \"xy\"\n");
    reads_as("{\\rtf1 x\\unknownword y\\par}", "p: \"xy\"\n");
    require(loss_count(LossKind::control_word_ignored) == 1, "an unknown word counted");
    reads_as("{\\rtf1\\trowd\\cellx1000\\cellx2000\\intbl a\\cell b\\cell\\row\\pard after\\par}",
             "p: \"a\"\np: \"b\"\np: \"after\"\n");
    require(loss_count(LossKind::table_flattened) == 1, "a table counted");
}

void failures() {
    std::uint64_t stop = 0;
    require(status_of("Hello", stop) == Status::malformed && stop == 0, "no header");
    require(status_of("{\\rtf1 open", stop) == Status::malformed && stop == 11, "unbalanced at the end");
    const std::string long_word = "{\\rtf1 \\" + std::string(33, 'a') + " x}";
    require(status_of(long_word, stop) == Status::malformed, "a 33-letter control word");
    require(status_of("{\\rtf1 \\fs99999999999 x}", stop) == Status::malformed, "an out-of-range parameter");
    require(status_of("{\\rtf1 \\bin100 x}", stop) == Status::malformed, "\\bin past the end");
    require(status_of("{\\rtf1 \\'4", stop) == Status::malformed, "a cut hexadecimal byte");
    Limits shallow{};
    shallow.group_depth = 4;
    RtfReader limited(shallow);
    Document document{};
    LossReport loss{};
    Result result = limited.read(bytes("{\\rtf1{{{{{x}}}}}}"), document, loss);
    require(result.status == Status::limit_exceeded, "group depth");
    Limits tight{};
    tight.work_per_input_byte = 1;
    RtfReader starved(tight);
    result = starved.read(bytes("{\\rtf1 \\b\\i\\ul\\strike text\\par}"), document, loss);
    require(result.status == Status::effort_exceeded, "effort");
    Limits small{};
    small.input_bytes = 8;
    RtfReader capped(small);
    result = capped.read(bytes("{\\rtf1 too long}"), document, loss);
    require(result.status == Status::limit_exceeded, "input limit");
    reads_as("{\\rtf1 done}trailing bytes", "p: \"done\"\n");
}

// RTF as WordPad, Word and TextEdit commonly write it, read and written
// as markdown.
void end_to_end() {
    Harness& h = harness();
    const std::string_view word =
        "{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat{\\fonttbl{\\f0\\fnil\\fcharset0 Calibri;}"
        "{\\f1\\fmodern\\fcharset0 Consolas;}}{\\colortbl ;\\red192\\green0\\blue0;}"
        "{\\*\\generator Riched20 10.0.22621}\\viewkind4\\uc1\\pard\\sa200\\sl276\\slmult1\\qc\\b\\f0\\fs32\\lang9 "
        "Report\\b0\\par\\pard\\sa200\\sl276\\slmult1\\fs22 Some {\\i emphasis}, \\cf1 red\\cf0  and "
        "{\\f1 code()}.\\par}";
    const Result read = h.reader.read(bytes(word), h.document, h.loss);
    require(read.status == Status::ok, "the Word-like document reads");
    std::vector<char> output(1024);
    const Result written = h.writer.write(h.document, std::span<char>(output.data(), output.size()));
    require(written.status == Status::ok, "and writes");
    const std::string markdown(output.data(), written.produced);
    const std::string expected =
        "{align=center}\n[**Report**]{size=+2}\n\nSome *emphasis*, [red]{color=#c00000} and `code()`.\n";
    if (markdown != expected) {
        fail("end to end:\n  expected:\n" + expected + "  actual:\n" + markdown);
    }
}

}

// Rule 2: read_rtf(write_rtf(d)) equals d for every document in normal form.
void rtf_round_trip(const Document& document, const std::string& label) {
    Harness& h = harness();
    static stx_rtf2md::RtfWriter writer{Limits{}};
    std::vector<std::byte> output(1U << 20U);
    const Result written = writer.write(document, std::span<std::byte>(output.data(), output.size()));
    if (written.status != Status::ok) {
        fail("write_rtf failed for " + label);
    }
    Document reread{};
    LossReport loss{};
    const Result read = h.reader.read(std::span<const std::byte>(output.data(), written.produced), reread, loss);
    if (read.status != Status::ok) {
        fail("write_rtf output does not read for " + label + " (status " + std::to_string(static_cast<int>(read.status))
             + ")");
    }
    if (!(reread == document)) {
        const std::string rtf(reinterpret_cast<const char*>(output.data()), written.produced);
        fail("read_rtf(write_rtf(d)) != d for " + label + "\n  expected:\n" + dump(document) + "  actual:\n"
             + dump(reread) + "  rtf:\n" + rtf);
    }
}

void round_trips() {
    const Limits limits{};
    stx_rtf2md::MarkdownReader markdown(limits);
    const char* const sources[] = {
        "# Title\n\n## Sub\n\n> quoted *text*\n>\n> > deeper\n",
        "- a\n\n  - b\n\n    - c\n\n7. seven\n\n9. nine\n\n> 3. quoted ordered\n",
        "```\nint x;\n\n  return;\n```\n\n> ```\n> quoted code\n> ```\n\n```\nsecond\n```\n",
        "{align=center}\ncentred\n\n{align=right}\n## right heading\n\n> # quoted heading\n",
        "**b** *i* ~~s~~ `c` [u]{u} [x]{sup}[y]{sub} [c]{color=#123456 bg=#abcdef} [big]{size=+4} [small]{size=-2}\n",
        "[link](https://example.com/a \"t\") [q](<a\"b\\\\c>) [[spanned]{u}](x) **`bold code`**\n",
        "a\\\n\\\nb with \t tab and caf\xC3\xA9 \xF0\x9F\x98\x80 and \\{braces\\} \\\\ back\n",
        "&#32;edge blanks&#32;\n\n[multi\\\nline link](u)\n",
    };
    for (const char* const source : sources) {
        Document document{};
        LossReport loss{};
        const std::string_view text(source);
        const Result read = markdown.read(std::span<const char>(text.data(), text.size()), document, loss);
        require(read.status == Status::ok, std::string("fixture reads: ") + source);
        rtf_round_trip(document, source);
    }
    // Documents read from RTF, including Windows Forms' 8.5-point text and
    // Word's 11-point text, which read as plain body text.
    Harness& h = harness();
    const char* const rtf_sources[] = {
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil Microsoft Sans Serif;}}\\pard\\f0\\fs17 plain body text written by "
        "the control\\par {\\b bold} and \\fs20 larger\\par}",
        "{\\rtf1{\\*\\defchp \\fs22 }\\pard\\plain\\fs22 eleven point body {\\i text}\\fs44  double\\par}",
    };
    for (const char* const rtf : rtf_sources) {
        const Result read = h.reader.read(bytes(rtf), h.document, h.loss);
        require(read.status == Status::ok, std::string("RTF fixture reads: ") + rtf);
        const Document document = h.document;
        require(document.runs[0].style.size_step == 0, std::string("plain body text: ") + rtf);
        rtf_round_trip(document, rtf);
    }
}

int main() {
    round_trips();
    characters();
    sizes();
    paragraphs();
    headings_and_styles();
    lists();
    fields();
    characters_and_encoding();
    skipped();
    failures();
    end_to_end();
    return 0;
}
