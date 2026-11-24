#include "adapter/config_loader.hpp"

#include <opencv2/core.hpp>
#include <stdexcept>

namespace adapter
{
    AdapterConfig load_adapter_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("ADAPTER_CONFIG_OPEN_FAILED:" + path.string());

        AdapterConfig config;
        if (const auto rotation = fs["rotation"]; !rotation.empty())
        {
            cv::Mat matrix;
            rotation >> matrix;
            matrix.copyTo(cv::Mat(config.rotation, false));
        }
        if (const auto translation = fs["translation"]; !translation.empty())
        {
            for (int i = 0; i < 3; ++i)
                config.translation[i] = static_cast<double>(translation[i]);
        }
        if (const auto follow = fs["follow_target_position"]; !follow.empty())
            config.follow_target_position = {static_cast<double>(follow[0]), static_cast<double>(follow[1])};
        if (const auto approach = fs["approach_target_position"]; !approach.empty())
            config.approach_target_position = {static_cast<double>(approach[0]), static_cast<double>(approach[1])};
        if (const auto backup = fs["backup_target_position"]; !backup.empty())
            config.backup_target_position = {static_cast<double>(backup[0]), static_cast<double>(backup[1])};
        if (!fs["follow_target_heading"].empty())
            fs["follow_target_heading"] >> config.follow_target_heading;
        if (!fs["backup_target_heading"].empty())
            fs["backup_target_heading"] >> config.backup_target_heading;
        if (!fs["align_target_heading"].empty())
            fs["align_target_heading"] >> config.align_target_heading;
        if (!fs["scan_angular_velocity"].empty())
            fs["scan_angular_velocity"] >> config.scan_angular_velocity;
        return config;
    }
}
