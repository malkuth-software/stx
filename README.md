# stx

The stx libraries: small, self-contained C++20 packages written for GUI.Forms
and usable without it.

| Package | What it is |
|---|---|
| `stx_zlib` | DEFLATE and zlib streams (inflate, deflate, checksums) |
| `stx_png` | PNG decoding and encoding (through `stx_zlib`) |
| `stx_conv` | one- and two-dimensional resampling |
| `stx_rtf2md` | rich text: RTF and Markdown reading and writing |
| `stx_vorbis` | Ogg Vorbis decoding |
| `stx_skia` | the CPU-only Skia derivative GUI.Forms draws with (pruned and converted from Skia `chrome/m152`) |

Each directory is a complete CMake package: configure it alone, or install
it and consume it with `find_package(<package> CONFIG)`. Its own README and
LICENSE (and PROVENANCE.md, where it derives from other work) describe it. The root
`CMakeLists.txt` builds all of them in one tree:

```text
cmake -S . -B .build/stx -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build .build/stx
ctest --test-dir .build/stx --output-on-failure
cmake --install .build/stx --prefix <prefix>
```

`-DSTX_BUILD_TESTS=OFF` leaves the tests out; `-DSTX_WITH_VORBIS=OFF` leaves
out `stx_vorbis`, `-DSTX_WITH_SKIA=OFF` leaves out `stx_skia`, and
`-DSTX_SKIA_SWIZZLE_ONLY=ON` builds and installs only its red/blue swap kernel
(`stx_skia::swizzle`, for a consumer that draws without Skia). `stx_skia`'s
own profiles (the reference comparison, the instruction-set comparisons and
the recorded timings) are `.github/workflows/stx-skia.yml`; its README,
PROVENANCE.md and AGENTS.md describe it, and its house-style allowance is
checked by `tools/check_stx_skia_style.py`. `tools/resampler_face_study` is `stx_conv`'s image-face
study, built on its own (`.github/workflows/stx-conv.yml`). The codecs'
oracle, fuzz and benchmark scripts are in `tools/` (`tools/CODEC_EXPERIMENTS.md`);
`codec-differential.yml` runs the oracle comparisons weekly and on dispatch.

`third_party/` holds pinned upstream sources that stx's consumers build from
a local copy instead of downloading them: FreeType 2.14.2 and HarfBuzz 14.6.0
(GUI.Forms' text stack), libunibreak 8.0, MD4C 0.6.0, SheenBidi 2.9.0, the
Unicode 17.0.0 grapheme data's licence and provenance, and the Noto fallback
fonts. Each is the upstream tree at its pin, byte for byte,
except the paths its intake note leaves out; `third_party/README.md` lists them
with their licences.

The toolchains (LLVM 22.1.8; the pinned CLANG64 archive on Windows) and the
house-style and lifetime checkers come from template_harness at the commit
`harness.lock` pins.

The stx libraries are MIT licensed (LICENSE); each package's LICENSE also
carries the notices of the work it derives from.
