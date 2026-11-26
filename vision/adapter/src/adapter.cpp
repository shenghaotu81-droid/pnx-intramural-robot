// Adapter 实现：几何变换 + 模式语义
#include "adapter/adapter.hpp"

#include <cmath>

#include <opencv2/calib3d.hpp>

namespace adapter
{
    Adapter::Adapter(const AdapterConfig& config) : config_(config)
    {
    }

    AdapterOutput Adapter::adapt(const AdapterInput& input) const
    {
        AdapterOutput output;

        // 车速直接透传（app 提供，车体系）
        output.current.car_velocity = input.car_velocity;
        output.current.car_angular_velocity = input.car_angular_velocity;

        // 位置：p_body = R · p_camera + t，取 x、y
        if (input.position_camera.has_value())
        {
            const cv::Vec3d p_body = config_.rotation * input.position_camera.value() + config_.translation;
            output.current.target_position = controller::Vec2{p_body[0], p_body[1]};
        }

        // 速度：v_body = R · v_camera（只转不平移），取 x、y
        if (input.velocity_camera.has_value())
        {
            const cv::Vec3d v_body = config_.rotation * input.velocity_camera.value();
            output.current.target_velocity = controller::Vec2{v_body[0], v_body[1]};
        }

        // 第三列是指向板背的模型 +z 轴。取其在车体水平面的方位角，正对为 0，俯视逆时针为正。
        if (input.rvec.has_value())
        {
            cv::Matx33d r_cam;
            cv::Rodrigues(input.rvec.value(), r_cam);
            const cv::Matx33d r_body = config_.rotation * r_cam;
            output.current.target_heading = std::atan2(r_body(1, 2), r_body(0, 2));
        }

        // DesiredState 按 mode + target 拼，期望朝向都从配置取，不写死
        switch (input.mode)
        {
            case Mode::Follow:
                output.desired.target_position = (input.target == Target::Mark) ? config_.follow_target_position
                                                                                : config_.approach_target_position;
                output.desired.target_heading = config_.follow_target_heading;
                break;
            case Mode::Search:
                output.desired.angular_velocity = config_.scan_angular_velocity;
                break;
            case Mode::Align:
                output.desired.target_heading = config_.align_target_heading;
                break;
            case Mode::Backup:
                output.desired.target_position = config_.backup_target_position;
                output.desired.target_heading = config_.backup_target_heading;
                break;
        }

        return output;
    }

} // namespace adapter
