// 控制律实现（前馈 + 位置 P + 航向 P）
#include "controller/controller.hpp"

namespace controller
{
    Controller::Controller(const ControllerConfig &config)
        : config_(config), yaw_filter_(config.yaw_filter)
    {
    }

    // 动作：跟 MARK / 退 TAG 用期望位置（位置伺服），对准 MARK/TAG 用期望朝向（航向伺服），转着扫用期望角速度（直接输出）
    // 线速度是控制器算出来的输出（P + 前馈），调用方不在控制器指定
    Command Controller::update(const CurrentState &current, const DesiredState &desired)
    {
        // 1. 先滤 yaw（有 target_heading ）。
        double smoothed_yaw = yaw_filter_.smoothed_yaw();
        if (current.target_heading.has_value())
        {
            smoothed_yaw = yaw_filter_.update(current.target_heading.value());
        }

        // 2. 算前馈（有表观速度：表观 + ω×p + 车速（ω×p = ω·(-p.y, p.x)）；TAG 没表观速度，前馈就是 0）。
        Vec2 feedforward_velocity{0.0, 0.0};
        if (current.target_velocity.has_value() && current.target_position.has_value() && desired.target_position.has_value())
        {
            feedforward_velocity = current.target_velocity.value();
            // ω×p = ω·(-p.y, p.x)
            feedforward_velocity.x += current.car_angular_velocity * (-current.target_position->y);
            feedforward_velocity.y += current.car_angular_velocity * (current.target_position->x);
            feedforward_velocity.x += current.car_velocity.x;
            feedforward_velocity.y += current.car_velocity.y;
        }

        // 3. 位置 P + 前馈（期望位置和实测位置都有）。
        Vec2 position_error{0.0, 0.0};
        if (desired.target_position.has_value() && current.target_position.has_value())
        {
            position_error.x = desired.target_position->x - current.target_position->x;
            position_error.y = desired.target_position->y - current.target_position->y;
        }
        // 位置环 P 增益，1/s
        Vec2 position_p{0.0, 0.0};
        position_p.x = config_.position.gain * -position_error.x;
        position_p.y = config_.position.gain * -position_error.y;
        Vec2 total_command{0.0, 0.0};
        total_command.x = position_p.x + feedforward_velocity.x;
        total_command.y = position_p.y + feedforward_velocity.y;

        // 4. 航向 P（期望朝向和平滑后的 yaw 都有）；search 时期望角速度直接输出。
        double heading_error = 0.0;
        if (desired.target_heading.has_value())
        {
            heading_error = desired.target_heading.value() - smoothed_yaw;
            // 将角度误差限制在 [-π, π) 范围内
            heading_error = wrap_angle(heading_error);
        }
        double heading_p = config_.heading.gain * -heading_error;
        if (desired.angular_velocity.has_value()) // 覆盖
        {
            heading_p = desired.angular_velocity.value();
        }

        // 5. 组 Command 返回。
        return Command{total_command.x, total_command.y, heading_p};
    }

    void Controller::reset()
    {
        yaw_filter_.reset();
    }

} // namespace controller
