#include "adapter/config_loader.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    bool same_position(controller::Vec2 a, controller::Vec2 b)
    {
        return a.x == b.x && a.y == b.y;
    }

    void check_config(const std::filesystem::path &path, const adapter::AdapterConfig &expected)
    {
        const auto actual = adapter::load_adapter_config(path);
        const auto check = [&](bool matches, const char *field)
        {
            if (!matches)
                throw std::runtime_error(path.string() + " mismatch in " + field);
        };
        check(actual.rotation == expected.rotation, "rotation");
        check(actual.translation == expected.translation, "translation");
        check(same_position(actual.follow_target_position, expected.follow_target_position), "follow_target_position");
        check(same_position(actual.approach_target_position, expected.approach_target_position), "approach_target_position");
        check(same_position(actual.backup_target_position, expected.backup_target_position), "backup_target_position");
        check(actual.follow_target_heading == expected.follow_target_heading, "follow_target_heading");
        check(actual.backup_target_heading == expected.backup_target_heading, "backup_target_heading");
        check(actual.align_target_heading == expected.align_target_heading, "align_target_heading");
        check(actual.scan_angular_velocity == expected.scan_angular_velocity, "scan_angular_velocity");
    }

    void check_missing_file()
    {
        const std::filesystem::path path = "config/missing.yaml";
        try
        {
            adapter::load_adapter_config(path);
        }
        catch (const std::runtime_error &error)
        {
            if (error.what() == "ADAPTER_CONFIG_OPEN_FAILED:" + path.string())
                return;
            throw;
        }
        throw std::runtime_error("missing adapter file accepted");
    }
}

int main()
{
    try
    {
        check_config("../config/adapter.yaml", {});

        adapter::AdapterConfig scan_only;
        scan_only.scan_angular_velocity = 0.0;
        check_config("config/scan_only.yaml", scan_only);

        adapter::AdapterConfig without_scan;
        without_scan.rotation = {0, -1, 0, 1, 0, 0, 0, 0, 1};
        without_scan.translation = {1, -2, 3};
        without_scan.follow_target_position = {1, 2};
        without_scan.approach_target_position = {3, 4};
        without_scan.backup_target_position = {5, 6};
        without_scan.follow_target_heading = 0.25;
        without_scan.backup_target_heading = -0.5;
        without_scan.align_target_heading = 0.75;
        check_config("config/without_scan.yaml", without_scan);
        check_missing_file();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
