// Consumes the installed package through find_package alone: reads a short
// dialect text and writes it back in canonical form.
#include <stx_rtf2md/document.hpp>
#include <stx_rtf2md/limits.hpp>
#include <stx_rtf2md/markdown.hpp>

#include <span>
#include <string_view>
#include <vector>

int main() {
    const stx_rtf2md::Limits limits{};
    stx_rtf2md::MarkdownReader reader(limits);
    stx_rtf2md::MarkdownWriter writer(limits);
    stx_rtf2md::Document document{};
    stx_rtf2md::LossReport loss{};
    const std::string_view source = "# Title\n\nSome __bold__ and [underlined]{u} text.\n";
    const stx_rtf2md::Result read = reader.read(std::span<const char>(source.data(), source.size()), document, loss);
    if (read.status != stx_rtf2md::Status::ok || document.blocks.size() != 2U) {
        return 1;
    }
    std::vector<char> output(256);
    const stx_rtf2md::Result written = writer.write(document, std::span<char>(output.data(), output.size()));
    if (written.status != stx_rtf2md::Status::ok) {
        return 2;
    }
    const std::string_view canonical(output.data(), written.produced);
    if (canonical != "# Title\n\nSome **bold** and [underlined]{u} text.\n") {
        return 3;
    }
    return 0;
}
