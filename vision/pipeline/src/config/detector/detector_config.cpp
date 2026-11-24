#include "vision/config/detector_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>
#include <filesystem>
#include <opencv2/core.hpp>

namespace mark::detector
{
    // 检测配置只提供分割（preprocess）、结构匹配（geometry）和物理角到屏幕角的安装映射（corner）。
    detector::DetectorConfig load_detector_config(const std::filesystem::path &path)
    {
        // OpenCV 专门读写 YAML/XML/JSON 的文件存储类（打开文件、解析、按 key 取节点）；只读打开
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        // 文件打不开（路径错、文件不存在）抛错
        if (!fs.isOpened())
            throw std::runtime_error("CONFIG_OPEN_FAILED:" + path.string());
        // 默认构造（带默认值）
        detector::DetectorConfig d;
        const auto preprocess = fs["preprocess"], geometry = fs["geometry"];
        if (!preprocess["work_width"].empty())
            d.preprocess_.work_width_ = config_value::integer(preprocess["work_width"]);
        if (!preprocess["work_height"].empty())
            d.preprocess_.work_height_ = config_value::integer(preprocess["work_height"]);
        if (!preprocess["threshold"].empty())
            d.preprocess_.threshold_ = config_value::integer(preprocess["threshold"]);
        if (!geometry["white_threshold"].empty())
            d.geometry_.white_threshold_ = config_value::integer(geometry["white_threshold"]);
        if (!geometry["approximation_epsilon"].empty())
            d.geometry_.approximation_epsilon_ = config_value::number(geometry["approximation_epsilon"]);
        const auto mapping = fs["corner"]["physical_to_screen"];
        if (!mapping.empty())
        {
            for (int i = 0; i < 4; ++i)
                d.corner_.physical_to_screen_[i] = config_value::integer(mapping[i]);
        }
        return d;
    }
}
