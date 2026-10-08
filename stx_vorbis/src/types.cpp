#include "stx_vorbis/types.hpp"
namespace stx_vorbis {
const char* status_name(const Status status) noexcept {
    switch (status) {
    case Status::ok: return "ok";
    case Status::need_input: return "need_input";
    case Status::need_output: return "need_output";
    case Status::packet: return "packet";
    case Status::pcm: return "pcm";
    case Status::event: return "event";
    case Status::end: return "end";
    case Status::invalid_argument: return "invalid_argument";
    case Status::invalid_header: return "invalid_header";
    case Status::corrupt_page: return "corrupt_page";
    case Status::sequence_gap: return "sequence_gap";
    case Status::invalid_packet: return "invalid_packet";
    case Status::truncated: return "truncated";
    case Status::resource_limit: return "resource_limit";
    case Status::allocation_failed: return "allocation_failed";
    case Status::unsupported: return "unsupported";
    case Status::io_error: return "io_error";
    }
    return "unknown";
}
} // namespace stx_vorbis
