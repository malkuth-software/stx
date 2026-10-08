#include <bfft/bodft.h>
#include "detail/bodft_kernel.hpp"
#include <cstring>
#include <limits>
#include <type_traits>

struct bodft_prepared_plan final {
    std::size_t size{};
    std::size_t bytes{};
    bodft_precision precision{BODFT_PRECISION_F64};
    const void* kernel{nullptr};
};
struct bodft_workspace final {
    std::size_t size{};
    std::size_t bytes{};
    bodft_precision precision{BODFT_PRECISION_F64};
    void* scratch{nullptr};
};

namespace {
constexpr std::size_t storage_alignment = 64;
struct Layout final {
    bodft_storage_requirements requirements{};
    std::size_t kernel_offset{};
    std::size_t arrays_offset{};
    std::size_t scratch_offset{};
};
struct Region final {
    std::uintptr_t begin{};
    std::uintptr_t end{};
};

bool region(const void* const pointer, const std::size_t bytes, Region& result) noexcept {
    if (pointer == nullptr) return false;
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(pointer);
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    result.begin = begin;
    result.end = begin + bytes;
    return true;
}
bool overlap(const Region& first, const Region& second) noexcept {
    const bool intersects = first.begin < second.end && second.begin < first.end;
    return intersects;
}
bool aligned(const void* const pointer, const std::size_t alignment) noexcept {
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
    const bool valid = pointer != nullptr && address % alignment == 0;
    return valid;
}
bool valid_size(const std::size_t size) noexcept {
    const bool valid = size >= 2 && size <= static_cast<std::size_t>(std::numeric_limits<int>::max())
        && (size & (size - 1)) == 0;
    return valid;
}

template <typename CT, typename RT>
bool measure(const std::size_t size, Layout& result) noexcept {
    using Kernel = bodft::plan_t<CT, RT, true>;
    static_assert(std::is_trivially_destructible<Kernel>::value, "Caller storage needs no destruction");
    if (!valid_size(size)) return false;
    bodft::storage_cursor plan{};
    std::size_t ignored = 0;
    if (!plan.take(1, sizeof(bodft_prepared_plan), storage_alignment, ignored)) return false;
    if (!plan.take(1, sizeof(Kernel), storage_alignment, result.kernel_offset)) return false;
    result.arrays_offset = plan.used;
    const int power = bruun::ilog2_pow2(static_cast<int>(size));
    const std::size_t leaf = power % 2 == 0 ? 4 : 2;
    // Match the plan constructor's active-stage, then permutation layout.
    for (std::size_t width = leaf * 4; width <= size;) {
        for (unsigned int table = 0; table < 3; ++table) {
            if (!plan.take(width / 8, sizeof(CT), alignof(CT), ignored)) return false;
        }
        if (width == size) break;
        width *= 4;
    }
    if (!plan.take(size, sizeof(int), alignof(int), ignored)) return false;
    bodft::storage_cursor workspace{};
    if (!workspace.take(1, sizeof(bodft_workspace), storage_alignment, ignored)) return false;
    if (!workspace.take(size, sizeof(CT), alignof(CT), result.scratch_offset)) return false;
    result.requirements.plan_bytes = plan.used;
    result.requirements.plan_alignment = storage_alignment;
    result.requirements.workspace_bytes = workspace.used;
    result.requirements.workspace_alignment = storage_alignment;
    return true;
}

bool layout(const std::size_t size, const bodft_precision precision, Layout& result) noexcept {
    if (precision == BODFT_PRECISION_F64) {
        const bool valid = measure<bfft_complex, double>(size, result);
        return valid;
    }
    if (precision == BODFT_PRECISION_F32) {
        const bool valid = measure<bfft_complex_f32, float>(size, result);
        return valid;
    }
    return false;
}

template <typename CT, typename RT>
const void* construct_kernel(const std::size_t size, unsigned char* const storage, const Layout& shape) {
    using Kernel = bodft::plan_t<CT, RT, true>;
    bodft::storage_cursor arrays{storage, shape.requirements.plan_bytes, shape.arrays_offset};
    Kernel* const kernel = new (storage + shape.kernel_offset) Kernel(static_cast<int>(size), &arrays);
    return kernel;
}

template <typename CT>
void construct_scratch(void* const storage, const std::size_t size) noexcept {
    CT* const values = static_cast<CT*>(storage);
    for (std::size_t index = 0; index < size; ++index) new (values + index) CT{};
}

template <typename Input, typename Output>
bool valid_call(const bodft_prepared_plan* const plan, const bodft_workspace* const workspace,
                const bodft_precision precision, const Input* const input, const std::size_t input_count,
                Output* const output, const std::size_t output_count, const bool inverse) noexcept {
    if (plan == nullptr || workspace == nullptr) return false;
    const bodft_prepared_plan& prepared = *plan;
    const bodft_workspace& work = *workspace;
    if (prepared.precision != precision || work.precision != precision || prepared.size != work.size) return false;
    const std::size_t required_input = inverse ? prepared.size / 2 : prepared.size;
    const std::size_t required_output = inverse ? prepared.size : prepared.size / 2;
    if (input_count < required_input || output_count < required_output) return false;
    if (!aligned(input, alignof(Input)) || !aligned(output, alignof(Output))) return false;
    Region regions[4]{};
    if (!region(plan, prepared.bytes, regions[0]) || !region(workspace, work.bytes, regions[1])
        || !region(input, required_input * sizeof(Input), regions[2])
        || !region(output, required_output * sizeof(Output), regions[3])) return false;
    for (unsigned int first = 0; first < 4; ++first) {
        for (unsigned int second = first + 1; second < 4; ++second) {
            if (overlap(regions[first], regions[second])) return false;
        }
    }
    return true;
}

template <typename CT, typename RT>
bfft_status forward(const bodft_prepared_plan* const plan, bodft_workspace* const workspace,
                    const bodft_precision precision, const RT* const input, const std::size_t input_count,
                    CT* const output, const std::size_t output_count) noexcept {
    if (!valid_call(plan, workspace, precision, input, input_count, output, output_count, false))
        return BFFT_ERROR_INVALID_ARGUMENT;
    using Kernel = bodft::plan_t<CT, RT, true>;
    const Kernel& kernel = *static_cast<const Kernel*>((*plan).kernel);
    CT* const scratch = static_cast<CT*>((*workspace).scratch);
    kernel.forward(input, output, scratch);
    return BFFT_OK;
}
template <typename CT, typename RT>
bfft_status inverse(const bodft_prepared_plan* const plan, bodft_workspace* const workspace,
                    const bodft_precision precision, const CT* const input, const std::size_t input_count,
                    RT* const output, const std::size_t output_count) noexcept {
    if (!valid_call(plan, workspace, precision, input, input_count, output, output_count, true))
        return BFFT_ERROR_INVALID_ARGUMENT;
    using Kernel = bodft::plan_t<CT, RT, true>;
    const Kernel& kernel = *static_cast<const Kernel*>((*plan).kernel);
    CT* const scratch = static_cast<CT*>((*workspace).scratch);
    kernel.inverse(input, output, scratch);
    return BFFT_OK;
}
} // namespace

bfft_status bodft_query_storage(const std::size_t n, const bodft_precision precision,
                                bodft_storage_requirements* const requirements) {
    if (requirements == nullptr) return BFFT_ERROR_INVALID_ARGUMENT;
    Layout shape{};
    if (!layout(n, precision, shape)) return BFFT_ERROR_INVALID_ARGUMENT;
    *requirements = shape.requirements;
    return BFFT_OK;
}

bfft_status bodft_prepare(const std::size_t n, const bodft_precision precision,
                          void* const storage, const std::size_t storage_bytes,
                          const bodft_prepared_plan** const plan) {
    if (plan == nullptr) return BFFT_ERROR_INVALID_ARGUMENT;
    Region supplied{};
    Region slot{};
    if (!region(plan, sizeof(*plan), slot)) return BFFT_ERROR_INVALID_ARGUMENT;
    if (!region(storage, storage_bytes, supplied)) { *plan = nullptr; return BFFT_ERROR_INVALID_ARGUMENT; }
    if (overlap(supplied, slot)) return BFFT_ERROR_INVALID_ARGUMENT;
    *plan = nullptr;
    Layout shape{};
    if (!layout(n, precision, shape)) return BFFT_ERROR_INVALID_ARGUMENT;
    if (!aligned(storage, storage_alignment) || storage_bytes < shape.requirements.plan_bytes
        || !region(storage, shape.requirements.plan_bytes, supplied)) return BFFT_ERROR_INVALID_ARGUMENT;
    unsigned char* const bytes = static_cast<unsigned char*>(storage);
    std::memset(bytes, 0, shape.requirements.plan_bytes);
    const void* kernel = nullptr;
    try {
        if (precision == BODFT_PRECISION_F64) kernel = construct_kernel<bfft_complex, double>(n, bytes, shape);
        else kernel = construct_kernel<bfft_complex_f32, float>(n, bytes, shape);
    } catch (...) { return BFFT_ERROR_INTERNAL; }
    const bodft_prepared_plan* const result = new (bytes) bodft_prepared_plan{
        n, shape.requirements.plan_bytes, precision, kernel};
    *plan = result;
    return BFFT_OK;
}

bfft_status bodft_prepare_workspace(const bodft_prepared_plan* const plan,
                                    void* const storage, const std::size_t storage_bytes,
                                    bodft_workspace** const workspace) {
    if (workspace == nullptr) return BFFT_ERROR_INVALID_ARGUMENT;
    Region supplied{};
    Region slot{};
    Region plan_region{};
    if (!region(workspace, sizeof(*workspace), slot)) return BFFT_ERROR_INVALID_ARGUMENT;
    if (plan != nullptr && region(plan, (*plan).bytes, plan_region) && overlap(plan_region, slot))
        return BFFT_ERROR_INVALID_ARGUMENT;
    if (!region(storage, storage_bytes, supplied)) { *workspace = nullptr; return BFFT_ERROR_INVALID_ARGUMENT; }
    if (overlap(supplied, slot)) return BFFT_ERROR_INVALID_ARGUMENT;
    *workspace = nullptr;
    if (plan == nullptr) return BFFT_ERROR_INVALID_ARGUMENT;
    Layout shape{};
    if (!layout((*plan).size, (*plan).precision, shape)) return BFFT_ERROR_INVALID_ARGUMENT;
    if (!aligned(storage, storage_alignment) || storage_bytes < shape.requirements.workspace_bytes
        || !region(storage, shape.requirements.workspace_bytes, supplied)
        || !region(plan, (*plan).bytes, plan_region) || overlap(plan_region, supplied)) return BFFT_ERROR_INVALID_ARGUMENT;
    unsigned char* const bytes = static_cast<unsigned char*>(storage);
    std::memset(bytes, 0, shape.requirements.workspace_bytes);
    void* const scratch = bytes + shape.scratch_offset;
    if ((*plan).precision == BODFT_PRECISION_F64) construct_scratch<bfft_complex>(scratch, (*plan).size);
    else construct_scratch<bfft_complex_f32>(scratch, (*plan).size);
    bodft_workspace* const result = new (bytes) bodft_workspace{
        (*plan).size, shape.requirements.workspace_bytes, (*plan).precision, scratch};
    *workspace = result;
    return BFFT_OK;
}

bfft_status bodft_forward_prepared(const bodft_prepared_plan* const plan, bodft_workspace* const workspace,
                                   const double* const input, const std::size_t input_count,
                                   bfft_complex* const output, const std::size_t output_count) {
    const bfft_status status = forward(plan, workspace, BODFT_PRECISION_F64, input, input_count, output, output_count);
    return status;
}
bfft_status bodft_inverse_prepared(const bodft_prepared_plan* const plan, bodft_workspace* const workspace,
                                   const bfft_complex* const input, const std::size_t input_count,
                                   double* const output, const std::size_t output_count) {
    const bfft_status status = inverse(plan, workspace, BODFT_PRECISION_F64, input, input_count, output, output_count);
    return status;
}
bfft_status bodft_forward_prepared_f32(const bodft_prepared_plan* const plan, bodft_workspace* const workspace,
                                       const float* const input, const std::size_t input_count,
                                       bfft_complex_f32* const output, const std::size_t output_count) {
    const bfft_status status = forward(plan, workspace, BODFT_PRECISION_F32, input, input_count, output, output_count);
    return status;
}
bfft_status bodft_inverse_prepared_f32(const bodft_prepared_plan* const plan, bodft_workspace* const workspace,
                                       const bfft_complex_f32* const input, const std::size_t input_count,
                                       float* const output, const std::size_t output_count) {
    const bfft_status status = inverse(plan, workspace, BODFT_PRECISION_F32, input, input_count, output, output_count);
    return status;
}
