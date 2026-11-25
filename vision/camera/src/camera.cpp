// Camera 实现：MaixCDK 相机转管线帧（BGR Mat + frame_id + timestamp_us）
#include "camera/camera.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <opencv2/core.hpp>

#include "maix_camera.hpp"
#include "maix_err.hpp"
#include "maix_image.hpp"

namespace camera
{
    namespace
    {
        // format 字符串映射到 MaixCDK 格式枚举。加载器只接受 "BGR888"，这里再守一道，
        // 防止绕过加载器直接构造 CameraConfig 时传入不支持的格式。
        maix::image::Format to_maix_format(const std::string &format)
        {
            if (format == "BGR888")
                return maix::image::Format::FMT_BGR888;
            throw std::runtime_error("CAMERA_UNSUPPORTED_FORMAT:" + format);
        }

        // 单调时钟，微秒。read() 返回时打时间戳，相机本身不带硬件时间戳。
        int64_t now_us()
        {
            const auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        }
    } // namespace

    struct Camera::Impl
    {
        CameraConfig config_;
        std::unique_ptr<maix::camera::Camera> cam_;
        std::unique_ptr<maix::image::Image> current_img_;
        uint64_t frame_id_ = 0;
    };

    Camera::Camera(const CameraConfig &config) : impl_(std::make_unique<Impl>())
    {
        impl_->config_ = config;
        const maix::image::Format format = to_maix_format(config.format);
        const char *stage = "camera construct failed: ";
        try
        {
            impl_->cam_ = std::make_unique<maix::camera::Camera>(
                config.width, config.height, format, nullptr, config.fps, config.buff_num, false, false);

            stage = "camera open failed: ";
            const auto err = impl_->cam_->open(config.width, config.height, format, config.fps, config.buff_num);
            if (err != maix::err::ERR_NONE)
                throw std::runtime_error(maix::err::to_str(err));

            stage = "camera skip_frames failed: ";
            impl_->cam_->skip_frames(config.skip_frames);
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(std::string(stage) + e.what());
        }
    }

    Camera::~Camera() = default;

    Frame Camera::read()
    {
        // 一旦开始下一次读取，上一帧的所有 Mat 视图即失效。
        impl_->current_img_.reset();
        try
        {
            std::unique_ptr<maix::image::Image> img(impl_->cam_->read());
            if (!img)
                throw std::runtime_error("null image");
            if (img->format() != maix::image::Format::FMT_BGR888 ||
                img->width() != impl_->config_.width || img->height() != impl_->config_.height)
                throw std::runtime_error("unexpected frame format or size");
            if (!img->data())
                throw std::runtime_error("null image data");

            // SDK 已将 BGR 像素逐行紧密写入 Image，Mat 只借用这块内存。
            Frame frame;
            frame.image = cv::Mat(img->height(), img->width(), CV_8UC3, img->data());
            impl_->current_img_ = std::move(img);
            frame.timestamp_us = now_us();
            frame.frame_id = impl_->frame_id_++;
            return frame;
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(std::string("camera read failed: ") + e.what());
        }
    }
} // namespace camera
