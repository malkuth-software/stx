# Runs a test program in one of its Release-check modes and passes only if a Release check
# stopped it. In Release builds with Clang, SkASSERT_RELEASE and SkUNREACHABLE end in
# __builtin_trap(): an illegal instruction on x86-64 (SIGILL; STATUS_ILLEGAL_INSTRUCTION,
# 0xC000001D, on Windows) and a breakpoint on arm64 (SIGTRAP). execute_process reports such an
# end as a description instead of an exit status. Only that trap passes: a normal exit with
# any status, an abort, an access violation or a sanitizer report fails, so a mode cannot pass
# by crashing for some other reason.
#
# Usage: cmake -DPROGRAM=<path> -DMODE=<--argument> -P expect_stop.cmake
execute_process(COMMAND "${PROGRAM}" "${MODE}" RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "${PROGRAM} ${MODE} exited with status ${result}; expected a Release-check stop")
endif()
if(NOT result MATCHES "^(Illegal instruction|Trace/breakpoint trap)$")
    message(FATAL_ERROR "${PROGRAM} ${MODE} ended with \"${result}\"; expected the Release-check trap")
endif()
message(STATUS "${PROGRAM} ${MODE} stopped: ${result}")
