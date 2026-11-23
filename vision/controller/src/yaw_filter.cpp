// YawFilter 实现
#include "controller/yaw_filter.hpp"

#include <cmath>

namespace controller
{
    // 构造存 config
    YawFilter::YawFilter(const YawFilterConfig &config) : config_(config)
    {
    }

    // update（首帧采纳，之后门控 + EMA）
    // yaw 的正负约定：逆时针为正，从 +x 轴起算（车体系 +x 前、+y 左、+z 上，右手系），正 yaw 就是朝左偏
    double YawFilter::update(double raw_yaw)
    {
        if (!initialized_)
        {
            smoothed_yaw_ = raw_yaw;
            initialized_ = true;
            return smoothed_yaw_;
        }

        double delta = wrap_angle(raw_yaw - smoothed_yaw_);
        if (std::abs(delta) > config_.gate_threshold)
        {
            // 超过门控阈值，拒收，返回上一个平滑值
            return smoothed_yaw_;
        }

        // EMA 更新
        smoothed_yaw_ += config_.alpha * delta;
        smoothed_yaw_ = wrap_angle(smoothed_yaw_);
        return smoothed_yaw_;
    }

    // reset（清 initialized_）
    void YawFilter::reset()
    {
        initialized_ = false;
    }

    // wrap_angle 包到 [-π, π] 规范范围
    double YawFilter::wrap_angle(double angle)
    {
        while (angle > M_PI)
            angle -= 2 * M_PI;
        while (angle < -M_PI)
            angle += 2 * M_PI;
        return angle;
    }

} // namespace controller