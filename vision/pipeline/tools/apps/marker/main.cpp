// marker 离线入口：解析输入、加载输出选项，再将视频交给 run_offline。
#include "vision/config/cli_options.hpp"
#include "vision/config/app_config.hpp"
#include "offline/offline_runner.hpp"
#include <iostream>
#include <filesystem>
#include <exception>

int main(int argc, char **argv)
{
    try
    {
        const auto args = mark::parse_options(argc, argv);  // “给出来的参数格式对不对”（格式）
        // 检查“map 里没有 --video”（必填项）
        if (!args.count("--video"))
            throw std::runtime_error("用法: marker_app --video VIDEO");  // “必填的参数到底给没给”（有无）
        // 拼出配置目录（当前VISION_SOURCE_ROOT 是编译时由 CMake 注入的源码根目录宏:顶层set(VISION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")，该层target_compile_definitions(marker_app PRIVATE VISION_SOURCE_ROOT="${VISION_ROOT}")）
        const auto directory = std::filesystem::path(VISION_SOURCE_ROOT) / "config";
        // Pipeline 构造时从同一目录加载检测、位姿和跟踪配置。
        mark::AppConfig app{mark::load_offline_config(directory / "offline.yaml")};
        // 跑完整管线；return 把管线的退出码（0 成功）返回给操作系统
        return mark::run_offline(app, args.at("--video"), directory);
    }
    catch (const std::exception &e)
    {
        std::cerr << "marker_app: " << e.what() << '\n';
        return 1;
    }
}
