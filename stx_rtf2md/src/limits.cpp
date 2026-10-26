#include "stx_rtf2md/limits.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace stx_rtf2md {

static_assert(loss_kind_count == static_cast<std::size_t>(LossKind::link_title_dropped) + 1U,
              "loss_kind_count must name every LossKind");

void LossReport::clear() noexcept {
    entries_.fill(LossEntry{});
}

void LossReport::record(LossKind kind, SourceOffset offset) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    LossEntry& entry = entries_[index];
    if (entry.count == 0) {
        entry.first = offset;
    }
    if (entry.count != std::numeric_limits<std::uint32_t>::max()) {
        entry.count += 1U;
    }
}

LossEntry LossReport::entry(LossKind kind) const noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    const LossEntry value = entries_[index];
    return value;
}

bool LossReport::empty() const noexcept {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].count != 0) {
            return false;
        }
    }
    return true;
}

}
