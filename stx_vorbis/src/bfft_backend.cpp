// Compile the unmodified, pinned provider as a private part of libstx_vorbis.
// Namespace prefixes also isolate its C++ kernel instantiations from a separate
// BFFT linked by a consumer. Numerical flags match the rest of the codec.
#include "bfft_api.hpp"
#define bodft stx_vorbis_bodft_detail
#define bruun stx_vorbis_bruun_detail
#include "../third_party/bfft/src/bodft_prepared.cpp"
