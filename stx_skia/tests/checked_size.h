// Checked container-size conversion shared by the deterministic tests.

#ifndef STX_SKIA_TESTS_CHECKED_SIZE_H
#define STX_SKIA_TESTS_CHECKED_SIZE_H

#include <cstddef>
#include <cstdlib>
#include <limits>

namespace test_sizes {

// Returns size as an int; aborts when it is not representable.
inline int to_int(size_t size) {
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        std::abort();
    }
    const int value = static_cast<int>(size);
    return value;
}

}  // namespace test_sizes

#endif
