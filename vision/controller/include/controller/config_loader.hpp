#pragma once

#include "controller/config.hpp"
#include <filesystem>

namespace controller
{
    ControllerConfig load_controller_config(const std::filesystem::path &path);
}
