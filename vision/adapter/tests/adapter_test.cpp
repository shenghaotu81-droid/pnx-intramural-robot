#include "adapter/adapter.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>

#include <opencv2/calib3d.hpp>

namespace
{
    using adapter::Adapter;
    using adapter::AdapterConfig;
    using adapter::AdapterInput;
    using adapter::Mode;
    using adapter::Target;
    using controller::CurrentState;
    using controller::DesiredState;
    using controller::Vec2;

    constexpr double kTolerance = 1e-9;
    constexpr double kPi = 3.14159265358979323846;
    const cv::Vec3d kCameraOffset{0.1, 0.05, 0.2};

    bool matches(double actual, double expected)
    {
        return std::abs(actual - expected) < kTolerance;
    }

    bool matches(Vec2 actual, Vec2 expected)
    {
        return matches(actual.x, expected.x) && matches(actual.y, expected.y);
    }

    template <typename T>
    bool matches(const std::optional<T> &actual, const std::optional<T> &expected)
    {
        if (!actual || !expected)
        {
            return actual.has_value() == expected.has_value();
        }
        return matches(*actual, *expected);
    }

    void print_value(double value)
    {
        std::cout << value;
    }

    void print_value(Vec2 value)
    {
        std::cout << '(' << value.x << ", " << value.y << ')';
    }

    template <typename T>
    void print_value(const std::optional<T> &value)
    {
        if (value)
        {
            print_value(*value);
        }
        else
        {
            std::cout << "nullopt";
        }
    }

    struct Checks
    {
        const char *group;
        bool passed = true;

        template <typename T>
        void expect(const char *scenario, const char *field, const T &actual, const T &expected)
        {
            if (matches(actual, expected))
            {
                return;
            }

            passed = false;
            std::cout << "  " << group << " / " << scenario << " / " << field << ": expected ";
            print_value(expected);
            std::cout << ", got ";
            print_value(actual);
            std::cout << '\n';
        }
    };

    AdapterConfig make_config()
    {
        AdapterConfig config;
        // 相机右、下、前分别映射到车体右、下、前，再取车体前向和左向。
        config.rotation = cv::Matx33d(0, 0, 1, -1, 0, 0, 0, -1, 0);
        config.translation = cv::Vec3d(0, 0, 0);
        config.follow_target_position = {0.5, 0.0};
        config.approach_target_position = {0.3, 0.0};
        config.backup_target_position = {0.8, 0.0};
        config.follow_target_heading = 0.1;
        config.backup_target_heading = -0.1;
        config.align_target_heading = 0.2;
        config.scan_angular_velocity = 0.5;
        return config;
    }

    AdapterInput make_input()
    {
        AdapterInput input{};
        input.target = Target::Mark;
        input.mode = Mode::Follow;
        return input;
    }

    void check_measurements(Checks &checks, const char *scenario, const CurrentState &current,
                            const std::optional<Vec2> &position,
                            const std::optional<Vec2> &velocity,
                            const std::optional<double> &heading)
    {
        checks.expect(scenario, "target_position", current.target_position, position);
        checks.expect(scenario, "target_velocity", current.target_velocity, velocity);
        checks.expect(scenario, "target_heading", current.target_heading, heading);
    }

    void check_desired(Checks &checks, const char *scenario, const DesiredState &actual,
                       const DesiredState &expected)
    {
        checks.expect(scenario, "target_position", actual.target_position,
                      expected.target_position);
        checks.expect(scenario, "target_heading", actual.target_heading, expected.target_heading);
        checks.expect(scenario, "angular_velocity", actual.angular_velocity,
                      expected.angular_velocity);
    }

    struct VectorCase
    {
        const char *name;
        cv::Vec3d camera;
        Vec2 expected;
    };

    void test_position(Checks &checks, const AdapterConfig &config)
    {
        const Adapter adapter(config);
        const VectorCase cases[] = {
            {"camera forward", {0, 0, 1}, {1.0, 0.0}},
            {"camera right", {1, 0, 0}, {0.0, -1.0}},
            {"camera down", {0, 1, 0}, {0.0, 0.0}},
        };
        auto input = make_input();
        for (const auto &test : cases)
        {
            input.position_camera = test.camera;
            const auto current = adapter.adapt(input).current;
            checks.expect(test.name, "target_position", current.target_position,
                          std::optional{test.expected});
        }

        auto offset_config = config;
        offset_config.translation = kCameraOffset;
        const Adapter offset_adapter(offset_config);
        input.position_camera = cv::Vec3d(0, 0, 1);
        checks.expect("forward with camera offset", "target_position",
                      offset_adapter.adapt(input).current.target_position,
                      std::optional{Vec2{1.1, 0.05}});
    }

    void test_velocity(Checks &checks, const AdapterConfig &config)
    {
        // 即使相机光心有偏移，速度也只应用旋转。
        auto offset_config = config;
        offset_config.translation = kCameraOffset;
        const Adapter adapter(offset_config);
        const VectorCase cases[] = {
            {"camera forward", {0, 0, 0.5}, {0.5, 0.0}},
            {"camera right", {0.2, 0, 0}, {0.0, -0.2}},
        };
        auto input = make_input();
        for (const auto &test : cases)
        {
            input.velocity_camera = test.camera;
            const auto current = adapter.adapt(input).current;
            checks.expect(test.name, "target_velocity", current.target_velocity,
                          std::optional{test.expected});
        }
    }

    void test_yaw(Checks &checks, const AdapterConfig &config)
    {
        // 分别绕相机 y、x、z 轴旋转，检查 yaw 的轴向和符号。
        const struct
        {
            const char *name;
            cv::Vec3d rvec;
            double expected;
        } cases[] = {
            {"zero rotation", {0, 0, 0}, 0.0},
            {"positive camera y", {0, 0.5236, 0}, -0.5236},
            {"negative camera y", {0, -0.7854, 0}, 0.7854},
            {"pure pitch", {0.3, 0, 0}, 0.0},
            {"pure roll", {0, 0, 0.3}, 0.0},
        };
        const Adapter adapter(config);
        auto input = make_input();
        for (const auto &test : cases)
        {
            input.rvec = test.rvec;
            checks.expect(test.name, "target_heading", adapter.adapt(input).current.target_heading,
                          std::optional{test.expected});
        }
    }

    void test_missing_target_data(Checks &checks, const AdapterConfig &config)
    {
        const Adapter adapter(config);
        const auto current = adapter.adapt(make_input()).current;
        check_measurements(checks, "all target data absent", current, {}, {}, {});
    }

    void test_desired_state(Checks &checks, const AdapterConfig &config)
    {
        // 每个模式都检查三个期望字段，包括应留空的字段。
        const struct
        {
            const char *name;
            Mode mode;
            Target target;
            DesiredState expected;
        } cases[] = {
            {"FOLLOW + MARK", Mode::Follow, Target::Mark, {Vec2{0.5, 0.0}, 0.1, {}}},
            {"FOLLOW + TAG", Mode::Follow, Target::Tag, {Vec2{0.3, 0.0}, 0.1, {}}},
            {"SEARCH", Mode::Search, Target::Tag, {{}, {}, 0.5}},
            {"ALIGN", Mode::Align, Target::Tag, {{}, 0.2, {}}},
            {"BACKUP", Mode::Backup, Target::Tag, {Vec2{0.8, 0.0}, -0.1, {}}},
        };
        const Adapter adapter(config);
        auto input = make_input();
        for (const auto &test : cases)
        {
            input.mode = test.mode;
            input.target = test.target;
            check_desired(checks, test.name, adapter.adapt(input).desired, test.expected);
        }
    }

    void test_car_motion(Checks &checks, const AdapterConfig &config)
    {
        const Adapter adapter(config);
        auto input = make_input();
        input.car_velocity = {0.3, -0.1};
        input.car_angular_velocity = 0.2;
        const auto current = adapter.adapt(input).current;
        checks.expect("body-frame motion", "car_velocity", current.car_velocity,
                      input.car_velocity);
        checks.expect("body-frame motion", "car_angular_velocity", current.car_angular_velocity,
                      input.car_angular_velocity);
    }

    void test_pitched_camera(Checks &checks, const AdapterConfig &config)
    {
        auto pitched_config = config;
        // 在默认 R 左侧乘绕车体 +y 轴的 30° 旋转，相机光轴向下倾斜。
        const double cos_pitch = std::sqrt(3.0) / 2.0;
        pitched_config.rotation = cv::Matx33d(0, -0.5, cos_pitch, -1, 0, 0, 0, -cos_pitch, -0.5);
        const Adapter pitched(pitched_config);
        auto input = make_input();
        input.position_camera = cv::Vec3d(0, 1, 0);
        input.velocity_camera = cv::Vec3d(0, 1, 0);
        check_measurements(checks, "camera down with pitch", pitched.adapt(input).current,
                           Vec2{-0.5, 0.0}, Vec2{-0.5, 0.0}, {});

        pitched_config.translation = kCameraOffset;
        const Adapter pitched_with_offset(pitched_config);
        input.position_camera = cv::Vec3d(0.4, 0.6, 2.0);
        input.velocity_camera = cv::Vec3d(-0.2, 0.8, -0.4);
        input.rvec = cv::Vec3d(0, 0.35, 0);
        check_measurements(checks, "mixed axes with pitch and offset",
                           pitched_with_offset.adapt(input).current,
                           Vec2{1.5320508075688772, -0.35}, Vec2{-0.7464101615137755, 0.2},
                           -0.39890119564063126);
    }

    void test_fixed_target_heading(Checks &checks, const AdapterConfig &config)
    {
        // 车的世界 yaw 为零时，正对目标的模型轴与默认安装的相机轴重合。
        const cv::Matx33d target_to_world = config.rotation;
        cv::Matx33d mount_offset;
        cv::Rodrigues(cv::Vec3d(0.17, -0.23, 0.31), mount_offset);
        auto mounted_config = config;
        mounted_config.rotation = mount_offset * config.rotation;
        const Adapter adapter(mounted_config);
        auto input = make_input();
        input.target = Target::Tag;
        for (double car_yaw : {0.0, 0.30, -0.15})
        {
            cv::Matx33d body_to_world;
            cv::Rodrigues(cv::Vec3d(0, 0, car_yaw), body_to_world);
            const cv::Matx33d camera_to_world = body_to_world * mounted_config.rotation;
            cv::Vec3d rvec;
            cv::Rodrigues(camera_to_world.t() * target_to_world, rvec);
            input.rvec = rvec;
            checks.expect("fixed world heading", "target_heading", adapter.adapt(input).current.target_heading,
                          std::optional{-car_yaw});
        }
    }

    void test_yaw_boundary(Checks &checks, const AdapterConfig &config)
    {
        constexpr double delta = 1e-6;
        const struct
        {
            const char *name;
            double camera_y;
            double expected;
        } cases[] = {
            {"inside positive pi", kPi - delta, -kPi + delta},
            {"inside negative pi", -kPi + delta, kPi - delta},
            {"past positive pi", kPi + delta, kPi - delta},
            {"past negative pi", -kPi - delta, -kPi + delta},
        };
        const Adapter adapter(config);
        auto input = make_input();
        for (const auto &test : cases)
        {
            input.rvec = cv::Vec3d(0, test.camera_y, 0);
            checks.expect(test.name, "target_heading", adapter.adapt(input).current.target_heading,
                          std::optional{test.expected});
        }
    }

    void test_measurements_together(Checks &checks, const AdapterConfig &config)
    {
        auto follow_config = config;
        follow_config.follow_target_position = {0.65, -0.12};
        follow_config.follow_target_heading = -0.3;
        const Adapter adapter(follow_config);

        auto input = make_input();
        input.position_camera = cv::Vec3d(0.4, -0.2, 1.2);
        input.velocity_camera = cv::Vec3d(-0.2, 0.25, 0.6);
        // 绕 (1, 1, 0) 转 90°，模型 +z 变为相机系 (1/√2, -1/√2, 0)，水平投影朝车右。
        const double rotation_component = kPi / (2.0 * std::sqrt(2.0));
        input.rvec = cv::Vec3d(rotation_component, rotation_component, 0);
        input.car_velocity = {0.3, -0.1};
        input.car_angular_velocity = 0.2;

        const Vec2 position{1.2, -0.4};
        const Vec2 velocity{0.6, 0.2};
        constexpr double heading = -kPi / 2.0;
        const DesiredState desired{Vec2{0.65, -0.12}, -0.3, {}};

        const auto full = adapter.adapt(input);
        check_measurements(checks, "all measurements", full.current, position, velocity, heading);
        checks.expect("all measurements", "car_velocity", full.current.car_velocity,
                      input.car_velocity);
        checks.expect("all measurements", "car_angular_velocity", full.current.car_angular_velocity,
                      input.car_angular_velocity);
        check_desired(checks, "all measurements", full.desired, desired);

        auto partial = input;
        partial.position_camera.reset();
        check_measurements(checks, "missing position", adapter.adapt(partial).current,
                           {}, velocity, heading);

        partial = input;
        partial.velocity_camera.reset();
        check_measurements(checks, "missing velocity", adapter.adapt(partial).current,
                           position, {}, heading);

        partial = input;
        partial.rvec.reset();
        check_measurements(checks, "missing rvec", adapter.adapt(partial).current,
                           position, velocity, {});

        partial.position_camera.reset();
        partial.velocity_camera.reset();
        const auto lost = adapter.adapt(partial);
        check_measurements(checks, "all measurements lost", lost.current, {}, {}, {});
        check_desired(checks, "all measurements lost", lost.desired, desired);
    }

    void test_modes_independent_of_target(Checks &checks, const AdapterConfig &config)
    {
        auto mode_config = config;
        mode_config.scan_angular_velocity = -0.7;
        mode_config.align_target_heading = -0.35;
        mode_config.backup_target_position = {0.65, -0.15};
        mode_config.backup_target_heading = 0.45;
        const Adapter adapter(mode_config);

        // 同一个模式只切换 target，两个输出都要符合配置中的期望值。
        const struct
        {
            Mode mode;
            const char *mark_name;
            const char *tag_name;
            DesiredState expected;
        } cases[] = {
            {Mode::Search, "SEARCH + MARK", "SEARCH + TAG", {{}, {}, -0.7}},
            {Mode::Align, "ALIGN + MARK", "ALIGN + TAG", {{}, -0.35, {}}},
            {Mode::Backup, "BACKUP + MARK", "BACKUP + TAG", {Vec2{0.65, -0.15}, 0.45, {}}},
        };
        auto input = make_input();
        for (const auto &test : cases)
        {
            input.mode = test.mode;
            input.target = Target::Mark;
            check_desired(checks, test.mark_name, adapter.adapt(input).desired, test.expected);
            input.target = Target::Tag;
            check_desired(checks, test.tag_name, adapter.adapt(input).desired, test.expected);
        }
    }
} // namespace

int main()
{
    const struct
    {
        const char *name;
        void (*run)(Checks &, const AdapterConfig &);
    } groups[] = {
        {"Position transform", test_position},
        {"Velocity transform", test_velocity},
        {"Yaw", test_yaw},
        {"Missing target data", test_missing_target_data},
        {"Desired state by mode", test_desired_state},
        {"Car motion passthrough", test_car_motion},
        {"Pitched camera", test_pitched_camera},
        {"Fixed target with rotated camera", test_fixed_target_heading},
        {"Yaw near pi", test_yaw_boundary},
        {"Combined and missing measurements", test_measurements_together},
        {"Modes independent of target", test_modes_independent_of_target},
    };

    const auto config = make_config();
    bool all_passed = true;
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto &group : groups)
    {
        Checks checks{group.name};
        group.run(checks, config);
        std::cout << (checks.passed ? "[PASS] " : "[FAIL] ") << group.name << '\n';
        all_passed = all_passed && checks.passed;
    }
    return all_passed ? 0 : 1;
}
