// Differential corpus for SkRBuffer, SkWBuffer and SkDataTable over their whole interfaces.
//
// Buffers: seeded write sequences (8-, 16- and 32-bit values, scalars, booleans, null pointers,
// raw bytes, skips and padding) into measuring, unbounded and exactly sized buffers, recording
// positions and bytes; and seeded read sequences (bytes, 32-bit values, raw reads, skips, typed
// skips, alignment) over buffers of every length, recording values, positions, availability and
// validity, including reads past the end, after which a reader stays invalid. Storage is 16-byte
// aligned, so address and offset alignment agree, and only null pointers are written, so no
// address is recorded.
//
// Data tables: every factory, with element and directory storage, empty, zero-length and string
// entries, and the free procedure's calls.
//
// Both builds must produce identical bytes (buffer-core.raw).
//
// The candidate alone checks that a read into a null destination skips, and that writing past a
// sized buffer, null reader storage with a length, bad table indexes, zero element sizes and
// overflowing table sizes stop the program; arguments beginning with "--" select a stop.

#include "differential_corpus.h"

#include "include/core/SkDataTable.h"
#include "include/core/SkRefCnt.h"
#include "src/core/SkBuffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using corpus::Output;
using corpus::Sequence;
using corpus::append;
using corpus::append_bool;

// One write operation, replayed identically against every kind of writer.
struct WriteStep {
    int kind = 0;
    int32_t value = 0;
    size_t size = 0;
};

std::vector<WriteStep> write_plan(Sequence& sequence, size_t steps) {
    std::vector<WriteStep> plan{};
    for (size_t index = 0; index < steps; ++index) {
        WriteStep step{};
        step.kind = static_cast<int>(sequence.pick(9));
        step.value = static_cast<int32_t>(sequence.next());
        step.size = sequence.pick(11);
        plan.push_back(step);
    }
    return plan;
}

void apply(SkWBuffer& buffer, const WriteStep& step, std::array<uint8_t, 16>& raw) {
    switch (step.kind) {
        case 0: buffer.write8(static_cast<int8_t>(step.value)); break;
        case 1: buffer.write16(static_cast<int16_t>(step.value)); break;
        case 2: buffer.write32(step.value); break;
        case 3: buffer.writeScalar(static_cast<float>(step.value) / 7.0f); break;
        case 4: buffer.writeBool((step.value & 1) != 0); break;
        case 5: buffer.writePtr(nullptr); break;
        case 6:
            for (size_t index = 0; index < raw.size(); ++index) {
                raw[index] = static_cast<uint8_t>(step.value + static_cast<int32_t>(index));
            }
            buffer.write(raw.data(), step.size);
            break;
        case 7: {
            void* skipped = buffer.skip(step.size);
            if (skipped != nullptr && step.size > 0) std::memset(skipped, 0x5C, step.size);
            break;
        }
        default: buffer.padToAlign4(); break;
    }
}

// Measures a plan, then writes it into an exactly sized buffer and into an unbounded one.
void writer_cases(Output& output) {
    Sequence sequence{};
    sequence.state = 11;
    for (int round = 0; round < 200; ++round) {
        const std::vector<WriteStep> plan = write_plan(sequence, sequence.pick(30) + 1);
        std::array<uint8_t, 16> raw{};
        SkWBuffer measuring{};
        for (const WriteStep& step : plan) {
            apply(measuring, step, raw);
            append(output, static_cast<uint64_t>(measuring.pos()));
        }
        const size_t size = measuring.pos();
        alignas(16) std::array<uint8_t, 512> sized{};
        sized.fill(0xEE);
        SkWBuffer exact(sized.data(), size);
        for (const WriteStep& step : plan) apply(exact, step, raw);
        append(output, static_cast<uint64_t>(exact.pos()));
        output.bytes.insert(output.bytes.end(), sized.begin(), sized.begin() + size + 4);
        alignas(16) std::array<uint8_t, 512> unbounded{};
        unbounded.fill(0xEE);
        SkWBuffer open(unbounded.data());
        for (const WriteStep& step : plan) apply(open, step, raw);
        append_bool(output, open.pos() == size);
        append_bool(output, std::memcmp(unbounded.data(), sized.data(), size + 4) == 0);
        // A buffer reset onto new storage starts over.
        alignas(16) std::array<uint8_t, 64> again{};
        exact.reset(again.data(), again.size());
        exact.write32(0x01020304);
        append(output, static_cast<uint64_t>(exact.pos()));
        exact.reset(again.data());
        append(output, static_cast<uint64_t>(exact.pos()));
        ++output.cases;
    }
}

void record_reader(Output& output, const SkRBuffer& reader) {
    append(output, static_cast<uint64_t>(reader.pos()));
    append(output, static_cast<uint64_t>(reader.size()));
    append(output, static_cast<uint64_t>(reader.available()));
    append_bool(output, reader.eof());
    append_bool(output, reader.isValid());
}

void reader_cases(Output& output) {
    alignas(16) std::array<uint8_t, 96> data{};
    for (size_t index = 0; index < data.size(); ++index) {
        data[index] = static_cast<uint8_t>(index * 37 + 11);
    }
    Sequence sequence{};
    sequence.state = 23;
    for (size_t length = 0; length <= data.size(); ++length) {
        for (int round = 0; round < 3; ++round) {
            SkRBuffer reader(data.data(), length);
            record_reader(output, reader);
            for (int step = 0; step < 24; ++step) {
                const int kind = static_cast<int>(sequence.pick(7));
                append(output, static_cast<uint8_t>(kind));
                switch (kind) {
                    case 0: {
                        uint8_t value = 0;
                        append_bool(output, reader.readU8(&value));
                        append(output, value);
                        break;
                    }
                    case 1: {
                        int32_t value = 0;
                        append_bool(output, reader.readS32(&value));
                        append(output, value);
                        break;
                    }
                    case 2: {
                        uint32_t value = 0;
                        append_bool(output, reader.readU32(&value));
                        append(output, value);
                        break;
                    }
                    case 3: {
                        std::array<uint8_t, 12> bytes{};
                        const size_t size = sequence.pick(13);
                        append_bool(output, reader.read(bytes.data(), size));
                        for (const uint8_t byte : bytes) append(output, byte);
                        break;
                    }
                    case 4: {
                        const void* skipped = reader.skip(sequence.pick(9));
                        append_bool(output, skipped != nullptr);
                        if (skipped != nullptr) {
                            append(output, static_cast<int64_t>(
                                                   static_cast<const uint8_t*>(skipped) -
                                                   data.data()));
                        }
                        break;
                    }
                    case 5: {
                        const uint32_t* values = reader.skipCount<uint32_t>(sequence.pick(4));
                        append_bool(output, values != nullptr);
                        break;
                    }
                    default:
                        append_bool(output, reader.skipToAlign4());
                        break;
                }
                record_reader(output, reader);
            }
            ++output.cases;
        }
    }
    // An empty, default reader.
    SkRBuffer empty{};
    record_reader(output, empty);
    uint8_t value = 0;
    append_bool(output, empty.readU8(&value));
    record_reader(output, empty);
    ++output.cases;
}

int gFreed = 0;

void count_free(void* context) {
    ++gFreed;
    append(*static_cast<Output*>(context), static_cast<uint8_t>(0xF4));
}

void record_table(Output& output, const SkDataTable& table) {
    append_bool(output, table.isEmpty());
    append(output, static_cast<int32_t>(table.count()));
    for (int index = 0; index < table.count(); ++index) {
        size_t size = 0;
        const uint8_t* entry = static_cast<const uint8_t*>(table.at(index, &size));
        append(output, static_cast<uint64_t>(size));
        append(output, static_cast<uint64_t>(table.atSize(index)));
        if (size > 0) output.bytes.insert(output.bytes.end(), entry, entry + size);
        append_bool(output, table.atT<uint8_t>(index) == entry);
    }
    ++output.cases;
}

void table_cases(Output& output) {
    record_table(output, *SkDataTable::MakeEmpty());
    append_bool(output, SkDataTable::MakeEmpty() == SkDataTable::MakeEmpty());
    const std::array<const char*, 5> words{"", "a", "stx", "skia data table", "z"};
    std::array<const void*, 5> pointers{};
    std::array<size_t, 5> sizes{};
    for (size_t index = 0; index < words.size(); ++index) {
        pointers[index] = words[index];
        sizes[index] = std::strlen(words[index]) + 1;
    }
    for (int count = -1; count <= 5; ++count) {
        const sk_sp<SkDataTable> table =
                SkDataTable::MakeCopyArrays(pointers.data(), sizes.data(), count);
        record_table(output, *table);
        for (int index = 0; index < (*table).count(); ++index) {
            const char* text = (*table).atStr(index);
            output.bytes.insert(output.bytes.end(), text, text + std::strlen(text) + 1);
        }
    }
    // Entries of zero length among others.
    const std::array<size_t, 4> mixed{0, 3, 0, 2};
    const char letters[] = "abcdef";
    const std::array<const void*, 4> mixedPointers{letters, letters, letters + 3, letters + 3};
    record_table(output, *SkDataTable::MakeCopyArrays(mixedPointers.data(), mixed.data(), 4));
    std::array<uint32_t, 9> values{};
    for (size_t index = 0; index < values.size(); ++index) {
        values[index] = static_cast<uint32_t>(index * 0x01010101u);
    }
    for (int count = -2; count <= 9; ++count) {
        record_table(output, *SkDataTable::MakeCopyArray(values.data(), sizeof(uint32_t), count));
        record_table(output, *SkDataTable::MakeCopyArray(values.data(), 3, count));
    }
    for (int count = -1; count <= 3; ++count) {
        Output events{};
        {
            const sk_sp<SkDataTable> table = SkDataTable::MakeArrayProc(
                    values.data(), sizeof(uint32_t), count, count_free, &events);
            record_table(output, *table);
        }
        output.bytes.insert(output.bytes.end(), events.bytes.begin(), events.bytes.end());
    }
    append(output, static_cast<int32_t>(gFreed));
    ++output.cases;
}

#ifndef STX_SKIA_REFERENCE
// A read with a null destination skips the bytes, as documented, instead of copying to null.
bool null_read_skips() {
    alignas(16) std::array<uint8_t, 8> data{1, 2, 3, 4, 5, 6, 7, 8};
    SkRBuffer reader(data.data(), data.size());
    if (!reader.read(nullptr, 3) || reader.pos() != 3) return false;
    uint8_t next = 0;
    return reader.readU8(&next) && next == 4 && !reader.read(nullptr, 5) && !reader.isValid();
}

void ignore_free(void*) {}

// Each mode violates one Release-checked precondition and must stop the program.
int stop_mode(const char* mode) {
    alignas(16) std::array<uint8_t, 16> storage{};
    if (std::strcmp(mode, "--write-past-end") == 0) {
        SkWBuffer writer(storage.data(), 6);
        writer.write32(1);
        writer.write32(2);
    } else if (std::strcmp(mode, "--pad-past-end") == 0) {
        SkWBuffer writer(storage.data(), 2);
        writer.write8(1);
        writer.padToAlign4();
    } else if (std::strcmp(mode, "--reader-null-data") == 0) {
        SkRBuffer reader(nullptr, 4);
        uint32_t value = 0;
        std::printf("%d\n", reader.readU32(&value) ? 1 : 0);
    } else if (std::strcmp(mode, "--table-index") == 0) {
        const sk_sp<SkDataTable> table = SkDataTable::MakeCopyArray(storage.data(), 4, 2);
        std::printf("%zu\n", (*table).atSize(2));
    } else if (std::strcmp(mode, "--table-zero-element") == 0) {
        const sk_sp<SkDataTable> table = SkDataTable::MakeCopyArray(storage.data(), 0, 3);
        std::printf("%d\n", (*table).count());
    } else if (std::strcmp(mode, "--table-proc-zero-element") == 0) {
        const sk_sp<SkDataTable> table =
                SkDataTable::MakeArrayProc(storage.data(), 0, 3, ignore_free, nullptr);
        std::printf("%d\n", (*table).count());
    } else if (std::strcmp(mode, "--table-size-overflow") == 0) {
        // The sizes wrap to 8 bytes in total; the copy would run far past the allocation.
        const std::array<const void*, 2> pointers{storage.data(), storage.data()};
        const std::array<size_t, 2> sizes{SIZE_MAX, 9};
        const sk_sp<SkDataTable> table =
                SkDataTable::MakeCopyArrays(pointers.data(), sizes.data(), 2);
        std::printf("%d\n", (*table).count());
    } else {
        return 2;
    }
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 1 || argc > 2) return 2;
#ifndef STX_SKIA_REFERENCE
    if (argc == 2 && std::strncmp(argv[1], "--", 2) == 0) return stop_mode(argv[1]);
    if (!null_read_skips()) {
        std::puts("buffer core: a read into a null destination did not skip");
        return 1;
    }
#endif
    Output output{};
    writer_cases(output);
    reader_cases(output);
    table_cases(output);
    if (argc == 2 && !corpus::write_file(argv[1], output)) return 2;
    const unsigned long long hash = corpus::fnv1a(output.bytes);
    std::printf("buffer core: %zu cases, %zu bytes, hash=%llu PASS\n", output.cases,
                output.bytes.size(), hash);
    return 0;
}
