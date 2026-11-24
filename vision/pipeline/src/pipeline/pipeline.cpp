#include "vision/pipeline.hpp"
#include <algorithm>
#include <iostream>

namespace mark
{
    namespace
    {
        tracking::TrackingConfig load_tracker_config(const std::filesystem::path &path)
        {
            auto config = tracking::load_tracking_config(path);
            if (!config.profile_)
                std::cout << "Tracking profile未配置：跳过跟踪数值计算\n";
            return config;
        }
    }

    Pipeline::Pipeline(const std::filesystem::path &config_dir)
        : detector_(detector::load_detector_config(config_dir / "detector.yaml")),
          pose_config_(pose::load_pose_config(config_dir / "pose.yaml")),
          solver_(pose_config_),
          tag_detector_(pose_config_.tag_.allowed_ids_.empty()
                            ? nullptr
                            : std::make_unique<pose::TagDetector>(pose_config_.tag_)),
          tracker_(load_tracker_config(config_dir / "tracking.yaml"))
    {
    }

    PipelineOutput Pipeline::process(const PipelineInput &input)
    {
        last_detection_ = detector_.process({input.frame, input.frame_id, input.timestamp_us});
        const pose::CaptureStamp stamp{input.frame_id, input.timestamp_us};
        const auto size = input.frame.size();

        PipelineOutput output;
        // 即使没有 MARK，也保留一条空观测并继续推进跟踪。
        const auto count = std::max(size_t{1}, last_detection_.detections_.size());
        for (size_t i = 0; i < count; ++i)
            output.poses.push_back(solver_.estimate_mark(last_detection_, i, size, stamp));

        if (tag_detector_)
            for (const auto &tag : tag_detector_->detect(input.frame, stamp))
                output.poses.push_back(pose::estimate_exit(tag, size, solver_));

        output.tracking = tracker_.step({stamp, output.poses});
        return output;
    }
}
