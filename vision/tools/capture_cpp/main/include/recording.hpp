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

inline std::int64_t micros(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

struct FrameTiming {
    Clock::time_point pop_start, captured, push_start, released;
    std::uint64_t driver_pts = 0;
    int pop_timeouts = 0;
    std::int64_t retry_us = 0, open_us = 0, writer_wait_us = 0, push_us = 0;
    std::int64_t preview_us = 0, photo_us = 0, release_us = 0;
    unsigned display_count = 0;
    std::int64_t display_us = 0;
};

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

    // Takes ownership of the last CPU reference. The SDK can reuse the VPSS
    // buffer after push; the output worker needs only the timing metadata.
    void append(std::unique_ptr<maix::pipeline::Frame> frame, FrameTiming timing);
    RecordStats stats() const;
    std::filesystem::path finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::filesystem::path save_jpeg(const std::filesystem::path &directory, const cv::Mat &nv21);

}  // namespace capture
