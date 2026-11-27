#include "nv21_frame.hpp"
#include "capture_config.hpp"

#include <cvi_sys.h>
#include <cvi_vpss.h>
#include <cstdio>
#include <limits>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace capture {
namespace {
constexpr int vpss_group = 0;  // MaixCAM Camera::open uses group 0.
}

int Nv21Frame::acquire(int timeout_ms) {
    const int status = CVI_VPSS_GetChnFrame(vpss_group, channel_, &native_, timeout_ms);
    acquired_ = status == CVI_SUCCESS;
    return status;
}

void Nv21Frame::validate() {
    const auto &f = info();
    if ((f.u32Width != width && f.u32Width != aligned_width) || f.u32Height != height ||
        f.enPixelFormat != PIXEL_FORMAT_NV21 || f.enCompressMode != COMPRESS_MODE_NONE ||
        f.enVideoFormat != VIDEO_FORMAT_LINEAR ||
        f.s16OffsetLeft || f.s16OffsetTop || f.s16OffsetBottom ||
        (f.s16OffsetRight != 0 && f.s16OffsetRight != static_cast<int>(f.u32Width) - width)) {
        throw std::runtime_error("Unexpected VPSS NV21 geometry, format or offsets");
    }
    const auto length = static_cast<std::uint64_t>(f.u32Length[0]) + f.u32Length[1] + f.u32Length[2];
    if (!f.u64PhyAddr[0] || f.u64PhyAddr[1] < f.u64PhyAddr[0] ||
        f.u32Stride[0] < f.u32Width || f.u32Stride[1] < f.u32Width ||
        f.u32Length[0] < static_cast<std::uint64_t>(f.u32Stride[0]) * height ||
        f.u32Length[1] < static_cast<std::uint64_t>(f.u32Stride[1]) * (height / 2) ||
        length > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Invalid VPSS NV21 plane layout");
    }
    const auto offset = f.u64PhyAddr[1] - f.u64PhyAddr[0];
    if (offset < f.u32Length[0] || offset > length - f.u32Length[1]) {
        throw std::runtime_error("VPSS chroma plane is outside the camera buffer");
    }
    length_ = static_cast<std::uint32_t>(length);
    vu_offset_ = offset;
}

void Nv21Frame::map_pixels() {
    if (base_) return;
    if (!acquired_ || !length_) throw std::logic_error("VPSS frame must be validated before mapping");
    // The pinned libsys invalidates this range inside CVI_SYS_MmapCache.
    base_ = static_cast<std::uint8_t *>(CVI_SYS_MmapCache(info().u64PhyAddr[0], length_));
    if (!base_) {
        throw std::runtime_error("Could not map VPSS frame for CPU pixel access");
    }
}

maix::pipeline::Frame Nv21Frame::encoder_frame() {
    // Frame copies the descriptor. This object owns unmap and channel release.
    return maix::pipeline::Frame(&native_, false, "vpsschn,0," + std::to_string(channel_));
}

cv::Mat Nv21Frame::sample(cv::Size size) const {
    if (!base_) throw std::logic_error("No CPU mapping for NV21 samples");
    const auto &f = info();
    cv::Mat y(height, width, CV_8UC1, base_, f.u32Stride[0]);
    cv::Mat vu(height / 2, width / 2, CV_8UC2, base_ + vu_offset_, f.u32Stride[1]);
    cv::Mat compact(size.height * 3 / 2, size.width, CV_8UC1);
    cv::Mat dst_y = compact.rowRange(0, size.height);
    cv::Mat dst_vu(size.height / 2, size.width / 2, CV_8UC2, compact.ptr(size.height), compact.step);
    if (size == y.size()) {
        y.copyTo(dst_y);
        vu.copyTo(dst_vu);
    } else {
        cv::resize(y, dst_y, dst_y.size(), 0, 0, cv::INTER_NEAREST);
        cv::resize(vu, dst_vu, dst_vu.size(), 0, 0, cv::INTER_NEAREST);
    }
    return compact;
}

void Nv21Frame::release() {
    int unmap = CVI_SUCCESS;
    if (base_) {
        unmap = CVI_SYS_Munmap(base_, length_);
        base_ = nullptr;
    }
    int put = CVI_SUCCESS;
    if (acquired_) {
        put = CVI_VPSS_ReleaseChnFrame(vpss_group, channel_, &native_);
        acquired_ = false;
    }
    if (put != CVI_SUCCESS || unmap != CVI_SUCCESS) {
        throw std::runtime_error("VPSS frame cleanup failed: unmap=" + std::to_string(unmap)
                                 + ", release=" + std::to_string(put));
    }
}

Nv21Frame::~Nv21Frame() {
    try {
        release();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
    }
}

}  // namespace capture
