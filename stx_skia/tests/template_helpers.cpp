#include "include/private/SkTemplates.h"
#include "include/core/SkTypes.h"
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include "src/utils/mac/SkUniqueCFRef.h"
#endif

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {
bool primitive_helpers() {
    int evaluated = 0;
    sk_ignore_unused_variable(++evaluated);
    if (evaluated != 1) return false;
    for (int value = -512; value <= 512; ++value) {
        const int expected = value < 0 ? -value : value;
        if (SkTAbs(value) != expected) return false;
    }
    const std::array<uint32_t, 6> bits{0, 0x80000000U, 0x3f800000U, 0xbf800000U,
                                       0x7fc00017U, 0xffc00017U};
    const std::array<uint32_t, 6> expected{0, 0x80000000U, 0x3f800000U, 0x3f800000U,
                                           0x7fc00017U, 0xffc00017U};
    for (size_t index = 0; index < bits.size(); ++index) {
        const float input = std::bit_cast<float>(bits[index]);
        const float output = SkTAbs(input);
        if (std::bit_cast<uint32_t>(output) != expected[index]) return false;
    }
    std::array<uint32_t, 8> words{};
    for (ptrdiff_t index = 0; index < 8; ++index) {
        const ptrdiff_t offset = index * static_cast<ptrdiff_t>(sizeof(uint32_t));
        if (SkTAddOffset<uint32_t>(words.data(), offset) != words.data() + index) return false;
        if (SkTAddOffset<const uint32_t>(words.data() + index, -offset) != words.data()) return false;
    }
    if (SkTAddOffset<void>(static_cast<void*>(nullptr), 0) != nullptr) return false;
    return true;
}

bool allocation_owner() {
    using skia_private::UniqueVoidPtr;
    UniqueVoidPtr source(sk_malloc_throw(64));
    void* address = source.get();
    std::memset(address, 0x37, 64);
    UniqueVoidPtr destination(std::move(source));
    if (source.get() != nullptr || destination.get() != address) return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(destination.get());
    for (size_t index = 0; index < 64; ++index) {
        if (bytes[index] != 0x37) return false;
    }
    void* released = destination.release();
    UniqueVoidPtr adopted(released);
    if (destination.get() != nullptr || adopted.get() != address) return false;
    adopted.reset();
    return adopted.get() == nullptr;
}

#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
int releasedItems = 0;
void release_item(CFAllocatorRef, const void*) { ++releasedItems; }

bool foundation_owner() {
    const CFArrayCallBacks callbacks{0, nullptr, release_item, nullptr, nullptr};
    int item = 42;
    {
        SkUniqueCFRef<CFMutableArrayRef> source(CFArrayCreateMutable(nullptr, 0, &callbacks));
        if (!source) return false;
        CFArrayAppendValue(source.get(), &item);
        SkUniqueCFRef<CFMutableArrayRef> destination(std::move(source));
        if (source || !destination || CFArrayGetCount(destination.get()) != 1) return false;
    }
    return releasedItems == 1;
}
#endif
}

int main() {
    if (!primitive_helpers() || !allocation_owner()) return 1;
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
    if (!foundation_owner()) return 2;
#endif
    std::printf("template helpers: scalar/offset behavior and named ownership PASS; void owner=%zu bytes\n",
                sizeof(skia_private::UniqueVoidPtr));
    return 0;
}
