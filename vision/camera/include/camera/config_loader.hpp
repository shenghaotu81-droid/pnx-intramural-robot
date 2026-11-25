#pragma once

#include "camera/config.hpp"
#include <filesystem>

namespace camera
{
    CameraConfig load_camera_config(const std::filesystem::path &path);
}
