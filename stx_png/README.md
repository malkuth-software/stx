# stx_png

`stx_png` is the independently buildable and installable owned PNG codec. It
depends only on [`stx_zlib`](../stx_zlib/README.md). The owner chose its
boundary on 2026-10-20 (decision I4, option 1): the codec lives here, and the
consumer's policy stays with the consumer. GUI.Forms keeps its colour-profile
rules, colour conversion, request limits and clipboard handling in
`src/core/image/`.

## Surface

- `stx_png/decoder.hpp`: a bounded, allocation-free decoder for every legal
  static PNG colour type and depth, including Adam7.
  - It produces top-down, straight-alpha RGBA16 samples. It applies no
    gamma, ICC or colour management.
  - `inspect_png_samples` validates the structure and reports the exact
    storage needed.
  - `decode_png_samples` also returns decoded metadata.
    `decode_png_samples_discarding_metadata` validates compressed metadata
    through a small scratch and stores none of it.
  - `measure_png_samples` reports the exact size of the decoded metadata.
  - The caller owns all storage, which must not overlap. Limits are finite.
    Output is unverified until success.
- `stx_png/encoder.hpp`: `encode_png` writes 8-bit truecolor or truecolor
  with alpha. It takes straight, premultiplied or opaque RGBA or BGRA input.
  - Each row gets the filter with the minimum sum of |signed byte|.
  - The IDAT is compressed with stx_zlib, at default level 6.
  - The output is deterministic: the same bytes on every host.
- `stx_png/pixels.hpp`: `convert_rgba16_samples` converts the samples to
  8-bit RGBA or BGRA, straight or premultiplied. It uses an exact
  nearest-integer rule, stated in the header.

## Build, test and install

```text
cmake -S stx_zlib -B build/stx_zlib -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build/stx_zlib && cmake --install build/stx_zlib
cmake -S stx_png -B build/stx_png -DCMAKE_PREFIX_PATH=<prefix> -DCMAKE_INSTALL_PREFIX=<prefix>
cmake --build build/stx_png
ctest --test-dir build/stx_png --output-on-failure
cmake --install build/stx_png
```

The installed package exports `stx_png::stx_png`. Its config finds
`stx_zlib`. `tests/installed` is a consumer that uses only
`find_package(stx_png CONFIG)`; CI builds it against a relocated prefix
(`.github/workflows/stx-packages.yml`). The licence and provenance are
installed under `share/licenses/stx_png/`.

The deeper evidence lives in the GUI.Forms suite, which builds this
package as a subdirectory:
- `gui_forms_png_decoder_tests`;
- `gui_forms_png_pixels_tests` and `gui_forms_png_writer_tests`, which check
  identity hashes on every CI platform;
- the malformed-input sweeps;
- the libFuzzer smoke run in `fuzz-smoke.yml`.
