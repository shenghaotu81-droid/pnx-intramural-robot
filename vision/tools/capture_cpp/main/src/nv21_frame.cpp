#include "nv21_frame.hpp"
#include "capture_config.hpp"

#include <cstddef>
#include <linux/cvi_comm_video.h>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace capture {

Nv21Frame::Nv21Frame(maix::pipeline::Frame &frame) {
    const auto &native = static_cast<const VIDEO_FRAME_INFO_S *>(frame.frame())->stVFrame;
    encoder_width_ = frame.width();
    driver_pts_ = native.u64PTS;
    if ((encoder_width_ != width && encoder_width_ != aligned_width) ||
        frame.height() != height || frame.format() != maix::image::FMT_YVU420SP ||
        native.enCompressMode != COMPRESS_MODE_NONE) {
        throw std::runtime_error("Unexpected VPSS geometry, format or compression");
    }
    if (native.s16OffsetLeft || native.s16OffsetTop || native.s16OffsetBottom ||
        (native.s16OffsetRight != 0 && native.s16OffsetRight != encoder_width_ - width)) {
        throw std::runtime_error("Unexpected VPSS frame offsets");
    }

    const auto y_stride = native.u32Stride[0];
    const auto vu_stride = native.u32Stride[1];
    const auto y_length = native.u32Length[0];
    const auto vu_length = native.u32Length[1];
    const auto y_phys = native.u64PhyAddr[0];
    const auto vu_phys = native.u64PhyAddr[1];
    auto *base = native.pu8VirAddr[0];
    if (!base || !y_phys || vu_phys < y_phys ||
        y_stride < static_cast<unsigned>(encoder_width_) ||
        vu_stride < static_cast<unsigned>(encoder_width_) ||
        y_length < static_cast<std::uint64_t>(y_stride) * height ||
        vu_length < static_cast<std::uint64_t>(vu_stride) * (height / 2)) {
        throw std::runtime_error("Invalid VPSS NV21 plane layout");
    }

    // Camera::pop maps both planes as one interval starting at plane 0.
    const auto vu_offset = vu_phys - y_phys;
    const auto mapped_length = static_cast<std::uint64_t>(y_length) + vu_length + native.u32Length[2];
    if (vu_offset < y_length || vu_offset > mapped_length - vu_length) {
        throw std::runtime_error("VPSS chroma plane is outside the camera mapping");
    }
    y_ = cv::Mat(height, width, CV_8UC1, base, y_stride);
    vu_ = cv::Mat(height / 2, width / 2, CV_8UC2, base + vu_offset, vu_stride);
}

cv::Mat Nv21Frame::preview_rgb(cv::Size size) const {
    cv::Mat y, vu, rgb;
    cv::resize(y_, y, size, 0, 0, cv::INTER_NEAREST);
    cv::resize(vu_, vu, {size.width / 2, size.height / 2}, 0, 0, cv::INTER_NEAREST);
    cv::cvtColorTwoPlane(y, vu, rgb, cv::COLOR_YUV2RGB_NV21);
    return rgb;
}

cv::Mat Nv21Frame::copy_pixels() const {
    cv::Mat compact(height * 3 / 2, width, CV_8UC1);
    y_.copyTo(compact.rowRange(0, height));
    cv::Mat dst(height / 2, width / 2, CV_8UC2, compact.ptr(height), compact.step);
    vu_.copyTo(dst);
    return compact;
}

}  // namespace capture
