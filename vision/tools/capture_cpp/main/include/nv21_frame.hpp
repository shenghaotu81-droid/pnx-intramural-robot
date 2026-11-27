#pragma once

#include "maix_pipeline.hpp"
#include <linux/cvi_comm_video.h>
#include <opencv2/core.hpp>

namespace capture {

// Write RGB directly into the display's ROI, which may have a larger parent stride.
void render_nv21_preview(const cv::Mat &nv21, cv::Mat view, cv::Mat &small_rgb);

// Owns one VPSS reference. Only CPU pixel consumers request a cached mapping.
class Nv21Frame {
public:
    explicit Nv21Frame(int channel) : channel_(channel) {}
    ~Nv21Frame();
    Nv21Frame(const Nv21Frame &) = delete;
    Nv21Frame &operator=(const Nv21Frame &) = delete;

    int acquire(int timeout_ms);
    void validate();
    void map_pixels();
    void release();
    maix::pipeline::Frame encoder_frame();

    const VIDEO_FRAME_S &info() const { return native_.stVFrame; }
    cv::Mat sample(cv::Size size) const;

private:
    VIDEO_FRAME_INFO_S native_{};
    int channel_;
    bool acquired_ = false;
    std::uint32_t length_ = 0;
    std::size_t vu_offset_ = 0;
    std::uint8_t *base_ = nullptr;
};

}  // namespace capture
