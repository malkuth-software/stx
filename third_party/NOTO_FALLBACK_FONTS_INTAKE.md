# Noto fallback fonts intake

GUI.Forms' showcase fallback fonts, which its `third_party/fetch_showcase_fallback_fonts.sh`
downloaded by URL and checked by SHA-256, moved here on 2026-11-05 (owner: the
main repository carries no third-party sources). No download: the files are
GUI.Forms' checked-in copies (`assets/fonts/` at `7f5d4648`), each checked
against the digest the fetch script pinned.

| File | Upstream (repository, commit, path) | SHA-256 |
|---|---|---|
| `fonts/noto/NotoSansCJKjp-Regular.otf` | notofonts/noto-cjk `f8d157532fbfaeda587e826d4cd5b21a49186f7c` `Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf` | `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5` |
| `fonts/noto/NotoEmoji-Regular.ttf` | googlefonts/noto-emoji `9a5261d871451f9b5183c93483cbd68ed916b1e9` `fonts/NotoEmoji-Regular.ttf` | `415dc6290378574135b64c808dc640c1df7531973290c4970c51fdeb849cb0c5` |
| `fonts/noto/OFL-NotoSansCJKjp.txt` | notofonts/noto-cjk `f8d157532fbfaeda587e826d4cd5b21a49186f7c` `Sans/LICENSE` | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` |
| `fonts/noto/OFL-NotoEmoji.txt` | googlefonts/noto-emoji `9a5261d871451f9b5183c93483cbd68ed916b1e9` `fonts/LICENSE` | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` |

All four digests matched. Both fonts are under the SIL Open Font License 1.1
(the two licence files are the same text); a program that ships a font ships
its OFL file beside it. The names are unchanged, so no Reserved Font Name
question arises.
