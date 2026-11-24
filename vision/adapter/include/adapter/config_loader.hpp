#pragma once

#include "adapter/config.hpp"
#include <filesystem>

namespace adapter
{
    AdapterConfig load_adapter_config(const std::filesystem::path &path);
}
