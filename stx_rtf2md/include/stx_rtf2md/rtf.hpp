#pragma once

#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

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

}
