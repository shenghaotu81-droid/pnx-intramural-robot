#pragma once

#include "recording.hpp"

#include "maix_display.hpp"
#include "maix_touchscreen.hpp"

#include <array>
#include <string>

namespace capture {

enum class Action { none, photo, record, exit };

struct PreviewState {
    cv::Mat rgb;
    RecordStats stats;
    std::string notice;
    bool recording = false;
};

class PreviewUi {
public:
    explicit PreviewUi(maix::display::Display &display);
    cv::Size preview_size() const { return preview_.size(); }
    Action read_touch(maix::touchscreen::TouchScreen &touch);
    void show(const PreviewState &state);

private:
    maix::display::Display &display_;
    maix::image::Image canvas_;
    std::array<cv::Rect, 3> buttons_{};
    cv::Rect preview_;
    bool pressed_ = false;
};

}  // namespace capture
