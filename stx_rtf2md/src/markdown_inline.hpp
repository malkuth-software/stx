#pragma once

// Private: the markdown reader's inline parser and the link-target store
// shared by the reader's blocks. Not installed.

#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"
#include "text_scan.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stx_rtf2md::detail {

// Where a line of inline content came from: content offsets at or after
// content_start, up to the next origin, map to source_start onward.
struct ContentOrigin final {
    std::size_t content_start = 0;
    std::uint64_t source_start = 0;
};

// Link targets in first-use order across one read. The reader appends them
// to Document::text after every run, which is the normal form's layout.
class LinkTargets final {
public:
    void clear() noexcept;
    // Stores a target and returns its index in the document's link table.
    [[nodiscard]] std::uint32_t add(std::string_view target);
    [[nodiscard]] std::size_t count() const noexcept;
    // Appends every target to document.text and fills document.links.
    void append_to(Document& document) const;

private:
    struct Entry final {
        std::size_t start = 0;
        std::size_t length = 0;
    };
    std::string text_;
    std::vector<Entry> entries_;
};

enum class InlineStatus : std::uint8_t { ok, effort_exceeded, limit_exceeded };

struct InlineRequest final {
    // One block's content: lines joined by line feeds, each line's leading
    // blanks removed and the last line's trailing blanks removed.
    std::string_view content;
    std::span<const ContentOrigin> origins;
    BlockKind kind = BlockKind::paragraph;
    // The block is a list item's first paragraph (task-marker detection).
    bool list_item = false;
};

// Parses CommonMark inlines as the dialect restricts them (README.md) and
// appends one block's runs to a document. Owns reusable scratch storage.
class InlineParser final {
public:
    // Appends runs (and their text) to document; the caller has opened the
    // block, whose first_run is document.runs.size() before the call. On a
    // status other than ok the document is left with partial runs; the
    // caller clears it.
    [[nodiscard]] InlineStatus parse(const InlineRequest& request, Document& document, LinkTargets& links,
                                     LossReport& loss, EffortMeter& effort, const Limits& limits);

private:
    enum class NodeKind : std::uint8_t { text, code, line_break, delimiter, bracket, dead };
    struct Node final {
        NodeKind kind = NodeKind::text;
        std::size_t text_start = 0;          // into node_text_
        std::size_t text_length = 0;
        std::uint32_t remaining = 0;         // delimiter characters not yet matched
        std::size_t skip_to = 0;             // a literal construct: first node after it, else 0
        // Closed constructs (autolinks, the last node of a link or span,
        // literals) never absorb later text.
        bool sealed = false;
    };
    struct Delimiter final {
        std::size_t node = 0;
        char character = '*';
        bool can_open = false;
        bool can_close = false;
        std::uint32_t original = 0;
        std::ptrdiff_t previous = -1;
        std::ptrdiff_t next = -1;
    };
    struct Bracket final {
        std::size_t node = 0;
        bool image = false;
        std::size_t content_start = 0;       // content offset after "[" or "!["
        std::ptrdiff_t delimiter_bottom = -1;
    };
    struct LinkRange final {
        std::size_t first_node = 0;
        std::size_t end_node = 0;
        std::size_t target_start = 0;        // into target_text_
        std::size_t target_length = 0;
    };
    enum class Position : std::uint8_t { none, superscript, subscript };
    struct SpanAttributes final {
        bool underline = false;
        bool bold = false;
        bool italic = false;
        bool strikethrough = false;
        Position position = Position::none;
        std::optional<Rgb> colour;
        std::optional<Rgb> background;
        std::int8_t size_step = 0;
    };
    struct SpanRange final {
        std::size_t first_node = 0;
        std::size_t end_node = 0;
        SpanAttributes attributes{};
    };
    struct BacktickRun final {
        std::size_t length = 0;
        std::size_t position = 0;
    };
    struct BacktickOrder final {
        bool operator()(const BacktickRun& left, const BacktickRun& right) const noexcept;
    };
    struct SpanOrder final {
        bool operator()(const SpanRange& left, const SpanRange& right) const noexcept;
    };
    struct ScanMemo final {
        bool valid = false;
        std::size_t from = 0;
        std::size_t result = 0;              // npos when none
    };
    struct LinkParse final {
        bool ok = false;
        bool has_title = false;
        std::size_t end = 0;
    };
    struct AttributeParse final {
        bool ok = false;
        std::size_t end = 0;
        SpanAttributes attributes{};
    };

    // Scanning
    void reset(const InlineRequest& request);
    bool scan();
    void collect_backtick_runs();
    std::size_t find_backtick_closer(std::size_t length, std::size_t after) const;
    void append_text(std::string_view text);
    void append_node(NodeKind kind, std::string_view text);
    std::size_t scan_plain(std::size_t position);
    std::size_t scan_backslash(std::size_t position);
    std::size_t scan_line_end(std::size_t position);
    std::size_t scan_code(std::size_t position);
    std::size_t scan_delimiter_run(std::size_t position);
    std::size_t scan_autolink_or_html(std::size_t position);
    std::size_t scan_entity(std::size_t position, std::string& target, bool count_loss);
    std::size_t close_bracket(std::size_t position);
    LinkParse parse_inline_link(std::size_t position);
    AttributeParse parse_attributes(std::size_t position) const;
    std::size_t next_unescaped(char character, std::size_t from, ScanMemo& memo);
    void make_literal(const Bracket& bracket, std::size_t source_start, std::size_t source_end);
    void record_loss(LossKind kind, std::size_t content_offset);

    // Delimiters and emphasis
    void push_delimiter(const Delimiter& delimiter);
    void remove_delimiter(std::ptrdiff_t index);
    void remove_delimiters_above(std::ptrdiff_t bottom);
    std::ptrdiff_t first_above(std::ptrdiff_t bottom) const;
    bool process_emphasis(std::ptrdiff_t bottom);

    // Output
    InlineStatus flatten(Document& document, LinkTargets& links, const Limits& limits);
    InlineStatus append_run(Document& document, RunKind kind, std::string_view text, const RunStyle& style,
                            const Limits& limits);

    InlineRequest request_{};
    std::string_view content_;
    // Borrowed from the caller for the duration of one parse call only;
    // never dereferenced after parse returns.
    Document* document_ = nullptr;
    LossReport* loss_ = nullptr;
    EffortMeter* effort_ = nullptr;
    std::size_t block_first_run_ = 0;
    std::vector<Node> nodes_;
    std::string node_text_;
    std::vector<Delimiter> delimiters_;
    std::ptrdiff_t delimiter_head_ = -1;
    std::ptrdiff_t delimiter_top_ = -1;
    std::vector<Bracket> brackets_;
    std::size_t link_floor_ = 0;             // bracket nodes below it cannot open links
    std::vector<LinkRange> link_ranges_;
    std::string target_text_;
    std::vector<SpanRange> span_ranges_;
    std::vector<std::int32_t> bold_delta_;
    std::vector<std::int32_t> italic_delta_;
    std::vector<std::int32_t> strike_delta_;
    std::vector<BacktickRun> backtick_runs_;
    std::string scratch_;
    ScanMemo double_quote_memo_{};
    ScanMemo single_quote_memo_{};
    ScanMemo parenthesis_memo_{};
    std::vector<SpanAttributes> span_stack_;
    std::vector<std::size_t> span_end_stack_;
    std::vector<std::int64_t> link_document_index_;
};

}
