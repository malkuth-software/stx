// Bitwise regression of stx_conv::stream over the 2,112 regression cases.
//
// usage: stx_conv_stream_regression BASE_ORACLE BUBBLE_RECORD
//        stx_conv_stream_regression --write-bubble-record BASE_ORACLE BUBBLE_RECORD
//
// BASE_ORACLE (tests/stream/base_oracle.sha256) holds the SHA-256 of the
// NumPy base model's output per case: the stream with bubble correction
// disabled must reproduce it bit for bit. BUBBLE_RECORD
// (tests/stream/bubble_record.sha256) holds the stream's own output with
// bubble correction enabled, as recorded when the stream was folded into
// GUI.Forms: it holds later changes to that output, and is not an independent
// oracle (tests/stream/bubble_oracle.cpp compares that output with the model
// to a tolerance). Both files also carry the digest of every case's input,
// which checks this generator against tests/stream/oracle/cases.py.
#include "regression_cases.hpp"
#include "sha256.hpp"
#include <stx_conv/stream.hpp>
#include <bit>
#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

static_assert(std::endian::native == std::endian::little, "digests are of little-endian samples");

struct DigestFile {
    std::string inputs;
    std::vector<std::string> outputs;
};

// Reads "inputs <hex>" and then "<index> <hex>" lines in index order; lines
// starting with '#' are comments. Returns false on any other form.
bool read_digests(const char* path, std::size_t cases, DigestFile& digests) {
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    digests.inputs.clear();
    digests.outputs.clear();
    digests.outputs.reserve(cases);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') { continue; }
        const std::size_t space = line.find(' ');
        if (space == std::string::npos || line.size() - space - 1 != 64) {
            std::fprintf(stderr, "%s: malformed line: %s\n", path, line.c_str());
            return false;
        }
        const std::string key = line.substr(0, space);
        const std::string hex = line.substr(space + 1);
        if (key == "inputs") {
            digests.inputs = hex;
            continue;
        }
        if (key != std::to_string(digests.outputs.size())) {
            std::fprintf(stderr, "%s: expected case %zu, found %s\n", path, digests.outputs.size(), key.c_str());
            return false;
        }
        digests.outputs.push_back(hex);
    }
    const bool complete = digests.inputs.size() == 64 && digests.outputs.size() == cases;
    if (!complete) { std::fprintf(stderr, "%s: incomplete (%zu cases)\n", path, digests.outputs.size()); }
    return complete;
}

bool write_digests(const char* path, const DigestFile& digests) {
    std::ofstream file(path, std::ios::binary);
    file << "# SHA-256 of stx_conv::stream output with bubble correction enabled, per case;\n";
    file << "# a record written by stx_conv_stream_regression --write-bubble-record.\n";
    file << "inputs " << digests.inputs << "\n";
    for (std::size_t index = 0; index < digests.outputs.size(); ++index) {
        file << index << " " << digests.outputs[index] << "\n";
    }
    const bool written = static_cast<bool>(file);
    return written;
}

template<class Scalar>
std::span<const std::byte> sample_bytes(const std::vector<Scalar>& samples) noexcept {
    const std::span<const Scalar> view(samples);
    const std::span<const std::byte> result = std::as_bytes(view);
    return result;
}

struct CaseDigests {
    std::string bubble_disabled;
    std::string bubble_enabled;
    bool resized = false;
};

// Resizes one case both ways. `input` receives the case's input bytes.
template<stx_conv::Precision Scalar>
CaseDigests run_case(const stx_conv_tests::RegressionCase& regression_case, const std::vector<double>& image,
                     stx_conv_tests::Sha256& input) {
    std::vector<Scalar> source(image.size());
    for (std::size_t index = 0; index < image.size(); ++index) { source[index] = static_cast<Scalar>(image[index]); }
    input.update(sample_bytes(source));
    const std::size_t channels = regression_case.channels;
    const std::size_t source_row = regression_case.source_width * channels;
    const std::size_t target_row = regression_case.target_width * channels;
    std::vector<Scalar> output(target_row * regression_case.target_height, Scalar(0));
    stx_conv::stream::Workspace<Scalar> workspace(
        regression_case.source_width, regression_case.source_height, regression_case.target_width,
        regression_case.target_height, channels, regression_case.sampling);
    stx_conv_tests::Sha256 digest;
    CaseDigests result{};
    workspace.set_bubble_enabled(false);
    const stx_conv::Status disabled = stx_conv::stream::resize<Scalar>(workspace, {source, source_row}, {output, target_row});
    digest.update(sample_bytes(output));
    result.bubble_disabled = digest.finish_hex();
    workspace.set_bubble_enabled(true);
    const stx_conv::Status enabled = stx_conv::stream::resize<Scalar>(workspace, {source, source_row}, {output, target_row});
    digest.update(sample_bytes(output));
    result.bubble_enabled = digest.finish_hex();
    result.resized = disabled == stx_conv::Status::ok && enabled == stx_conv::Status::ok;
    return result;
}

} // namespace

int main(int argument_count, char** arguments) {
    const bool write_record = argument_count == 4 && std::string(arguments[1]) == "--write-bubble-record";
    if (argument_count != 3 && !write_record) {
        std::fprintf(stderr, "usage: stx_conv_stream_regression [--write-bubble-record] BASE_ORACLE BUBBLE_RECORD\n");
        return 2;
    }
    const char* base_path = arguments[write_record ? 2 : 1];
    const char* record_path = arguments[write_record ? 3 : 2];
    const std::vector<stx_conv_tests::RegressionCase> cases = stx_conv_tests::regression_cases();
    DigestFile base{};
    if (!read_digests(base_path, cases.size(), base)) { return 2; }
    DigestFile record{};
    if (!write_record && !read_digests(record_path, cases.size(), record)) { return 2; }
    DigestFile written{};
    written.outputs.reserve(cases.size());
    stx_conv_tests::Sha256 input;
    std::vector<double> image;
    std::size_t base_mismatches = 0;
    std::size_t record_mismatches = 0;
    std::size_t failures = 0;
    for (const stx_conv_tests::RegressionCase& regression_case : cases) {
        stx_conv_tests::fill_case_image(regression_case, image);
        const CaseDigests digests = regression_case.precision_bits == 32
            ? run_case<float>(regression_case, image, input)
            : run_case<double>(regression_case, image, input);
        const std::string label = stx_conv_tests::describe(regression_case);
        if (!digests.resized) {
            ++failures;
            std::printf("FAILED case %zu %s: resize did not return ok\n", regression_case.index, label.c_str());
        }
        if (digests.bubble_disabled != base.outputs[regression_case.index]) {
            ++base_mismatches;
            std::printf("BASE MISMATCH case %zu %s\n", regression_case.index, label.c_str());
        }
        if (!write_record && digests.bubble_enabled != record.outputs[regression_case.index]) {
            ++record_mismatches;
            std::printf("BUBBLE RECORD MISMATCH case %zu %s\n", regression_case.index, label.c_str());
        }
        written.outputs.push_back(digests.bubble_enabled);
    }
    written.inputs = input.finish_hex();
    const bool inputs_match = written.inputs == base.inputs && (write_record || written.inputs == record.inputs);
    if (!inputs_match) { std::printf("INPUT MISMATCH: generator %s, oracle %s\n", written.inputs.c_str(), base.inputs.c_str()); }
    std::printf("cases=%zu failures=%zu base_mismatches=%zu bubble_record_mismatches=%zu inputs=%s\n", cases.size(),
                failures, base_mismatches, record_mismatches, inputs_match ? "match" : "differ");
    if (write_record) {
        if (!write_digests(record_path, written)) {
            std::fprintf(stderr, "cannot write %s\n", record_path);
            return 2;
        }
    }
    const bool passed = inputs_match && failures == 0 && base_mismatches == 0 && record_mismatches == 0;
    const int result = passed ? 0 : 1;
    return result;
}
