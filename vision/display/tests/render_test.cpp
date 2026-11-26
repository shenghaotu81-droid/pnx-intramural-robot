#include "display/render.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    const cv::Size pro_screen{640, 480};
    const cv::Vec3b black{0, 0, 0}, green{0, 255, 0}, orange{0, 180, 255}, white{255, 255, 255};

    void check(bool ok, const std::string &message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    display::DisplayInput make_input(cv::Size size)
    {
        display::DisplayInput input{};
        input.frame = cv::Mat::zeros(size, CV_8UC3);
        input.frame_id = 42;
        input.mode = "FOLLOW";
        input.command = {{0.5, -0.2}, 0.1};
        input.loop_hz = 29.7;
        return input;
    }

    cv::Mat render_at_size(const display::DisplayInput &input, cv::Size size)
    {
        cv::Mat result = display::render(input, size);
        if (result.size() != size || result.type() != CV_8UC3)
        {
            std::ostringstream message;
            message << "期望 " << size << " CV_8UC3，实际尺寸 " << result.size()
                    << "，类型编号 " << result.type();
            throw std::runtime_error(message.str());
        }
        return result;
    }

    void check_pixel(const cv::Mat &image, cv::Point point, const cv::Vec3b &color, const char *label)
    {
        const auto actual = image.at<cv::Vec3b>(point);
        if (actual != color)
        {
            std::ostringstream message;
            message << label << " " << point << " 期望 BGR " << color << "，实际 " << actual;
            throw std::runtime_error(message.str());
        }
    }

    cv::Mat color_mask(const cv::Mat &image, const cv::Vec3b &color)
    {
        cv::Mat mask;
        cv::inRange(image, color, color, mask);
        return mask;
    }

    void test_resized_frame()
    {
        auto input = make_input({1440, 1080});
        const cv::Vec3b left{20, 70, 130}, right{40, 110, 10}, bottom{90, 30, 180};
        input.frame.setTo(right);
        input.frame.colRange(0, 360).setTo(left);
        input.frame.rowRange(540, 1080).setTo(bottom);

        for (const auto size : {pro_screen, cv::Size{720, 360}})
        {
            const auto screen = render_at_size(input, size);
            // 离开文字和插值边界，检查两条分界线的两侧及对角边缘。
            check_pixel(screen, {0, 0}, left, "左上边缘");
            check_pixel(screen, {size.width / 4 - 2, size.height / 3}, left, "左侧色块");
            check_pixel(screen, {size.width / 4 + 2, size.height / 3}, right, "右侧色块");
            check_pixel(screen, {size.width / 2, size.height / 2 - 2}, right, "上半帧");
            check_pixel(screen, {size.width / 2, size.height / 2 + 2}, bottom, "下半帧");
            check_pixel(screen, {size.width - 1, size.height - 1}, bottom, "右下边缘");
        }
    }

    void test_scaled_geometry()
    {
        auto input = make_input({960, 640});
        // sx=1/2、sy=3/4。四角应落在 (60,168)、(150,180)、(144,252)、(54,240)。
        input.target_quad = std::array<cv::Point2f, 4>{{{120, 224}, {300, 240}, {288, 336}, {108, 320}}};
        input.track_pixel = cv::Point2f{500, 320};
        input.velocity_pixel = cv::Point2f{660, 240};
        const auto screen = render_at_size(input, {480, 480});

        for (cv::Point corner : {cv::Point{60, 168}, {150, 180}, {144, 252}, {54, 240}})
            check_pixel(screen, corner, green, "四边形顶点");
        for (cv::Point midpoint : {cv::Point{105, 174}, {147, 216}, {99, 246}, {57, 204}})
            check_pixel(screen, midpoint, green, "四边形边中点");
        check_pixel(screen, {102, 210}, black, "四边形内部");
        check_pixel(screen, {245, 240}, orange, "跟踪圆左侧");
        check_pixel(screen, {250, 245}, orange, "跟踪圆下侧");
        check_pixel(screen, {250, 240}, orange, "箭头起点");
        check_pixel(screen, {290, 210}, orange, "箭杆中点");
        check_pixel(screen, {330, 180}, orange, "箭头端点");
        // 端点左侧、偏离箭杆的区域。普通线段或反向箭头在这里均无箭翼。
        const cv::Rect wing{319, 177, 7, 5};
        check(cv::countNonZero(color_mask(screen(wing), orange)) > 0, "端点缺箭翼");
    }

    void test_optional_geometry()
    {
        auto input = make_input(pro_screen);
        const auto empty = render_at_size(input, pro_screen);
        check(cv::countNonZero(color_mask(empty, green)) == 0, "无四边形时出现绿框");
        check(cv::countNonZero(color_mask(empty, orange)) == 0, "无跟踪点时出现橙色图形");

        input.velocity_pixel = cv::Point2f{440, 240};
        const auto end_only = render_at_size(input, pro_screen);
        check(cv::norm(empty, end_only, cv::NORM_INF) == 0, "只有端点时画面发生变化");

        input.velocity_pixel.reset();
        input.target_quad = std::array<cv::Point2f, 4>{{{80, 180}, {160, 180}, {160, 260}, {80, 260}}};
        const auto quad_only = render_at_size(input, pro_screen);
        check_pixel(quad_only, {80, 220}, green, "只有四边形时的闭合边");
        check(cv::countNonZero(color_mask(quad_only, orange)) == 0, "只有四边形时出现橙色图形");

        input.target_quad.reset();
        input.track_pixel = cv::Point2f{320, 240};
        const auto dot_only = render_at_size(input, pro_screen);
        check_pixel(dot_only, {320, 235}, orange, "只有跟踪点时的圆周");
        check_pixel(dot_only, {320, 240}, black, "空心跟踪点");
        check(cv::countNonZero(color_mask(dot_only, green)) == 0, "只有跟踪点时出现绿框");
        auto ink = color_mask(dot_only, orange);
        ink(cv::Rect{313, 233, 15, 15}).setTo(0);
        check(cv::countNonZero(ink) == 0, "缺少端点时圆外出现橙色图形");

        input.velocity_pixel = cv::Point2f{440, 240};
        const auto arrow = render_at_size(input, pro_screen);
        check_pixel(arrow, {380, 240}, orange, "有起点和端点、无四边形时的箭杆");
    }

    void test_text_rows()
    {
        auto full = make_input(pro_screen);
        full.track_state = "COAST";
        full.target_position = cv::Vec2d{0.53, -0.24};
        full.target_velocity = cv::Vec2d{-0.31, 0.12};
        full.actual = display::Velocity{{-0.15, 0.04}, 0.32};
        const auto reference = color_mask(render_at_size(full, pro_screen), white);

        // 只取每行中部的横带，避开相邻行的字形下伸部分。
        const auto check_layout = [&](const display::DisplayInput &input, const char *scene)
        {
            const auto screen = render_at_size(input, pro_screen);
            const auto mask = color_mask(screen, white);
            const auto check_row = [&](cv::Rect band, bool visible, const std::string &label)
            {
                const std::string where = std::string(scene) + " " + label;
                check(cv::countNonZero(reference(band)) > 0, "完整布局缺少白字：" + label);
                if (visible)
                    check(cv::norm(mask(band), reference(band), cv::NORM_INF) == 0, where + " 缺字或移位");
                else
                    check(cv::norm(screen(band), cv::NORM_INF) == 0, where + " 应留空");
            };

            check_row({0, 14, 300, 16}, true, "左上帧号和频率");
            check_row({0, 46, 300, 16}, input.track_state.has_value(), "左上跟踪状态");
            check_row({0, 78, 300, 16}, true, "左上 mode");
            check_row({0, 419, 300, 16}, input.target_position.has_value(), "左下位置");
            check_row({0, 451, 300, 16}, input.target_velocity.has_value(), "左下速度");
            for (int row = 0; row < 4; ++row)
            {
                check_row({480, 14 + 32 * row, 160, 16}, true,
                          "右上 cmd 第 " + std::to_string(row + 1) + " 行");
                check_row({480, 355 + 32 * row, 160, 16}, input.actual.has_value(),
                          "右下 act 第 " + std::to_string(row + 1) + " 行");
            }
            if (!input.actual)
                check(cv::norm(screen(cv::Rect{320, 240, 320, 240}), cv::NORM_INF) == 0,
                      std::string(scene) + " 缺实际速度时右下整块应留空");
        };

        const auto empty = make_input(pro_screen);
        check_layout(empty, "可选文字全缺");
        auto input = empty;
        input.track_state = full.track_state;
        check_layout(input, "仅跟踪状态");
        input = empty;
        input.target_position = full.target_position;
        check_layout(input, "仅位置");
        input = empty;
        input.target_velocity = full.target_velocity;
        check_layout(input, "仅速度");
        input = empty;
        input.actual = full.actual;
        check_layout(input, "仅实际速度");
    }

    void test_input_frame()
    {
        auto input = make_input({680, 500});
        cv::Mat parent = input.frame;
        cv::RNG rng(42);
        rng.fill(parent, cv::RNG::UNIFORM, 0, 256);
        input.frame = parent(cv::Rect{13, 7, pro_screen.width, pro_screen.height});
        input.track_pixel = cv::Point2f{320, 240};
        const auto before = parent.clone();
        auto contiguous = input;
        contiguous.frame = input.frame.clone();

        for (const auto size : {pro_screen, cv::Size{480, 360}})
        {
            auto result = render_at_size(input, size);
            check(cv::norm(parent, before, cv::NORM_INF) == 0, "绘制修改了输入子图或父图");
            const auto expected = render_at_size(contiguous, size);
            check(cv::norm(result, expected, cv::NORM_INF) == 0, "带行间距的子图与连续副本渲染结果不同");
            result.setTo(cv::Scalar::all(0));
            check(cv::norm(parent, before, cv::NORM_INF) == 0, "改写返回图影响了输入帧");
        }
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
        {"输出尺寸与底图", test_resized_frame},
        {"非等比图形缩放", test_scaled_geometry},
        {"可选图形", test_optional_geometry},
        {"固定文字行", test_text_rows},
        {"输入帧完整性", test_input_frame},
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
