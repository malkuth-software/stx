#pragma once

#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace stx_rtf2md {

// Which rule chose a document's body size (stage plan section 5).
enum class BodyRule : std::uint8_t {
    none,                       // no sized text; the 12-point default
    default_character_format,   // {\*\defchp \fsN}
    normal_style,               // the Normal style's \fs
    most_common,                // the size covering the most characters
};

// The body size a read chose, in half-points, and the rule that chose it.
// Size steps are relative to it, so an import can explain its sizes.
struct BodySize final {
    std::int32_t half_points = 24;
    BodyRule rule = BodyRule::none;
};

// Reads the RTF 1.9.1 subset of the stage plan (section 5) into a Document
// in normal form. The reader owns a group-state stack and tables reserved
// from Limits at construction and reused across calls. One execution context
// per reader; no callbacks and no retained borrows after a call returns.
class RtfReader final {
public:
    explicit RtfReader(const Limits& limits);
    ~RtfReader();
    RtfReader(const RtfReader&) = delete;
    RtfReader& operator=(const RtfReader&) = delete;
    RtfReader(RtfReader&&) = delete;
    RtfReader& operator=(RtfReader&&) = delete;

    // Clears document and loss, then reads rtf. On ok the document is in
    // normal form and loss counts what the subset lacked. On any other
    // status the document is cleared and stop names the offset where reading
    // stopped: malformed for broken syntax (no "{\rtf" header, unbalanced
    // groups, an over-long control word or an out-of-range parameter),
    // unsupported_encoding for a byte above 0x7F that needs a code page other
    // than 1252, limit_exceeded or effort_exceeded for a bound. Allocation
    // failure is reported as limit_exceeded with the document cleared.
    [[nodiscard]] Result read(std::span<const std::byte> rtf, Document& document, LossReport& loss);

    // The body size chosen by the last successful read.
    [[nodiscard]] BodySize last_body() const noexcept;

    struct Workspace;

private:
    Limits limits_;
    std::unique_ptr<Workspace> workspace_;
};

// Font names written into the font table. The code font must be one the
// reader recognises as monospace (plan section 5) for code to round-trip.
struct RtfWriteOptions final {
    std::string_view body_font = "Calibri";
    std::string_view code_font = "Consolas";
};

// Writes a Document in normal form as canonical RTF (plan section 6): ASCII
// with \uN for other characters, a 12-point body declared in \*\defchp,
// heading, Quote and Code styles, list tables, HYPERLINK fields, and one
// ignorable {\*\stxblock ...} group per paragraph carrying the block's exact
// fields, so RtfReader reads the document back unchanged (rule 2). The
// writer owns a reusable assembly buffer.
class RtfWriter final {
public:
    explicit RtfWriter(const Limits& limits, const RtfWriteOptions& options = RtfWriteOptions{});
    ~RtfWriter();
    RtfWriter(const RtfWriter&) = delete;
    RtfWriter& operator=(const RtfWriter&) = delete;
    RtfWriter(RtfWriter&&) = delete;
    RtfWriter& operator=(RtfWriter&&) = delete;

    // Writes document into output: invalid_document outside normal form,
    // output_too_small with the exact size required, ok with produced bytes.
    // On any status other than ok, produced is 0 and output is unspecified.
    [[nodiscard]] Result write(const Document& document, std::span<std::byte> output);

    struct Workspace;

private:
    Limits limits_;
    RtfWriteOptions options_;
    std::unique_ptr<Workspace> workspace_;
};

}
