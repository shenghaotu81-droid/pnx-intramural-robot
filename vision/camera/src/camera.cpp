// Camera 实现：MaixCDK 相机转管线帧（BGR Mat + frame_id + timestamp_us）
#include "camera/camera.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

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
        // 推迟构造（按配置而非默认值）
        std::unique_ptr<maix::camera::Camera> cam_;
        cv::Mat buffer_;
        uint64_t frame_id_ = 0;
    };

    Camera::Camera(const CameraConfig &config) : impl_(std::make_unique<Impl>())
    {
        impl_->config_ = config;
        const maix::image::Format format = to_maix_format(config.format);
        // 构造时 open=false，不自动打开；下面显式 open 并检查返回值。
        // MaixCDK 的 Camera 构造器 open=true 时会自动 open，失败时抛 err::Exception；
        // 我们要自己检查 open() 的返回值，转成 std::runtime_error。
        impl_->cam_ =
            std::make_unique<maix::camera::Camera>(config.width, config.height, format, nullptr,
                                                   config.fps, config.buff_num, false, false);
        const maix::err::Err err =
            impl_->cam_->open(config.width, config.height, format, config.fps, config.buff_num);
        if (err != maix::err::ERR_NONE)
            throw std::runtime_error("camera open failed: " + maix::err::to_str(err));
        // 丢启动帧，等自动曝光收敛。只在启动时丢一次。
        impl_->cam_->skip_frames(config.skip_frames);
        // 零拷贝读帧 buffer：BGR888，每像素 3 字节。read() 直接读进这块 buffer，
        // Frame 里的 cv::Mat 共享它，不拷贝像素。(先分配内存，一直复用)
        impl_->buffer_ = cv::Mat(config.height, config.width, CV_8UC3);
    }

    Camera::~Camera() = default;

    Frame Camera::read()
    {
        // 阻塞读一帧，直接读进 buffer_（零拷贝）。MaixCDK 的 read(buff) 返回一个 Image*，
        // 它的 data() 就是我们传进去的 buffer_（copy=false，不另分配），所以 Frame 的 cv::Mat
        // 可以直接共享 buffer_，不拷贝像素。delete Image* 只删包装对象，不动 buffer_，
        // buffer_ 归本库所有，下帧复用。
        maix::image::Image *img = nullptr;
        try
        {
            img = impl_->cam_->read(impl_->buffer_.data,
                                    impl_->buffer_.total() * impl_->buffer_.elemSize(), true, -1);
        }
        catch (const maix::err::Exception &e)
        {
            // 阻塞模式下 MaixCDK 读失败抛 err::Exception（不是返回 nullptr），转成 std::runtime_error。
            throw std::runtime_error(std::string("camera read failed: ") + e.what());
        }
        if (img == nullptr)
            throw std::runtime_error("camera read failed: null image");
        // 核对返回的帧确实是请求的 BGR888 和尺寸。配错了（比如驱动不支持该分辨率）
        // 早点报出来，别让错帧悄悄流进管线。
        if (img->format() != maix::image::Format::FMT_BGR888 ||
            img->width() != impl_->config_.width || img->height() != impl_->config_.height)
        {
            delete img;
            throw std::runtime_error("camera read failed: unexpected frame format or size");
        }
        Frame frame;
        frame.image = impl_->buffer_; // 共享 buffer_，不拷贝像素
        frame.frame_id = impl_->frame_id_++;
        frame.timestamp_us = now_us();
        // img 由 MaixCDK 在 read() 内 new 出，调用方负责 delete；只删包装，不动 buffer_（copy=false，析构不释像素）。
        delete img; // 只删 Image 包装，buffer_ 归本库所有，下帧复用
        return frame;
    }
} // namespace camera
