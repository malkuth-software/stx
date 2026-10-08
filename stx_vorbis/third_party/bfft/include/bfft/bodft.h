#ifndef BFFT_BODFT_H
#define BFFT_BODFT_H

/* Public C ABI for the BODFT kernel: the native half-bin-shifted real transform
   (odd-frequency DFT). This header lives alongside <bfft/bfft.h> and reuses its
   complex and status types.

   BODFT is a first-class half-bin spectral primitive. Forward maps N real
   samples to N/2 packed complex bins:

       H[k] = sum_{n=0}^{N-1} x[n] * exp(-2*pi*i*(k+1/2)*n/N),   k = 0..N/2-1.

   The upper half is recovered by H[N-1-k] = conj(H[k]). Inverse maps the N/2
   packed bins back to the N real samples exactly. */

#include <bfft/bfft.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque BODFT plan. Create with bodft_plan_create, destroy with
   bodft_plan_destroy. A plan serves both double and single precision. */
typedef struct bodft_plan bodft_plan;

/* Explicitly provisioned API. Plan storage is immutable after preparation;
   execution scratch belongs to a separate workspace. No function in this API
   allocates, frees, retains input/output, or uses a global allocator. */
typedef struct bodft_prepared_plan bodft_prepared_plan;
typedef struct bodft_workspace bodft_workspace;
typedef enum bodft_precision {
    BODFT_PRECISION_F64 = 0,
    BODFT_PRECISION_F32 = 1
} bodft_precision;
typedef struct bodft_storage_requirements {
    size_t plan_bytes;
    size_t plan_alignment;
    size_t workspace_bytes;
    size_t workspace_alignment;
} bodft_storage_requirements;

/* Query before allocating. Counts include opaque metadata and all alignment
   padding. Unsupported sizes/overflow return INVALID_ARGUMENT and preserve
   requirements. Power-of-two N >= 2; the implementation index bound is INT_MAX. */
bfft_status bodft_query_storage(size_t n, bodft_precision precision,
                                bodft_storage_requirements* requirements);

/* Supply disjoint, aligned storage of at least the queried size. On failure
   the returned handle is NULL. Validation failures leave storage unchanged.
   The caller owns these allocations and must not move, modify, or free them
   while handles are in use. No destroy call is needed: stored objects have
   trivial destructors. A new preparation may reuse storage after all prior
   calls and borrows end. Preparation output-handle slots must not overlap the
   supplied storage (or the plan when preparing a workspace). An overlapping
   output slot is itself invalid and is left unchanged, to preserve storage. */
bfft_status bodft_prepare(size_t n, bodft_precision precision,
                          void* storage, size_t storage_bytes,
                          const bodft_prepared_plan** plan);
bfft_status bodft_prepare_workspace(const bodft_prepared_plan* plan,
                                    void* storage, size_t storage_bytes,
                                    bodft_workspace** workspace);

/* Plan may be shared across threads; each concurrent call needs a distinct
   workspace. Workspaces are reusable with plans of the same N and precision.
   Counts are element capacities: input/output need N or N/2 live elements as
   appropriate. Only these active prefixes are read/written. Plan, workspace,
   active input and active output must all be disjoint. Wrong precision, shape,
   or overlap is rejected before mutating output or workspace. Floating-point
   values, including exceptional values, are transformed without validation.
   Caller storage alignment is checked at preparation; typed input/output must
   also have their natural alignment. Forward is unnormalized; inverse scales
   by 2/N over the independent packed half-spectrum. */
bfft_status bodft_forward_prepared(const bodft_prepared_plan* plan, bodft_workspace* workspace,
                                   const double* input, size_t input_count,
                                   bfft_complex* output, size_t output_count);
bfft_status bodft_inverse_prepared(const bodft_prepared_plan* plan, bodft_workspace* workspace,
                                   const bfft_complex* input, size_t input_count,
                                   double* output, size_t output_count);
bfft_status bodft_forward_prepared_f32(const bodft_prepared_plan* plan, bodft_workspace* workspace,
                                       const float* input, size_t input_count,
                                       bfft_complex_f32* output, size_t output_count);
bfft_status bodft_inverse_prepared_f32(const bodft_prepared_plan* plan, bodft_workspace* workspace,
                                       const bfft_complex_f32* input, size_t input_count,
                                       float* output, size_t output_count);

/* SIMD backend selected by the build target (shared with the BFFT kernel). */
const char* bodft_backend_name(void);

/* Create a BODFT plan for a power-of-two transform size N >= 2. */
bfft_status bodft_plan_create(size_t n, bodft_plan** plan);

/* Destroy a plan. Passing NULL is allowed. */
void bodft_plan_destroy(bodft_plan* plan);

/* Transform size N, and the packed bin count N/2. Return 0 for a NULL plan. */
size_t bodft_plan_size(const bodft_plan* plan);
size_t bodft_plan_bins(const bodft_plan* plan);

/* Double-precision forward and inverse. input/output have N doubles; the packed
   spectrum has bodft_plan_bins(plan) complex values. */
bfft_status bodft_forward(const bodft_plan* plan,
                          const double* input,
                          bfft_complex* output);

/* Numba-compatible forward entry point with the same call shape as
   bfft_forward. work and native_scratch are accepted for drop-in call-site
   compatibility and are ignored. */
bfft_status bodft_forward_numba(const bodft_plan* plan,
                                const double* input,
                                bfft_complex* output,
                                double* work,
                                bfft_complex* native_scratch);

bfft_status bodft_inverse(const bodft_plan* plan,
                          const bfft_complex* input,
                          double* output);

/* Single-precision forward and inverse. */
bfft_status bodft_forward_f32(const bodft_plan* plan,
                              const float* input,
                              bfft_complex_f32* output);

/* Single-precision Numba-compatible forward entry point. */
bfft_status bodft_forward_numba_f32(const bodft_plan* plan,
                                    const float* input,
                                    bfft_complex_f32* output,
                                    float* work,
                                    bfft_complex_f32* native_scratch);

bfft_status bodft_inverse_f32(const bodft_plan* plan,
                              const bfft_complex_f32* input,
                              float* output);

#ifdef __cplusplus
}
#endif

#endif
