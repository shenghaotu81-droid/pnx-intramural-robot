// Camera 类声明
// Camera
#pragma once

#include <cstdint>
#include <memory>

#include <opencv2/core.hpp>

#include "camera/config.hpp"

namespace camera
{

    struct Frame
    {
        // 借用 Camera 当前帧的像素，尺寸同配置。Mat 浅拷贝共享这段借用期。
        // 下一次 read() 开始（包括读取失败）或 Camera 析构时失效；跨此边界请提前 clone()。
        cv::Mat image;        // BGR, CV_8UC3
        uint64_t frame_id;    // 从 0 开始，每帧 +1
        int64_t timestamp_us; // 单调时钟，微秒
    };

    class Camera
    {
      public:
        // SDK 相机构造、打开或预热失败抛 std::runtime_error，信息包含阶段和原因。
        explicit Camera(const CameraConfig &config);
        // 析构函数，声明在头文件、定义在 .cpp；pimpl 的原因：析构时要 delete Impl，必须在 Impl 完整定义的地方
        ~Camera();
        // 禁止拷贝构造和拷贝赋值。Camera 独占一个 Impl，不能复制，也顺带禁了移动（不会自动生成）（“一处拥有、不可复制”的对象）
        Camera(const Camera &) = delete;
        Camera &operator=(const Camera &) = delete;

        // 每次调用只尝试一次阻塞读取。失败抛 std::runtime_error，由调用方决定是否再读或重建。
        Frame read();

      private:
        // Impl 独占 SDK 相机和当前 Image，SDK 类型仅出现在实现文件里。
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace camera
