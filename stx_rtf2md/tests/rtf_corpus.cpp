// The first-party RTF corpus (tests/corpus, PROVENANCE.md): each file must
// convert to its reviewed markdown, and a malformed sweep over every file
// (every prefix, and every byte replaced by a few hostile values) must
// return a status without fault, leaving the document cleared on failure and
// in normal form on success. Every corpus document must also survive
// write_rtf then read_rtf unchanged (rule 2). The corpus directory is the
// first argument.
#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"
#include "stx_rtf2md/markdown.hpp"
#include "stx_rtf2md/rtf.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

using stx_rtf2md::Document;
using stx_rtf2md::Limits;
using stx_rtf2md::LossReport;
using stx_rtf2md::MarkdownWriter;
using stx_rtf2md::NormalFormViolation;
using stx_rtf2md::Result;
using stx_rtf2md::RtfReader;
using stx_rtf2md::RtfWriter;
using stx_rtf2md::Status;

void fail(const std::string& message) {
    std::fprintf(stderr, "stx_rtf2md: %s\n", message.c_str());
    std::abort();
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail("cannot open " + path);
    }
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return contents;
}

std::span<const std::byte> bytes(const std::string& text) {
    const std::span<const std::byte> span(reinterpret_cast<const std::byte*>(text.data()), text.size());
    return span;
}

struct Harness final {
    Limits limits{};
    RtfReader reader{limits};
    MarkdownWriter writer{limits};
    Document document{};
    LossReport loss{};
    std::vector<char> output = std::vector<char>(1U << 20U);
    RtfWriter rtf_writer{limits};
    std::vector<std::byte> rtf = std::vector<std::byte>(1U << 20U);
    Document reread{};
};

void check_any(Harness& h, const std::string& input, const std::string& label) {
    const Result result = h.reader.read(bytes(input), h.document, h.loss);
    if (result.status != Status::ok) {
        if (!h.document.blocks.empty() || !h.document.text.empty()) {
            fail("a failed read left content: " + label);
        }
        return;
    }
    if (stx_rtf2md::check_normal_form(h.document).violation != NormalFormViolation::none) {
        fail("an accepted read is not in normal form: " + label);
    }
}

void converts(Harness& h, const std::string& directory, const std::string& name) {
    const std::string rtf = read_file(directory + "/" + name + ".rtf");
    std::string expected = read_file(directory + "/" + name + ".md");
    // Checkouts may convert the golden's line endings; the writer emits LF.
    std::string normalised;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (expected[index] != '\r') {
            normalised.push_back(expected[index]);
        }
    }
    const Result read = h.reader.read(bytes(rtf), h.document, h.loss);
    if (read.status != Status::ok) {
        fail(name + ".rtf does not read (status " + std::to_string(static_cast<int>(read.status)) + ")");
    }
    const Result written = h.writer.write(h.document, std::span<char>(h.output.data(), h.output.size()));
    if (written.status != Status::ok) {
        fail(name + ".rtf does not write");
    }
    const std::string markdown(h.output.data(), written.produced);
    if (markdown != normalised) {
        fail(name + ".rtf converts to:\n" + markdown + "\nexpected:\n" + normalised);
    }
    const Result rtf_written = h.rtf_writer.write(h.document, std::span<std::byte>(h.rtf.data(), h.rtf.size()));
    if (rtf_written.status != Status::ok) {
        fail(name + ".rtf does not write as RTF");
    }
    const Result reread = h.reader.read(std::span<const std::byte>(h.rtf.data(), rtf_written.produced), h.reread, h.loss);
    if (reread.status != Status::ok || !(h.reread == h.document)) {
        fail(name + ".rtf does not survive write_rtf then read_rtf");
    }
}

void sweep(Harness& h, const std::string& directory, const std::string& name) {
    const std::string rtf = read_file(directory + "/" + name + ".rtf");
    for (std::size_t length = 0; length <= rtf.size(); ++length) {
        check_any(h, rtf.substr(0, length), name + " prefix " + std::to_string(length));
    }
    const char replacements[] = {'{', '}', '\\', static_cast<char>(0xFF)};
    std::string changed = rtf;
    for (std::size_t offset = 0; offset < rtf.size(); ++offset) {
        for (const char replacement : replacements) {
            changed[offset] = replacement;
            check_any(h, changed, name + " byte " + std::to_string(offset));
        }
        changed[offset] = rtf[offset];
    }
}

}

int main(int count, char** values) {
    if (count < 2) {
        fail("usage: stx_rtf2md_rtf_corpus_tests <corpus directory>");
    }
    const std::string directory = values[1];
    static Harness harness{};
    const char* const names[] = {"wordpad", "word", "textedit", "richtextbox"};
    for (const char* const name : names) {
        converts(harness, directory, name);
    }
    // The Windows Forms and Word documents read with plain body text.
    const std::string richtextbox = read_file(directory + "/richtextbox.rtf");
    Result result = harness.reader.read(bytes(richtextbox), harness.document, harness.loss);
    if (result.status != Status::ok || harness.reader.last_body().half_points != 17) {
        fail("the RichTextBox body is its 8.5-point default");
    }
    const std::string word = read_file(directory + "/word.rtf");
    result = harness.reader.read(bytes(word), harness.document, harness.loss);
    if (result.status != Status::ok || harness.reader.last_body().rule != stx_rtf2md::BodyRule::default_character_format) {
        fail("the Word body comes from \\defchp");
    }
    for (const char* const name : names) {
        sweep(harness, directory, name);
    }
    return 0;
}
