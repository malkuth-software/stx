#pragma once

#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"

#include <memory>
#include <span>

namespace stx_rtf2md {

// Reads dialect text (README.md, "The dialect") into a Document in normal
// form. The reader owns reusable workspaces that grow to the largest input
// seen and keep their capacity, so one reader serves many calls. One
// execution context per reader; no callbacks and no retained borrows of the
// input or the document after a call returns.
class MarkdownReader final {
public:
    explicit MarkdownReader(const Limits& limits);
    ~MarkdownReader();
    MarkdownReader(const MarkdownReader&) = delete;
    MarkdownReader& operator=(const MarkdownReader&) = delete;
    MarkdownReader(MarkdownReader&&) = delete;
    MarkdownReader& operator=(MarkdownReader&&) = delete;

    // Clears document and loss, then reads text. On ok the document is in
    // normal form and loss counts what was kept as text, flattened or
    // dropped. On any other status the document is cleared and loss is
    // unspecified. Invalid UTF-8 is malformed at its offset. Allocation
    // failure is reported as limit_exceeded with the document cleared.
    [[nodiscard]] Result read(std::span<const char> text, Document& document, LossReport& loss);

    struct Workspace;

private:
    Limits limits_;
    std::unique_ptr<Workspace> workspace_;
};

// Writes a Document in normal form as the dialect's canonical form. The
// writer owns a reusable assembly buffer and a MarkdownReader, which checks
// each block's emphasis (README.md, "Emphasis fallback").
class MarkdownWriter final {
public:
    explicit MarkdownWriter(const Limits& limits);
    ~MarkdownWriter();
    MarkdownWriter(const MarkdownWriter&) = delete;
    MarkdownWriter& operator=(const MarkdownWriter&) = delete;
    MarkdownWriter(MarkdownWriter&&) = delete;
    MarkdownWriter& operator=(MarkdownWriter&&) = delete;

    // Writes document into output. A document not in normal form is
    // invalid_document. When output is shorter than the canonical text the
    // result is output_too_small with required set to the exact length; the
    // writer is deterministic, so a second call with that much storage
    // succeeds. On ok, produced bytes of output hold the text; on any other
    // status produced is 0 and output's contents are unspecified. Output
    // must not overlap the document's storage.
    [[nodiscard]] Result write(const Document& document, std::span<char> output);

    struct Workspace;

private:
    Limits limits_;
    std::unique_ptr<Workspace> workspace_;
};

}
