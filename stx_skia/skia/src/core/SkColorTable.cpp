/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColorTable.h"

#include "include/core/SkImageInfo.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkWriteBuffer.h"

#include <algorithm>
#include <array>
#include <utility>

// Each channel is copied once into its own 256-byte row. Null channels are
// identity maps; select that case before traversing the row.
static void CopyChannelTable(const uint8_t* source, uint8_t* destination) {
    if (source != nullptr) {
        std::copy_n(source, 256, destination);
        return;
    }
    for (int value = 0; value < 256; ++value) {
        destination[value] = static_cast<uint8_t>(value);
    }
}

sk_sp<SkColorTable> SkColorTable::Make(const uint8_t tableA[256],
                                     const uint8_t tableR[256],
                                     const uint8_t tableG[256],
                                     const uint8_t tableB[256]) {
    if (tableA == nullptr && tableR == nullptr && tableG == nullptr && tableB == nullptr) {
        return nullptr; // The table is the identity.
    }
    SkBitmap table{};
    const SkImageInfo tableInfo = SkImageInfo::MakeA8(256, 4);
    if (!table.tryAllocPixels(tableInfo)) {
        return nullptr;
    }
    CopyChannelTable(tableA, table.getAddr8(0, 0));
    CopyChannelTable(tableR, table.getAddr8(0, 1));
    CopyChannelTable(tableG, table.getAddr8(0, 2));
    CopyChannelTable(tableB, table.getAddr8(0, 3));
    table.setImmutable();
    sk_sp<SkColorTable> result(new SkColorTable(std::move(table)));
    return result;
}

void SkColorTable::flatten(SkWriteBuffer& buffer) const {
    buffer.writeByteArray(fTable.getAddr8(0, 0), 4 * 256);
}

sk_sp<SkColorTable> SkColorTable::Deserialize(SkReadBuffer& buffer) {
    std::array<uint8_t, 4 * 256> channels{};
    if (!buffer.readByteArray(channels.data(), channels.size())) {
        return nullptr;
    }
    const uint8_t* alpha = channels.data();
    const uint8_t* red = alpha + 256;
    const uint8_t* green = red + 256;
    const uint8_t* blue = green + 256;
    sk_sp<SkColorTable> table = Make(alpha, red, green, blue);
    return table;
}
