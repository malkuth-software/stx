# The instruction-set profile, STX_SKIA_ISA (docs/ISA_PROFILES.md). The flags apply to every
# target in this directory, the library and its tests alike, so each translation unit that
# includes skvx or an opts header sees the same vector layout. A profile other than baseline
# is a standalone build only: a consumer compiled without the same flags would disagree with
# the library about those layouts.
if(STX_SKIA_ISA STREQUAL "baseline")
    return()
endif()
if(NOT stx_skia_top_level)
    message(FATAL_ERROR "STX_SKIA_ISA=${STX_SKIA_ISA} is for standalone stx_skia builds; "
        "GUI.Forms builds the baseline profile")
endif()
if(STX_SKIA_BASELINE)
    message(FATAL_ERROR "STX_SKIA_ISA=${STX_SKIA_ISA} applies to the candidate, not the "
        "reference profile")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "STX_SKIA_ISA=${STX_SKIA_ISA} needs Clang")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(stx_isa_x86_64 ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
    set(stx_isa_x86_64 OFF)
else()
    message(FATAL_ERROR "STX_SKIA_ISA=${STX_SKIA_ISA} is defined for x86-64 and arm64 only, "
        "not ${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(STX_SKIA_ISA STREQUAL "scalar")
    # Skia's own switches: skvx stores plain arrays and the raster pipeline compiles its
    # portable one-pixel stages (and no lowp stages).
    add_compile_definitions(SKVX_DISABLE_SIMD SKRP_CPU_SCALAR)
    if(stx_isa_x86_64)
        # Below SSE1, so no SSE2 block in the opts and blitter sources is compiled. x86-64
        # still does its scalar floating point in SSE registers, which is not SIMD.
        add_compile_definitions(SK_CPU_X64_LEVEL=0)
    else()
        # Without Advanced SIMD the compiler leaves __ARM_NEON undefined, so SkFeatures.h
        # does not define SK_ARM_HAS_NEON and no NEON block is compiled.
        add_compile_options(-march=armv8.5-a+nosimd)
    endif()
    # No fused multiply-add anywhere, the tests' inline Skia code included: arm64 has a
    # scalar fmadd, and Clang contracts within an expression unless told not to.
    add_compile_options(-fno-vectorize -fno-slp-vectorize -ffp-contract=off)
elseif(STX_SKIA_ISA MATCHES "^x86-64-v[23]$")
    if(NOT stx_isa_x86_64)
        message(FATAL_ERROR "STX_SKIA_ISA=${STX_SKIA_ISA} needs an x86-64 target")
    endif()
    add_compile_options(-march=${STX_SKIA_ISA})
else()
    message(FATAL_ERROR "STX_SKIA_ISA must be baseline, scalar, x86-64-v2 or x86-64-v3, "
        "not ${STX_SKIA_ISA}")
endif()
