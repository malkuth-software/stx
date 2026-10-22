// Included inside comparison.cpp's private namespace. Both sides reuse plans,
// buffers and workspaces; both execute one worker, without pixel conversions.
void reference_axis(const paint::resample_plan& plan, paint::LineWorkspace& work,
                    const std::vector<double>& source, std::vector<double>& output,
                    std::size_t width, std::size_t height, bool vertical_axis) {
    const std::size_t length = vertical_axis ? height : width;
    const std::size_t lines = vertical_axis ? width : height;
    const std::size_t target = plan.target_length;
    const std::size_t source_stride = vertical_axis ? width * 4 : 4;
    const std::size_t output_stride = vertical_axis ? width * 4 : 4;
    for (std::size_t line = 0; line < lines; ++line) {
        const std::size_t source_offset = vertical_axis ? line * 4 : line * width * 4;
        const std::size_t output_offset = vertical_axis ? line * 4 : line * target * 4;
        check(work.resample_line(source.data(), static_cast<unsigned>(length), static_cast<unsigned>(source_offset),
                                static_cast<unsigned>(source_stride), plan, output.data(),
                                static_cast<unsigned>(output_offset), static_cast<unsigned>(output_stride)) != 0,
              "serial reference axis");
    }
}
void image_timings() {
    constexpr std::size_t width = 512;
    constexpr std::size_t height = 384;
    std::mt19937 engine(101010);
    std::uniform_real_distribution<double> random(0.0, 1.0);
    for (int pattern = 0; pattern < 3; ++pattern) {
        for (bool enlarge : {false, true}) {
            const std::size_t tw = enlarge ? 769 : 237;
            const std::size_t th = enlarge ? 577 : 181;
            const stx_conv::ImagePlan plan(width, height, tw, th);
            stx_conv::ImageWorkspace work(plan, 4);
            paint::resample_plan horizontal_plan;
            paint::resample_plan vertical_plan;
            paint::LineWorkspace horizontal_work(static_cast<unsigned>(width));
            paint::LineWorkspace vertical_work(static_cast<unsigned>(height));
            paint::LineWorkspace::build_resample_plan(horizontal_plan, static_cast<unsigned>(width), static_cast<unsigned>(tw));
            paint::LineWorkspace::build_resample_plan(vertical_plan, static_cast<unsigned>(height), static_cast<unsigned>(th));
            std::vector<double> source(width * height * 4, 0.0);
            for (std::size_t i = 0; i < source.size(); ++i) {
                const std::size_t x = (i / 4) % width;
                const std::size_t y = (i / 4) / width;
                switch (pattern) {
                case 0: source[i] = random(engine); break;
                case 1: source[i] = (static_cast<double>(x) + static_cast<double>(y)) / static_cast<double>(width + height); break;
                default: source[i] = static_cast<double>((x + y) % 2); break;
                }
            }
            std::vector<double> actual(tw * th * 4, 0.0);
            std::vector<double> expected(actual.size(), 0.0);
            const std::size_t middle_count = enlarge ? tw * height * 4 : width * th * 4;
            std::vector<double> middle(middle_count, 0.0);
            std::vector<double> new_times;
            std::vector<double> old_times;
            for (int repeat = 0; repeat < 12; ++repeat) {
                for (int half = 0; half < 2; ++half) {
                    const bool candidate = ((repeat + half) % 2) == 0;
                    const Clock::time_point begin = Clock::now();
                    if (candidate) {
                        check(stx_conv::resize(plan, {source, width * 4}, {actual, tw * 4}, work) == stx_conv::Status::ok,
                              "image benchmark candidate");
                    } else if (enlarge) {
                        reference_axis(horizontal_plan, horizontal_work, source, middle, width, height, false);
                        reference_axis(vertical_plan, vertical_work, middle, expected, tw, height, true);
                    } else {
                        reference_axis(vertical_plan, vertical_work, source, middle, width, height, true);
                        reference_axis(horizontal_plan, horizontal_work, middle, expected, width, th, false);
                    }
                    const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
                    if (repeat != 0) {
                        if (candidate) { new_times.push_back(elapsed); }
                        else { old_times.push_back(elapsed); }
                    }
                }
                compare(actual, expected);
            }
            std::sort(new_times.begin(), new_times.end());
            std::sort(old_times.begin(), old_times.end());
            std::printf("image pattern=%d input=512x384x4 target=%zux%zu repeats=11 new_ms=%.6f old_ms=%.6f time_ratio=%.4f workspace_bytes=%zu plan_bytes=%zu\n",
                        pattern, tw, th, new_times[5], old_times[5], new_times[5] / old_times[5], work.storage_bytes(), plan.storage_bytes());
        }
    }
}
