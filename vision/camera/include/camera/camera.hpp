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
    cv::Mat image;        // BGR, CV_8UC3, 1440x1080
    uint64_t frame_id;    // 从 0 开始，每帧 +1
    int64_t timestamp_us; // 单调时钟，微秒
};

class Camera
{
  public:
    explicit Camera(const CameraConfig &config);
    // 析构函数，声明在头文件、定义在 .cpp；pimpl 的原因：析构时要 delete Impl，必须在 Impl 完整定义的地方
    ~Camera();
    // 禁止拷贝构造和拷贝赋值。Camera 独占一个 Impl，不能复制，也顺带禁了移动（不会自动生成）（“一处拥有、不可复制”的对象）
    Camera(const Camera &) = delete;
    Camera &operator=(const Camera &) = delete;

    // 阻塞读一帧；读不到抛 std::runtime_error
    Frame read();

  private:
    // struct Impl; + std::unique_ptr<Impl> impl_;——同 TagDetector 的 pimpl 手法
    // Impl 内部：config_（CameraConfig）、cam_（MaixCDK 相机）、buffer_（零拷贝读帧 buffer）、frame_id_（帧号计数器）
    // Impl 里用的是 MaixCDK 的类型（maix::camera::Camera），避免 MaixCDK 依赖泄漏到公共头文件
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace camera
