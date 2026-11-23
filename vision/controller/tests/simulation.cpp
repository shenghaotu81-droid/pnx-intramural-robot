#include "controller/controller.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
    using controller::Command;
    using controller::Controller;
    using controller::ControllerConfig;
    using controller::CurrentState;
    using controller::DesiredState;
    using controller::Vec2;

    constexpr int kHz = 30;
    constexpr double kDt = 1.0 / kHz;
    constexpr double kTau = 0.10;
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kLegFrames = 6 * kHz;
    constexpr int kMarkFrames = 5 * kLegFrames;
    constexpr double kMarkSpeed = 0.5;
    constexpr double kScanSpeed = 0.6;
    constexpr double kPositionTolerance = 0.02;
    constexpr double kHeadingTolerance = 0.02;
    constexpr double kSpeedTolerance = 0.02;
    constexpr double kAngularSpeedTolerance = 0.02;
    constexpr double kPositionCandidate = 3.0;
    constexpr double kHeadingCandidate = 3.0;

    enum class Scenario
    {
        Mark,
        TagAlign,
        TagRetreat,
        Search
    };

    struct Car
    {
        Vec2 position{};
        double yaw = 0.0;
        Vec2 velocity{};
        double omega = 0.0;
    };

    struct Target
    {
        Vec2 position;
        Vec2 velocity;
    };

    struct ScenarioSettings
    {
        Car initial_car;
        DesiredState desired;
        int frames;
    };

    struct Sample
    {
        double position_error;
        double rail_error;
        double heading_error;
        double relative_speed;
        double omega;
        double car_yaw;
        double displacement;
        Command command;
    };

    struct Metrics
    {
        double position_peak = 0.0;
        double rail_mean = 0.0;
        double heading_peak = 0.0;
        double relative_speed_peak = 0.0;
        double angular_speed_peak = 0.0;
        double linear_slew_peak = 0.0;
        double angular_slew_peak = 0.0;
    };

    struct Reversals
    {
        double position_peak = 0.0;
        double overshoot_peak = 0.0;
        std::optional<double> worst_settling_time = 0.0;
    };

    Vec2 difference(Vec2 left, Vec2 right)
    {
        return {left.x - right.x, left.y - right.y};
    }

    double norm(Vec2 value)
    {
        return std::hypot(value.x, value.y);
    }

    Vec2 rotate(Vec2 value, double angle)
    {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return {c * value.x - s * value.y, s * value.x + c * value.y};
    }

    double wrap(double angle)
    {
        return std::remainder(angle, 2.0 * kPi);
    }

    double mark_direction(int frame)
    {
        return (frame / kLegFrames) % 2 == 0 ? 1.0 : -1.0;
    }

    Target target_at(Scenario scenario, int frame)
    {
        if (scenario == Scenario::Mark)
        {
            const double direction = mark_direction(frame);
            const double y = direction * (-1.5 + kMarkSpeed * kDt * (frame % kLegFrames));
            return {{1.0, y}, {0.0, direction * kMarkSpeed}};
        }
        if (scenario == Scenario::TagRetreat)
        {
            return {{0.5, 0.0}, {}};
        }
        return {{1.0, 0.0}, {}};
    }

    ScenarioSettings settings(Scenario scenario)
    {
        switch (scenario)
        {
        case Scenario::Mark:
            return {{{0.0, -1.8}, -0.15}, {Vec2{0.8, 0.0}, 0.0, {}}, kMarkFrames};
        case Scenario::TagAlign:
            return {{{}, -0.30}, {{}, 0.0, {}}, 8 * kHz};
        case Scenario::TagRetreat:
            return {{}, {Vec2{1.0, 0.0}, {}, {}}, 8 * kHz};
        case Scenario::Search:
            return {{}, {{}, {}, kScanSpeed}, 4 * kHz};
        }
        return {};
    }

    CurrentState observe(Scenario scenario, const Car &car, const Target &target, double rail_bias)
    {
        CurrentState current{};
        current.car_velocity = car.velocity;
        current.car_angular_velocity = car.omega;
        if (scenario == Scenario::Search)
        {
            return current;
        }

        const Vec2 position = rotate(difference(target.position, car.position), -car.yaw);
        current.target_heading = wrap(-car.yaw); // MARK 和 TAG 的世界朝向均为 0。
        if (scenario == Scenario::TagAlign)
        {
            return current;
        }
        current.target_position = position;
        if (scenario == Scenario::Mark)
        {
            const Vec2 biased_ground_velocity{target.velocity.x, target.velocity.y + rail_bias};
            const Vec2 ground_velocity = rotate(biased_ground_velocity, -car.yaw);
            // d(R(-yaw) * (target - car))/dt = R(-yaw) * v_target - v_car - omega × p.
            current.target_velocity = {ground_velocity.x - car.velocity.x + car.omega * position.y,
                                       ground_velocity.y - car.velocity.y - car.omega * position.x};
        }
        return current;
    }

    void advance(Car &car, const Command &command)
    {
        const double response = 1.0 - std::exp(-kDt / kTau);
        car.velocity.x += response * (command.velocity.x - car.velocity.x);
        car.velocity.y += response * (command.velocity.y - car.velocity.y);
        car.omega += response * (command.angular_velocity - car.omega);
        const Vec2 world_velocity = rotate(car.velocity, car.yaw);
        car.position.x += world_velocity.x * kDt;
        car.position.y += world_velocity.y * kDt;
        car.yaw = wrap(car.yaw + car.omega * kDt);
    }

    ControllerConfig gains(double position, double heading)
    {
        ControllerConfig config{};
        config.position.gain = position;
        config.heading.gain = heading;
        return config;
    }

    std::vector<Sample> simulate(Scenario scenario, const ControllerConfig &config,
                                 double rail_bias = 0.0)
    {
        const auto setup = settings(scenario);
        Controller controller(config);
        Car car = setup.initial_car;
        std::vector<Sample> samples;
        samples.reserve(setup.frames);
        for (int frame = 0; frame < setup.frames; ++frame)
        {
            const Target target = target_at(scenario, frame);
            const auto current = observe(scenario, car, target, rail_bias);
            const Command command = controller.update(current, setup.desired);
            const double displacement = norm(difference(car.position, setup.initial_car.position));
            // Stop divergent runs before the numerical state leaves the useful range.
            if (!std::isfinite(displacement) || displacement > 100.0 ||
                !std::isfinite(norm(command.velocity)) || !std::isfinite(command.angular_velocity))
            {
                break;
            }

            Vec2 error{};
            double relative_speed = 0.0;
            double heading_error = 0.0;
            if (setup.desired.target_position)
            {
                error = difference(*current.target_position, *setup.desired.target_position);
                relative_speed = norm(difference(target.velocity, rotate(car.velocity, car.yaw)));
            }
            if (setup.desired.target_heading)
            {
                heading_error = wrap(*current.target_heading - *setup.desired.target_heading);
            }
            // Compare truth and command at the same frame, before the command moves the car.
            samples.push_back({norm(error), rotate(error, car.yaw).y, heading_error, relative_speed,
                               car.omega, car.yaw, displacement, command});
            advance(car, command);
        }
        return samples;
    }

    Metrics summarize(const std::vector<Sample> &samples, int begin, int end)
    {
        Metrics metrics;
        for (int frame = begin; frame < end; ++frame)
        {
            const auto &sample = samples[frame];
            metrics.position_peak = std::max(metrics.position_peak, sample.position_error);
            metrics.rail_mean += sample.rail_error;
            metrics.heading_peak = std::max(metrics.heading_peak, std::abs(sample.heading_error));
            metrics.relative_speed_peak =
                std::max(metrics.relative_speed_peak, sample.relative_speed);
            metrics.angular_speed_peak =
                std::max(metrics.angular_speed_peak, std::abs(sample.omega));
            if (frame > 0)
            {
                const auto &previous = samples[frame - 1].command;
                const double linear_slew =
                    norm(difference(sample.command.velocity, previous.velocity));
                const double angular_slew =
                    std::abs(sample.command.angular_velocity - previous.angular_velocity);
                metrics.linear_slew_peak = std::max(metrics.linear_slew_peak, linear_slew / kDt);
                metrics.angular_slew_peak = std::max(metrics.angular_slew_peak, angular_slew / kDt);
            }
        }
        metrics.rail_mean /= end - begin;
        return metrics;
    }

    std::optional<double> settling_time(const std::vector<Sample> &samples, int begin, int end)
    {
        for (int frame = end - 1; frame >= begin; --frame)
        {
            const auto &sample = samples[frame];
            if (sample.position_error > kPositionTolerance ||
                std::abs(sample.heading_error) > kHeadingTolerance ||
                sample.relative_speed > kSpeedTolerance ||
                std::abs(sample.omega) > kAngularSpeedTolerance)
            {
                if (frame == end - 1)
                {
                    return std::nullopt;
                }
                return (frame + 1 - begin) * kDt;
            }
        }
        return 0.0;
    }

    Reversals measure_reversals(const std::vector<Sample> &samples)
    {
        Reversals result;
        for (int start = kLegFrames; start < kMarkFrames; start += kLegFrames)
        {
            const int end = start + kLegFrames;
            const double direction = mark_direction(start);
            for (int frame = start; frame < end; ++frame)
            {
                const auto &sample = samples[frame];
                result.position_peak = std::max(result.position_peak, sample.position_error);
                result.overshoot_peak =
                    std::max(result.overshoot_peak, -direction * sample.rail_error);
            }
            const auto elapsed = settling_time(samples, start, end);
            if (!elapsed)
            {
                result.worst_settling_time.reset();
            }
            else if (result.worst_settling_time)
            {
                result.worst_settling_time = std::max(*result.worst_settling_time, *elapsed);
            }
        }
        return result;
    }

    bool report_mark(const ControllerConfig &config)
    {
        const auto samples = simulate(Scenario::Mark, config);
        std::cout << config.position.gain << ',' << config.heading.gain << ',';
        if (samples.size() != kMarkFrames)
        {
            std::cout << "diverged,,,,,,,,,,\n";
            return false;
        }

        const auto full = summarize(samples, 0, kMarkFrames);
        const auto tail = summarize(samples, kMarkFrames - kHz, kMarkFrames);
        const auto turns = measure_reversals(samples);
        const bool settled = settling_time(samples, kMarkFrames - kHz, kMarkFrames) == 0.0 &&
                             turns.worst_settling_time.has_value();
        std::cout << (settled ? "settled" : "unsettled") << ',' << full.position_peak << ','
                  << tail.position_peak << ',' << turns.position_peak << ',' << turns.overshoot_peak
                  << ',';
        if (turns.worst_settling_time)
        {
            std::cout << *turns.worst_settling_time;
        }
        std::cout << ',' << tail.relative_speed_peak << ',' << tail.heading_peak << ','
                  << full.heading_peak << ',' << full.linear_slew_peak << ','
                  << full.angular_slew_peak << '\n';
        return settled;
    }

    void print_mark_header()
    {
        std::cout << "kp,kh,status,pos_peak_m,pos_tail_peak_m,turn_peak_m,turn_overshoot_m,"
                     "turn_settle_s,rel_speed_tail_peak_mps,yaw_tail_peak_rad,yaw_peak_rad,"
                     "linear_slew_peak_mps2,angular_slew_peak_radps2\n";
    }

    bool report_tag(Scenario scenario, const ControllerConfig &config)
    {
        const auto samples = simulate(scenario, config);
        const int frames = settings(scenario).frames;
        std::cout << config.position.gain << ',' << config.heading.gain << ',';
        if (static_cast<int>(samples.size()) != frames)
        {
            std::cout << "diverged,,,,,,,,,\n";
            return false;
        }

        const auto full = summarize(samples, 0, frames);
        const auto tail = summarize(samples, frames - kHz, frames);
        const auto elapsed = settling_time(samples, 0, frames);
        const bool settled = elapsed && *elapsed <= (frames - kHz) * kDt;
        std::cout << (settled ? "settled" : "unsettled") << ',' << full.position_peak << ','
                  << tail.position_peak << ',' << full.heading_peak << ',' << tail.heading_peak
                  << ',' << tail.relative_speed_peak << ',' << tail.angular_speed_peak << ','
                  << full.linear_slew_peak << ',' << full.angular_slew_peak << ',';
        if (elapsed)
        {
            std::cout << *elapsed;
        }
        std::cout << '\n';
        return settled;
    }

    void print_tag_header()
    {
        std::cout << "kp,kh,status,pos_peak_m,pos_tail_peak_m,yaw_peak_rad,yaw_tail_peak_rad,"
                     "rel_speed_tail_peak_mps,omega_tail_peak_radps,linear_slew_peak_mps2,"
                     "angular_slew_peak_radps2,settle_s\n";
    }

    bool report_bias()
    {
        bool passed = true;
        std::cout << "kp,kh,bias_mps,rail_mean_negative_leg_m,rail_mean_positive_leg_m,"
                     "rail_expected_m,rel_speed_tail_peak_mps\n";
        for (const double position : {1.0, kPositionCandidate})
        {
            const auto config = gains(position, kHeadingCandidate);
            for (const double bias : {-0.10, -0.05, 0.0, 0.05, 0.10})
            {
                const auto samples = simulate(Scenario::Mark, config, bias);
                if (samples.size() != kMarkFrames)
                {
                    std::cerr << "Biased MARK run diverged at kp=" << position << ", bias=" << bias
                              << '\n';
                    passed = false;
                    continue;
                }
                const int negative_end = kMarkFrames - kLegFrames;
                const auto negative = summarize(samples, negative_end - kHz, negative_end);
                const auto positive = summarize(samples, kMarkFrames - kHz, kMarkFrames);
                const double expected = -bias / position;
                const double speed_peak =
                    std::max(negative.relative_speed_peak, positive.relative_speed_peak);
                std::cout << position << ',' << config.heading.gain << ',' << bias << ','
                          << negative.rail_mean << ',' << positive.rail_mean << ',' << expected
                          << ',' << speed_peak << '\n';
                const double mean_error = std::max(std::abs(negative.rail_mean - expected),
                                                   std::abs(positive.rail_mean - expected));
                passed = passed && mean_error <= 0.001 && speed_peak <= 0.002;
            }
        }
        return passed;
    }

    bool check_search()
    {
        const auto config = gains(kPositionCandidate, kHeadingCandidate);
        const auto samples = simulate(Scenario::Search, config);
        const int frames = settings(Scenario::Search).frames;
        if (static_cast<int>(samples.size()) != frames)
        {
            return false;
        }
        for (const auto &sample : samples)
        {
            if (norm(sample.command.velocity) != 0.0 ||
                sample.command.angular_velocity != kScanSpeed)
            {
                return false;
            }
        }

        const auto &last = samples.back();
        const int steps = frames - 1;
        const double decay = std::exp(-kDt / kTau);
        const double remaining = std::pow(decay, steps);
        const double expected_yaw =
            kScanSpeed * kDt * (steps - decay * (1.0 - remaining) / (1.0 - decay));
        std::cout << "displacement_m,omega_radps,yaw_rad\n"
                  << last.displacement << ',' << last.omega << ',' << last.car_yaw << '\n';
        return last.displacement == 0.0 &&
               std::abs(last.omega - kScanSpeed * (1.0 - remaining)) < 1e-12 &&
               std::abs(last.car_yaw - expected_yaw) < 1e-12;
    }

    bool check_yaw_hold()
    {
        const auto config = gains(kPositionCandidate, kHeadingCandidate);
        Controller controller(config);
        CurrentState current{{}, 0.0, Vec2{1.0, 0.0}, Vec2{0.0, 0.5}, 0.10};
        const DesiredState desired{Vec2{0.8, 0.0}, 0.0, {}};
        const auto measured = controller.update(current, desired);
        current.target_heading.reset();
        // The caller still supplies an extrapolated position and velocity during the missed frame.
        current.target_position = Vec2{1.0, 0.5 * kDt};
        const auto extrapolated = controller.update(current, desired);
        current.target_heading = 0.08;
        const auto recovered = controller.update(current, desired);
        std::cout << "measured_omega_radps,held_omega_radps,recovered_omega_radps,coast_vy_mps\n"
                  << measured.angular_velocity << ',' << extrapolated.angular_velocity << ','
                  << recovered.angular_velocity << ',' << extrapolated.velocity.y << '\n';
        return std::abs(measured.angular_velocity - config.heading.gain * 0.10) < 1e-12 &&
               extrapolated.angular_velocity == measured.angular_velocity &&
               std::abs(recovered.angular_velocity - config.heading.gain * 0.09) < 1e-12 &&
               std::abs(extrapolated.velocity.y - (0.5 + config.position.gain * 0.5 * kDt)) < 1e-12;
    }

    void sweep_gains()
    {
        std::cout << "MARK position sweep\n";
        print_mark_header();
        for (const double kp : {0.25, 0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 20.0, 40.0,
                                80.0, 160.0, 320.0, 360.0, 365.0, 400.0})
        {
            report_mark(gains(kp, kHeadingCandidate));
        }

        const double heading_gains[] = {0.25, 0.5,  1.0,  2.0,  3.0,  4.0,  5.0,  6.0, 8.0,
                                        10.0, 12.0, 14.0, 16.0, 17.0, 18.0, 19.0, 20.0};
        std::cout << "TAG align heading sweep\n";
        print_tag_header();
        for (const double kh : heading_gains)
        {
            report_tag(Scenario::TagAlign, gains(kPositionCandidate, kh));
        }
        std::cout << "MARK heading sweep\n";
        print_mark_header();
        for (const double kh : heading_gains)
        {
            report_mark(gains(kPositionCandidate, kh));
        }
    }
} // namespace

int main(int argc, char **argv)
{
    const std::string_view mode = argc == 2 ? argv[1] : "";
    const auto candidate = gains(kPositionCandidate, kHeadingCandidate);
    std::cout << std::fixed << std::setprecision(6);
    bool passed = false;
    if (mode == "mark")
    {
        print_mark_header();
        const bool defaults_passed = report_mark(gains(1.0, 1.0));// 基线固定 1.0/1.0，不跟 config 默认值走。
        const bool candidate_passed = report_mark(candidate);
        passed = defaults_passed && candidate_passed;
    }
    else if (mode == "tag-align" || mode == "tag-retreat")
    {
        const Scenario scenario = mode == "tag-align" ? Scenario::TagAlign : Scenario::TagRetreat;
        print_tag_header();
        const bool defaults_passed = report_tag(scenario, gains(1.0, 1.0));// 基线固定 1.0/1.0，不跟 config 默认值走。
        const bool candidate_passed = report_tag(scenario, candidate);
        passed = defaults_passed && candidate_passed;
    }
    else if (mode == "search")
    {
        passed = check_search();
    }
    else if (mode == "yaw-hold")
    {
        passed = check_yaw_hold();
    }
    else if (mode == "feedforward-bias")
    {
        passed = report_bias();
    }
    else if (mode == "gain-sweep")
    {
        sweep_gains();
        return 0;
    }
    else
    {
        std::cerr << "Usage: controller_sim "
                     "<mark|tag-align|tag-retreat|search|yaw-hold|feedforward-bias|gain-sweep>\n";
        return 2;
    }
    if (!passed)
    {
        std::cerr << mode << ": regression check failed\n";
    }
    return passed ? 0 : 1;
}
