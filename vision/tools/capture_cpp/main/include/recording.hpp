#pragma once

#include "maix_pipeline.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <opencv2/core.hpp>

namespace capture {

using Clock = std::chrono::steady_clock;

struct RecordStats {
    std::size_t frames = 0;
    std::int64_t elapsed_us = 0;

    double actual_fps() const {
        return frames > 1 && elapsed_us > 0 ? (frames - 1) * 1'000'000.0 / elapsed_us : 0.0;
    }
};

class Recording {
public:
    Recording(const std::filesystem::path &directory, int native_width);
    ~Recording();

    void append(maix::pipeline::Frame &frame, Clock::time_point captured, std::uint64_t driver_pts);
    const RecordStats &stats() const;
    std::filesystem::path finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::filesystem::path save_jpeg(const std::filesystem::path &directory, const cv::Mat &nv21);

}  // namespace capture
