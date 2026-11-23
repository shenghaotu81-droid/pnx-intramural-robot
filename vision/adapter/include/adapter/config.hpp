// 适配层配置
// AdapterConfig
#pragma once

#include <opencv2/core.hpp>

#include "controller/types.hpp"

namespace adapter
{
    // 配置
    struct AdapterConfig
    {
        cv::Matx33d rotation;                      // 相机 → 车体旋转 R
        cv::Vec3d translation;                     // 相机光心在车体系的位置 t，m
        controller::Vec2 follow_target_position;   // FOLLOW 跟 MARK 的期望目标位置，车体系，m
        controller::Vec2 approach_target_position; // FOLLOW 跟 TAG 的期望目标位置，车体系，m
        controller::Vec2 backup_target_position;   // BACKUP 的期望目标位置，车体系，m
        double scan_angular_velocity;              // SEARCH 的扫描角速度，rad/s
        double align_target_heading;               // ALIGN 的期望朝向，车体系，rad
    };

} // namespace adapter
