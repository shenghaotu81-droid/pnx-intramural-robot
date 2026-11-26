// render() 声明（纯 OpenCV）
// render 函数声明
// render
#pragma once

#include <opencv2/core.hpp>

#include "display/display.hpp"

namespace display
{

    // 纯 OpenCV 绘制：输入一帧数据，输出画好的屏幕图（screen_size 尺寸）。
    // 不碰 MaixCDK，可在笔记本上编和测。Display::show() 内部调它。
    cv::Mat render(const DisplayInput &input, const cv::Size &screen_size);

} // namespace display
