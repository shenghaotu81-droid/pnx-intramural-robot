// Vec2 + CurrentState + DesiredState + Command
#pragma once

#include <optional>

namespace controller
{
    // 二维向量
    struct Vec2
    {
        double x = 0.0;
        double y = 0.0;
    };

    // 当前状态
    struct CurrentState
    {
        Vec2 car_velocity;                    // 车子相对地面的速度，车体系，m/s
        double car_angular_velocity;          // 车子绕 z 轴的角速度 ω，rad/s
        std::optional<Vec2> target_position;  // 目标实测位置，车体系，m
        std::optional<Vec2> target_velocity;  // 目标表观速度，车体系，m/s
        std::optional<double> target_heading; // 目标实测朝向，rad
    };

    // 期望状态
    struct DesiredState
    {
        std::optional<Vec2> target_position;    // 目标期望位置，车体系，m
        std::optional<double> target_heading;   // 目标期望朝向，rad
        std::optional<double> angular_velocity; // 期望角速度，rad/s
    };

    // 输出
    struct Command
    {
        Vec2 velocity;           // 车子期望线速度 (vx, vy)，车体系，m/s
        double angular_velocity; // 车子期望角速度 ω，rad/s
    };

} // namespace controller