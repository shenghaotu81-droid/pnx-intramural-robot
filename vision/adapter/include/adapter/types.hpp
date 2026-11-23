// 适配层类型定义
// Mode + Target + AdapterInput + AdapterOutput
#pragma once

#include <optional>

#include <opencv2/core.hpp>

#include "controller/types.hpp"

namespace adapter
{
    // 模式
    enum class Mode
    {
        Follow, // 边走边对
        Search, // 原地转着找 TAG
        Align,  // 站住只转不移
        Backup  // 后退扫 RFID
    };

    // 目标
    enum class Target
    {
        Mark, // MARK
        Tag   // TAG（exit 的 end TAG）
    };

    // 输入
    struct AdapterInput
    {
        Target target;                            // 这个位姿属于哪个目标
        std::optional<cv::Vec3d> position_camera; // 目标位置，相机系，m
        std::optional<cv::Vec3d> velocity_camera; // 目标表观速度，相机系，m/s
        std::optional<cv::Vec3d> rvec;            // 目标 rvec，相机系
        controller::Vec2 car_velocity;            // 车速，车体系，m/s
        double car_angular_velocity;              // 车角速度，车体系，rad/s
        Mode mode;                                // G3 给的模式
    };

    // 输出
    struct AdapterOutput
    {
        controller::CurrentState current; // 当前状态，车体系
        controller::DesiredState desired; // 期望状态，车体系
    };

} // namespace adapter
