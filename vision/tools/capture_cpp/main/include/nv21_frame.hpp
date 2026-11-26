#pragma once

#include "maix_pipeline.hpp"
#include <cstdint>
#include <opencv2/core.hpp>

namespace capture {

// The caller keeps the MaixCDK frame alive through encode and all pixel access.
class Nv21Frame {
public:
    explicit Nv21Frame(maix::pipeline::Frame &frame);

    int encoder_width() const { return encoder_width_; }
    std::uint64_t driver_pts() const { return driver_pts_; }
    cv::Mat preview_rgb(cv::Size size) const;
    cv::Mat copy_pixels() const;

private:
    cv::Mat y_;
    cv::Mat vu_;
    int encoder_width_;
    std::uint64_t driver_pts_;
};

}  // namespace capture
