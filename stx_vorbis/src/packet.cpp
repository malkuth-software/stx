#include "packet.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace stx_vorbis::detail {
namespace {
void charge(Workspace& workspace, const std::uint64_t count) {
    require(count <= workspace.operation_limit - workspace.operations, Status::resource_limit);
    workspace.operations += count;
}
unsigned int packet_bits(BitReader& reader, const unsigned int count) {
    std::uint32_t value = 0;
    require(reader.read(count, value), Status::truncated, reader.position());
    return value;
}
unsigned int symbol(Workspace& workspace, BitReader& reader, const Codebook& book) {
    charge(workspace, 1);
    std::uint32_t entry = 0;
    const Status status = book.decode(reader, entry);
    require(status == Status::ok, status, reader.position());
    return entry;
}
int predict(const int x, const int x0, const int x1, const int y0, const int y1) noexcept {
    const int dy = y1 - y0;
    const int offset = std::abs(dy) * (x - x0) / (x1 - x0);
    const int result = dy < 0 ? y0 - offset : y0 + offset;
    return result;
}
void line(std::span<double> curve, const std::array<double, 256>& inverse_db,
          const int x0, const int x1, const int y0, const int y1) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int base = dy / dx;
    const int step = dy < 0 ? base - 1 : base + 1;
    const int remainder = std::abs(dy) - std::abs(base) * dx;
    int error = 0;
    int y = y0;
    const int end = std::min(x1, static_cast<int>(curve.size()));
    for (int x = x0; x < end; ++x) {
        require(y >= 0 && y < 256, Status::invalid_packet);
        curve[static_cast<std::size_t>(x)] = inverse_db[static_cast<std::size_t>(y)];
        error += remainder;
        if (error >= dx) { error -= dx; y += step; }
        else y += base;
    }
}
bool floor1(Workspace& workspace, const Setup& setup, const Floor& floor,
            BitReader& reader, const std::span<double> curve) {
    if (packet_bits(reader, 1) == 0) return false;
    constexpr std::array<unsigned int, 4> ranges{256, 128, 86, 64};
    const unsigned int range = ranges[floor.multiplier - 1];
    const unsigned int bits = ilog(range - 1);
    workspace.floor_y[0] = static_cast<int>(packet_bits(reader, bits));
    workspace.floor_y[1] = static_cast<int>(packet_bits(reader, bits));
    require(workspace.floor_y[0] < static_cast<int>(range) && workspace.floor_y[1] < static_cast<int>(range), Status::invalid_packet);
    unsigned int point = 2;
    for (unsigned int part = 0; part < floor.partitions; ++part) {
        const FloorClass& configuration = floor.classes[floor.partition_class[part]];
        unsigned int classes = 0;
        if (configuration.subclasses != 0) classes = symbol(workspace, reader, setup.books[configuration.master]);
        const unsigned int mask = (1U << configuration.subclasses) - 1;
        for (unsigned int index = 0; index < configuration.dimensions; ++index) {
            const int book = configuration.books[classes & mask];
            classes >>= configuration.subclasses;
            workspace.floor_y[point] = book < 0 ? 0 : static_cast<int>(symbol(workspace, reader, setup.books[static_cast<std::size_t>(book)]));
            ++point;
        }
    }
    workspace.floor_active.fill(false);
    workspace.floor_active[0] = true; workspace.floor_active[1] = true;
    for (unsigned int index = 2; index < floor.points; ++index) {
        const unsigned int low = floor.low[index]; const unsigned int high = floor.high[index];
        const int predicted = predict(floor.x[index], floor.x[low], floor.x[high], workspace.floor_y[low], workspace.floor_y[high]);
        const int high_room = static_cast<int>(range) - predicted;
        const int room = 2 * std::min(high_room, predicted);
        const int value = workspace.floor_y[index];
        int result = predicted;
        if (value != 0) {
            workspace.floor_active[low] = true; workspace.floor_active[high] = true; workspace.floor_active[index] = true;
            if (value >= room) result = high_room > predicted ? value : predicted - value + high_room - 1;
            else if ((value & 1) != 0) result = predicted - (value + 1) / 2;
            else result = predicted + value / 2;
        }
        require(result >= 0 && result < static_cast<int>(range), Status::invalid_packet, reader.position());
        workspace.floor_y[index] = result;
    }
    int last_x = 0; int last_y = workspace.floor_y[0] * static_cast<int>(floor.multiplier);
    for (unsigned int sorted = 1; sorted < floor.points; ++sorted) {
        const unsigned int index = floor.sorted[sorted];
        if (!workspace.floor_active[index]) continue;
        const int y = workspace.floor_y[index] * static_cast<int>(floor.multiplier);
        line(curve, setup.inverse_db, last_x, floor.x[index], last_y, y);
        last_x = floor.x[index]; last_y = y;
    }
    require(last_y >= 0 && last_y < 256, Status::invalid_packet);
    for (std::size_t index = static_cast<std::size_t>(last_x); index < curve.size(); ++index)
        curve[index] = setup.inverse_db[static_cast<std::size_t>(last_y)];
    return true;
}
bool floor0(Workspace& workspace, const Setup& setup, const Floor& floor,
            BitReader& reader, const std::span<double> curve, const bool large) {
    std::uint64_t raw = 0;
    require(reader.read64(floor.amplitude_bits, raw), Status::truncated, reader.position());
    if (raw == 0) return false;
    const std::uint64_t maximum = (std::uint64_t{1} << floor.amplitude_bits) - 1;
    const double amplitude = static_cast<double>(raw) * floor.amplitude_db / static_cast<double>(maximum);
    const unsigned int selected = packet_bits(reader, ilog(floor.book_count));
    require(selected < floor.book_count, Status::invalid_packet, reader.position());
    const Codebook& book = setup.books[floor.books[selected]];
    double last = 0;
    unsigned int position = 0;
    while (position < floor.order) {
        const unsigned int entry = symbol(workspace, reader, book);
        const std::size_t base = std::size_t{entry} * book.dimensions;
        const unsigned int count = std::min(book.dimensions, floor.order - position);
        charge(workspace, count);
        for (unsigned int index = 0; index < count; ++index) workspace.lsp[position + index] = book.values[base + index] + last;
        position += count;
        last = workspace.lsp[position - 1];
    }
    for (unsigned int index = 0; index < floor.order; ++index) workspace.lsp[index] = 2 * std::cos(workspace.lsp[index]);
    const std::pmr::vector<double>& bark = large ? floor.bark1 : floor.bark0;
    charge(workspace, std::uint64_t{curve.size()} * floor.order);
    const double db_scale = std::log(10.0) / 20.0;
    for (std::size_t bin = 0; bin < curve.size(); ++bin) {
        const double w = bark[bin];
        double p = 0.5; double q = 0.5;
        unsigned int index = 1;
        for (; index < floor.order; index += 2) { q *= w - workspace.lsp[index - 1]; p *= w - workspace.lsp[index]; }
        if (index == floor.order) {
            q *= w - workspace.lsp[index - 1]; p *= p * (4 - w * w); q *= q;
        } else { p *= p * (2 - w); q *= q * (2 + w); }
        const double denominator = p + q;
        require(denominator > 0 && std::isfinite(denominator), Status::invalid_packet);
        const double value = std::exp((amplitude / std::sqrt(denominator) - floor.amplitude_db) * db_scale);
        require(std::isfinite(value), Status::invalid_packet);
        curve[bin] = value;
    }
    return true;
}
void residue(Workspace& workspace, const Setup& setup, const Residue& configuration,
             BitReader& reader, const unsigned int channels, const unsigned int bins) {
    if (channels == 0) return;
    const unsigned int vectors = configuration.type == 2 ? 1 : channels;
    const unsigned int extent = configuration.type == 2 ? bins * channels : bins;
    const unsigned int begin = std::min(configuration.begin, extent);
    const unsigned int end = std::min(configuration.end, extent);
    const unsigned int partitions = (end - begin) / configuration.partition_size;
    if (partitions == 0) return;
    const Codebook& classbook = setup.books[configuration.classbook];
    const unsigned int class_stride = partitions;
    require(std::size_t{partitions} * vectors <= workspace.classifications.size(), Status::resource_limit);
    for (unsigned int pass = 0; pass < 8; ++pass) {
        unsigned int partition = 0;
        while (partition < partitions) {
            if (pass == 0) {
                for (unsigned int channel = 0; channel < vectors; ++channel) {
                    unsigned int value = symbol(workspace, reader, classbook);
                    // The most significant classification describes the first partition.
                    // Digits beyond the final partial group are still consumed/validated.
                    for (unsigned int digit = classbook.dimensions; digit > 0; --digit) {
                        const unsigned int classification = value % configuration.classifications;
                        value /= configuration.classifications;
                        const unsigned int destination = partition + digit - 1;
                        if (destination < partitions) workspace.classifications[channel * class_stride + destination] = classification;
                    }
                    charge(workspace, classbook.dimensions);
                    require(value == 0, Status::invalid_packet, reader.position());
                }
            }
            const unsigned int group = std::min(classbook.dimensions, partitions - partition);
            for (unsigned int within = 0; within < group; ++within, ++partition) {
                const unsigned int offset = begin + partition * configuration.partition_size;
                for (unsigned int channel = 0; channel < vectors; ++channel) {
                    const unsigned int classification = workspace.classifications[channel * class_stride + partition];
                    const int selected = configuration.books[classification][pass];
                    if (selected < 0) continue;
                    const Codebook& book = setup.books[static_cast<std::size_t>(selected)];
                    if (configuration.type == 0) {
                        const unsigned int step = configuration.partition_size / book.dimensions;
                        for (unsigned int column = 0; column < step; ++column) {
                            const unsigned int entry = symbol(workspace, reader, book);
                            charge(workspace, book.dimensions);
                            const std::size_t source = std::size_t{entry} * book.dimensions;
                            const std::size_t base = std::size_t{workspace.bundle[channel]} * workspace.stride + offset + column;
                            for (unsigned int row = 0; row < book.dimensions; ++row)
                                workspace.spectrum[base + row * step] += book.values[source + row];
                        }
                    } else {
                        unsigned int position = 0;
                        while (position < configuration.partition_size) {
                            const unsigned int entry = symbol(workspace, reader, book);
                            const unsigned int count = std::min(book.dimensions, configuration.partition_size - position);
                            charge(workspace, count);
                            const std::size_t source = std::size_t{entry} * book.dimensions;
                            if (configuration.type == 1) {
                                const std::size_t destination = std::size_t{workspace.bundle[channel]} * workspace.stride + offset + position;
                                for (unsigned int index = 0; index < count; ++index) workspace.spectrum[destination + index] += book.values[source + index];
                            } else {
                                for (unsigned int index = 0; index < count; ++index) {
                                    const unsigned int interleaved = offset + position + index;
                                    const unsigned int output_channel = workspace.bundle[interleaved % channels];
                                    const std::size_t destination = std::size_t{output_channel} * workspace.stride + interleaved / channels;
                                    workspace.spectrum[destination] += book.values[source + index];
                                }
                            }
                            position += count;
                        }
                    }
                }
            }
        }
    }
}
void window(std::span<double> samples, const Setup& setup, const bool large,
            const bool previous_large, const bool next_large) noexcept {
    const unsigned int size = static_cast<unsigned int>(samples.size());
    const unsigned int left_size = large && !previous_large ? setup.identification.blocks[0] : size;
    const unsigned int right_size = large && !next_large ? setup.identification.blocks[0] : size;
    const unsigned int left_begin = size / 4 - left_size / 4;
    const unsigned int left_end = left_begin + left_size / 2;
    const unsigned int right_begin = 3 * size / 4 - right_size / 4;
    const unsigned int right_end = right_begin + right_size / 2;
    const Transform& left = setup.transforms[left_size == setup.identification.blocks[0] ? 0 : 1];
    const Transform& right = setup.transforms[right_size == setup.identification.blocks[0] ? 0 : 1];
    std::fill(samples.begin(), samples.begin() + left_begin, 0.0);
    for (unsigned int index = left_begin; index < left_end; ++index) samples[index] *= left.window[index - left_begin];
    for (unsigned int index = right_begin; index < right_end; ++index) samples[index] *= right.window[right_end - index - 1];
    std::fill(samples.begin() + right_end, samples.end(), 0.0);
}
}
void prepare_workspace(Workspace& workspace, const Setup& setup, const Limits& limits, const Synthesis synthesis) {
    const unsigned int channels = setup.identification.channels;
    const unsigned int block = setup.identification.blocks[1];
    workspace.stride = block / 2;
    require(workspace.stride <= limits.output_frames, Status::resource_limit);
    const std::size_t count = product(channels, workspace.stride, limits.memory_bytes / sizeof(double));
    workspace.spectrum.resize(count); workspace.floor_curve.resize(count); workspace.previous.resize(count);
    workspace.time.resize(count * 2); workspace.pcm.resize(count); workspace.classifications.resize(count);
    workspace.real.resize(block); workspace.imaginary.resize(block);
    workspace.operation_limit = limits.packet_operations;
    workspace.butterfly = select_butterfly(synthesis);
}
void reset_overlap(Workspace& workspace) noexcept {
    workspace.previous_block = 0; workspace.frames = 0;
    std::fill(workspace.previous.begin(), workspace.previous.end(), 0.0);
}
void decode_packet(Workspace& workspace, const Setup& setup, const std::span<const std::uint8_t> packet) {
    workspace.operations = 0; workspace.frames = 0;
    BitReader reader(packet);
    require(packet_bits(reader, 1) == 0, Status::invalid_packet);
    const unsigned int mode_index = packet_bits(reader, ilog(static_cast<std::uint32_t>(setup.modes.size() - 1)));
    require(mode_index < setup.modes.size(), Status::invalid_packet, reader.position());
    const Mode& mode = setup.modes[mode_index];
    const unsigned int block = setup.identification.blocks[mode.large ? 1 : 0];
    const unsigned int bins = block / 2;
    const unsigned int channels = setup.identification.channels;
    charge(workspace, std::uint64_t{channels} * block * 4);
    bool previous_large = false; bool next_large = false;
    if (mode.large) { previous_large = packet_bits(reader, 1) != 0; next_large = packet_bits(reader, 1) != 0; }
    const Mapping& mapping = setup.mappings[mode.mapping];
    charge(workspace, std::uint64_t{mapping.couplings.size()} * bins);
    std::fill(workspace.spectrum.begin(), workspace.spectrum.end(), 0.0);
    for (unsigned int channel = 0; channel < channels; ++channel) {
        const Floor& floor = setup.floors[mapping.floors[mapping.mux[channel]]];
        const std::span<double> curve(workspace.floor_curve.data() + std::size_t{channel} * workspace.stride, bins);
        bool present = false;
        try {
            if (floor.type == 1) present = floor1(workspace, setup, floor, reader, curve);
            else present = floor0(workspace, setup, floor, reader, curve, mode.large);
        } catch (const DecodeFailure& error) {
            if (error.status != Status::truncated) throw;
            // Vorbis packet peeling permits an exhausted floor to become silence.
        }
        workspace.floor_present[channel] = present; workspace.residue_present[channel] = present;
    }
    for (const Coupling& coupling : mapping.couplings) {
        if (workspace.residue_present[coupling.magnitude] || workspace.residue_present[coupling.angle]) {
            workspace.residue_present[coupling.magnitude] = true; workspace.residue_present[coupling.angle] = true;
        }
    }
    try {
        for (unsigned int submap = 0; submap < mapping.submaps; ++submap) {
            const Residue& configuration = setup.residues[mapping.residues[submap]];
            unsigned int bundled = 0; bool any = false;
            for (unsigned int channel = 0; channel < channels; ++channel) {
                if (mapping.mux[channel] != submap) continue;
                if (workspace.residue_present[channel]) any = true;
                if (configuration.type == 2 || workspace.residue_present[channel]) workspace.bundle[bundled++] = channel;
            }
            if (any) residue(workspace, setup, configuration, reader, bundled, bins);
        }
    } catch (const DecodeFailure& error) {
        if (error.status != Status::truncated) throw;
        // Unspecified residue after a peeled packet remains zero.
    }
    for (std::size_t index = mapping.couplings.size(); index > 0; --index) {
        const Coupling& coupling = mapping.couplings[index - 1];
        const std::size_t magnitude_base = std::size_t{coupling.magnitude} * workspace.stride;
        const std::size_t angle_base = std::size_t{coupling.angle} * workspace.stride;
        for (unsigned int bin = 0; bin < bins; ++bin) {
            const double magnitude = workspace.spectrum[magnitude_base + bin];
            const double angle = workspace.spectrum[angle_base + bin];
            double left = magnitude; double right = magnitude;
            if (magnitude > 0) { if (angle > 0) right = magnitude - angle; else left = magnitude + angle; }
            else { if (angle > 0) right = magnitude + angle; else left = magnitude - angle; }
            workspace.spectrum[magnitude_base + bin] = left; workspace.spectrum[angle_base + bin] = right;
        }
    }
    const unsigned int frames = workspace.previous_block == 0 ? 0 : (workspace.previous_block + block) / 4;
    for (unsigned int channel = 0; channel < channels; ++channel) {
        const std::size_t base = std::size_t{channel} * workspace.stride;
        const std::span<double> spectrum(workspace.spectrum.data() + base, bins);
        const std::span<double> time(workspace.time.data() + 2 * base, block);
        if (workspace.floor_present[channel]) {
            charge(workspace, std::uint64_t{block} * ilog(block - 1));
            for (unsigned int bin = 0; bin < bins; ++bin) {
                spectrum[bin] *= workspace.floor_curve[base + bin];
                require(std::isfinite(spectrum[bin]), Status::invalid_packet);
            }
            inverse_mdct(setup.transforms[mode.large ? 1 : 0], spectrum, time, workspace.real, workspace.imaginary, workspace.butterfly);
            window(time, setup, mode.large, previous_large, next_large);
        } else std::fill(time.begin(), time.end(), 0.0);
        const int current_start = (static_cast<int>(block) - static_cast<int>(workspace.previous_block)) / 4;
        for (unsigned int index = 0; index < frames; ++index) {
            const int current = current_start + static_cast<int>(index);
            double value = index < workspace.previous_block / 2 ? workspace.previous[base + index] : 0;
            if (current >= 0 && current < static_cast<int>(block)) value += time[static_cast<std::size_t>(current)];
            require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(), Status::invalid_packet);
            workspace.pcm[base + index] = static_cast<float>(value);
        }
        std::copy(time.begin() + bins, time.end(), workspace.previous.begin() + static_cast<std::ptrdiff_t>(base));
    }
    workspace.previous_block = block;
    workspace.frames = frames;
}
} // namespace stx_vorbis::detail
