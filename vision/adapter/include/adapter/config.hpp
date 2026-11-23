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
        cv::Matx33d rotation{0, 0, 1, -1, 0, 0, 0, -1, 0};                      // 相机 → 车体旋转 R
        cv::Vec3d translation{0, 0, 0};                     // 相机光心在车体系的位置 t，m
        controller::Vec2 follow_target_position{0.5, 0.0};   // FOLLOW 跟 MARK 的期望目标位置，车体系，m
        controller::Vec2 approach_target_position{0.5, 0.0}; // FOLLOW 跟 TAG 的期望目标位置，车体系，m
        controller::Vec2 backup_target_position{0.5, 0.0};   // BACKUP 的期望目标位置，车体系，m
        double follow_target_heading=0.0;              // FOLLOW 的期望朝向，车体系，rad
        double backup_target_heading=0.0;              // BACKUP 的期望朝向，车体系，rad
        double scan_angular_velocity=0.5;              // SEARCH 的扫描角速度，rad/s
        double align_target_heading=0.0;               // ALIGN 的期望朝向，车体系，rad
    };

} // namespace adapter
