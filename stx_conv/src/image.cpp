#include "internal.hpp"
#include <algorithm>
#include <utility>

namespace stx_conv {
template<Precision Scalar>
BasicImagePlan<Scalar>::BasicImagePlan(std::size_t source_width, std::size_t source_height,
                     std::size_t target_width, std::size_t target_height, Sampling sampling)
    : horizontal_(source_width, target_width, sampling), vertical_(source_height, target_height, sampling) {}
// Moving each line plan leaves it empty, so the moved-from image plan is empty.
template<Precision Scalar>
BasicImagePlan<Scalar>::BasicImagePlan(BasicImagePlan<Scalar>&& other) noexcept
    : horizontal_(std::move(other.horizontal_)), vertical_(std::move(other.vertical_)) {}
template<Precision Scalar>
BasicImagePlan<Scalar>& BasicImagePlan<Scalar>::operator=(BasicImagePlan<Scalar>&& other) noexcept {
    if (this != &other) {
        horizontal_ = std::move(other.horizontal_);
        vertical_ = std::move(other.vertical_);
    }
    return *this;
}
template<Precision Scalar>
std::size_t BasicImagePlan<Scalar>::storage_bytes() const noexcept {
    const std::size_t bytes = horizontal_.storage_bytes() + vertical_.storage_bytes();
    return bytes;
}
template<Precision Scalar>
BasicImageWorkspace<Scalar>::BasicImageWorkspace(const BasicImagePlan<Scalar>& plan, std::size_t channels)
    : source_width_(plan.horizontal_.source_length()), source_height_(plan.vertical_.source_length()),
      target_width_(plan.horizontal_.target_length()), target_height_(plan.vertical_.target_length()),
      channels_(channels) {
    detail::validate_length(source_width_);
    detail::validate_length(source_height_);
    detail::validate_length(channels);
    // A changed axis can grow a row even when no intermediate is allocated.
    // Establish both row products before resize uses them (including 32-bit hosts).
    detail::checked_product<Scalar>(source_width_, channels);
    detail::checked_product<Scalar>(target_width_, channels);
    const bool across = source_width_ != target_width_;
    const bool down = source_height_ != target_height_;
    if (across) {
        horizontal_.emplace(AxisWork{BasicProfile<Scalar>(source_width_, channels),
                                     BasicLineWorkspace<Scalar>(source_width_, channels)});
    }
    if (down) {
        vertical_.emplace(AxisWork{BasicProfile<Scalar>(source_height_, channels),
                                   BasicLineWorkspace<Scalar>(source_height_, channels)});
    }
    if (across && down) {
        const bool vertical_first = target_height_ < source_height_;
        const std::size_t width = vertical_first ? source_width_ : target_width_;
        const std::size_t height = vertical_first ? target_height_ : source_height_;
        middle_.resize(detail::checked_product<Scalar>(detail::checked_product<Scalar>(width, height), channels));
    }
}
template<Precision Scalar>
BasicImageWorkspace<Scalar>::BasicImageWorkspace(BasicImageWorkspace<Scalar>&& other) noexcept
    : horizontal_(std::move(other.horizontal_)), vertical_(std::move(other.vertical_)),
      middle_(std::move(other.middle_)), source_width_(other.source_width_), source_height_(other.source_height_),
      target_width_(other.target_width_), target_height_(other.target_height_), channels_(other.channels_) {
    other.release();
}
template<Precision Scalar>
BasicImageWorkspace<Scalar>& BasicImageWorkspace<Scalar>::operator=(BasicImageWorkspace<Scalar>&& other) noexcept {
    if (this != &other) {
        horizontal_ = std::move(other.horizontal_);
        vertical_ = std::move(other.vertical_);
        middle_ = std::move(other.middle_);
        source_width_ = other.source_width_;
        source_height_ = other.source_height_;
        target_width_ = other.target_width_;
        target_height_ = other.target_height_;
        channels_ = other.channels_;
        other.release();
    }
    return *this;
}
// The empty state: no axes, no intermediate, zero dimensions and channels.
// Moving an optional leaves it engaged (holding an empty owner), so reset it.
template<Precision Scalar>
void BasicImageWorkspace<Scalar>::release() noexcept {
    horizontal_.reset();
    vertical_.reset();
    std::vector<Scalar>().swap(middle_);
    source_width_ = 0;
    source_height_ = 0;
    target_width_ = 0;
    target_height_ = 0;
    channels_ = 0;
}
template<Precision Scalar>
std::size_t BasicImageWorkspace<Scalar>::storage_bytes() const noexcept {
    std::size_t bytes = middle_.capacity() * sizeof(Scalar);
    if (horizontal_.has_value()) {
        bytes += (*horizontal_).profile.storage_bytes() + (*horizontal_).work.storage_bytes();
    }
    if (vertical_.has_value()) {
        bytes += (*vertical_).profile.storage_bytes() + (*vertical_).work.storage_bytes();
    }
    return bytes;
}
template<Precision Scalar>
Status resize(const BasicImagePlan<Scalar>& plan, BasicImageInput<Scalar> source, BasicImageOutput<Scalar> output, BasicImageWorkspace<Scalar>& workspace) noexcept {
    const Status result = detail::Kernel<Scalar>::resize_checked(plan, source, output, workspace);
    return result;
}
} // namespace stx_conv

namespace stx_conv::detail {
namespace {
template<Precision Scalar>
Status horizontal(const BasicLinePlan<Scalar>& plan, BasicImageInput<Scalar> source, BasicImageOutput<Scalar> output, std::size_t rows,
                  std::size_t channels, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& work) noexcept {
    for (std::size_t row = 0; row < rows; ++row) {
        const BasicLineInput<Scalar> input{source.samples.subspan(row * source.row_stride), channels};
        const BasicLineOutput<Scalar> destination{output.samples.subspan(row * output.row_stride), channels};
        const Status status = Kernel<Scalar>::compile_unchecked(input, profile, work);
        if (status != Status::ok) { return status; }
        Kernel<Scalar>::evaluate_unchecked(profile, plan, destination);
    }
    return Status::ok;
}
template<Precision Scalar>
Status vertical(const BasicLinePlan<Scalar>& plan, BasicImageInput<Scalar> source, BasicImageOutput<Scalar> output, std::size_t columns,
                std::size_t channels, BasicProfile<Scalar>& profile, BasicLineWorkspace<Scalar>& work) noexcept {
    for (std::size_t column = 0; column < columns; ++column) {
        const BasicLineInput<Scalar> input{source.samples.subspan(column * channels), source.row_stride};
        const BasicLineOutput<Scalar> destination{output.samples.subspan(column * channels), output.row_stride};
        const Status status = Kernel<Scalar>::compile_unchecked(input, profile, work);
        if (status != Status::ok) { return status; }
        Kernel<Scalar>::evaluate_unchecked(profile, plan, destination);
    }
    return Status::ok;
}
}
template<Precision Scalar>
Status Kernel<Scalar>::resize_checked(const BasicImagePlan<Scalar>& plan, BasicImageInput<Scalar> source, BasicImageOutput<Scalar> output,
                              BasicImageWorkspace<Scalar>& work) noexcept {
    const std::size_t sw = plan.horizontal_.source_length_;
    const std::size_t sh = plan.vertical_.source_length_;
    const std::size_t tw = plan.horizontal_.target_length_;
    const std::size_t th = plan.vertical_.target_length_;
    const std::size_t channels = work.channels_;
    // Empty (moved-from) plans and workspaces have zero lengths. A constructed
    // workspace whose dimensions match holds line storage for exactly the
    // axes whose length changes, and the intermediate when both change.
    if (sw == 0 || sh == 0 || channels == 0 || sw != work.source_width_ || sh != work.source_height_
        || tw != work.target_width_ || th != work.target_height_) { return Status::invalid_shape; }
    const std::size_t source_row = sw * channels;
    const std::size_t target_row = tw * channels;
    if (!fits(sh, source.row_stride, source_row, source.samples.size())
        || !fits(th, output.row_stride, target_row, output.samples.size())) { return Status::insufficient_storage; }
    if (overlaps(source.samples, output.samples)) { return Status::overlap; }
    const Status validated = validate_samples<Scalar>({source.samples, source.row_stride}, sh, source_row);
    if (validated != Status::ok) { return validated; }
    if (sw == tw && sh == th) {
        for (std::size_t row = 0; row < sh; ++row) {
            std::copy_n(source.samples.data() + row * source.row_stride, source_row,
                        output.samples.data() + row * output.row_stride);
        }
        return Status::ok;
    }
    if (sw == tw) {
        typename BasicImageWorkspace<Scalar>::AxisWork& down = *work.vertical_;
        const Status result = vertical(plan.vertical_, source, output, sw, channels, down.profile, down.work);
        return result;
    }
    if (sh == th) {
        typename BasicImageWorkspace<Scalar>::AxisWork& across = *work.horizontal_;
        const Status result = horizontal(plan.horizontal_, source, output, sh, channels, across.profile, across.work);
        return result;
    }
    typename BasicImageWorkspace<Scalar>::AxisWork& across = *work.horizontal_;
    typename BasicImageWorkspace<Scalar>::AxisWork& down = *work.vertical_;
    const bool vertical_first = th < sh;
    const std::size_t middle_width = vertical_first ? sw : tw;
    const std::size_t middle_stride = middle_width * channels;
    const BasicImageOutput<Scalar> middle_output{work.middle_, middle_stride};
    const BasicImageInput<Scalar> middle_input{work.middle_, middle_stride};
    if (vertical_first) {
        const Status first = vertical(plan.vertical_, source, middle_output, sw, channels, down.profile, down.work);
        if (first != Status::ok) { return first; }
        const Status second = horizontal(plan.horizontal_, middle_input, output, th, channels, across.profile, across.work);
        return second;
    }
    const Status first = horizontal(plan.horizontal_, source, middle_output, sh, channels, across.profile, across.work);
    if (first != Status::ok) { return first; }
    const Status second = vertical(plan.vertical_, middle_input, output, tw, channels, down.profile, down.work);
    return second;
}
} // namespace stx_conv::detail

namespace stx_conv {
template class BasicImagePlan<float>;
template class BasicImageWorkspace<float>;
template Status resize(const BasicImagePlan<float>&, BasicImageInput<float>, BasicImageOutput<float>, BasicImageWorkspace<float>&) noexcept;
template class BasicImagePlan<double>;
template class BasicImageWorkspace<double>;
template Status resize(const BasicImagePlan<double>&, BasicImageInput<double>, BasicImageOutput<double>, BasicImageWorkspace<double>&) noexcept;
}
