# FreeType 2.14.2 intake

- Upstream: https://gitlab.freedesktop.org/freetype/freetype (official mirror
  https://github.com/freetype/freetype).
- Pinned commit: `f4205da14867c5387cd6a329b90ee10a6df6eeff`, tree
  `eae964ec343082cde899593d5011f5ea4ffa44da`, the pin of GUI.Forms'
  `third_party/fetch_text_stack.sh` (FreeType 2.14.2) when it moved here on
  2026-11-05.
- Kept: the whole tree, 744 files.
- Left out: `subprojects/dlg`, a git submodule link (FreeType's optional debug
  logging library, which GUI.Forms' build does not use). A submodule's content
  is not part of the pinned tree, so nothing of it was ever fetched.

## How the copy was made and checked

No download. The source was the checkout `fetch_text_stack.sh` had already
fetched into a GUI.Forms workspace (`D:/gui_forms_int/gui_forms/third_party/freetype`),
clean and detached at the pin. `git archive` of the pinned commit, with
`core.autocrlf=false` so no line ending was converted, wrote the tree; the
executable bits were restored in the index. Every blob stx records under
`third_party/freetype/` was then compared with the pinned commit's blob at the
same path and mode: 744 equal, none different or extra, and the only path
missing is `subprojects/dlg`. Because git names a blob by its content, equal
blob ids mean byte-identical files.

## Licence

FreeType is offered under two mutually exclusive licences, the FreeType
License (`docs/FTL.TXT`) or the GNU General Public License version 2
(`docs/GPLv2.TXT`), at the user's choice (`LICENSE.TXT`). **stx and GUI.Forms
use FreeType under the FreeType License (FTL).** The FTL asks that a product's
documentation credit the FreeType project; GUI.Forms carries that credit in its
THIRD_PARTY_NOTICES.md and installs `docs/FTL.TXT` with its SDK. Some files
carry their own notices (`LICENSE.TXT` lists them); they stay with those files.
