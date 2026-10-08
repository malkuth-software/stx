#pragma once

// Private foreign boundary. Prefix the embedded provider's C symbols and opaque
// types so an application can also link its own BFFT without symbol collisions.
// This header is never installed or exposed by the public stx headers.
#define bodft_prepared_plan stx_vorbis_bodft_prepared_plan
#define bodft_workspace stx_vorbis_bodft_workspace
#define bodft_query_storage stx_vorbis_bodft_query_storage
#define bodft_prepare stx_vorbis_bodft_prepare
#define bodft_prepare_workspace stx_vorbis_bodft_prepare_workspace
#define bodft_forward_prepared stx_vorbis_bodft_forward_prepared
#define bodft_inverse_prepared stx_vorbis_bodft_inverse_prepared
#define bodft_forward_prepared_f32 stx_vorbis_bodft_forward_prepared_f32
#define bodft_inverse_prepared_f32 stx_vorbis_bodft_inverse_prepared_f32
#include <bfft/bodft.h>
