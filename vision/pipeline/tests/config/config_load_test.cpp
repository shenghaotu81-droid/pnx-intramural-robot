#include "vision/config/detector_config.hpp"
#include "vision/config/app_config.hpp"
#include "vision/config/pose_config.hpp"
#include "vision/config/tracking_config.hpp"
#include "vision/config/cli_options.hpp"
#include "assertions.hpp"
#include <iostream>

namespace
{
    const std::array<cv::Point3d, 4> mark_square{{{0, 0, 0}, {.08, 0, 0}, {.08, .08, 0}, {0, .08, 0}}};

    void check_partial_configs(const std::filesystem::path &directory)
    {
        using test_support::check;
        const auto detector = mark::detector::load_detector_config(directory / "detector.yaml");
        check(detector.preprocess_.work_width_ == 960 && detector.preprocess_.work_height_ == 720,
              "missing work size lost its defaults");
        check(detector.preprocess_.threshold_ == 0, "explicit zero threshold ignored");
        check(detector.geometry_.white_threshold_ == 200 && detector.geometry_.approximation_epsilon_ == 2.0,
              "missing geometry lost its defaults");
        check(detector.corner_.physical_to_screen_ == std::array<int, 4>{{0, 1, 2, 3}}, "missing corner mapping");

        const auto pose = mark::pose::load_pose_config(directory / "pose.yaml");
        check(pose.mark_points_m_ == mark_square, "missing model_points lost the 80 mm square");
        check(pose.camera_.K_.empty() && pose.camera_.dist_.empty() && pose.camera_.image_size_ == cv::Size(),
              "missing calibration changed the camera model");
        check(pose.tag_.allowed_ids_ == std::set<int>{7}, "tag configuration ignored");
        const auto calibrated = mark::pose::load_pose_config(directory / "pose_with_calibration.yaml");
        check(calibrated.camera_.image_size_ == cv::Size(640, 0), "image dimensions were not read independently");
        const std::array<cv::Point3d, 4> custom_points{{{0, 0, 0}, {.04, 0, 0}, {.04, .06, 0}, {0, .06, 0}}};
        check(calibrated.mark_points_m_ == custom_points, "custom model_points lost their millimetre scale");

        const auto tracking = mark::tracking::load_tracking_config(directory / "tracking.yaml");
        check(tracking.source_ == "MARK", "missing tracking source lost its default");
        check(tracking.profile_.has_value(), "partial tracking profile missing");
        const cv::Matx33d covariance{2e-8, 0, 0, 0, 2e-8, 0, 0, 0, 2.5e-7};
        check(tracking.profile_->camera_mse_m2_ == covariance, "missing measurement noise lost its default");
        check(tracking.profile_->acceleration_psd_m2ps3_ == 0.5, "missing q lost its default");
        check(tracking.profile_->initial_velocity_variance_m2ps2_ == 1.25, "initial velocity variance ignored");
        const auto custom = mark::tracking::load_tracking_config(directory / "tracking_with_covariance.yaml");
        check(custom.profile_.has_value(), "custom tracking profile missing");
        const cv::Matx33d custom_covariance{2, 1, 0, 1, 3, 0, 0, 0, 4};
        check(custom.profile_->camera_mse_m2_ == custom_covariance, "custom measurement noise ignored");
        check(custom.profile_->acceleration_psd_m2ps3_ == 0.0, "explicit zero q ignored");
        check(custom.profile_->initial_velocity_variance_m2ps2_ == 0.25, "missing velocity variance lost its default");
        const auto no_profile = mark::tracking::load_tracking_config(directory / "tracking_no_profile.yaml");
        check(no_profile.source_ == "APRILTAG" && !no_profile.profile_, "missing profile enabled tracking");
    }
}

int main()
{
    try
    {
        using test_support::check;
        const auto directory = std::filesystem::path(VISION_SOURCE_ROOT) / "config";
        const auto detector = mark::detector::load_detector_config(directory / "detector.yaml");
        check(detector.corner_.physical_to_screen_ == std::array<int, 4>{{0,1,2,3}}, "fixed mapping");
        const auto pose = mark::pose::load_pose_config(directory / "pose.yaml");
        check(pose.mark_points_m_ == mark_square, "metric model changed");
        const auto tracking = mark::tracking::load_tracking_config(directory / "tracking.yaml");
        check(tracking.profile_.has_value() && tracking.profile_->camera_mse_m2_(2,2) == 2.5e-7,
              "profile path not loaded");
        const auto offline = mark::load_offline_config(directory / "offline.yaml");
        check(offline.overlay_ && offline.video_filename_ == "overlay.avi", "offline options lost");
        check_partial_configs(std::filesystem::path(VISION_SOURCE_ROOT) / "tests/config/partial");
        // 终端误输仍须给出明确错误，不把选项名当作视频路径。
        for (const auto &tokens : std::vector<std::vector<std::string>>{
                {"app", "--camera", "0"}, {"app", "--config", "config.yaml"},
                {"app", "--video"}, {"app", "--video", "--video"},
                {"app", "--video", "a.avi", "--video", "b.avi"}})
        {
            auto copy = tokens;
            std::vector<char *> argv;
            for (auto &token : copy) argv.push_back(token.data());
            bool failed = false;
            try { mark::parse_options(static_cast<int>(argv.size()), argv.data()); }
            catch (const std::runtime_error &) { failed = true; }
            check(failed, "bad terminal input accepted");
        }
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
