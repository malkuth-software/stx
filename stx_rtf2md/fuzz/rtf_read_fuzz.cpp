// stx_rtf2md RTF reader fuzzer: any bytes to RtfReader. A failed read must
// leave the document cleared; an accepted document must be in normal form,
// write as markdown, and re-read from that markdown unchanged (rule 1 of the
// stage plan; rule 2 joins when write_rtf exists). Any contract break
// aborts. A build check for CI smoke runs, not a certificate (owner revision
// of decision 6A).
#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"
#include "stx_rtf2md/markdown.hpp"
#include "stx_rtf2md/rtf.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace stx_rtf2md;

constexpr std::size_t input_limit = 65536;

struct Workspace final {
    Limits limits{};
    RtfReader reader{limits};
    MarkdownReader markdown_reader{limits};
    MarkdownWriter writer{limits};
    Document document{};
    Document reread{};
    LossReport loss{};
    std::vector<char> markdown = std::vector<char>(input_limit * 64U);
};

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > input_limit) {
        return 0;
    }
    static Workspace* workspace = new Workspace();
    const std::span<const std::byte> rtf(reinterpret_cast<const std::byte*>(data), size);
    const Result result = (*workspace).reader.read(rtf, (*workspace).document, (*workspace).loss);
    if (result.status != Status::ok) {
        require((*workspace).document.blocks.empty() && (*workspace).document.text.empty());
        return 0;
    }
    require(check_normal_form((*workspace).document).violation == NormalFormViolation::none);
    const Result written = (*workspace).writer.write((*workspace).document, std::span<char>((*workspace).markdown));
    require(written.status == Status::ok);
    const std::span<const char> text((*workspace).markdown.data(), written.produced);
    const Result reread = (*workspace).markdown_reader.read(text, (*workspace).reread, (*workspace).loss);
    require(reread.status == Status::ok);
    require((*workspace).reread == (*workspace).document);
    return 0;
}

#if defined(STX_RTF2MD_FUZZ_STANDALONE)
int main() {
    // Fixed inputs exercising the subset, then every prefix of the longest.
    const char* const inputs[] = {
        "{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}{\\f1\\fmodern Courier New;}}{\\colortbl;\\red1\\green2\\blue3;}"
        "\\qc\\b Title\\b0\\par\\pard{\\*\\listtable{\\list{\\listlevel\\levelnfc0}\\listid1}}"
        "{\\*\\listoverridetable{\\listoverride\\listid1\\ls1}}\\ls1 item\\par\\pard{\\field{\\*\\fldinst HYPERLINK "
        "\"u\"}{\\fldrslt link}} \\cf1\\f1 code\\line next\\u8364?\\par}",
        "{\\rtf1{\\stylesheet{\\s1 heading 1;}{\\s2 Code;}{\\s3 Quote;}}\\s1 H\\par\\s2 a\\par\\s2 b\\par\\s3\\li720 q}",
        "{\\rtf1 {\\pict\\bin2 }}}x{\\*\\unknown y}\\trowd\\intbl c\\cell\\row}",
    };
    for (const char* const input : inputs) {
        const std::string text(input);
        for (std::size_t length = 0; length <= text.size(); ++length) {
            LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(text.data()), length);
        }
    }
    return 0;
}
#endif
