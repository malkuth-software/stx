# Pinned BODFT source subset

Unmodified files from `falseywinchnet/bfft` commit
`0f75ca79fbdc9176af729fc67afed59dca436ff1` (merged through provider PR #90).
`provenance.json` records SHA-256 after normalizing CRLF to LF. CMake verifies
every listed file and reconfigures when these inputs change.

Only `src/bodft_prepared.cpp` is compiled, through the first-party private
`../../src/bfft_backend.cpp` wrapper. It uses the caller-provisioned API and
does not compile the provider's legacy allocation-owning implementation.
The wrapper prefixes C entry points, opaque types and internal C++ namespaces;
this permits a consumer to link an independent BFFT version without symbol
collisions. No modified provider source or generated source projection is used.

This directory is vendor code, excluded from the first-party house-style scan.
The codec installs this MIT license and provenance alongside its own license.
Updating it requires an explicit provider revision and reviewed hashes; no
consumer build fetches a moving branch or downloads BFFT.
