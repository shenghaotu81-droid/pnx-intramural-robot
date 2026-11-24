#pragma once

#include "vision/detection/detector.hpp"
#include "vision/pose/pose_estimator.hpp"
#include "vision/pose/tag_detector.hpp"
#include "vision/tracking/target_tracker.hpp"
#include "vision/types/pose_types.hpp"
#include "vision/types/tracking_types.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <opencv2/core.hpp>
#include <vector>

namespace mark
{

    // 输入：app 每帧给 pipeline 的东西。
    struct PipelineInput
    {
        // CV_8UC3，BGR 通道序（和 VideoCapture 一致；MaixCam 的 RGB888 要先转 BGR）。
        cv::Mat frame;
        // 帧号。
        uint64_t frame_id{};
        // 接收时间戳，微秒（板上用捕获时间，离线用帧号/FPS 推算）。
        int64_t timestamp_us{};
    };

    // 输出：pipeline 每帧还给 app 的东西。
    struct PipelineOutput
    {
        // G4：tracks_ + events_ + NIS。
        tracking::TrackingResult tracking;
        // G2：位姿观测，source_ 区分 MARK / APRILTAG。
        std::vector<pose::PoseObservation> poses;
    };

    // 按帧调用的管线：拥有 detector + pose estimator + tag detector + tracker，
    // 串联检测、位姿、跟踪三步，替代 offline_runner 里内联的逐帧链。
    // 有状态（tracker 跨帧演进），不可拷贝。
    class Pipeline
    {
      public:
        // 构造时加载 detector.yaml / pose.yaml / tracking.yaml（从配置目录）。
        explicit Pipeline(const std::filesystem::path &config_dir);
        Pipeline(const Pipeline &) = delete;
        Pipeline &operator=(const Pipeline &) = delete;

        // 同一帧流复用一个实例，按序串行调用。
        PipelineOutput process(const PipelineInput &input);

        // 两个引用都借自本实例。配置中的 Mat 及其共享数据只供读取。
        const pose::PoseConfig &pose_config() const { return pose_config_; }

        // process 正常返回后读取本帧结果。下一次调用覆盖缓存，跨帧保留请复制。
        const detector::FrameResult &last_detection() const { return last_detection_; }

      private:
        detector::Detector detector_;
        pose::PoseConfig pose_config_;
        pose::PoseEstimator solver_;
        std::unique_ptr<pose::TagDetector> tag_detector_;
        tracking::TargetTracker tracker_;
        detector::FrameResult last_detection_;
    };

} // namespace mark
