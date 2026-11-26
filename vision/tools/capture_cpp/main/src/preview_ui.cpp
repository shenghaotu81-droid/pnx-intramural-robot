#include "preview_ui.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace capture {
namespace {

using maix::image::Color;
using maix::image::Image;

const Color background = Color::from_rgb(18, 23, 31);
const Color button = Color::from_rgb(39, 50, 65);
const Color blue = Color::from_rgb(20, 86, 127);
const Color red = Color::from_rgb(169, 40, 53);

void fill(Image &canvas, const cv::Rect &area, const Color &color) {
    canvas.draw_rect(area.x, area.y, area.width, area.height, color, -1);
}

void centered_label(Image &canvas, const cv::Rect &area, const std::string &label) {
    auto size = maix::image::string_size(label, 1, -1);
    canvas.draw_string(area.x + (area.width - size.width()) / 2,
        area.y + (area.height - size.height()) / 2, label, maix::image::COLOR_WHITE, 1, -1, false);
}

std::string recording_label(const RecordStats &stats) {
    const auto seconds = stats.elapsed_us / 1'000'000;
    std::ostringstream label;
    label << "REC " << std::setfill('0') << std::setw(2) << seconds / 60 << ':'
          << std::setw(2) << seconds % 60 << "   " << std::fixed << std::setprecision(1)
          << stats.actual_fps() << " fps";
    return label.str();
}

}  // namespace

PreviewUi::PreviewUi(maix::display::Display &display)
    : display_(display), canvas_(display.width(), display.height(), maix::image::FMT_RGB888) {
    constexpr int margin = 10;
    constexpr int title_height = 28;
    const int button_height = std::clamp(canvas_.height() / 8, 36, 56);
    const int button_top = canvas_.height() - margin - button_height;
    const int button_width = (canvas_.width() - 4 * margin) / 3;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        buttons_[i] = {margin + static_cast<int>(i) * (button_width + margin), button_top,
                       button_width, button_height};
    }
    const int area_height = button_top - margin - title_height;
    const int unit = std::min((canvas_.width() - 2 * margin) / 4, area_height / 3) & ~1;
    if (unit <= 0 || button_width <= 0) {
        throw std::runtime_error("Screen is too small for capture controls");
    }
    preview_ = {(canvas_.width() - unit * 4) / 2, title_height + (area_height - unit * 3) / 2,
                unit * 4, unit * 3};
}

Action PreviewUi::read_touch(maix::touchscreen::TouchScreen &touch) {
    const auto point = touch.read();
    const bool new_press = point[2] && !pressed_;
    pressed_ = point[2];
    if (new_press) {
        constexpr std::array actions{Action::photo, Action::record, Action::exit};
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            if (buttons_[i].contains({point[0], point[1]})) {
                return actions[i];
            }
        }
    }
    return Action::none;
}

void PreviewUi::show(const PreviewState &state) {
    fill(canvas_, {0, 0, canvas_.width(), canvas_.height()}, background);
    if (state.rgb.empty()) {
        centered_label(canvas_, preview_, "Waiting for camera");
    } else {
        Image view(state.rgb.cols, state.rgb.rows, maix::image::FMT_RGB888, state.rgb.data,
                   static_cast<int>(state.rgb.total() * state.rgb.elemSize()), false);
        canvas_.draw_image(preview_.x, preview_.y, view);
    }
    canvas_.draw_string(10, 8, state.recording ? recording_label(state.stats) : "1440 x 1080");
    if (!state.notice.empty()) {
        auto size = maix::image::string_size(state.notice, 1, -1);
        canvas_.draw_string(canvas_.width() - 10 - size.width(), 8, state.notice);
    }
    fill(canvas_, buttons_[0], blue);
    fill(canvas_, buttons_[1], state.recording ? red : button);
    fill(canvas_, buttons_[2], button);
    centered_label(canvas_, buttons_[0], "Photo");
    centered_label(canvas_, buttons_[1], state.recording ? "Stop REC" : "Start REC");
    centered_label(canvas_, buttons_[2], "Exit");
    if (display_.show(canvas_) != maix::err::ERR_NONE) {
        throw std::runtime_error("Display update failed");
    }
}

}  // namespace capture
