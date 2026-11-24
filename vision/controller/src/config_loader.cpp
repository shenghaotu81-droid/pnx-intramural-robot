#include "controller/config_loader.hpp"

#include <opencv2/core.hpp>
#include <stdexcept>

namespace controller
{
    ControllerConfig load_controller_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("CONTROLLER_CONFIG_OPEN_FAILED:" + path.string());

        ControllerConfig config;
        const auto position = fs["position"], heading = fs["heading"], yaw = fs["yaw_filter"];
        if (!position["gain"].empty())
            position["gain"] >> config.position.gain;
        if (!heading["gain"].empty())
            heading["gain"] >> config.heading.gain;
        if (!yaw["gate_threshold"].empty())
            yaw["gate_threshold"] >> config.yaw_filter.gate_threshold;
        if (!yaw["alpha"].empty())
            yaw["alpha"] >> config.yaw_filter.alpha;
        return config;
    }
}
