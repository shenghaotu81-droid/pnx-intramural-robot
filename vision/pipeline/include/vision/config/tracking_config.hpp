#pragma once
#include "vision/types/tracking_types.hpp"
#include <filesystem>

namespace mark::tracking
{
    // 当前链只输出相机坐标的位置/表观速度。R/Q是模型，不是测量资格阈值。
    // R/Q 分别描述量测与过程噪声。跟踪按 NIS 排序选择量测，最小分数平局时跳过更新。
    // 默认值与 tracking_profile.yaml 当前值一致。调参改 YAML，默认值只作缺字段回退。
    struct TrackingProfile
    {
        // R，测量噪声协方差（3×3，单位 m²）。
        cv::Matx33d camera_mse_m2_{2e-8, 0, 0, 0, 2e-8, 0, 0, 0, 2.5e-7};
        double acceleration_psd_m2ps3_{0.5};          // q，加速度噪声功率谱密度，m²/s³
        double initial_velocity_variance_m2ps2_{0.25}; // 初始 P 的速度块对角项，m²/s²
    };

    // 顶层配置
    // 追踪配置包含量测来源 source 和可选的 profile。
    struct TrackingConfig
    {
        // source_——跟踪源，默认 "MARK"
        std::string source_{"MARK"};
        // R/Q profile
        std::optional<TrackingProfile> profile_;
    };

    // 自由函数，声明在头文件
    TrackingConfig load_tracking_config(const std::filesystem::path &);
}
