#include "stx_rtf2md/rtf.hpp"

#include "markdown_inline.hpp"
#include "text_scan.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stx_rtf2md {

namespace {

constexpr std::uint64_t offset_capacity = std::numeric_limits<std::uint32_t>::max();
// The specification bounds a control word at 32 letters.
constexpr std::size_t maximum_word_letters = 32;
// Twips per quote level, matching what write_rtf writes (plan section 6).
constexpr std::int32_t twips_per_quote_level = 720;
// The body size against which RTF's absolute sizes are read: 12 points.
constexpr std::int32_t body_half_points = 24;

// Code page 1252's 0x80 to 0x9F; 0 marks a byte the code page leaves
// undefined. 0xA0 to 0xFF are Latin-1.
constexpr std::array<std::uint16_t, 32> windows_1252_high{
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

enum class Destination : std::uint8_t {
    body,
    font_table,
    colour_table,
    stylesheet,
    list_table,
    list_override_table,
    field,
    field_instruction,
    field_result,
    paragraph_numbering,   // {\*\pn ...}: properties, no text
    default_character,     // {\*\defchp ...}: the default character size
    exact_block,           // {\*\stxblock ...}: write_rtf's exact block fields
    skip,
};

enum class Position : std::uint8_t { none, superscript, subscript };

struct CharacterFormat final {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool hidden = false;
    Position position = Position::none;
    std::int32_t font = -1;
    std::int32_t colour = 0;
    std::int32_t background = 0;
    std::int32_t half_points = body_half_points;
};

struct ParagraphFormat final {
    Alignment alignment = Alignment::left;
    bool justified = false;
    std::int32_t style = 0;
    std::int32_t outline = -1;
    std::int32_t list_override = 0;
    std::int32_t list_level = 0;
    bool in_table = false;
    std::int32_t left_indent = 0;
    std::int32_t first_indent = 0;
    std::int32_t spacing = 0;
    // write_rtf's {\*\stxblock} fields, which take precedence over styles,
    // outline levels and lists when present.
    bool exact = false;
    std::int32_t exact_kind = 0;
    std::int32_t exact_level = 0;
    std::int32_t exact_quote = 0;
    std::int32_t exact_marker = 0;
    std::int32_t exact_ordinal = 0;
    bool exact_start = false;
    bool numbered = false;           // old-style \pn numbering present
    bool numbered_ordered = false;
    std::int32_t numbered_start = 1;
    std::int32_t numbered_level = 0;
};

struct GroupState final {
    Destination destination = Destination::body;
    CharacterFormat character{};
    ParagraphFormat paragraph{};
    std::uint32_t unicode_skip = 1;
    std::uint32_t pending_skip = 0;
    // The next control word is the group's first token (destination test).
    bool first_token = true;
    bool ignorable = false;
    // This group itself entered its destination (its close finishes it).
    bool entered = false;
    // The table entry this group itself started: 1 a font, style, list or
    // override entry; 2 a list level. Never inherited by child groups.
    std::uint8_t table_part = 0;
};

enum class StyleKind : std::uint8_t { normal, heading, quote, code };

struct FontEntry final {
    std::int32_t id = 0;
    bool modern = false;
    std::int32_t charset = 0;
    bool monospace = false;
};

struct FontOrder final {
    bool operator()(const FontEntry& left, const FontEntry& right) const noexcept {
        return left.id < right.id;
    }
};

struct StyleEntry final {
    std::int32_t id = 0;
    StyleKind kind = StyleKind::normal;
    std::uint8_t level = 0;
    // The style's \fs, or -1.
    std::int32_t half_points = -1;
};

// How many body characters were set in one size.
struct SizeCount final {
    std::int32_t half_points = 0;
    std::uint64_t characters = 0;
};

struct SizeCountOrder final {
    bool operator()(const SizeCount& left, std::int32_t right) const noexcept {
        return left.half_points < right;
    }
};

struct ListLevel final {
    bool ordered = false;
    std::int32_t start = 1;
};

struct ListEntry final {
    std::int32_t id = 0;
    std::array<ListLevel, 9> levels{};
};

struct ListOverride final {
    std::int32_t list_id = 0;
    std::int32_t override_id = 0;
};

struct Counter final {
    std::int32_t list = 0;
    std::int32_t level = 0;
    std::uint32_t next = 1;
};

struct Field final {
    std::string instruction;
    bool hyperlink = false;
    std::string target;
    std::uint32_t serial = 0;
};

// A run of the paragraph being assembled. field is the hyperlink field's
// serial, or 0.
struct PendingRun final {
    RunKind kind = RunKind::text;
    std::size_t start = 0;
    std::size_t length = 0;
    RunStyle style{};
    std::uint32_t field = 0;
    // The absolute size in half-points; a step is assigned once the
    // document's body size is known.
    std::int32_t half_points = body_half_points;
    std::size_t source = 0;
};

// A hyperlink field's link within the block being emitted.
struct BlockLink final {
    std::uint32_t field = 0;
    std::uint32_t link = 0;
};

enum class Word : std::uint8_t {
    unknown,
    // destinations
    fonttbl, colortbl, stylesheet, defchp, stxblock, stxkind, stxlevel, stxquote, stxmarker, stxordinal, stxstart, listtable, listoverridetable, list, listlevel, listoverride, field, fldinst,
    fldrslt, pntext, listtext, pn, pict, shppict, object, shp, nonshppict, info, header, footer, footnote,
    annotation, silent_destination,
    // document
    rtf, ansi, mac, pc, pca, ansicpg, deff, uc, u,
    // fonts and colours
    f, froman, fswiss, fmodern, fscript, fdecor, ftech, fbidi, fnil, fcharset, red, green, blue,
    // styles and lists
    s, cs, ds, ts, listid, levelnfc, levelnfcn, levelstartat, ls, ilvl, outlinelevel,
    pnlvlblt, pnlvlbody, pnlvlcont, pnlvl, pndec, pnucrm, pnlcrm, pnucltr, pnlcltr, pnord, pnordt, pncard,
    pnstart,
    // character formatting
    plain, b, i, ul, ulnone, ul_variant, strike, striked, super, sub, nosupersub, up, dn, fs, cf, cb, highlight, v,
    visible_ignored, silent,
    // paragraphs
    pard, par, line, tab, page, sect, ql, qc, qr, qj, li, fi, ri, sb, sa, intbl, cell, nestcell, row, nestrow,
    trowd, bin,
    // special characters
    emdash, endash, bullet, lquote, rquote, ldblquote, rdblquote, emspace, enspace, qmspace, zwj, zwnj, ltrmark,
    rtlmark,
};

struct WordEntry final {
    std::string_view name;
    Word word = Word::unknown;
};

struct WordOrder final {
    bool operator()(const WordEntry& left, std::string_view right) const noexcept {
        return left.name < right;
    }
};

// Sorted by name for binary search. Words in silent change nothing the model
// holds and are not counted; visible_ignored words change appearance the
// model cannot hold and are counted.
constexpr WordEntry words[] = {
    {"ab", Word::silent}, {"acf", Word::silent}, {"adeff", Word::silent}, {"adeflang", Word::silent},
    {"adjustright", Word::silent}, {"adn", Word::silent}, {"aenddoc", Word::silent}, {"af", Word::silent},
    {"afelev", Word::silent}, {"afs", Word::silent}, {"ai", Word::silent}, {"alang", Word::silent},
    {"allowfieldendsel", Word::silent}, {"alntblind", Word::silent}, {"annotation", Word::annotation},
    {"ansi", Word::ansi}, {"ansicpg", Word::ansicpg}, {"asianbrkrule", Word::silent}, {"aspalpha", Word::silent},
    {"aspnum", Word::silent}, {"atnauthor", Word::silent_destination}, {"atndate", Word::silent_destination},
    {"atnid", Word::silent_destination}, {"aup", Word::silent}, {"author", Word::silent_destination},
    {"b", Word::b}, {"bin", Word::bin}, {"bkmkend", Word::silent_destination},
    {"bkmkstart", Word::silent_destination}, {"blue", Word::blue}, {"brdrnone", Word::silent},
    {"bullet", Word::bullet}, {"buptim", Word::silent_destination}, {"cachedcolbal", Word::silent},
    {"caps", Word::visible_ignored}, {"cb", Word::cb}, {"cell", Word::cell}, {"cf", Word::cf},
    {"cgrid", Word::silent}, {"charrsid", Word::silent}, {"cocoaplatform", Word::silent},
    {"cocoartf", Word::silent}, {"cocoasubrtf", Word::silent}, {"cocoatextscaling", Word::silent},
    {"colorschememapping", Word::silent_destination}, {"colortbl", Word::colortbl}, {"colsx", Word::silent},
    {"comment", Word::silent_destination}, {"company", Word::silent_destination}, {"contextualspace", Word::silent},
    {"creatim", Word::silent_destination}, {"cs", Word::cs}, {"datastore", Word::silent_destination},
    {"dbch", Word::silent}, {"defchp", Word::defchp}, {"deff", Word::deff}, {"deflang", Word::silent},
    {"deflangfe", Word::silent}, {"defpap", Word::silent_destination}, {"deftab", Word::silent},
    {"delrsid", Word::silent}, {"dghorigin", Word::silent}, {"dghshow", Word::silent}, {"dghspace", Word::silent},
    {"dgvorigin", Word::silent}, {"dgvshow", Word::silent}, {"dgvspace", Word::silent}, {"dn", Word::dn},
    {"doccomm", Word::silent_destination}, {"donotembedlingdata", Word::silent},
    {"donotembedsysfont", Word::silent}, {"ds", Word::ds}, {"embo", Word::visible_ignored},
    {"emdash", Word::emdash}, {"emspace", Word::emspace}, {"endash", Word::endash}, {"endnhere", Word::silent},
    {"enspace", Word::enspace}, {"expandedcolortbl", Word::silent_destination}, {"expnd", Word::silent},
    {"expndtw", Word::silent}, {"f", Word::f}, {"faauto", Word::silent}, {"facingp", Word::silent},
    {"falt", Word::silent_destination}, {"fbias", Word::silent}, {"fbidi", Word::fbidi},
    {"fcharset", Word::fcharset}, {"fcs", Word::silent}, {"fdecor", Word::fdecor}, {"felnbrelev", Word::silent},
    {"fet", Word::silent}, {"fi", Word::fi}, {"field", Word::field}, {"fldinst", Word::fldinst},
    {"fldrslt", Word::fldrslt}, {"fmodern", Word::fmodern}, {"fname", Word::silent_destination},
    {"fnil", Word::fnil}, {"fonttbl", Word::fonttbl}, {"footer", Word::footer}, {"footerf", Word::footer},
    {"footerl", Word::footer}, {"footerr", Word::footer}, {"footery", Word::silent}, {"footnote", Word::footnote},
    {"formshade", Word::silent}, {"fprq", Word::silent}, {"froman", Word::froman}, {"fromtext", Word::silent},
    {"fs", Word::fs}, {"fscript", Word::fscript}, {"fswiss", Word::fswiss}, {"ftech", Word::ftech},
    {"ftnbj", Word::silent}, {"ftncn", Word::silent_destination}, {"ftnlytwnine", Word::silent},
    {"ftnsep", Word::silent_destination}, {"ftnsepc", Word::silent_destination},
    {"generator", Word::silent_destination}, {"green", Word::green}, {"grfdocevents", Word::silent},
    {"gutter", Word::silent}, {"header", Word::header}, {"headerf", Word::header}, {"headerl", Word::header},
    {"headerr", Word::header}, {"headery", Word::silent}, {"hich", Word::silent}, {"highlight", Word::highlight},
    {"horzdoc", Word::silent}, {"htmautsp", Word::silent}, {"hwelev", Word::silent}, {"hyphpar", Word::silent},
    {"i", Word::i}, {"ignoremixedcontent", Word::silent}, {"ilvl", Word::ilvl}, {"impr", Word::visible_ignored},
    {"indrlsweleven", Word::silent}, {"info", Word::info}, {"insrsid", Word::silent}, {"intbl", Word::intbl},
    {"itap", Word::silent}, {"jexpand", Word::silent}, {"keep", Word::silent}, {"keepn", Word::silent},
    {"kerning", Word::silent}, {"keywords", Word::silent_destination}, {"krnprsnet", Word::silent},
    {"lang", Word::silent}, {"langfe", Word::silent}, {"langfenp", Word::silent}, {"langnp", Word::silent},
    {"latentstyles", Word::silent_destination}, {"ldblquote", Word::ldblquote}, {"level", Word::silent},
    {"levelnfc", Word::levelnfc}, {"levelnfcn", Word::levelnfcn}, {"levelstartat", Word::levelstartat},
    {"li", Word::li}, {"lin", Word::li}, {"line", Word::line}, {"linex", Word::silent}, {"list", Word::list},
    {"listid", Word::listid}, {"listlevel", Word::listlevel}, {"listoverride", Word::listoverride},
    {"listoverridetable", Word::listoverridetable}, {"listpicture", Word::silent_destination},
    {"listtable", Word::listtable}, {"listtext", Word::listtext}, {"lnbrkrule", Word::silent},
    {"loch", Word::silent}, {"lquote", Word::lquote}, {"ls", Word::ls}, {"ltrch", Word::silent},
    {"ltrdoc", Word::silent}, {"ltrmark", Word::ltrmark}, {"ltrpar", Word::silent}, {"ltrsect", Word::silent},
    {"lytcalctblwd", Word::silent}, {"lyttblrtgr", Word::silent}, {"mac", Word::mac}, {"margb", Word::silent},
    {"margbsxn", Word::silent}, {"margl", Word::silent}, {"marglsxn", Word::silent}, {"margr", Word::silent},
    {"margrsxn", Word::silent}, {"margt", Word::silent}, {"margtsxn", Word::silent},
    {"mmathPr", Word::silent_destination}, {"nestcell", Word::nestcell}, {"nestrow", Word::nestrow},
    {"newtblstyruls", Word::silent}, {"noafcnsttbl", Word::silent}, {"nobrkwrptbl", Word::silent},
    {"nocxsptable", Word::silent}, {"nogrowautofit", Word::silent}, {"noindnmbrts", Word::silent},
    {"nonshppict", Word::nonshppict}, {"noproof", Word::silent}, {"nosnaplinegrid", Word::silent},
    {"nosupersub", Word::nosupersub}, {"notbrkcnstfrctbl", Word::silent}, {"notcvasp", Word::silent},
    {"notvatxbx", Word::silent}, {"nouicompat", Word::silent}, {"nowidctlpar", Word::silent},
    {"object", Word::object}, {"oldcprops", Word::silent}, {"operator", Word::silent_destination},
    {"outl", Word::visible_ignored}, {"outlinelevel", Word::outlinelevel}, {"outlinelevelnone", Word::silent},
    {"page", Word::page}, {"panose", Word::silent_destination}, {"paperh", Word::silent}, {"paperw", Word::silent},
    {"par", Word::par}, {"pararsid", Word::silent}, {"pard", Word::pard}, {"pardeftab", Word::silent},
    {"pardirnatural", Word::silent}, {"partightenfactor", Word::silent}, {"pc", Word::pc}, {"pca", Word::pca},
    {"pgdsctbl", Word::silent_destination}, {"pghsxn", Word::silent}, {"pgnstarts", Word::silent},
    {"pgwsxn", Word::silent}, {"pict", Word::pict}, {"plain", Word::plain}, {"pn", Word::pn},
    {"pncard", Word::pncard}, {"pndec", Word::pndec}, {"pnlcltr", Word::pnlcltr}, {"pnlcrm", Word::pnlcrm},
    {"pnlvl", Word::pnlvl}, {"pnlvlblt", Word::pnlvlblt}, {"pnlvlbody", Word::pnlvlbody},
    {"pnlvlcont", Word::pnlvlcont}, {"pnord", Word::pnord}, {"pnordt", Word::pnordt}, {"pnstart", Word::pnstart},
    {"pntext", Word::pntext}, {"pnucltr", Word::pnucltr}, {"pnucrm", Word::pnucrm},
    {"printim", Word::silent_destination}, {"qc", Word::qc}, {"qd", Word::silent}, {"qj", Word::qj},
    {"ql", Word::ql}, {"qmspace", Word::qmspace}, {"qr", Word::qr}, {"rdblquote", Word::rdblquote},
    {"red", Word::red}, {"relyonvml", Word::silent}, {"revtim", Word::silent_destination}, {"ri", Word::ri},
    {"rin", Word::ri}, {"row", Word::row}, {"rquote", Word::rquote}, {"rsidroot", Word::silent},
    {"rsidtbl", Word::silent_destination}, {"rtf", Word::rtf}, {"rtlch", Word::silent}, {"rtlmark", Word::rtlmark},
    {"rtlpar", Word::silent}, {"rxe", Word::silent_destination}, {"s", Word::s}, {"sa", Word::sa},
    {"saauto", Word::silent}, {"saveinvalidxml", Word::silent}, {"sb", Word::sb}, {"sbauto", Word::silent},
    {"scaps", Word::visible_ignored}, {"sect", Word::sect}, {"sectd", Word::silent},
    {"sectdefaultcl", Word::silent}, {"sectlinegrid", Word::silent}, {"sectrsid", Word::silent},
    {"sftnbj", Word::silent}, {"shad", Word::visible_ignored}, {"showplaceholdtext", Word::silent},
    {"showxmlerrors", Word::silent}, {"shp", Word::shp}, {"shppict", Word::shppict}, {"sl", Word::silent},
    {"slmult", Word::silent}, {"snaptogridincell", Word::silent}, {"spltpgpar", Word::silent},
    {"splytwnine", Word::silent}, {"strike", Word::strike}, {"striked", Word::striked}, {"stshfbi", Word::silent},
    {"stshfdbch", Word::silent}, {"stshfhich", Word::silent}, {"stshfloch", Word::silent},
    {"stxblock", Word::stxblock}, {"stxkind", Word::stxkind}, {"stxlevel", Word::stxlevel},
    {"stxmarker", Word::stxmarker}, {"stxordinal", Word::stxordinal}, {"stxquote", Word::stxquote},
    {"stxstart", Word::stxstart}, {"stylesheet", Word::stylesheet}, {"sub", Word::sub},
    {"subject", Word::silent_destination}, {"super", Word::super}, {"tab", Word::tab}, {"tblrsid", Word::silent},
    {"tc", Word::silent_destination}, {"themedata", Word::silent_destination}, {"themelang", Word::silent},
    {"themelangcs", Word::silent}, {"themelangfe", Word::silent}, {"title", Word::silent_destination},
    {"trackformatting", Word::silent}, {"trackmoves", Word::silent}, {"trowd", Word::trowd}, {"ts", Word::ts},
    {"tx", Word::silent}, {"txe", Word::silent_destination}, {"u", Word::u}, {"uc", Word::uc}, {"ul", Word::ul},
    {"uld", Word::ul_variant}, {"uldash", Word::ul_variant}, {"uldashd", Word::ul_variant},
    {"uldashdd", Word::ul_variant}, {"uldb", Word::ul_variant}, {"ulhwave", Word::ul_variant},
    {"ulldash", Word::ul_variant}, {"ulnone", Word::ulnone}, {"ulth", Word::ul_variant},
    {"ulthd", Word::ul_variant}, {"ulthdash", Word::ul_variant}, {"ulw", Word::ul_variant},
    {"ulwave", Word::ul_variant}, {"up", Word::up}, {"useltbaln", Word::silent},
    {"usenormstyforlist", Word::silent}, {"userprops", Word::silent_destination}, {"utinl", Word::silent},
    {"v", Word::v}, {"validatexml", Word::silent}, {"viewh", Word::silent}, {"viewkind", Word::silent},
    {"viewscale", Word::silent}, {"vieww", Word::silent}, {"wgrffmtfilter", Word::silent_destination},
    {"widctlpar", Word::silent}, {"widowctrl", Word::silent}, {"wrapdefault", Word::silent},
    {"wrppunct", Word::silent}, {"xe", Word::silent_destination}, {"xmlnstbl", Word::silent_destination},
    {"zwj", Word::zwj}, {"zwnj", Word::zwnj},
};

constexpr std::size_t word_count = sizeof(words) / sizeof(words[0]);

// The table must stay sorted for find_word's binary search.
constexpr bool words_sorted() {
    for (std::size_t index = 1; index < word_count; ++index) {
        if (!(words[index - 1U].name < words[index].name)) {
            return false;
        }
    }
    return true;
}
static_assert(words_sorted(), "the control-word table must be sorted by name");

Word find_word(std::string_view name) {
    const WordOrder order{};
    const WordEntry* const end = words + word_count;
    const WordEntry* const found = std::lower_bound(words, end, name, order);
    if (found == end || (*found).name != name) {
        return Word::unknown;
    }
    return (*found).word;
}

bool is_letter(std::uint8_t byte) noexcept {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
}

bool is_digit(std::uint8_t byte) noexcept {
    return byte >= '0' && byte <= '9';
}

std::uint32_t hex_value(std::uint8_t byte) noexcept {
    if (is_digit(byte)) {
        return static_cast<std::uint32_t>(byte - '0');
    }
    if (byte >= 'a' && byte <= 'f') {
        return static_cast<std::uint32_t>(byte - 'a') + 10U;
    }
    return static_cast<std::uint32_t>(byte - 'A') + 10U;
}

bool is_hex(std::uint8_t byte) noexcept {
    return is_digit(byte) || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
}

char lower_ascii(char character) noexcept {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

std::string trimmed_lower(std::string_view text) {
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && (text[start] == ' ' || text[start] == '\t')) {
        start += 1U;
    }
    while (end > start && (text[end - 1U] == ' ' || text[end - 1U] == '\t')) {
        end -= 1U;
    }
    std::string result;
    for (std::size_t index = start; index < end; ++index) {
        result.push_back(lower_ascii(text[index]));
    }
    return result;
}

bool monospace_name(std::string_view name) {
    const std::string lower = trimmed_lower(name);
    const std::array<std::string_view, 10> names{
        "courier new", "courier", "consolas", "lucida console", "menlo", "monaco", "cascadia mono",
        "cascadia code", "dejavu sans mono", "liberation mono",
    };
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (lower == names[index]) {
            return true;
        }
    }
    return false;
}

std::int8_t size_step(std::int32_t half_points) noexcept {
    if (half_points <= 17) {
        return -2;
    }
    if (half_points <= 21) {
        return -1;
    }
    if (half_points <= 25) {
        return 0;
    }
    if (half_points <= 31) {
        return 1;
    }
    if (half_points <= 39) {
        return 2;
    }
    if (half_points <= 55) {
        return 3;
    }
    return 4;
}

bool canonical_size(std::int32_t half_points) noexcept {
    return half_points == 16 || half_points == 20 || half_points == 24 || half_points == 28 || half_points == 36
        || half_points == 48 || half_points == 64;
}

enum class ReadState : std::uint8_t { ok, malformed, unsupported_encoding, limit_exceeded, effort_exceeded };

}

struct RtfReader::Workspace final {
    std::vector<GroupState> groups;
    std::vector<FontEntry> fonts;
    std::vector<std::optional<Rgb>> colours;
    std::vector<StyleEntry> styles;
    std::vector<ListEntry> lists;
    std::vector<ListOverride> overrides;
    std::vector<Counter> counters;
    std::vector<Field> fields;
    std::string entry_name;
    std::string paragraph_text;
    std::vector<PendingRun> paragraph_runs;
    std::string code_text;
    std::string scratch;
    // Every field's hyperlink target, indexed by serial - 1 (empty for other
    // fields); kept after the field's group closes.
    std::vector<std::string> field_targets;
    std::vector<BlockLink> block_links;
    std::vector<SizeCount> size_counts;
    // For each document run: its absolute size (-1 where a step never
    // applies: line breaks, headings, code) and its source offset.
    std::vector<std::int32_t> run_half_points;
    std::vector<std::size_t> run_sources;
    BodySize body{};
    detail::LinkTargets links;
    detail::EffortMeter effort;
};

namespace {

// One read: borrows the reader's workspace and the caller's document and
// loss report for the duration of RtfReader::read.
class Interpreter final {
public:
    Interpreter(RtfReader::Workspace& workspace, Document& document, LossReport& loss, const Limits& limits)
        : workspace_(workspace), document_(document), loss_(loss), limits_(limits) {}

    ReadState run(std::span<const std::byte> input, SourceOffset& stop);

private:
    std::uint8_t byte_at(std::size_t offset) const;
    GroupState& state();
    ReadState control_word(std::string_view name, bool has_parameter, std::int32_t parameter, std::size_t offset);
    ReadState control_symbol(std::uint8_t symbol, std::size_t offset);
    ReadState text_byte(std::uint8_t byte, std::size_t offset);
    ReadState scalar(std::uint32_t value, std::size_t offset);
    ReadState push_group(std::size_t offset);
    ReadState pop_group(std::size_t offset);
    bool enter_destination(Word word, std::size_t offset);
    void finish_font_entry();
    void finish_style_entry();
    void finish_field_instruction();
    ReadState finish_paragraph(bool explicit_end, std::size_t offset);
    ReadState flush_code(std::size_t offset);
    ReadState emit_block(const Block& block, std::size_t first_pending, std::size_t end_pending,
                         bool heading, std::size_t offset);
    RunStyle style_for(const CharacterFormat& format) const;
    const FontEntry* font(std::int32_t id) const;
    StyleKind style_kind(std::int32_t id, std::uint8_t& level) const;
    bool list_level(std::int32_t override_id, std::int32_t level, ListLevel& result) const;
    void record(LossKind kind, std::size_t offset);
    void count_size(std::int32_t half_points);
    ReadState assign_size_steps();

    RtfReader::Workspace& workspace_;
    Document& document_;
    LossReport& loss_;
    const Limits& limits_;
    std::span<const std::byte> input_;
    std::int32_t code_page_ = 1252;
    std::int32_t default_font_ = 0;
    std::uint32_t high_surrogate_ = 0;
    std::uint32_t next_field_serial_ = 1;
    bool fonts_sorted_ = false;
    bool table_counted_ = false;
    bool body_started_ = false;
    // A code block being assembled: its quote depth, and where it began.
    bool code_open_ = false;
    std::uint8_t code_quote_ = 0;
    std::size_t code_offset_ = 0;
    // The list level being parsed inside \listlevel, and the list's entry.
    std::size_t list_level_index_ = 0;
    ListEntry list_entry_{};
    ListOverride override_entry_{};
    FontEntry font_entry_{};
    StyleEntry style_entry_{};
    bool font_entry_open_ = false;
    bool style_entry_open_ = false;
    std::array<std::int32_t, 3> colour_components_{};
    // {\*\defchp \fsN}, or -1.
    std::int32_t default_half_points_ = -1;
    bool colour_has_component_ = false;
};

std::uint8_t Interpreter::byte_at(std::size_t offset) const {
    const std::uint8_t value = std::to_integer<std::uint8_t>(input_[offset]);
    return value;
}

GroupState& Interpreter::state() {
    GroupState& top = workspace_.groups.back();
    return top;
}

void Interpreter::record(LossKind kind, std::size_t offset) {
    loss_.record(kind, SourceOffset{offset});
}

// Counts one body character in a size; sizes are kept sorted, so a count is
// a binary search and a new size one insertion.
void Interpreter::count_size(std::int32_t half_points) {
    std::vector<SizeCount>& counts = workspace_.size_counts;
    const SizeCountOrder order{};
    const std::vector<SizeCount>::iterator found = std::lower_bound(counts.begin(), counts.end(), half_points, order);
    if (found != counts.end() && (*found).half_points == half_points) {
        (*found).characters += 1U;
        return;
    }
    SizeCount entry{};
    entry.half_points = half_points;
    entry.characters = 1;
    counts.insert(found, entry);
}

// Chooses the body size, then gives every sized run its step against it:
// the absolute size scaled to a 12-point body, read through the step table.
// Runs whose steps become equal are merged again, so the document stays in
// normal form.
ReadState Interpreter::assign_size_steps() {
    BodySize body{};
    body.half_points = body_half_points;
    body.rule = BodyRule::none;
    if (default_half_points_ > 0) {
        body.half_points = default_half_points_;
        body.rule = BodyRule::default_character_format;
    } else {
        std::int32_t normal = -1;
        for (std::size_t index = 0; index < workspace_.styles.size(); ++index) {
            const StyleEntry& style = workspace_.styles[index];
            if (style.id == 0 && style.half_points > 0) {
                normal = style.half_points;
                break;
            }
        }
        if (normal > 0) {
            body.half_points = normal;
            body.rule = BodyRule::normal_style;
        } else if (!workspace_.size_counts.empty()) {
            std::uint64_t most = 0;
            for (std::size_t index = 0; index < workspace_.size_counts.size(); ++index) {
                const SizeCount& count = workspace_.size_counts[index];
                if (count.characters > most) {
                    most = count.characters;
                    body.half_points = count.half_points;
                }
            }
            body.rule = BodyRule::most_common;
        }
    }
    workspace_.body = body;
    if (!workspace_.effort.charge(document_.runs.size() + 1U)) {
        return ReadState::effort_exceeded;
    }
    for (std::size_t index = 0; index < document_.runs.size(); ++index) {
        const std::int32_t half_points = workspace_.run_half_points[index];
        if (half_points < 0) {
            continue;
        }
        const std::int64_t scaled64 = (static_cast<std::int64_t>(half_points) * body_half_points
                                       + body.half_points / 2) / body.half_points;
        const std::int32_t scaled = static_cast<std::int32_t>(std::min<std::int64_t>(scaled64, 100000));
        document_.runs[index].style.size_step = size_step(scaled);
        if (!canonical_size(scaled)) {
            record(LossKind::size_quantised, workspace_.run_sources[index]);
        }
    }
    // Merge neighbours that are now equal, block by block.
    std::size_t write = 0;
    for (std::size_t block_index = 0; block_index < document_.blocks.size(); ++block_index) {
        Block& block = document_.blocks[block_index];
        const std::size_t first = block.first_run.value;
        const std::size_t end = first + block.run_count;
        const std::size_t block_start = write;
        for (std::size_t index = first; index < end; ++index) {
            const Run run = document_.runs[index];
            if (write > block_start) {
                Run& last = document_.runs[write - 1U];
                if (run.kind == RunKind::text && last.kind == RunKind::text && last.style == run.style) {
                    last.length.value += run.length.value;
                    continue;
                }
            }
            document_.runs[write] = run;
            write += 1U;
        }
        block.first_run = RunIndex{static_cast<std::uint32_t>(block_start)};
        block.run_count = static_cast<std::uint32_t>(write - block_start);
    }
    document_.runs.resize(write);
    return ReadState::ok;
}

ReadState Interpreter::run(std::span<const std::byte> input, SourceOffset& stop) {
    input_ = input;
    const std::string_view header = "{\\rtf";
    if (input.size() < header.size()) {
        stop = SourceOffset{0};
        return ReadState::malformed;
    }
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (byte_at(index) != static_cast<std::uint8_t>(header[index])) {
            stop = SourceOffset{0};
            return ReadState::malformed;
        }
    }
    // The outermost group's base state; the document's "{" pushes onto it.
    workspace_.groups.push_back(GroupState{});
    std::size_t position = 0;
    std::size_t closed_at = 0;
    bool document_closed = false;
    while (position < input.size()) {
        if (!workspace_.effort.charge(1)) {
            stop = SourceOffset{position};
            return ReadState::effort_exceeded;
        }
        const std::uint8_t byte = byte_at(position);
        ReadState result = ReadState::ok;
        if (document_closed) {
            // Bytes after the document's closing brace are not part of it.
            break;
        }
        if (byte == '{') {
            result = push_group(position);
            position += 1U;
        } else if (byte == '}') {
            result = pop_group(position);
            position += 1U;
            if (result == ReadState::ok && workspace_.groups.size() == 1U) {
                document_closed = true;
                closed_at = position;
            }
        } else if (byte == '\\') {
            const std::size_t start = position;
            position += 1U;
            if (position >= input.size()) {
                stop = SourceOffset{start};
                return ReadState::malformed;
            }
            const std::uint8_t next = byte_at(position);
            if (is_letter(next)) {
                std::size_t end = position;
                while (end < input.size() && is_letter(byte_at(end))) {
                    end += 1U;
                }
                if (end - position > maximum_word_letters) {
                    stop = SourceOffset{start};
                    return ReadState::malformed;
                }
                workspace_.scratch.clear();
                for (std::size_t index = position; index < end; ++index) {
                    workspace_.scratch.push_back(static_cast<char>(byte_at(index)));
                }
                bool has_parameter = false;
                std::int64_t value = 0;
                bool negative = false;
                std::size_t cursor = end;
                if (cursor < input.size() && byte_at(cursor) == '-') {
                    negative = true;
                    cursor += 1U;
                }
                const std::size_t digits_start = cursor;
                while (cursor < input.size() && is_digit(byte_at(cursor))) {
                    if (cursor - digits_start >= 10U) {
                        stop = SourceOffset{start};
                        return ReadState::malformed;
                    }
                    value = value * 10 + static_cast<std::int64_t>(byte_at(cursor) - '0');
                    cursor += 1U;
                }
                if (cursor > digits_start) {
                    has_parameter = true;
                    if (negative) {
                        value = -value;
                    }
                    const bool fits = value >= std::numeric_limits<std::int32_t>::min()
                        && value <= std::numeric_limits<std::int32_t>::max();
                    if (!fits) {
                        stop = SourceOffset{start};
                        return ReadState::malformed;
                    }
                } else if (negative) {
                    cursor = end;
                }
                if (cursor < input.size() && byte_at(cursor) == ' ') {
                    cursor += 1U;
                }
                position = cursor;
                // A view of the reused scratch buffer; nothing below writes
                // to scratch before the word is handled.
                const std::string_view name(workspace_.scratch);
                if (name == "bin" && has_parameter) {
                    // \binN: N raw bytes follow; they are never text.
                    if (value < 0 || static_cast<std::uint64_t>(value) > input.size() - position) {
                        stop = SourceOffset{start};
                        return ReadState::malformed;
                    }
                    if (state().destination != Destination::skip) {
                        record(LossKind::destination_skipped, start);
                    }
                    position += static_cast<std::size_t>(value);
                    continue;
                }
                result = control_word(name, has_parameter, static_cast<std::int32_t>(value), start);
            } else if (next == '\'') {
                if (position + 2U >= input.size() || !is_hex(byte_at(position + 1U)) || !is_hex(byte_at(position + 2U))) {
                    stop = SourceOffset{start};
                    return ReadState::malformed;
                }
                const std::uint32_t decoded = hex_value(byte_at(position + 1U)) * 16U + hex_value(byte_at(position + 2U));
                position += 3U;
                state().first_token = false;
                result = text_byte(static_cast<std::uint8_t>(decoded), start);
            } else {
                position += 1U;
                result = control_symbol(next, start);
            }
        } else if (byte == '\r' || byte == '\n') {
            position += 1U;
        } else {
            state().first_token = false;
            result = text_byte(byte, position);
            position += 1U;
        }
        if (result != ReadState::ok) {
            stop = SourceOffset{position};
            return result;
        }
    }
    if (!document_closed) {
        stop = SourceOffset{input.size()};
        return ReadState::malformed;
    }
    ReadState result = finish_paragraph(false, closed_at);
    if (result == ReadState::ok) {
        result = flush_code(closed_at);
    }
    if (result != ReadState::ok) {
        stop = SourceOffset{closed_at};
        return result;
    }
    result = assign_size_steps();
    if (result != ReadState::ok) {
        stop = SourceOffset{closed_at};
        return result;
    }
    if (workspace_.links.count() > limits_.links) {
        return ReadState::limit_exceeded;
    }
    workspace_.links.append_to(document_);
    if (document_.text.size() > offset_capacity) {
        return ReadState::limit_exceeded;
    }
    stop = SourceOffset{input.size()};
    return ReadState::ok;
}

ReadState Interpreter::push_group(std::size_t offset) {
    if (workspace_.groups.size() > limits_.group_depth) {
        static_cast<void>(offset);
        return ReadState::limit_exceeded;
    }
    GroupState copy = state();
    state().first_token = false;
    copy.first_token = true;
    copy.ignorable = false;
    copy.entered = false;
    copy.table_part = 0;
    copy.pending_skip = 0;
    workspace_.groups.push_back(copy);
    return ReadState::ok;
}

ReadState Interpreter::pop_group(std::size_t offset) {
    if (workspace_.groups.size() <= 1U) {
        return ReadState::malformed;
    }
    const GroupState closing = state();
    workspace_.groups.pop_back();
    switch (closing.destination) {
    case Destination::font_table:
        if (closing.table_part == 1U || closing.entered) {
            finish_font_entry();
        }
        if (closing.entered) {
            const FontOrder order{};
            std::sort(workspace_.fonts.begin(), workspace_.fonts.end(), order);
            fonts_sorted_ = true;
        }
        break;
    case Destination::stylesheet:
        if (closing.table_part == 1U || closing.entered) {
            finish_style_entry();
        }
        break;
    case Destination::list_table:
        if (closing.table_part == 2U) {
            list_level_index_ += 1U;
        } else if (closing.table_part == 1U) {
            if (workspace_.lists.size() >= limits_.list_entries) {
                return ReadState::limit_exceeded;
            }
            workspace_.lists.push_back(list_entry_);
        }
        break;
    case Destination::list_override_table:
        if (closing.table_part == 1U) {
            if (workspace_.overrides.size() >= limits_.list_entries) {
                return ReadState::limit_exceeded;
            }
            workspace_.overrides.push_back(override_entry_);
        }
        break;
    case Destination::field_instruction:
        if (closing.entered) {
            finish_field_instruction();
        }
        break;
    case Destination::field:
        if (closing.entered && !workspace_.fields.empty()) {
            if (!workspace_.fields.back().hyperlink) {
                record(LossKind::field_result_kept, offset);
            }
            workspace_.fields.pop_back();
        }
        break;
    default:
        break;
    }
    static_cast<void>(offset);
    return ReadState::ok;
}

void Interpreter::finish_font_entry() {
    if (!font_entry_open_) {
        workspace_.entry_name.clear();
        return;
    }
    font_entry_open_ = false;
    FontEntry entry = font_entry_;
    entry.monospace = entry.modern || monospace_name(workspace_.entry_name);
    if (workspace_.fonts.size() < limits_.font_entries) {
        workspace_.fonts.push_back(entry);
        fonts_sorted_ = false;
    }
    workspace_.entry_name.clear();
    font_entry_ = FontEntry{};
}

void Interpreter::finish_style_entry() {
    if (!style_entry_open_) {
        workspace_.entry_name.clear();
        return;
    }
    style_entry_open_ = false;
    StyleEntry entry = style_entry_;
    std::string name = trimmed_lower(workspace_.entry_name);
    if (name.size() >= 9U && name.substr(0, 8) == "heading " && name.size() == 9U && name[8] >= '1' && name[8] <= '6') {
        entry.kind = StyleKind::heading;
        entry.level = static_cast<std::uint8_t>(name[8] - '0');
    } else if (name == "quote" || name == "intense quote") {
        entry.kind = StyleKind::quote;
    } else if (name == "code" || name == "html preformatted" || name == "plain text") {
        entry.kind = StyleKind::code;
    }
    if (workspace_.styles.size() < limits_.style_entries) {
        workspace_.styles.push_back(entry);
    }
    workspace_.entry_name.clear();
    style_entry_ = StyleEntry{};
}

// HYPERLINK "target" (with an optional \l "anchor") becomes a link; any
// other field keeps its result text.
void Interpreter::finish_field_instruction() {
    if (workspace_.fields.empty()) {
        return;
    }
    Field& field = workspace_.fields.back();
    const std::string& instruction = field.instruction;
    std::size_t start = 0;
    while (start < instruction.size() && instruction[start] == ' ') {
        start += 1U;
    }
    const std::string_view keyword = "HYPERLINK";
    if (instruction.compare(start, keyword.size(), keyword) != 0) {
        return;
    }
    std::size_t cursor = start + keyword.size();
    bool anchor = false;
    bool target_found = false;
    while (cursor < instruction.size()) {
        const char character = instruction[cursor];
        if (character == '\\' && cursor + 1U < instruction.size() && instruction[cursor + 1U] == 'l') {
            anchor = true;
            cursor += 2U;
            continue;
        }
        if (character == '"') {
            std::string quoted;
            std::size_t close = cursor + 1U;
            bool closed = false;
            while (close < instruction.size()) {
                const char inner = instruction[close];
                if (inner == '\\' && close + 1U < instruction.size()) {
                    quoted.push_back(instruction[close + 1U]);
                    close += 2U;
                    continue;
                }
                if (inner == '"') {
                    closed = true;
                    break;
                }
                quoted.push_back(inner);
                close += 1U;
            }
            if (!closed) {
                return;
            }
            // A quoted target makes the field a link, even an empty one.
            target_found = true;
            if (anchor) {
                field.target.append("#");
                field.target.append(quoted);
                break;
            }
            field.target = quoted;
            cursor = close + 1U;
            continue;
        }
        cursor += 1U;
    }
    field.hyperlink = target_found;
    if (field.serial > 0 && field.serial <= workspace_.field_targets.size()) {
        workspace_.field_targets[field.serial - 1U] = field.target;
    }
}

bool Interpreter::enter_destination(Word word, std::size_t offset) {
    GroupState& current = state();
    Destination destination = Destination::skip;
    bool counted_as = false;
    LossKind kind = LossKind::destination_skipped;
    switch (word) {
    case Word::fonttbl:
        destination = Destination::font_table;
        break;
    case Word::colortbl:
        destination = Destination::colour_table;
        colour_has_component_ = false;
        colour_components_ = std::array<std::int32_t, 3>{};
        break;
    case Word::stylesheet:
        destination = Destination::stylesheet;
        break;
    case Word::listtable:
        destination = Destination::list_table;
        break;
    case Word::listoverridetable:
        destination = Destination::list_override_table;
        break;
    case Word::field: {
        destination = Destination::field;
        Field field{};
        field.serial = next_field_serial_;
        next_field_serial_ += 1U;
        workspace_.fields.push_back(field);
        workspace_.field_targets.push_back(std::string());
        break;
    }
    case Word::fldinst:
        destination = Destination::field_instruction;
        break;
    case Word::defchp:
        destination = Destination::default_character;
        break;
    case Word::stxblock:
        destination = Destination::exact_block;
        if (workspace_.groups.size() >= 2U) {
            workspace_.groups[workspace_.groups.size() - 2U].paragraph.exact = true;
        }
        break;
    case Word::fldrslt:
        destination = Destination::field_result;
        break;
    case Word::pn:
        destination = Destination::paragraph_numbering;
        break;
    case Word::pntext: case Word::listtext: case Word::nonshppict: case Word::info: case Word::silent_destination:
        destination = Destination::skip;
        break;
    case Word::pict: case Word::shppict:
        counted_as = true;
        kind = LossKind::picture_skipped;
        break;
    case Word::object: case Word::shp:
        counted_as = true;
        kind = LossKind::object_skipped;
        break;
    case Word::header: case Word::footer: case Word::footnote: case Word::annotation:
        counted_as = true;
        break;
    default:
        return false;
    }
    if (word == Word::pn && workspace_.groups.size() >= 2U) {
        // \pn properties belong to the paragraph that contains the group.
        workspace_.groups[workspace_.groups.size() - 2U].paragraph.numbered = true;
    }
    current.destination = destination;
    current.entered = true;
    if (counted_as) {
        record(kind, offset);
    }
    return true;
}

const FontEntry* Interpreter::font(std::int32_t id) const {
    const std::vector<FontEntry>& fonts = workspace_.fonts;
    FontEntry probe{};
    probe.id = id;
    const FontOrder order{};
    const std::vector<FontEntry>::const_iterator found = std::lower_bound(fonts.begin(), fonts.end(), probe, order);
    if (found == fonts.end() || (*found).id != id) {
        return nullptr;
    }
    return &(*found);
}

StyleKind Interpreter::style_kind(std::int32_t id, std::uint8_t& level) const {
    const std::vector<StyleEntry>& styles = workspace_.styles;
    for (std::size_t index = 0; index < styles.size(); ++index) {
        if (styles[index].id == id) {
            level = styles[index].level;
            return styles[index].kind;
        }
    }
    level = 0;
    return StyleKind::normal;
}

bool Interpreter::list_level(std::int32_t override_id, std::int32_t level, ListLevel& result) const {
    std::int32_t list_id = 0;
    bool found_override = false;
    for (std::size_t index = 0; index < workspace_.overrides.size(); ++index) {
        if (workspace_.overrides[index].override_id == override_id) {
            list_id = workspace_.overrides[index].list_id;
            found_override = true;
            break;
        }
    }
    if (!found_override) {
        return false;
    }
    for (std::size_t index = 0; index < workspace_.lists.size(); ++index) {
        if (workspace_.lists[index].id == list_id) {
            const std::size_t clamped = static_cast<std::size_t>(std::clamp(level, 0, 8));
            result = workspace_.lists[index].levels[clamped];
            return true;
        }
    }
    return false;
}

RunStyle Interpreter::style_for(const CharacterFormat& format) const {
    RunStyle style{};
    style.bold = format.bold;
    style.italic = format.italic;
    style.underline = format.underline;
    style.strikethrough = format.strikethrough;
    style.superscript = format.position == Position::superscript;
    style.subscript = format.position == Position::subscript;
    const std::vector<std::optional<Rgb>>& colours = workspace_.colours;
    if (format.colour > 0 && static_cast<std::size_t>(format.colour) < colours.size()) {
        style.colour = colours[static_cast<std::size_t>(format.colour)];
    }
    if (format.background > 0 && static_cast<std::size_t>(format.background) < colours.size()) {
        style.background = colours[static_cast<std::size_t>(format.background)];
    }
    const FontEntry* entry = font(format.font < 0 ? default_font_ : format.font);
    if (entry != nullptr && (*entry).monospace && (*entry).id != default_font_) {
        style.code = true;
    }
    return style;
}

ReadState Interpreter::scalar(std::uint32_t value, std::size_t offset) {
    // Each decoded character is charged as output, beyond its input bytes.
    if (!workspace_.effort.charge(1)) {
        return ReadState::effort_exceeded;
    }
    GroupState& current = state();
    switch (current.destination) {
    case Destination::font_table:
    case Destination::stylesheet:
        if (value == ';') {
            if (current.destination == Destination::font_table) {
                finish_font_entry();
            } else {
                finish_style_entry();
            }
            return ReadState::ok;
        }
        if (workspace_.entry_name.size() < 256U) {
            detail::append_scalar(workspace_.entry_name, value);
        }
        return ReadState::ok;
    case Destination::colour_table:
        if (value == ';') {
            if (workspace_.colours.size() >= limits_.colour_entries) {
                return ReadState::limit_exceeded;
            }
            if (colour_has_component_) {
                Rgb colour{};
                colour.red = static_cast<std::uint8_t>(std::clamp(colour_components_[0], 0, 255));
                colour.green = static_cast<std::uint8_t>(std::clamp(colour_components_[1], 0, 255));
                colour.blue = static_cast<std::uint8_t>(std::clamp(colour_components_[2], 0, 255));
                workspace_.colours.push_back(colour);
            } else {
                workspace_.colours.push_back(std::nullopt);
            }
            colour_has_component_ = false;
            colour_components_ = std::array<std::int32_t, 3>{};
        }
        return ReadState::ok;
    case Destination::field_instruction:
        if (!workspace_.fields.empty() && workspace_.fields.back().instruction.size() < 4096U) {
            detail::append_scalar(workspace_.fields.back().instruction, value);
        }
        return ReadState::ok;
    case Destination::body:
    case Destination::field_result:
        break;
    default:
        return ReadState::ok;
    }
    if (current.character.hidden) {
        return ReadState::ok;
    }
    if (detail::forbidden_scalar(value, false)) {
        record(LossKind::control_character_dropped, offset);
        return ReadState::ok;
    }
    body_started_ = true;
    std::uint32_t field = 0;
    for (std::size_t index = workspace_.fields.size(); index > 0; --index) {
        if (workspace_.fields[index - 1U].hyperlink) {
            field = workspace_.fields[index - 1U].serial;
            break;
        }
    }
    const RunStyle style = style_for(current.character);
    const std::int32_t half_points = current.character.half_points;
    count_size(half_points);
    std::string& text = workspace_.paragraph_text;
    std::vector<PendingRun>& runs = workspace_.paragraph_runs;
    const std::size_t before = text.size();
    detail::append_scalar(text, value);
    const std::size_t length = text.size() - before;
    if (!runs.empty()) {
        PendingRun& last = runs.back();
        const bool same = last.kind == RunKind::text && last.field == field && last.style == style
            && last.half_points == half_points;
        if (same) {
            last.length += length;
            return ReadState::ok;
        }
    }
    PendingRun run{};
    run.kind = RunKind::text;
    run.start = before;
    run.length = length;
    run.style = style;
    run.field = field;
    run.half_points = half_points;
    run.source = offset;
    runs.push_back(run);
    return ReadState::ok;
}

// A byte of text: skipped as \u fallback, decoded through code page 1252,
// or refused when it needs another code page.
ReadState Interpreter::text_byte(std::uint8_t byte, std::size_t offset) {
    GroupState& current = state();
    if (current.pending_skip > 0) {
        current.pending_skip -= 1U;
        return ReadState::ok;
    }
    if (current.destination == Destination::skip || current.destination == Destination::paragraph_numbering
        || current.destination == Destination::exact_block || current.destination == Destination::default_character
        || current.destination == Destination::list_table || current.destination == Destination::list_override_table
        || current.destination == Destination::field) {
        return ReadState::ok;
    }
    if (byte < 0x80U) {
        if (byte == '\t') {
            return scalar(0x09U, offset);
        }
        return scalar(byte, offset);
    }
    // Names in the font table and style sheet only classify entries; their
    // other code pages never decode into the document.
    const bool table_name = current.destination == Destination::font_table
        || current.destination == Destination::stylesheet;
    std::int32_t charset = 0;
    const FontEntry* entry = font(current.character.font < 0 ? default_font_ : current.character.font);
    if (entry != nullptr) {
        charset = (*entry).charset;
    }
    const bool windows_1252 = code_page_ == 1252 && (charset == 0 || charset == 1);
    if (!windows_1252) {
        if (table_name) {
            return ReadState::ok;
        }
        return ReadState::unsupported_encoding;
    }
    std::uint32_t value = byte;
    if (byte < 0xA0U) {
        value = windows_1252_high[static_cast<std::size_t>(byte - 0x80U)];
        if (value == 0) {
            record(LossKind::control_character_dropped, offset);
            return ReadState::ok;
        }
    }
    return scalar(value, offset);
}

ReadState Interpreter::control_symbol(std::uint8_t symbol, std::size_t offset) {
    GroupState& current = state();
    if (symbol == '*') {
        current.ignorable = true;
        return ReadState::ok;
    }
    current.first_token = false;
    switch (symbol) {
    case '{': case '}': case '\\':
        return text_byte(symbol, offset);
    case '~':
        return scalar(0x00A0U, offset);
    case '_':
        return scalar(0x2011U, offset);
    case '-':
        return ReadState::ok;
    case '\n': case '\r':
        return finish_paragraph(true, offset);
    default:
        // \: (index subentry), \| (formula) and others carry nothing.
        return ReadState::ok;
    }
}

ReadState Interpreter::control_word(std::string_view name, bool has_parameter, std::int32_t parameter,
                                    std::size_t offset) {
    if (!workspace_.effort.charge(name.size())) {
        return ReadState::effort_exceeded;
    }
    GroupState& current = state();
    const Word word = find_word(name);
    const bool first = current.first_token;
    current.first_token = false;
    // \* marks only the control word that follows it.
    const bool ignorable = current.ignorable;
    current.ignorable = false;
    if (current.destination == Destination::skip) {
        return ReadState::ok;
    }
    if (current.pending_skip > 0) {
        // A control word inside \u fallback text is skipped as one character.
        current.pending_skip -= 1U;
        return ReadState::ok;
    }
    if (first || ignorable) {
        if (enter_destination(word, offset)) {
            return ReadState::ok;
        }
        if (ignorable) {
            const bool in_table = current.destination != Destination::body
                && current.destination != Destination::field_result;
            current.destination = Destination::skip;
            if (!in_table) {
                record(LossKind::destination_skipped, offset);
            }
            return ReadState::ok;
        }
    }
    const std::int32_t value = has_parameter ? parameter : 1;
    CharacterFormat& character = current.character;
    ParagraphFormat& paragraph = current.paragraph;
    switch (current.destination) {
    case Destination::font_table:
        if (word == Word::f) {
            if (first && !current.entered) {
                current.table_part = 1;
            }
            font_entry_.id = value;
            font_entry_open_ = true;
        } else if (word == Word::fmodern) {
            font_entry_.modern = true;
        } else if (word == Word::fcharset) {
            font_entry_.charset = value;
        }
        return ReadState::ok;
    case Destination::colour_table:
        if (word == Word::red) {
            colour_components_[0] = value;
            colour_has_component_ = true;
        } else if (word == Word::green) {
            colour_components_[1] = value;
            colour_has_component_ = true;
        } else if (word == Word::blue) {
            colour_components_[2] = value;
            colour_has_component_ = true;
        }
        return ReadState::ok;
    case Destination::stylesheet:
        // Each entry group starts an entry, with or without \s: an entry
        // without \s is style 0, Normal.
        if (first && !current.entered && current.table_part == 0) {
            current.table_part = 1;
            style_entry_ = StyleEntry{};
            style_entry_open_ = true;
        }
        if (word == Word::s) {
            style_entry_.id = value;
        } else if (word == Word::fs && has_parameter && parameter > 0) {
            style_entry_.half_points = parameter;
        } else if (word == Word::cs || word == Word::ds || word == Word::ts) {
            // Character, section and table styles never set a block kind.
            style_entry_open_ = false;
            current.destination = Destination::skip;
        }
        return ReadState::ok;
    case Destination::list_table:
        if (word == Word::list) {
            current.table_part = 1;
            list_entry_ = ListEntry{};
            list_level_index_ = 0;
        } else if (word == Word::listlevel) {
            current.table_part = 2;
        } else if (word == Word::listid && current.table_part == 1U) {
            list_entry_.id = value;
        } else if ((word == Word::levelnfc || word == Word::levelnfcn) && list_level_index_ < 9U) {
            // 23 is a bullet and 255 no number; every other format numbers.
            list_entry_.levels[list_level_index_].ordered = value != 23 && value != 255;
        } else if (word == Word::levelstartat && list_level_index_ < 9U) {
            list_entry_.levels[list_level_index_].start = value;
        }
        return ReadState::ok;
    case Destination::list_override_table:
        if (word == Word::listoverride) {
            current.table_part = 1;
            override_entry_ = ListOverride{};
        } else if (word == Word::listid) {
            override_entry_.list_id = value;
        } else if (word == Word::ls) {
            override_entry_.override_id = value;
        }
        return ReadState::ok;
    case Destination::paragraph_numbering: {
        // Old-style numbering: the properties apply to the enclosing paragraph.
        ParagraphFormat& target = workspace_.groups.size() >= 2U
            ? workspace_.groups[workspace_.groups.size() - 2U].paragraph
            : paragraph;
        if (word == Word::pnlvlblt) {
            target.numbered_ordered = false;
        } else if (word == Word::pnlvlbody || word == Word::pndec || word == Word::pnucrm || word == Word::pnlcrm
                   || word == Word::pnucltr || word == Word::pnlcltr || word == Word::pnord || word == Word::pnordt
                   || word == Word::pncard) {
            target.numbered_ordered = true;
        } else if (word == Word::pnlvl) {
            target.numbered_level = std::clamp(value - 1, 0, 8);
        } else if (word == Word::pnstart) {
            target.numbered_start = value;
        }
        target.numbered = true;
        return ReadState::ok;
    }
    case Destination::field_instruction: {
        // An instruction's text still takes \uN, \ucN and character words;
        // formatting and paragraph words mean nothing inside it.
        const bool character_word = word == Word::u || word == Word::uc || word == Word::tab || word == Word::emdash
            || word == Word::endash || word == Word::bullet || word == Word::lquote || word == Word::rquote
            || word == Word::ldblquote || word == Word::rdblquote;
        if (!character_word) {
            return ReadState::ok;
        }
        break;
    }
    case Destination::field:
        return ReadState::ok;
    case Destination::default_character:
        if (word == Word::fs && has_parameter && parameter > 0) {
            default_half_points_ = parameter;
        }
        return ReadState::ok;
    case Destination::exact_block: {
        // The fields belong to the paragraph that contains the group.
        ParagraphFormat& target = workspace_.groups.size() >= 2U
            ? workspace_.groups[workspace_.groups.size() - 2U].paragraph
            : paragraph;
        if (word == Word::stxkind) {
            target.exact_kind = value;
        } else if (word == Word::stxlevel) {
            target.exact_level = value;
        } else if (word == Word::stxquote) {
            target.exact_quote = value;
        } else if (word == Word::stxmarker) {
            target.exact_marker = value;
        } else if (word == Word::stxordinal) {
            target.exact_ordinal = value;
        } else if (word == Word::stxstart) {
            target.exact_start = true;
        }
        return ReadState::ok;
    }
    default:
        break;
    }
    switch (word) {
    case Word::rtf: case Word::ansi:
        return ReadState::ok;
    case Word::mac: case Word::pc: case Word::pca:
        code_page_ = 0;
        return ReadState::ok;
    case Word::ansicpg:
        code_page_ = value;
        return ReadState::ok;
    case Word::deff:
        default_font_ = value;
        return ReadState::ok;
    case Word::uc:
        current.unicode_skip = static_cast<std::uint32_t>(std::clamp(value, 0, 16));
        return ReadState::ok;
    case Word::u: {
        std::uint32_t code = static_cast<std::uint32_t>(parameter < 0 ? parameter + 65536 : parameter);
        current.pending_skip = current.unicode_skip;
        if (code >= 0xD800U && code <= 0xDBFFU) {
            high_surrogate_ = code;
            return ReadState::ok;
        }
        if (code >= 0xDC00U && code <= 0xDFFFU) {
            if (high_surrogate_ == 0) {
                record(LossKind::control_character_dropped, offset);
                return ReadState::ok;
            }
            code = 0x10000U + ((high_surrogate_ - 0xD800U) << 10U) + (code - 0xDC00U);
        }
        high_surrogate_ = 0;
        if (code > 0x10FFFFU) {
            record(LossKind::control_character_dropped, offset);
            return ReadState::ok;
        }
        return scalar(code, offset);
    }
    case Word::f:
        if (!fonts_sorted_) {
            const FontOrder order{};
            std::sort(workspace_.fonts.begin(), workspace_.fonts.end(), order);
            fonts_sorted_ = true;
        }
        character.font = value;
        return ReadState::ok;
    case Word::plain:
        character = CharacterFormat{};
        return ReadState::ok;
    case Word::b:
        character.bold = value != 0;
        return ReadState::ok;
    case Word::i:
        character.italic = value != 0;
        return ReadState::ok;
    case Word::ul:
        character.underline = value != 0;
        return ReadState::ok;
    case Word::ul_variant:
        character.underline = value != 0;
        if (value != 0) {
            record(LossKind::underline_style_collapsed, offset);
        }
        return ReadState::ok;
    case Word::ulnone:
        character.underline = false;
        return ReadState::ok;
    case Word::strike: case Word::striked:
        character.strikethrough = value != 0;
        return ReadState::ok;
    case Word::super:
        character.position = Position::superscript;
        return ReadState::ok;
    case Word::sub:
        character.position = Position::subscript;
        return ReadState::ok;
    case Word::nosupersub:
        character.position = Position::none;
        return ReadState::ok;
    case Word::up:
        character.position = value != 0 ? Position::superscript : Position::none;
        return ReadState::ok;
    case Word::dn:
        character.position = value != 0 ? Position::subscript : Position::none;
        return ReadState::ok;
    case Word::fs:
        character.half_points = has_parameter && parameter > 0 ? parameter : body_half_points;
        return ReadState::ok;
    case Word::cf:
        character.colour = value;
        return ReadState::ok;
    case Word::cb: case Word::highlight:
        character.background = value;
        return ReadState::ok;
    case Word::v:
        character.hidden = value != 0;
        if (value != 0) {
            record(LossKind::destination_skipped, offset);
        }
        return ReadState::ok;
    case Word::visible_ignored:
        if (value != 0) {
            record(LossKind::control_word_ignored, offset);
        }
        return ReadState::ok;
    case Word::silent:
        return ReadState::ok;
    case Word::pard:
        paragraph = ParagraphFormat{};
        return ReadState::ok;
    case Word::s:
        paragraph.style = value;
        return ReadState::ok;
    case Word::outlinelevel:
        paragraph.outline = value;
        return ReadState::ok;
    case Word::ls:
        paragraph.list_override = value;
        return ReadState::ok;
    case Word::ilvl:
        paragraph.list_level = std::clamp(value, 0, 8);
        return ReadState::ok;
    case Word::ql:
        paragraph.alignment = Alignment::left;
        paragraph.justified = false;
        return ReadState::ok;
    case Word::qc:
        paragraph.alignment = Alignment::center;
        paragraph.justified = false;
        return ReadState::ok;
    case Word::qr:
        paragraph.alignment = Alignment::right;
        paragraph.justified = false;
        return ReadState::ok;
    case Word::qj:
        paragraph.alignment = Alignment::left;
        paragraph.justified = true;
        return ReadState::ok;
    case Word::li:
        paragraph.left_indent = value;
        return ReadState::ok;
    case Word::fi:
        paragraph.first_indent = value;
        return ReadState::ok;
    case Word::ri:
        if (value != 0) {
            record(LossKind::indent_dropped, offset);
        }
        return ReadState::ok;
    case Word::sb: case Word::sa:
        if (value != 0) {
            paragraph.spacing = value;
        }
        return ReadState::ok;
    case Word::intbl:
        paragraph.in_table = true;
        return ReadState::ok;
    case Word::trowd:
        if (!table_counted_) {
            record(LossKind::table_flattened, offset);
            table_counted_ = true;
        }
        return ReadState::ok;
    case Word::par: case Word::cell: case Word::nestcell:
        return finish_paragraph(true, offset);
    case Word::page: case Word::sect:
        return finish_paragraph(false, offset);
    case Word::row: case Word::nestrow:
        return ReadState::ok;
    case Word::line: {
        if (character.hidden) {
            return ReadState::ok;
        }
        PendingRun run{};
        run.kind = RunKind::line_break;
        run.start = workspace_.paragraph_text.size();
        workspace_.paragraph_runs.push_back(run);
        return ReadState::ok;
    }
    case Word::tab:
        return scalar(0x09U, offset);
    case Word::emdash:
        return scalar(0x2014U, offset);
    case Word::endash:
        return scalar(0x2013U, offset);
    case Word::bullet:
        return scalar(0x2022U, offset);
    case Word::lquote:
        return scalar(0x2018U, offset);
    case Word::rquote:
        return scalar(0x2019U, offset);
    case Word::ldblquote:
        return scalar(0x201CU, offset);
    case Word::rdblquote:
        return scalar(0x201DU, offset);
    case Word::emspace:
        return scalar(0x2003U, offset);
    case Word::enspace:
        return scalar(0x2002U, offset);
    case Word::qmspace:
        return scalar(0x2005U, offset);
    case Word::zwj:
        return scalar(0x200DU, offset);
    case Word::zwnj:
        return scalar(0x200CU, offset);
    case Word::ltrmark:
        return scalar(0x200EU, offset);
    case Word::rtlmark:
        return scalar(0x200FU, offset);
    default:
        if (word == Word::unknown) {
            record(LossKind::control_word_ignored, offset);
        }
        return ReadState::ok;
    }
}

ReadState Interpreter::flush_code(std::size_t offset) {
    if (!code_open_) {
        return ReadState::ok;
    }
    code_open_ = false;
    std::string& text = workspace_.code_text;
    if (text.empty()) {
        record(LossKind::empty_block_dropped, code_offset_);
        return ReadState::ok;
    }
    if (document_.blocks.size() >= limits_.blocks || document_.runs.size() >= limits_.runs
        || document_.text.size() + text.size() > offset_capacity) {
        return ReadState::limit_exceeded;
    }
    Block block{};
    block.kind = BlockKind::code_block;
    block.quote_depth = code_quote_;
    block.first_run = RunIndex{static_cast<std::uint32_t>(document_.runs.size())};
    block.run_count = 1;
    Run run{};
    run.start = TextOffset{static_cast<std::uint32_t>(document_.text.size())};
    run.length = TextLength{static_cast<std::uint32_t>(text.size())};
    document_.text.append(text);
    document_.runs.push_back(run);
    workspace_.run_half_points.push_back(-1);
    workspace_.run_sources.push_back(code_offset_);
    document_.blocks.push_back(block);
    text.clear();
    static_cast<void>(offset);
    return ReadState::ok;
}

ReadState Interpreter::emit_block(const Block& planned, std::size_t first_pending, std::size_t end_pending,
                                  bool heading, std::size_t offset) {
    const std::uint64_t elements = 1U + (end_pending - first_pending);
    if (!workspace_.effort.charge(elements)) {
        return ReadState::effort_exceeded;
    }
    Block block = planned;
    block.first_run = RunIndex{static_cast<std::uint32_t>(document_.runs.size())};
    workspace_.block_links.clear();
    bool merged_break = false;
    for (std::size_t index = first_pending; index < end_pending; ++index) {
        const PendingRun& pending = workspace_.paragraph_runs[index];
        if (pending.kind == RunKind::line_break) {
            if (heading) {
                // Headings are single lines; a line break reads as a space.
                merged_break = true;
                RunStyle style = document_.runs.empty() || block.run_count == 0 ? RunStyle{} : document_.runs.back().style;
                const std::size_t start = document_.text.size();
                document_.text.push_back(' ');
                if (block.run_count > 0 && document_.runs.back().kind == RunKind::text) {
                    document_.runs.back().length.value += 1U;
                } else {
                    Run run{};
                    run.start = TextOffset{static_cast<std::uint32_t>(start)};
                    run.length = TextLength{1};
                    run.style = style;
                    document_.runs.push_back(run);
                    workspace_.run_half_points.push_back(-1);
                    workspace_.run_sources.push_back(offset);
                    block.run_count += 1U;
                }
                continue;
            }
            Run run{};
            run.kind = RunKind::line_break;
            run.start = TextOffset{static_cast<std::uint32_t>(document_.text.size())};
            document_.runs.push_back(run);
            workspace_.run_half_points.push_back(-1);
            workspace_.run_sources.push_back(offset);
            block.run_count += 1U;
            continue;
        }
        RunStyle style = pending.style;
        // Headings' level stands for their size: no step applies.
        const std::int32_t half_points = heading ? -1 : pending.half_points;
        if (heading) {
            style.size_step = 0;
        }
        if (pending.field != 0) {
            // A hyperlink split across paragraphs becomes one link per block.
            std::uint32_t link = 0;
            bool known = false;
            for (std::size_t entry = 0; entry < workspace_.block_links.size(); ++entry) {
                if (workspace_.block_links[entry].field == pending.field) {
                    link = workspace_.block_links[entry].link;
                    known = true;
                    break;
                }
            }
            if (!known) {
                const std::string& target = workspace_.field_targets[pending.field - 1U];
                if (workspace_.links.count() >= limits_.links) {
                    return ReadState::limit_exceeded;
                }
                link = workspace_.links.add(target);
                BlockLink entry{};
                entry.field = pending.field;
                entry.link = link;
                workspace_.block_links.push_back(entry);
            }
            style.link = LinkIndex{link};
        }
        if (document_.text.size() + pending.length > offset_capacity) {
            return ReadState::limit_exceeded;
        }
        const std::string_view text(workspace_.paragraph_text.data() + pending.start, pending.length);
        if (block.run_count > 0) {
            Run& last = document_.runs.back();
            const bool same = last.kind == RunKind::text && last.style == style
                && workspace_.run_half_points.back() == half_points;
            if (same) {
                document_.text.append(text);
                last.length.value += static_cast<std::uint32_t>(pending.length);
                continue;
            }
        }
        if (document_.runs.size() >= limits_.runs) {
            return ReadState::limit_exceeded;
        }
        Run run{};
        run.start = TextOffset{static_cast<std::uint32_t>(document_.text.size())};
        run.length = TextLength{static_cast<std::uint32_t>(pending.length)};
        run.style = style;
        document_.text.append(text);
        document_.runs.push_back(run);
        workspace_.run_half_points.push_back(half_points);
        workspace_.run_sources.push_back(pending.source);
        block.run_count += 1U;
    }
    if (merged_break) {
        record(LossKind::structure_flattened, offset);
    }
    if (document_.blocks.size() >= limits_.blocks) {
        return ReadState::limit_exceeded;
    }
    document_.blocks.push_back(block);
    return ReadState::ok;
}

// Ends the paragraph being assembled. explicit_end is set for \par and
// \cell; an empty paragraph there is spacing the model cannot hold.
ReadState Interpreter::finish_paragraph(bool explicit_end, std::size_t offset) {
    std::vector<PendingRun>& runs = workspace_.paragraph_runs;
    const ParagraphFormat paragraph = state().paragraph;
    if (!paragraph.in_table) {
        table_counted_ = false;
    }
    // Trailing line breaks cannot end a block.
    std::size_t end = runs.size();
    while (end > 0 && runs[end - 1U].kind == RunKind::line_break) {
        end -= 1U;
    }
    bool has_text = false;
    for (std::size_t index = 0; index < end; ++index) {
        if (runs[index].kind == RunKind::text) {
            has_text = true;
            break;
        }
    }
    std::uint8_t style_level = 0;
    StyleKind kind = style_kind(paragraph.style, style_level);
    std::uint8_t quote = 0;
    if (kind == StyleKind::quote) {
        const std::int32_t levels = paragraph.left_indent / twips_per_quote_level;
        quote = static_cast<std::uint8_t>(std::clamp(levels, 1, static_cast<std::int32_t>(maximum_quote_depth)));
    }
    if (paragraph.exact) {
        quote = static_cast<std::uint8_t>(std::clamp(paragraph.exact_quote, 0, static_cast<std::int32_t>(maximum_quote_depth)));
        kind = paragraph.exact_kind == static_cast<std::int32_t>(BlockKind::code_block) ? StyleKind::code : StyleKind::normal;
    }
    ReadState result = ReadState::ok;
    if (kind == StyleKind::code) {
        const bool new_block = paragraph.exact && paragraph.exact_start;
        if (code_open_ && (code_quote_ != quote || new_block)) {
            result = flush_code(offset);
            if (result != ReadState::ok) {
                return result;
            }
        }
        if (code_open_) {
            workspace_.code_text.push_back('\n');
        } else {
            code_open_ = true;
            code_quote_ = quote;
            code_offset_ = offset;
            workspace_.code_text.clear();
        }
        for (std::size_t index = 0; index < runs.size(); ++index) {
            if (runs[index].kind == RunKind::line_break) {
                workspace_.code_text.push_back('\n');
            } else {
                workspace_.code_text.append(workspace_.paragraph_text, runs[index].start, runs[index].length);
            }
        }
        runs.clear();
        workspace_.paragraph_text.clear();
        return ReadState::ok;
    }
    result = flush_code(offset);
    if (result != ReadState::ok) {
        return result;
    }
    if (!has_text) {
        if (explicit_end && body_started_) {
            record(LossKind::spacing_dropped, offset);
        }
        runs.clear();
        workspace_.paragraph_text.clear();
        return ReadState::ok;
    }
    Block block{};
    block.quote_depth = quote;
    block.alignment = paragraph.alignment;
    if (paragraph.justified) {
        record(LossKind::justify_read_as_left, offset);
    }
    if (paragraph.spacing != 0) {
        record(LossKind::spacing_dropped, offset);
    }
    bool heading = false;
    ListLevel level_format{};
    const bool modern_list = paragraph.list_override > 0;
    const bool listed = modern_list || paragraph.numbered;
    const bool exact_heading = paragraph.exact && paragraph.exact_kind == static_cast<std::int32_t>(BlockKind::heading);
    const bool exact_item = paragraph.exact && paragraph.exact_kind == static_cast<std::int32_t>(BlockKind::list_item);
    if (paragraph.exact && !exact_heading && !exact_item) {
        // An exact paragraph: no style, outline or list applies.
    } else if (exact_heading) {
        heading = true;
        block.kind = BlockKind::heading;
        block.level = static_cast<std::uint8_t>(std::clamp(paragraph.exact_level, 1, static_cast<std::int32_t>(maximum_heading_level)));
    } else if (exact_item) {
        block.kind = BlockKind::list_item;
        std::int32_t allowed = 0;
        if (!document_.blocks.empty()) {
            const Block& previous = document_.blocks.back();
            if (previous.kind == BlockKind::list_item && previous.quote_depth == quote) {
                allowed = previous.level + 1;
            }
        }
        std::int32_t level = std::clamp(paragraph.exact_level, 0, static_cast<std::int32_t>(maximum_list_level));
        if (level > allowed) {
            level = allowed;
            record(LossKind::structure_flattened, offset);
        }
        block.level = static_cast<std::uint8_t>(level);
        if (paragraph.exact_marker == static_cast<std::int32_t>(ListMarker::ordered)) {
            block.marker = ListMarker::ordered;
            block.ordinal = static_cast<std::uint32_t>(std::clamp(paragraph.exact_ordinal, 0, static_cast<std::int32_t>(999999999)));
        }
    } else if (paragraph.outline >= 0 && paragraph.outline <= 5) {
        heading = true;
        block.kind = BlockKind::heading;
        block.level = static_cast<std::uint8_t>(paragraph.outline + 1);
    } else if (kind == StyleKind::heading) {
        heading = true;
        block.kind = BlockKind::heading;
        block.level = style_level;
    } else if (listed) {
        block.kind = BlockKind::list_item;
        std::int32_t list_key = -1;
        std::int32_t level = 0;
        bool ordered = false;
        std::int32_t start = 1;
        if (modern_list) {
            list_key = paragraph.list_override;
            level = paragraph.list_level;
            if (list_level(paragraph.list_override, paragraph.list_level, level_format)) {
                ordered = level_format.ordered;
                start = level_format.start;
            }
        } else {
            level = paragraph.numbered_level;
            ordered = paragraph.numbered_ordered;
            start = paragraph.numbered_start;
        }
        // The model's nesting is written under a parent: at most one level
        // deeper than the previous list item at the same quote depth.
        std::int32_t allowed = 0;
        if (!document_.blocks.empty()) {
            const Block& previous = document_.blocks.back();
            if (previous.kind == BlockKind::list_item && previous.quote_depth == quote) {
                allowed = previous.level + 1;
            }
        }
        std::int32_t clamped = std::min(level, static_cast<std::int32_t>(maximum_list_level));
        if (clamped > allowed) {
            clamped = allowed;
            record(LossKind::structure_flattened, offset);
        }
        block.level = static_cast<std::uint8_t>(clamped);
        if (ordered) {
            block.marker = ListMarker::ordered;
            std::vector<Counter>& counters = workspace_.counters;
            std::size_t keep = 0;
            std::uint32_t number = static_cast<std::uint32_t>(std::max(start, 0));
            for (std::size_t index = 0; index < counters.size(); ++index) {
                const Counter counter = counters[index];
                if (counter.list == list_key && counter.level > level) {
                    continue;
                }
                if (counter.list == list_key && counter.level == level) {
                    number = counter.next;
                    continue;
                }
                counters[keep] = counter;
                keep += 1U;
            }
            counters.resize(keep);
            Counter counter{};
            counter.list = list_key;
            counter.level = level;
            counter.next = number + 1U;
            counters.push_back(counter);
            block.ordinal = std::min(number, maximum_ordinal);
        }
    } else if ((paragraph.left_indent != 0 || paragraph.first_indent != 0) && kind != StyleKind::quote) {
        record(LossKind::indent_dropped, offset);
    }
    result = emit_block(block, 0, end, heading, offset);
    runs.clear();
    workspace_.paragraph_text.clear();
    return result;
}

}

RtfReader::RtfReader(const Limits& limits) : limits_(limits), workspace_(std::make_unique<Workspace>()) {
    (*workspace_).groups.reserve(static_cast<std::size_t>(limits.group_depth) + 2U);
}

RtfReader::~RtfReader() {}

BodySize RtfReader::last_body() const noexcept {
    const BodySize body = (*workspace_).body;
    return body;
}

Result RtfReader::read(std::span<const std::byte> rtf, Document& document, LossReport& loss) {
    Result result{};
    document.clear();
    loss.clear();
    if (rtf.size() > limits_.input_bytes) {
        result.status = Status::limit_exceeded;
        return result;
    }
    Workspace& workspace = *workspace_;
    workspace.groups.clear();
    workspace.fonts.clear();
    workspace.colours.clear();
    workspace.styles.clear();
    workspace.lists.clear();
    workspace.overrides.clear();
    workspace.counters.clear();
    workspace.fields.clear();
    workspace.field_targets.clear();
    workspace.size_counts.clear();
    workspace.run_half_points.clear();
    workspace.run_sources.clear();
    workspace.body = BodySize{};
    workspace.entry_name.clear();
    workspace.paragraph_text.clear();
    workspace.paragraph_runs.clear();
    workspace.code_text.clear();
    workspace.links.clear();
    const std::uint64_t allowance = static_cast<std::uint64_t>(limits_.work_per_input_byte) * (rtf.size() + 1U);
    workspace.effort.reset(allowance);
    ReadState state = ReadState::ok;
    SourceOffset stop{};
    try {
        Interpreter interpreter(workspace, document, loss, limits_);
        state = interpreter.run(rtf, stop);
    } catch (const std::bad_alloc&) {
        state = ReadState::limit_exceeded;
    }
    result.stop = stop;
    result.work = WorkCount{workspace.effort.used()};
    if (state == ReadState::ok) {
        const NormalFormCheck check = check_normal_form(document);
        if (check.violation != NormalFormViolation::none) {
            // A reader defect: never present a document outside normal form.
            document.clear();
            result.status = Status::invalid_document;
            return result;
        }
        result.status = Status::ok;
        return result;
    }
    document.clear();
    switch (state) {
    case ReadState::malformed:
        result.status = Status::malformed;
        break;
    case ReadState::unsupported_encoding:
        result.status = Status::unsupported_encoding;
        break;
    case ReadState::effort_exceeded:
        result.status = Status::effort_exceeded;
        break;
    default:
        result.status = Status::limit_exceeded;
        break;
    }
    return result;
}

}
