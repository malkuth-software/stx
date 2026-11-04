# The SIMD tier comparison (include/stx_skia/simd.hpp, docs/ISA_PROFILES.md "SIMD tiers").
# Runs the instruction-set workload once at the vector128 tier and once at avx2_fma, each after
# SkGraphics::Init(), and compares the recordings (default and high precision through the raster
# pipeline, and legacy through Skia's own blitter choice) case by case against
# tests/simd_tier_bounds.txt.
# Every case's difference is printed, so the test log is the record the FMA-determinism policy
# asks for. Prints "SKIPPED:" when this machine cannot run avx2_fma.
#   cmake -DWORKLOAD=<program> -DBOUNDS=<bounds file> -DOUT=<directory> -P simd_tiers.cmake
foreach(level IN ITEMS vector128 avx2_fma)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env STX_SIMD_LEVEL=${level}
            ${WORKLOAD} --dispatch ${OUT}/simd-tier-${level}.raw ${OUT}/simd-tier-${level}-highp.raw
            ${OUT}/simd-tier-${level}-legacy.raw
        RESULT_VARIABLE status ERROR_VARIABLE errors OUTPUT_QUIET)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "the workload at ${level} failed (${status}): ${errors}")
    endif()
    string(REGEX MATCH "level=[^\n]*" selected_${level} "${errors}")
endforeach()
if(NOT selected_vector128 STREQUAL "level=sse2-128")
    message(FATAL_ERROR "a vector128 request selected '${selected_vector128}'")
endif()
if(NOT selected_avx2_fma STREQUAL "level=avx2-fma-256")
    message("SKIPPED: no avx2_fma tier here (an avx2_fma request selected '${selected_avx2_fma}')")
    return()
endif()
set(failed FALSE)
foreach(label IN ITEMS default highp legacy)
    set(suffix "")
    if(NOT label STREQUAL "default")
        set(suffix "-${label}")
    endif()
    execute_process(COMMAND ${WORKLOAD} --compare ${BOUNDS} ${label}
            ${OUT}/simd-tier-vector128${suffix}.raw ${OUT}/simd-tier-avx2_fma${suffix}.raw
        RESULT_VARIABLE status OUTPUT_VARIABLE report)
    message("${report}")
    if(NOT status EQUAL 0)
        set(failed TRUE)
    endif()
endforeach()
if(failed)
    message(FATAL_ERROR "avx2_fma differs from vector128 beyond tests/simd_tier_bounds.txt")
endif()
