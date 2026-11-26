// render() 实现：缩图、画叠加层、写字（纯 OpenCV）
#include "display/render.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace display
{
    namespace
    {

        // 绘制参数（定死的常量，见 README）
        constexpr int kFontFace = cv::FONT_HERSHEY_SIMPLEX;
        constexpr double kFontScale = 1.2;
        constexpr int kFontThickness = 2;
        const cv::Scalar kTextColor(255, 255, 255); // 白
        const cv::Scalar kQuadColor(0, 255, 0);     // 绿
        const cv::Scalar kOrange(0, 180, 255);      // 橙
        constexpr int kQuadThickness = 2;
        constexpr int kDotRadius = 5;
        constexpr int kDotThickness = 2;
        constexpr int kArrowThickness = 2;
        constexpr int kMargin = 8;  // 文字贴边留白
        constexpr int kLineGap = 6; // 行间距

        // 2 位小数，如 0.50
        std::string format2(double value)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << value;
            return oss.str();
        }

        // 帧坐标点缩到屏幕坐标
        cv::Point scale_point(const cv::Point2f &p, double sx, double sy)
        {
            return cv::Point(cvRound(p.x * sx), cvRound(p.y * sy));
        }

        // 一行字的高度（不含行间距），由字号决定
        int text_height()
        {
            int baseline = 0;
            const cv::Size size =
                cv::getTextSize("0", kFontFace, kFontScale, kFontThickness, &baseline);
            return size.height;
        }

    } // namespace

    cv::Mat render(const DisplayInput &input, const cv::Size &screen_size)
    {
        // 1. 缩图到屏幕尺寸
        cv::Mat screen;
        cv::resize(input.frame, screen, screen_size);
        const double sx = static_cast<double>(screen_size.width) / input.frame.cols;
        const double sy = static_cast<double>(screen_size.height) / input.frame.rows;

        // 2. 画图形
        if (input.target_quad)
        {
            std::vector<cv::Point> corners;
            corners.reserve(4);
            for (const cv::Point2f &p : *input.target_quad)
            {
                corners.push_back(scale_point(p, sx, sy));
            }
            cv::polylines(screen, corners, true, kQuadColor, kQuadThickness);
        }
        if (input.track_pixel)
        {
            const cv::Point center = scale_point(*input.track_pixel, sx, sy);
            cv::circle(screen, center, kDotRadius, kOrange, kDotThickness);
            if (input.velocity_pixel)
            {
                const cv::Point end = scale_point(*input.velocity_pixel, sx, sy);
                cv::arrowedLine(screen, center, end, kOrange, kArrowThickness);
            }
        }

        // 3. 写字
        const int height = text_height();
        const int line_height = height + kLineGap;

        // 左上：3 行
        const std::string hz = std::to_string(static_cast<int>(std::lround(input.loop_hz)));
        const std::string frame_line = "frame " + std::to_string(input.frame_id) + " " + hz + "Hz";
        int y = kMargin + height;
        cv::putText(screen, frame_line, cv::Point(kMargin, y), kFontFace, kFontScale, kTextColor,
                    kFontThickness);
        y += line_height;
        if (input.track_state)
        {
            cv::putText(screen, *input.track_state, cv::Point(kMargin, y), kFontFace, kFontScale,
                        kTextColor, kFontThickness);
        }
        y += line_height;
        cv::putText(screen, input.mode, cv::Point(kMargin, y), kFontFace, kFontScale, kTextColor,
                    kFontThickness);

        // 右上：4 行，右对齐
        const auto draw_right = [&](const std::string &text, int baseline_y)
        {
            int baseline = 0;
            const cv::Size size =
                cv::getTextSize(text, kFontFace, kFontScale, kFontThickness, &baseline);
            cv::putText(screen, text,
                        cv::Point(screen_size.width - kMargin - size.width, baseline_y), kFontFace,
                        kFontScale, kTextColor, kFontThickness);
        };
        y = kMargin + height;
        draw_right("cmd", y);
        y += line_height;
        draw_right(format2(input.command.linear[0]), y);
        y += line_height;
        draw_right(format2(input.command.linear[1]), y);
        y += line_height;
        draw_right(format2(input.command.angular), y);

        // 左下：2 行
        y = screen_size.height - kMargin - line_height;
        if (input.target_position)
        {
            const std::string p_line = "p " + format2((*input.target_position)[0]) + " " +
                                       format2((*input.target_position)[1]);
            cv::putText(screen, p_line, cv::Point(kMargin, y), kFontFace, kFontScale, kTextColor,
                        kFontThickness);
        }
        y += line_height;
        if (input.target_velocity)
        {
            const std::string v_line = "v " + format2((*input.target_velocity)[0]) + " " +
                                       format2((*input.target_velocity)[1]);
            cv::putText(screen, v_line, cv::Point(kMargin, y), kFontFace, kFontScale, kTextColor,
                        kFontThickness);
        }

        // 右下：4 行，右对齐；无实际速度时整块不画
        if (input.actual)
        {
            y = screen_size.height - kMargin - 3 * line_height;
            draw_right("act", y);
            y += line_height;
            draw_right(format2(input.actual->linear[0]), y);
            y += line_height;
            draw_right(format2(input.actual->linear[1]), y);
            y += line_height;
            draw_right(format2(input.actual->angular), y);
        }

        return screen;
    }

} // namespace display
