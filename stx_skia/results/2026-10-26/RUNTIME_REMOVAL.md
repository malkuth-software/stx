# CPU shaders without SkSL — 2026-10-26

The owner excludes runtime effects while retaining ordinary CPU gradients,
image sampling, standard blend modes, matrix color filters and mask effects.
This batch removes the remaining runtime compiler dependency from that contract.

## Removed and retained behavior

The candidate no longer contains the SkSL source/header trees, runtime effect
compiler, known runtime effects, runtime shaders/color filters/blenders, gainmap
shader factories, arithmetic blender factories, color-filter Lerp/luma/overdraw
factories or programmable mesh specifications. Original source and headers are
preserved under baseline/skia for the reference build.

Programmable meshes were already unimplemented by upstream SkBitmapDevice.
Their canvas, device and recording APIs are removed instead of retaining a
successful no-op. Ordinary SkVertices drawing remains. The SkCapabilities object
only advertised the shader-language version; it and the surface capability API
are removed. Read-buffer/deserialization flags that enabled SkSL are also removed.

Standard SkShaders::Blend uses existing CPU raster stages. Its blender overload
continues to accept native blend-mode objects and defaults a null blender to
source-over. A custom runtime blender returns null. The candidate no longer
publishes runtime-effect factories capable of constructing such objects.

Compiler tracing stages and contexts are removed from the raster pipeline.
Compiler-oriented arithmetic stages still present there need a separate usage
analysis before removal. This batch does not claim that the raster pipeline,
canvas, surface, device or recording implementations are fully converted.

## Blend-shader conversion

SkBlendShader.cpp/.h now follow house style. Named opacity tests replace the
capturing predicate; the shader retains two immutable child owners and its mode.
Arena-backed scratch storage has explicit coordinates and first-result arrays.
The active lanes are stored before they are loaded. Child local matrices retain
the original order, and coordinate preservation occurs only if the parent had
already seeded them. A failed child append leaves a partial pipeline that its
caller discards. Invalid mode enums return null before allocating a shader.

No new arithmetic approximation, platform intrinsic, forced vector width or
speed claim is introduced. Generic CPU raster stages retain their arithmetic.

## Verification

M4 Mac mini, macOS arm64, non-Apple Clang 23.1.2/C++20; GUI.Forms fixture uses
its pinned LLVM 22.1.8 toolchain.

The new shader comparison covers all 29 standard modes, four input-alpha
combinations and three canvas transforms, for both paired gradient shaders and
colored/textured vertex drawing: 696 scenes and 5,103,072 bytes. Gradient children
have different local matrices. The standard-mode and blender-object factories
must produce equal pixels. An independent check verifies that every constant
shader reported opaque really writes opaque pixels with source blending.
Candidate-only checks reject an invalid blend mode and verify removed APIs and
runtime compiler headers are unavailable.

All seven complete artifacts compare byte-for-byte with the unchanged original
implementations. The new shader output hash is 10017532295877655872; all previous
artifact hashes remain unchanged. Fourteen deterministic tests pass in each
profile. Whole Release and Debug instrumented suites report PASS. The relocated
installed package builds and runs its consumer, including runtime-header absence
and retained consumer RTTI. Seven selected GUI.Forms tests pass on the isolated
fixture; the authoritative GUI.Forms checkout is unchanged.

Defined-symbol inspection finds no SkRuntime, SkKnownRuntime, SkSL::,
SkMeshSpecification or SkCapabilities implementation in the candidate archive.
The macOS compile database now has 310 library source units versus 425 before
this batch and 492 in the original reference. These counts are maintenance/build
scope evidence, not a measured drawing-speed improvement.

Records: build/runtime-reference-final.log, build/runtime-gates.log and
build/runtime-gui.log. A subsequent span-constraint correction prompted repeated
release/reference and instrumented gates in build/runtime-span-gates.log.
Earlier instrumented diagnostics were preserved; their contents remain unread.
Native CI compares the new seventh artifact on Linux x64/arm64 and Windows.
Cross-platform completion remains a separate gate; no local test proves it.

## Cross-platform failure found during verification

CI for the preceding commit db44f01 failed on both Linux architectures while
instantiating variant<SkRRect, SkSpan<const SkRect>>. The previously converted
SkSpan lvalue-container constructor was unconstrained. libstdc++'s variant
selection attempted that constructor for SkRRect and failed inside std::data
and std::size; libc++ did not expose it in local tests.

The correction constrains participation through a named private trait requiring
data and an integral size, with a data pointer convertible to the span's element
pointer. Regression assertions reject non-containers, incompatible element types,
const-to-mutable conversion and vector<bool>, while admitting valid const views
and variant selection. Temporary owning-container rejection remains intact.
This is a constructor-participation correction, not a relaxation of borrowing.
Both Linux failure logs are retained under build/ci-db44f01-linux*.log. New CI
must verify the actual libstdc++ environment; local success is not substituted.
