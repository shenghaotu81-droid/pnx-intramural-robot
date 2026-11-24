// 逐帧写观测和轨迹，运行结束后写汇总。叠加视频可选。
#include "offline/offline_runner.hpp"
#include "vision/pose/pose_io.hpp"
#include "vision/pipeline.hpp"
#include "rendering/debug_renderer.hpp"
#include "reporting/run_console.hpp"
#include "export/video_exporter.hpp"
#include "io/tracking_io.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <opencv2/videoio.hpp>

namespace mark
{
    int run_offline(const AppConfig &app, const std::string &video,
                    const std::filesystem::path &config_dir)
    {
        // 用单调时钟统计整次运行的处理时间。
        using Clock = std::chrono::steady_clock;

        cv::VideoCapture capture(video);
        if (!capture.isOpened())
            throw std::runtime_error("INPUT_OPEN_FAILED: " + video);
        const double fps = capture.get(cv::CAP_PROP_FPS);
        if (!std::isfinite(fps) || fps <= 0)
            throw std::runtime_error("INPUT_FPS_INVALID");

        Pipeline pipeline(config_dir);

        const std::filesystem::path directory(app.offline_.run_directory_);
        std::filesystem::create_directories(directory);

        VideoExporter exporter;
        RunSummary summary;

        cv::FileStorage observations((directory / "observations.yaml").string(),
                                     cv::FileStorage::WRITE);
        observations << "schema_version" << 6 << "stream_type" << "PoseObservations"
                     << "position_unit" << "m" << "time_unit" << "us" << "frames" << "[";

        cv::FileStorage tracks_file((directory / "tracks.yaml").string(), cv::FileStorage::WRITE);
        tracks_file << "schema_version" << 2 << "stream_type" << "TrackingResult" << "frames"
                    << "[";

        const auto start = Clock::now();

        cv::Mat image_;

        for (uint64_t id = 0; capture.read(image_); ++id)
        {
            // 用帧号和 FPS 推算离线名义时间，四舍五入到整数微秒。
            const long double timestamp = static_cast<long double>(id) * 1e6L / fps;

            const PipelineInput input{image_, id, std::llround(timestamp)};
            const auto output = pipeline.process(input);
            const auto &raw = pipeline.last_detection();
            const auto &result = output.tracking;

            summary.add(raw);
            for (const auto &p : output.poses)
                ++summary.poses_[pose::pose_status_name(p)];
            for (const auto &t : result.tracks_)
                ++summary.tracks_[tracking::track_state_name(t.state_)];
            for (const auto &event : result.events_)
                ++summary.reasons_[event];

            tracking::write_tracking_result(tracks_file, result);

            observations << "{" << "stamp";
            pose::write_capture_stamp(observations, {input.frame_id, input.timestamp_us});
            observations << "observations" << "[";
            for (const auto &p : output.poses)
                pose::write_pose_observation(observations, p);
            observations << "]" << "}";

            if (app.offline_.overlay_)
            {
                if (id == 0)
                    exporter.open(directory / app.offline_.video_filename_, image_.size(), fps);

                exporter.write(render_debug_frame(image_, raw, output.poses, result.tracks_,
                                                  &pipeline.pose_config()));
            }
        }

        // 有叠加帧时关闭编码器，将临时文件改名成配置指定的目标。
        if (app.offline_.overlay_ && summary.frames_)
            exporter.finish();

        observations << "]";
        tracks_file << "]";

        summary.elapsed_s_ = std::chrono::duration<double>(Clock::now() - start).count();

        write_run_summary(summary, (directory / "summary.yaml").string());

        write_run_console(std::cout, summary);

        return 0;
    }
}
