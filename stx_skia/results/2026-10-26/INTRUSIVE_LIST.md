# Intrusive list and unused cache wrappers — 2026-10-26

## Remove unused code rather than convert it

SkLRUCache.h, SkTDynamicHash.h and SkTMultiMap.h have no consumer in the retained
CPU implementation. Source inspection finds their remaining consumers in excluded
GPU headers, plus the multimap's use of the dynamic-hash wrapper. Ninja's compiler
dependency database for the candidate contains none of these three headers.
Candidate release rebuilding after their removal passes all 25 then-existing tests.
They now reside byte-for-byte under baseline/skia/src/core for reference use.
tools/check_build_scope.py rejects their reintroduction into candidate sources.

This is a reduction in maintained candidate source, not a measured binary or
runtime saving: these unused templates did not contribute to the retained compile
closure. Private src/core headers were already excluded from installation; an
installed-header absence check would not prove this particular removal. Upstream
test sources and imported build inventories remain as reference material.

The preceding hash-storage record originally grouped these wrappers among retained
users. That statement is corrected: actual retained hash consumers include the
resource cache and flattenable dictionaries. Dependency inspection, not the
presence of a utility in the source tree, decides whether to retain it.

## Convert the retained list

SkTInternalLList.h is a real compiler dependency of SkPolyUtils.cpp and
SkDiscardableMemoryPool.cpp. The list observes nodes owned elsewhere. It contains
two endpoint pointers; membership uses links already stored in each node. The
debug-only membership pointer is observational, not ownership. The original header
is preserved byte-for-byte in the reference include tree.

The conversion keeps pointer updates and ordering explicit, initializes iterators,
and documents mutation/traversal lifetimes. Reset remains a constant-time operation
that abandons the chain without clearing node links or destroying values. Callers
discard/reinitialize that node storage, or remove individual nodes before reuse.
Destruction does not walk the chain. Concatenation transfers links and empties the
source; it does not transfer ownership of the nodes.

Temporary-list iterator initialization and begin/end are rejected at compile time.
The traversal-start enum has explicit int representation and a default that returns
an empty iterator for unsupported values. Debug assertions enforce the existing
detached-node insertion and distinct-source concatenation preconditions. The valid
operation algorithm is unchanged. Reset, destruction and link mutation gain no
loops, allocations, locks, counters or release-build membership tracking.

Both original and candidate measure 16 bytes for the list and 8 bytes for its
iterator on M4 arm64. Static assertions check two-pointer/one-pointer layouts on
other pointer widths. Debug concatenation retains the original walk that updates
membership observers; Release concatenation remains constant time. No throughput
improvement is claimed for this conversion.

## Verification

The added intrusive_list_contract fixture checks insertion permutations through
six nodes, each possible first removal and reinsertion, complete removal while
advancing the iterator first, relative insertion at every position, forward/reverse
and range traversal, iterator copy/assignment, empty states, concatenation at all
splits of eight nodes, reset, and caller-controlled node destruction. Debug runs
also validate bidirectional links and membership. Candidate-only compile-time tests
reject temporary-list iteration; a runtime case checks the invalid-start fallback.

The real polygon consumer triangulates a concave stair-step polygon in both windings,
at three scales and eight translations. All 48 cases produce 18 valid vertex indices
and match the original sequence digest 10536865794126918867. This is a bounded
consumer comparison, not a claim about every possible polygon. The real discardable
pool fixture exercises oldest-unlocked eviction, promotion after reacquisition,
failed relocking of discarded blocks, locked blocks exceeding a zero budget, and
purge on final unlock. Test failure paths release acquired block locks before
destruction.

All 26 tests pass in candidate/reference Release profiles on M4 macOS arm64 with
non-Apple Clang 23.1.2. All nine rendering/mipmap artifacts match byte-for-byte.
Release and Debug instrumented suites report PASS; earlier diagnostics are preserved
and detailed logs remain unread. A fresh relocated installed consumer and candidate
source/symbol scope checks pass. Seven selected pinned GUI.Forms fixture tests pass
under non-Apple LLVM 22.1.8. The authoritative GUI.Forms checkout is unchanged.
Both changed C++ files pass spelling/lifetime checks; the node and iterator contracts
were reviewed separately from those syntactic checks.

The standard release/instrumented/GUI commands are in AGENTS.md. The complete local
driver is build/intrusive_list_gate.sh, with ordinary output in
build/intrusive-list-gates.log and GUI output in build/intrusive-list-gui.log.
It runs both release profiles, compares the nine artifacts and the list/triangulation
result text, preserves old diagnostic logs, runs both instrumented profiles and a
fresh relocated installation. Reference and candidate result text are retained on
the M4 mirror under build/intrusive-list-{baseline,candidate}-result.txt.

Native Linux x64/arm64 and Windows CI passed for the preceding hash-storage head
4c50e74. CI for this new conversion is a separate gate. Full library conversion,
broader measurements, the owner's independent review and production integration
remain outstanding.
