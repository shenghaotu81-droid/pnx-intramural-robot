#include "camera/config_loader.hpp"

#include <opencv2/core.hpp>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    // 显式列出约定的默认值，以便检测结构体默认值的变化。
    const camera::CameraConfig defaults{1440, 1080, "BGR888", 30, 1, 10};

    template <typename T>
    void check_field(const char *file, const char *field, const T &actual, const T &expected)
    {
        if (actual != expected)
        {
            std::ostringstream message;
            message << file << " 的 " << field << " 期望为 " << expected << "，实际为 " << actual;
            throw std::runtime_error(message.str());
        }
    }

    void check_config(const char *file, const camera::CameraConfig &expected)
    {
        const auto actual = camera::load_camera_config(file);
        check_field(file, "width", actual.width, expected.width);
        check_field(file, "height", actual.height, expected.height);
        check_field(file, "format", actual.format, expected.format);
        check_field(file, "fps", actual.fps, expected.fps);
        check_field(file, "buff_num", actual.buff_num, expected.buff_num);
        check_field(file, "skip_frames", actual.skip_frames, expected.skip_frames);
    }

    void check_config_error(const char *file, const std::string &expected)
    {
        try
        {
            camera::load_camera_config(file);
        }
        catch (const std::runtime_error &error)
        {
            check_field(file, "异常信息", std::string(error.what()), expected);
            return;
        }
        throw std::runtime_error(std::string(file) + " 应抛出 std::runtime_error，实际加载成功");
    }

    void check_invalid_yaml(const char *file)
    {
        try
        {
            camera::load_camera_config(file);
        }
        catch (const cv::Exception &)
        {
            // OpenCV 解析失败，具体诊断文本随版本变化。
            return;
        }
        throw std::runtime_error(std::string(file) + " 应抛出解析异常，实际加载成功");
    }

    void test_full_config()
    {
        check_config("config/full.yaml", {1280, 720, "BGR888", 25, 3, 5});
    }

    void test_partial_config()
    {
        auto expected = defaults;
        expected.width = 960;
        expected.fps = 12.5;
        check_config("config/partial.yaml", expected);

        expected = defaults;
        expected.skip_frames = 0;
        check_config("config/zero_skip_frames.yaml", expected);
    }

    void test_missing_file()
    {
        check_config_error("config/missing.yaml", "CAMERA_CONFIG_OPEN_FAILED:config/missing.yaml");
    }

    void test_unknown_format()
    {
        check_config_error("config/unknown_format.yaml", "CAMERA_CONFIG_UNKNOWN_FORMAT:FOO");
        check_config_error("config/lowercase_format.yaml", "CAMERA_CONFIG_UNKNOWN_FORMAT:bgr888");
    }

    void test_no_fields()
    {
        check_config("config/no_fields.yaml", defaults);
    }

    void test_invalid_yaml()
    {
        check_invalid_yaml("config/empty.yaml");
        check_invalid_yaml("config/comment_only.yaml");
    }
}

int main()
{
    struct TestGroup
    {
        const char *name;
        void (*run)();
    };
    const TestGroup groups[] =
    {
        {"完整配置", test_full_config},
        {"缺字段留默认及显式零值", test_partial_config},
        {"缺文件抛错", test_missing_file},
        {"未知格式抛错", test_unknown_format},
        {"合法 YAML 无配置字段", test_no_fields},
        {"空文件和纯注释文件", test_invalid_yaml},
    };

    int failures = 0;
    for (const auto &group : groups)
    {
        try
        {
            group.run();
            std::cout << "[通过] " << group.name << '\n';
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << "[失败] " << group.name << "：" << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
