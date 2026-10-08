#include "stx_vorbis/source.hpp"
#include <algorithm>
#include <limits>
namespace stx_vorbis {
MemorySource::MemorySource(const std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
Status MemorySource::read(const std::span<std::uint8_t> destination, std::size_t& count) noexcept {
    count = std::min(destination.size(), bytes_.size() - position_);
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), count, destination.begin());
    position_ += count;
    if (count == 0 && position_ == bytes_.size()) return Status::end;
    return Status::ok;
}
Status MemorySource::seek(const std::uint64_t byte_offset) noexcept {
    if (byte_offset > bytes_.size()) return Status::invalid_argument;
    position_ = static_cast<std::size_t>(byte_offset);
    return Status::ok;
}
FileSource::~FileSource() { close(); }
Status FileSource::open(const char* const path) noexcept {
    if (path == nullptr) return Status::invalid_argument;
    std::FILE* const file = std::fopen(path, "rb");
    if (file == nullptr) return Status::io_error;
    close(); file_ = file; owned_ = true; origin_ = 0;
    return Status::ok;
}
Status FileSource::attach(std::FILE* const file, const bool take_ownership) noexcept {
    if (file == nullptr || file == file_) return Status::invalid_argument;
#if defined(_WIN32)
    const std::int64_t position = _ftelli64(file);
#else
    const off_t position = ftello(file);
#endif
    if (position < 0) return Status::io_error;
    close(); file_ = file; origin_ = static_cast<std::uint64_t>(position); owned_ = take_ownership;
    return Status::ok;
}
void FileSource::close() noexcept {
    if (file_ != nullptr && owned_) std::fclose(file_);
    file_ = nullptr; owned_ = false; origin_ = 0;
}
Status FileSource::read(const std::span<std::uint8_t> destination, std::size_t& count) noexcept {
    count = 0;
    if (file_ == nullptr) return Status::invalid_argument;
    count = std::fread(destination.data(), 1, destination.size(), file_);
    if (std::ferror(file_) != 0) return Status::io_error;
    if (count == 0 && std::feof(file_) != 0) return Status::end;
    return Status::ok;
}
Status FileSource::seek(const std::uint64_t byte_offset) noexcept {
    if (file_ == nullptr || byte_offset > std::uint64_t{INT64_MAX} - origin_) return Status::invalid_argument;
    const std::uint64_t absolute = origin_ + byte_offset;
#if defined(_WIN32)
    const int result = _fseeki64(file_, static_cast<std::int64_t>(absolute), SEEK_SET);
#else
    if (absolute > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) return Status::invalid_argument;
    const int result = fseeko(file_, static_cast<off_t>(absolute), SEEK_SET);
#endif
    if (result != 0) return Status::io_error;
    return Status::ok;
}
PullDecoder::PullDecoder(Source& source, const Limits limits, const Recovery recovery,
                         const Synthesis synthesis, std::pmr::memory_resource* const memory)
    : source_(source), decoder_(limits, recovery, synthesis, memory) {}
Decoder& PullDecoder::decoder() noexcept { return decoder_; }
Status PullDecoder::advance() noexcept {
    while (true) {
        const Status status = decoder_.advance();
        if (status != Status::need_input) return status;
        if (used_ == size_ && !final_) {
            used_ = 0; size_ = 0;
            const Status read = source_.read(input_, size_);
            if (size_ > input_.size()) return Status::invalid_argument;
            if (read == Status::end) final_ = true;
            else if (read != Status::ok) return read;
            else if (size_ == 0) return Status::io_error;
        }
        if (final_sent_) return Status::truncated;
        const FeedResult result = decoder_.push(std::span<const std::uint8_t>(input_.data() + used_, size_ - used_), final_);
        used_ += result.accepted;
        if (final_ && used_ == size_) final_sent_ = true;
        if (result.status != Status::ok && result.status != Status::need_output) return result.status;
        if (result.accepted == 0 && !final_) return Status::resource_limit;
    }
}
Status PullDecoder::rewind() noexcept {
    const Status status = source_.seek(0);
    if (status != Status::ok) return status;
    decoder_.reset(); size_ = 0; used_ = 0; final_ = false; final_sent_ = false;
    return Status::ok;
}
Status PullDecoder::seek(const SamplePosition target) noexcept {
    const Status reset = rewind();
    if (reset != Status::ok) return reset;
    while (true) {
        const Status status = advance();
        if (status == Status::event) {
            const Event event = decoder_.event();
            if (event.kind == EventKind::stream_end && event.stream.chain_index == target.chain_index) {
                if (event.stream.next_sample != target.sample) return Status::invalid_argument;
                return Status::end;
            }
            decoder_.acknowledge_event(); continue;
        }
        if (status != Status::pcm) return status == Status::end ? Status::invalid_argument : status;
        const PcmView output = decoder_.output();
        if (output.chain_index > target.chain_index) return Status::invalid_argument;
        if (output.chain_index == target.chain_index && target.sample < output.first_sample + output.frames) {
            if (target.sample < output.first_sample) return Status::invalid_argument;
            const Status consumed = decoder_.consume(static_cast<std::uint32_t>(target.sample - output.first_sample));
            return consumed;
        }
        const Status consumed = decoder_.consume(output.frames);
        if (consumed != Status::ok) return consumed;
    }
}
} // namespace stx_vorbis
