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
out `stx_vorbis`. `tools/resampler_face_study` is `stx_conv`'s image-face
study, built on its own (`.github/workflows/stx-conv.yml`).

The toolchains (LLVM 22.1.8; the pinned CLANG64 archive on Windows) and the
house-style and lifetime checkers come from template_harness at the commit
`harness.lock` pins.

The stx libraries are MIT licensed (LICENSE); each package's LICENSE also
carries the notices of the work it derives from.
