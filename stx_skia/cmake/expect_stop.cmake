# Runs a test program in one of its Release-check modes and passes only if a Release check
# stopped it. In Release builds with Clang, SkASSERT_RELEASE and SkUNREACHABLE end in
# __builtin_trap(), whose stop depends on the processor, and CMakeLists.txt passes which one
# as TRAP:
#   illegal_instruction  x86-64: ud2, SIGILL; STATUS_ILLEGAL_INSTRUCTION (0xC000001D) on
#                        Windows. execute_process reports "Illegal instruction" (Windows and
#                        Linux) or "SIGILL".
#   breakpoint           arm64: brk, SIGTRAP. execute_process reports "SIGTRAP" (Linux arm64)
#                        or "Trace/breakpoint trap".
# Only that processor's trap passes: a normal exit with any status, the other trap, an abort,
# a segmentation or bus fault, an access violation or a sanitizer report fails, so a mode
# cannot pass by crashing for some other reason.
#
# Usage: cmake -DPROGRAM=<path> -DMODE=<--argument> -DTRAP=<illegal_instruction|breakpoint>
#              -P expect_stop.cmake
if(TRAP STREQUAL "illegal_instruction")
    set(accepted "^(Illegal instruction|SIGILL)$")
elseif(TRAP STREQUAL "breakpoint")
    set(accepted "^(Trace/breakpoint trap|SIGTRAP)$")
else()
    message(FATAL_ERROR "expect_stop.cmake: TRAP must be illegal_instruction or breakpoint, not \"${TRAP}\"")
endif()
execute_process(COMMAND "${PROGRAM}" "${MODE}" RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "${PROGRAM} ${MODE} exited with status ${result}; expected a Release-check stop")
endif()
if(NOT result MATCHES "${accepted}")
    message(FATAL_ERROR "${PROGRAM} ${MODE} ended with \"${result}\"; expected the Release-check trap (${TRAP})")
endif()
message(STATUS "${PROGRAM} ${MODE} stopped: ${result}")
