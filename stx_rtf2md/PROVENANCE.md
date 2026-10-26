# stx_rtf2md provenance

All source, tests and fixtures in this directory are newly written for
GUI.Forms under the project licence (`LICENSE`). No third-party code is
copied, linked or fetched.

Specifications consulted, not copied:

- Rich Text Format (RTF) Specification, version 1.9.1 (Microsoft, 2008):
  the grammar of groups, control words, control symbols and destinations,
  and the meaning of the control words listed in the stage plan.
- CommonMark Spec, version 0.31.2: block and inline structure, the
  delimiter-run rules for emphasis, backslash escapes and numeric
  character references.
- GitHub Flavored Markdown Spec: the strikethrough extension.

The dialect's extensions (attribute spans and block attribute lines) are
this library's own; `README.md` defines them.

## Test corpus

`tests/corpus/*.rtf` are authored for this repository, not saved by or
copied from any program. Each reproduces the control-word patterns one
producer is known to write, from the RTF specification and general
knowledge of the format:

| File | Patterns of |
|---|---|
| `wordpad.rtf` | WordPad / RichEdit 2.0: `\nouicompat`, `\pn` bullets with `{\pntext}`, `\sa`/`\sl` spacing |
| `word.rtf` | Microsoft Word: `{\*\defchp}`, a style sheet with heading and Quote styles, list and override tables, `\rsid` noise, a HYPERLINK field, `\info` |
| `textedit.rtf` | macOS TextEdit (Cocoa): `\cocoartf`, `{\*\expandedcolortbl}`, `{\*\levelmarker}`, backslash-newline paragraphs, `\uc0\uN` |
| `richtextbox.rtf` | Windows Forms RichTextBox: an 8.5-point `\fs17` body, `\line`, escaped braces |

`tests/corpus/*.md` are the reviewed markdown each file converts to. Real
saves from those programs are added only at the owner's request (owner
decision 4, 2026-10-26).
