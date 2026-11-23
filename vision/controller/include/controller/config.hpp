// ControllerConfig：位置/航向增益 + yaw 门控阈值/EMA alpha

#pragma once

namespace controller
{

    // 增益单位是 1/s（位置误差 m → 速度 m/s；航向误差 rad → 角速度 rad/s）
    struct PositionConfig
    {
        double gain = 3.0; // 位置环 P 增益，3/s
    };

    struct HeadingConfig
    {
        double gain = 3.0; // 航向环 P 增益，3/s
    };

    struct YawFilterConfig
    {
        double gate_threshold = 0.17; // 门控阈值，rad，约 10°
        double alpha = 0.5;           // EMA 平滑系数，0~1
    };

    struct ControllerConfig
    {
        PositionConfig position;    // 位置环参数
        HeadingConfig heading;      // 航向环参数
        YawFilterConfig yaw_filter; // yaw 滤波参数
    };

} // namespace controller