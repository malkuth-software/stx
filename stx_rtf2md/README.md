# stx_rtf2md

`stx_rtf2md` is GUI.Forms' first-party rich-text library: one document
model, a markdown-like dialect read and written losslessly, and RTF read
and written by meaning. `RichTextBox` and SwiftEdit both use it. It has no
dependency outside this directory. The stage plan is
`planning/stages/RT-rich-text/PLAN.md`; the owner's decisions are its
section 12. This page is normative for the dialect: the reader and writer
implement exactly what it says, and a difference is a defect.

Status: RT1 in progress. The model, normal form and limits are built; the
markdown writer and reader follow. RTF arrives in RT2 and RT3.

## The model

`stx_rtf2md/document.hpp` defines `Document`: blocks of runs over one UTF-8
string, plus a link table. `check_normal_form` states every rule; the main
ones are these:

- Block kinds are paragraph, heading (levels 1 to 6), list item (bullet or
  ordered, nesting 0 to 7, with the number written on the marker) and code
  block. Any block may be quoted to depth 7. Paragraphs, headings and list
  items may be aligned left, centre or right.
- A run is styled text or a line break. Runs carry bold, italic,
  underline, strikethrough, inline code, superscript or subscript, a link,
  a colour, a background colour and a size step from -2 to +4. Step 0 is the
  document's body size.
- A code block is one unstyled text run. Its text holds its lines joined by
  line feeds and may contain blank lines.
- Blanks are kept wherever they occur. No text contains a line feed except
  a code block's.
- A line break may open a block or follow another line break. It may not
  end a block, and headings have none.
- A list item at nesting level L > 0 directly follows a list item at the
  same quote depth whose level is at least L - 1, so nesting is always
  written under a parent.

## The dialect

The dialect is CommonMark 0.31.2 restricted to the constructs below, plus
GitHub's `~~strikethrough~~`, plus attribute spans and block attribute
lines. Text is UTF-8; lines end in LF, CRLF or CR.

### Blocks

| Construct | Read | Written |
|---|---|---|
| Paragraph | consecutive non-blank lines that start no other block; a soft line break reads as one space | the block's text on one line |
| ATX heading | 1 to 6 `#`, a space, the text; an optional closing `#` sequence is removed | `#` × level, one space, the text |
| Setext heading | a paragraph line followed by a line of `=` (level 1) or `-` (level 2) | never |
| Bullet list item | `-`, `*` or `+`, then 1 to 4 spaces | `-` and one space |
| Ordered list item | 1 to 9 digits, `.` or `)`, then 1 to 4 spaces; the number is kept as written | the ordinal, `.`, one space |
| Nesting | a list item indented to its parent item's content column is one level deeper | indent by the parent marker's width plus one (2 for `-`, 3 for `1.`, 4 for `10.`) |
| Block quote | `>` and an optional space, up to 3 spaces before it; nested quotes repeat it | `> ` × depth before every line of the block |
| Fenced code | 3 or more backticks or tildes; the closing fence has the same character and at least the same length; unclosed fences end at the end of their container | a backtick fence one longer than the longest backtick run in the text, at least 3 |
| Indented code | lines indented 4 or more spaces that do not continue a paragraph | never |
| Block attribute line | a line holding only `{align=left}`, `{align=center}` or `{align=right}`, at the start of a block position (never interrupting a paragraph), immediately followed by a paragraph, heading or list item in the same container | before a paragraph, heading or list item aligned centre or right, at the block's indentation |
| Blank line | separates blocks | exactly one between consecutive blocks |

The writer ends the document with one LF and writes no trailing spaces.

### Inlines

| Construct | Read | Written |
|---|---|---|
| Emphasis | `*`/`_` delimiter runs with CommonMark's flanking and rule-of-three rules; strong is bold and emphasis is italic | `**` for bold and `*` for italic, nested in the order bold, italic, strikethrough |
| Strikethrough | `~~` delimiter runs with the flanking rules of `*`; a single `~` is text | `~~` |
| Inline code | a backtick string closed by one of the same length; line endings become spaces; one leading and one trailing space are removed when both are present and the content is not all spaces | a backtick string one longer than the longest backtick run inside, with one space of padding on each side when the text starts or ends with a backtick or a space and is not all spaces |
| Link | `[text](destination)` or `[text](<destination>)`, with an optional title, which is dropped and counted; `<scheme:...>` and `<user@host>` autolinks | `[text](destination)`, or the `<destination>` form when the target holds a space, `(`, `)` or `<`; `<` and `>` in a bracketed destination are escaped |
| Attribute span | `[content]{attributes}`, the `{` directly after the `]`; see below | see below |
| Hard line break | `\` at the end of a line, or two or more spaces at the end of a line | `\` at the end of the line |
| Backslash escape | `\` before ASCII punctuation is that character; before any other character it is a backslash | before each of ``\ ` * _ [ ] ~ < { &`` anywhere, and before `# > - + = |` and a digit run's `.` or `)` at the start of a line |
| Character reference | `&#` decimal `;` and `&#x` hexadecimal `;` for any scalar the model admits | `&#32;` for a space and `&#9;` for a tab at the start of a line or the end of a block; nothing else |

A link's text may hold any inline construct except another link. Inline
code is an attribute like the others: a code span may be emphasised,
spanned or linked.

### Attribute spans

An attribute span gives its content the listed attributes. The attribute
list is one or more items separated by spaces:

| Item | Attribute |
|---|---|
| `u` | underline |
| `sup`, `sub` | superscript, subscript (not both) |
| `color=#rrggbb` | text colour, six hexadecimal digits, read in either case |
| `bg=#rrggbb` | background colour |
| `size=+1` to `size=+4`, `size=-1`, `size=-2` | size step |
| `b`, `i`, `s` | bold, italic, strikethrough (the writer's fallback, below) |

Any other item, a repeated item, or `sup` with `sub` makes the whole
`[...]{...}` ordinary text: its brackets stay literal, and its content is
read as ordinary inlines. Spans may nest. An inner span adds its
attributes, and its colour, background or size replaces an outer value.

The writer writes one span for each maximal sequence of runs whose span
attributes (underline, super- or subscript, colours and size) are equal and
not all default. It lists the items in the order of the table, with single
spaces and lowercase hexadecimal digits. Inside a span, emphasis and code
are written as usual. A link is written outside any span it contains:
`[[text]{u}](target)`.

**Emphasis fallback.** CommonMark's flanking rules cannot place `**`, `*`
and `~~` at every run boundary. For example, bold "foo." followed by plain
"bar" cannot be written `**foo.**bar`. The writer first writes a block's
emphasis with delimiters, then reads the block back. If the runs differ,
it writes that block's bold, italic and strikethrough as the span items
`b`, `i` and `s` instead, merged into the span attributes above.

### Outside the dialect

These are read as literal text, never interpreted, and counted in the loss
report: raw HTML (`html_kept_as_text`), images (`image_kept_as_text`), link
reference definitions (`reference_link_kept_as_text`), tables
(`table_kept_as_text`), task-list markers (`task_marker_kept_as_text`),
footnote references (`footnote_kept_as_text`), named character references,
and numeric references to characters the model forbids
(`entity_kept_as_text`), and thematic breaks
(`thematic_break_kept_as_text`).

Structure the model cannot hold is flattened and counted
(`structure_flattened`). This covers a second paragraph or a code block
inside a list item, a list or heading inside a list item, and a quote
inside a list item. The text is kept, as blocks at the nearest
representable position. Blocks with no text (an empty heading, list item
or code block) are dropped and counted (`empty_block_dropped`). A fence's
info string is dropped and counted (`code_language_dropped`). A link title
is dropped and counted (`link_title_dropped`). A forbidden control
character is dropped and counted (`control_character_dropped`). A leading
U+FEFF is ignored.

### Round trip

- `write_markdown(read_markdown(m))` is the canonical form of `m`, and
  writing is idempotent. For any document `d` in normal form,
  `read_markdown(write_markdown(d))` equals `d`.
- In a CommonMark or GitHub renderer, attribute spans show as literal
  `[text]{u}` and block attribute lines as a paragraph reading
  `{align=center}`. Nothing is hidden.
