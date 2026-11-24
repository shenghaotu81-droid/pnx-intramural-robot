#include "controller/config_loader.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void check_config(const std::filesystem::path &path, const controller::ControllerConfig &expected)
    {
        const auto actual = controller::load_controller_config(path);
        if (actual.position.gain != expected.position.gain || actual.heading.gain != expected.heading.gain)
            throw std::runtime_error(path.string() + " gain mismatch");
        if (actual.yaw_filter.gate_threshold != expected.yaw_filter.gate_threshold ||
            actual.yaw_filter.alpha != expected.yaw_filter.alpha)
            throw std::runtime_error(path.string() + " yaw filter mismatch");
    }

    void check_missing_file()
    {
        const std::filesystem::path path = "config/missing.yaml";
        try
        {
            controller::load_controller_config(path);
        }
        catch (const std::runtime_error &error)
        {
            if (error.what() == "CONTROLLER_CONFIG_OPEN_FAILED:" + path.string())
                return;
            throw;
        }
        throw std::runtime_error("missing controller file accepted");
    }
}

int main()
{
    try
    {
        check_config("../config/controller.yaml", {});

        controller::ControllerConfig alpha_only;
        alpha_only.yaw_filter.alpha = 0.0;
        check_config("config/alpha_only.yaml", alpha_only);

        controller::ControllerConfig without_alpha;
        without_alpha.position.gain = 4.0;
        without_alpha.heading.gain = 2.0;
        without_alpha.yaw_filter.gate_threshold = 0.25;
        check_config("config/without_alpha.yaml", without_alpha);
        check_missing_file();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
