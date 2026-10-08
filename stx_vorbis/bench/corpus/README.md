# Original benchmark input

These are original generated test signals, not recordings or third-party media.
`tools/reference.cpp` generates sine waves, seeded noise and impulses; libvorbis
1.3.7 encodes them. Files are checked in so different platforms decode identical
bytes even if their encoder/math-library output differs. They are not installed
or linked into the shipping library.

```sh
stx_vorbis_reference encode stereo-10s.ogg 2 48000 480017 0.5
stx_vorbis_reference encode surround-10s.ogg 6 48000 480017 0.4
```

Each contains 480,017 PCM frames at 48 kHz (10.000354 seconds). SHA-256:

| File | Hash |
|---|---|
| stereo-10s.ogg | `f3bec4da04a0e61675b1f71dc55e41605826b1cb5150265c25d78597d26c4e16` |
| surround-10s.ogg | `8b02847d4d3bab7e5034340c0e8048ac74a5972343e853aef2e3a6ea2ad8f3fa` |
