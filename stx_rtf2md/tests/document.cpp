// Document model and normal-form tests: a valid document passes, and each
// rule is broken once, from a valid starting point, to show that the checker
// reports exactly that violation at the stated index.
#include "stx_rtf2md/document.hpp"
#include "stx_rtf2md/limits.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using stx_rtf2md::Alignment;
using stx_rtf2md::Block;
using stx_rtf2md::BlockKind;
using stx_rtf2md::Document;
using stx_rtf2md::Link;
using stx_rtf2md::LinkIndex;
using stx_rtf2md::ListMarker;
using stx_rtf2md::NormalFormCheck;
using stx_rtf2md::NormalFormViolation;
using stx_rtf2md::Run;
using stx_rtf2md::RunKind;
using stx_rtf2md::RunStyle;

void require(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::fprintf(stderr, "stx_rtf2md: %s\n", message);
    std::abort();
}

// Builds documents in the canonical layout: run texts are appended as they
// are added, link targets are kept aside and appended by finish.
class DocumentBuilder final {
public:
    void begin_block(BlockKind kind, std::uint8_t level) {
        Block block{};
        block.kind = kind;
        block.level = level;
        block.first_run = stx_rtf2md::RunIndex{static_cast<std::uint32_t>(document_.runs.size())};
        document_.blocks.push_back(block);
    }

    Block& last_block() {
        Block& block = document_.blocks.back();
        return block;
    }

    void add_text(const std::string& text, const RunStyle& style) {
        Run run{};
        run.kind = RunKind::text;
        run.start = stx_rtf2md::TextOffset{static_cast<std::uint32_t>(document_.text.size())};
        run.length = stx_rtf2md::TextLength{static_cast<std::uint32_t>(text.size())};
        run.style = style;
        document_.text += text;
        document_.runs.push_back(run);
        document_.blocks.back().run_count += 1U;
    }

    void add_line_break() {
        Run run{};
        run.kind = RunKind::line_break;
        run.start = stx_rtf2md::TextOffset{static_cast<std::uint32_t>(document_.text.size())};
        document_.runs.push_back(run);
        document_.blocks.back().run_count += 1U;
    }

    LinkIndex add_link(const std::string& target) {
        const LinkIndex index{static_cast<std::uint32_t>(targets_.size())};
        targets_.push_back(target);
        return index;
    }

    Document finish() {
        for (std::size_t index = 0; index < targets_.size(); ++index) {
            Link link{};
            link.target_start = stx_rtf2md::TextOffset{static_cast<std::uint32_t>(document_.text.size())};
            link.target_length = stx_rtf2md::TextLength{static_cast<std::uint32_t>(targets_[index].size())};
            document_.text += targets_[index];
            document_.links.push_back(link);
        }
        Document result = document_;
        return result;
    }

private:
    Document document_{};
    std::vector<std::string> targets_;
};

RunStyle bold_style() {
    RunStyle style{};
    style.bold = true;
    return style;
}

RunStyle linked(LinkIndex link) {
    RunStyle style{};
    style.link = link;
    return style;
}

// A heading, a paragraph with a link and a line break, an ordered list item
// inside a quote and a code block: every block kind and most run features.
Document sample() {
    DocumentBuilder builder{};
    builder.begin_block(BlockKind::heading, 2);
    builder.add_text("Title", RunStyle{});
    builder.begin_block(BlockKind::paragraph, 0);
    builder.last_block().alignment = Alignment::center;
    builder.add_text(" lead ", RunStyle{});
    builder.add_text("bold", bold_style());
    const LinkIndex link = builder.add_link("https://example.com/a b");
    builder.add_text("link", linked(link));
    builder.add_line_break();
    builder.add_text("next\tline ", RunStyle{});
    builder.begin_block(BlockKind::list_item, 0);
    builder.last_block().marker = ListMarker::ordered;
    builder.last_block().ordinal = 3;
    builder.last_block().quote_depth = 2;
    builder.add_text("caf\xC3\xA9 \xF0\x9F\x99\x82", RunStyle{});
    builder.begin_block(BlockKind::code_block, 0);
    builder.add_text("int x;\n\n  return;", RunStyle{});
    Document document = builder.finish();
    return document;
}

void expect(const Document& document, NormalFormViolation violation, std::uint64_t index, const char* message) {
    const NormalFormCheck check = stx_rtf2md::check_normal_form(document);
    if (check.violation != violation || check.index != index) {
        std::fprintf(stderr, "stx_rtf2md: %s (got violation %d at %llu)\n", message,
                     static_cast<int>(check.violation), static_cast<unsigned long long>(check.index));
        std::abort();
    }
}

void valid_documents() {
    const Document empty{};
    expect(empty, NormalFormViolation::none, 0, "an empty document is in normal form");
    const Document document = sample();
    expect(document, NormalFormViolation::none, 0, "the sample is in normal form");
    // Blanks at block edges are kept: the paragraph starts with a space and
    // ends with one, and the code block keeps its indentation.
    require(document.text.substr(5, 6) == " lead ", "edge blanks are text");
    Document copy = document;
    require(copy == document, "a copy compares equal");
    copy.clear();
    require(copy.text.empty() && copy.blocks.empty() && copy.runs.empty() && copy.links.empty(),
            "clear ends every element");
    require(copy.runs.capacity() >= document.runs.size(), "clear keeps capacity");
}

void tiling_and_layout() {
    Document document = sample();
    document.blocks[1].first_run.value += 1U;
    expect(document, NormalFormViolation::blocks_not_contiguous, 1, "a gap between blocks");
    document = sample();
    document.runs.push_back(Run{});
    expect(document, NormalFormViolation::blocks_not_contiguous, document.blocks.size(),
           "runs left over after the last block");
    document = sample();
    document.blocks[0].run_count = 0;
    expect(document, NormalFormViolation::block_without_text_run, 0, "an empty block");
    document = sample();
    document.runs[0].length.value = 1000;
    expect(document, NormalFormViolation::run_range_outside_text, 0, "a run past the text");
    document = sample();
    document.text += "x";
    expect(document, NormalFormViolation::text_layout_not_canonical, document.text.size() - 1U,
           "text after the link targets");
    document = sample();
    document.links[0].target_length.value += 50U;
    expect(document, NormalFormViolation::link_range_outside_text, 0, "a link past the text");
    document = sample();
    document.runs[2].length.value = 0;
    expect(document, NormalFormViolation::empty_text_run, 2, "an empty text run");
    document = sample();
    document.runs[4].length.value = 1;
    expect(document, NormalFormViolation::line_break_with_text, 4, "a line break with text");
}

void encoding() {
    Document document = sample();
    const std::size_t accent = document.text.find('\xC3');
    document.text[accent + 1U] = 'x';
    expect(document, NormalFormViolation::invalid_utf8, accent, "a broken continuation byte");
    document = sample();
    // Cut the four-byte scalar between two runs: each range alone is invalid.
    document.runs[6].length.value -= 2U;
    document.runs[7].start.value -= 2U;
    document.runs[7].length.value += 2U;
    expect(document, NormalFormViolation::invalid_utf8, document.runs[6].start.value + 6U,
           "a scalar cut by a run boundary");
    document = sample();
    document.text[0] = '\x01';
    expect(document, NormalFormViolation::forbidden_character, 0, "a control character");
    document = sample();
    document.text[document.runs[5].start.value] = '\n';
    expect(document, NormalFormViolation::forbidden_character, document.runs[5].start.value,
           "a line feed outside a code block");
    document = sample();
    // An overlong encoding of '/' in place of "Ti".
    document.text[0] = '\xC0';
    document.text[1] = '\xAF';
    expect(document, NormalFormViolation::invalid_utf8, 0, "an overlong form");
}

void block_fields() {
    Document document = sample();
    document.blocks[0].level = 7;
    expect(document, NormalFormViolation::level_out_of_range, 0, "heading level 7");
    document = sample();
    document.blocks[1].level = 1;
    expect(document, NormalFormViolation::level_out_of_range, 1, "a paragraph with a level");
    document = sample();
    document.blocks[2].quote_depth = 8;
    expect(document, NormalFormViolation::quote_depth_out_of_range, 2, "quote depth 8");
    document = sample();
    document.blocks[1].marker = ListMarker::ordered;
    expect(document, NormalFormViolation::list_fields_on_other_kind, 1, "a paragraph with a marker");
    document = sample();
    document.blocks[2].marker = ListMarker::bullet;
    expect(document, NormalFormViolation::list_fields_on_other_kind, 2, "a bullet with an ordinal");
    document = sample();
    document.blocks[2].ordinal = 1000000000U;
    expect(document, NormalFormViolation::ordinal_out_of_range, 2, "a ten-digit ordinal");
    document = sample();
    document.blocks[3].alignment = Alignment::right;
    expect(document, NormalFormViolation::alignment_on_code_block, 3, "an aligned code block");
}

void run_rules() {
    Document document = sample();
    document.runs[4].style.bold = true;
    expect(document, NormalFormViolation::line_break_styled, 4, "a styled line break");
    document = sample();
    document.runs[0].style.size_step = 1;
    expect(document, NormalFormViolation::size_step_in_heading, 0, "a sized heading run");
    document = sample();
    document.runs[1].style.size_step = 5;
    expect(document, NormalFormViolation::size_step_out_of_range, 1, "size step 5");
    document = sample();
    document.runs[2].style.code = true;
    expect(document, NormalFormViolation::none, 0, "bold inline code");
    document = sample();
    document.runs[1].style.superscript = true;
    document.runs[1].style.subscript = true;
    expect(document, NormalFormViolation::superscript_with_subscript, 1, "superscript and subscript");
    document = sample();
    document.runs[2].style = RunStyle{};
    expect(document, NormalFormViolation::adjacent_equal_runs, 2, "unmerged equal runs");
    document = sample();
    document.runs[7].style.italic = true;
    expect(document, NormalFormViolation::styled_run_in_code_block, 7, "a styled code block");
    // Inline code may carry a link.
    document = sample();
    document.runs[3].style.code = true;
    expect(document, NormalFormViolation::none, 0, "linked inline code");
}

void code_blocks() {
    // The sample's code block holds a blank line and indentation in one run.
    const Document document = sample();
    require(document.blocks[3].run_count == 1U, "one run per code block");
    DocumentBuilder builder{};
    builder.begin_block(BlockKind::code_block, 0);
    builder.add_text("a", RunStyle{});
    builder.add_line_break();
    builder.add_text("b", RunStyle{});
    const Document split = builder.finish();
    expect(split, NormalFormViolation::code_block_not_one_run, 0, "a code block of three runs");
}

void line_breaks() {
    // Leading and adjacent line breaks are admitted.
    DocumentBuilder builder{};
    builder.begin_block(BlockKind::paragraph, 0);
    builder.add_line_break();
    builder.add_text("a", RunStyle{});
    builder.add_line_break();
    builder.add_line_break();
    builder.add_text("b", RunStyle{});
    Document document = builder.finish();
    expect(document, NormalFormViolation::none, 0, "leading and adjacent line breaks");
    DocumentBuilder trailing{};
    trailing.begin_block(BlockKind::paragraph, 0);
    trailing.add_text("a", RunStyle{});
    trailing.add_line_break();
    document = trailing.finish();
    expect(document, NormalFormViolation::line_break_at_block_end, 1, "a trailing line break");
    DocumentBuilder heading{};
    heading.begin_block(BlockKind::heading, 1);
    heading.add_text("a", RunStyle{});
    heading.add_line_break();
    heading.add_text("b", RunStyle{});
    document = heading.finish();
    expect(document, NormalFormViolation::line_break_in_heading, 1, "a line break in a heading");
}

void list_levels() {
    DocumentBuilder orphan{};
    orphan.begin_block(BlockKind::paragraph, 0);
    orphan.add_text("p", RunStyle{});
    orphan.begin_block(BlockKind::list_item, 1);
    orphan.add_text("nested", RunStyle{});
    Document document = orphan.finish();
    expect(document, NormalFormViolation::list_level_without_parent, 1, "a nested item after a paragraph");
    DocumentBuilder chain{};
    chain.begin_block(BlockKind::list_item, 0);
    chain.add_text("a", RunStyle{});
    chain.begin_block(BlockKind::list_item, 1);
    chain.add_text("b", RunStyle{});
    chain.begin_block(BlockKind::list_item, 2);
    chain.add_text("c", RunStyle{});
    chain.begin_block(BlockKind::list_item, 1);
    chain.add_text("d", RunStyle{});
    document = chain.finish();
    expect(document, NormalFormViolation::none, 0, "a nesting chain");
    document.blocks[2].level = 3;
    expect(document, NormalFormViolation::list_level_without_parent, 2, "a skipped level");
}

void links() {
    // A link interrupted only by a line break is one span.
    DocumentBuilder builder{};
    builder.begin_block(BlockKind::paragraph, 0);
    const LinkIndex first = builder.add_link("a");
    builder.add_text("one", linked(first));
    builder.add_line_break();
    builder.add_text("two", linked(first));
    Document document = builder.finish();
    expect(document, NormalFormViolation::none, 0, "a link across a line break");

    DocumentBuilder split{};
    split.begin_block(BlockKind::paragraph, 0);
    const LinkIndex reused = split.add_link("a");
    split.add_text("one", linked(reused));
    split.add_text(" gap ", RunStyle{});
    split.add_text("two", linked(reused));
    document = split.finish();
    expect(document, NormalFormViolation::link_split, 2, "a link reused after a gap");

    DocumentBuilder order{};
    order.begin_block(BlockKind::paragraph, 0);
    const LinkIndex a = order.add_link("a");
    const LinkIndex b = order.add_link("b");
    order.add_text("b", linked(b));
    order.add_text("a", linked(a));
    document = order.finish();
    expect(document, NormalFormViolation::links_not_in_first_use_order, 0, "links out of order");

    DocumentBuilder unused{};
    unused.begin_block(BlockKind::paragraph, 0);
    const LinkIndex used = unused.add_link("a");
    unused.add_link("b");
    unused.add_text("a", linked(used));
    document = unused.finish();
    expect(document, NormalFormViolation::link_unused, 1, "an unused link");

    document = sample();
    document.runs[3].style.link = LinkIndex{4};
    expect(document, NormalFormViolation::link_index_out_of_range, 3, "a link index past the table");
}

void loss_report() {
    stx_rtf2md::LossReport report{};
    require(report.empty(), "a new report is empty");
    report.record(stx_rtf2md::LossKind::picture_skipped, stx_rtf2md::SourceOffset{40});
    report.record(stx_rtf2md::LossKind::picture_skipped, stx_rtf2md::SourceOffset{90});
    const stx_rtf2md::LossEntry pictures = report.entry(stx_rtf2md::LossKind::picture_skipped);
    require(pictures.count == 2 && pictures.first.value == 40, "counts and the first offset");
    require(!report.empty(), "a report with a count is not empty");
    report.clear();
    require(report.empty(), "clear empties the report");
}

}

int main() {
    valid_documents();
    tiling_and_layout();
    encoding();
    block_fields();
    run_rules();
    code_blocks();
    line_breaks();
    list_levels();
    links();
    loss_report();
    return 0;
}
