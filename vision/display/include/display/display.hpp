// DisplayInput + Velocity + Display 类声明
// Display 类声明
// Display
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <opencv2/core.hpp>

namespace display
{

    // 线速度 + 角速度。cmd 和 act 共用这个形状。
    // 与控制器 Command（vx、vy、ω）和 0x0015 回传（vx、vy、ω）一致。
    struct Velocity
    {
        cv::Vec2d linear; // vx, vy
        double angular;   // ω
    };

    struct DisplayInput
    {
        // 必填
        cv::Mat frame;     // BGR, CV_8UC3，1440x1080，来自相机库
        uint64_t frame_id; // 帧号
        std::string mode;  // G3 状态名，如 "FOLLOW"
        Velocity command;  // 发给 C 板的速度（vx, vy, ω）
        double loop_hz;    // 主循环频率，app 算好传进来

        // 可选：缺失时对应元素不画
        std::optional<std::array<cv::Point2f, 4>> target_quad; // 当前目标四边形，2D 像素点，帧坐标
        std::optional<cv::Point2f> track_pixel;                // 跟踪点像素，帧坐标
        std::optional<cv::Point2f> velocity_pixel;             // 速度箭头端点像素，帧坐标
        std::optional<std::string> track_state;                // "MEASURED" 或 "COAST"
        std::optional<cv::Vec2d> target_position;              // 车体系目标位置 x, y，米
        std::optional<cv::Vec2d> target_velocity;              // 车体系目标速度 vx, vy，米/秒
        std::optional<Velocity> actual;                        // C 板实际速度（vx, vy, ω），0x0015 回传
    };

    class Display
    {
    public:
        // 以 SDK 默认格式 RGB888 打开默认屏。打开失败抛 std::runtime_error。
        Display();
        // 析构函数，声明在头文件、定义在 .cpp；pimpl 的原因：析构时要 delete Impl，必须在 Impl 完整定义的地方
        ~Display();
        // 禁止拷贝构造和拷贝赋值。Display 独占一个 Impl，不能复制，也顺带禁了移动（不会自动生成）
        Display(const Display &) = delete;
        Display &operator=(const Display &) = delete;

        // 画一帧并推屏。SDK show() 返回非 ERR_NONE 时抛 std::runtime_error。
        void show(const DisplayInput &input);

    private:
        // Impl 独占 SDK 屏幕，SDK 类型仅出现在实现文件里。
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace display
