// 配置加载器实现：读 camera.yaml，缺字段留默认值，缺文件抛错
#include "camera/config_loader.hpp"

#include <opencv2/core.hpp>
#include <stdexcept>

namespace camera
{
    CameraConfig load_camera_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("CAMERA_CONFIG_OPEN_FAILED:" + path.string());

        CameraConfig config;
        if (!fs["width"].empty())
            fs["width"] >> config.width;
        if (!fs["height"].empty())
            fs["height"] >> config.height;
        if (!fs["format"].empty())
        {
            fs["format"] >> config.format;
            if (config.format != "BGR888")
                throw std::runtime_error("CAMERA_CONFIG_UNKNOWN_FORMAT:" + config.format);
        }
        if (!fs["fps"].empty())
            fs["fps"] >> config.fps;
        if (!fs["buff_num"].empty())
            fs["buff_num"] >> config.buff_num;
        if (!fs["skip_frames"].empty())
            fs["skip_frames"] >> config.skip_frames;
        return config;
    }
}
