// 实现（绘制 + 上屏）
// Display::show() 实现：调 render()，转 Image，上屏（MaixCDK）
#include "display/display.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <maix_display.hpp>
#include <maix_err.hpp>
#include <maix_image.hpp>
#include <opencv2/core.hpp>

#include "display/render.hpp"

namespace display
{

    struct Display::Impl
    {
        // 默认构造：自动检测尺寸，RGB888，第一块屏，自动打开。
        // 打开失败时 maix::display::Display 构造抛 maix::err::Exception。
        maix::display::Display disp;
    };

    Display::Display()
    {
        try
        {
            impl_ = std::make_unique<Impl>();
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(std::string("display open failed: ") + e.what());
        }
    }

    Display::~Display() = default;

    void Display::show(const DisplayInput &input)
    {
        // 屏幕实际尺寸（MaixCAM-Pro 为 640x480），传给 render()。
        const cv::Size screen_size(impl_->disp.width(), impl_->disp.height());
        cv::Mat screen = render(input, screen_size);

        // 用 BGR888 包 Mat 像素，不拷贝、不接管（Image 析构不释放）。
        // show() 内部会把 BGR888 转成 RGB888 再推屏（BGR888 不在它接受的格式列表里），
        // 每帧一次转换，display 只作调试节流调用，开销可接受。
        maix::image::Image img(screen.cols, screen.rows, maix::image::Format::FMT_BGR888,
                               screen.data, screen.cols * screen.rows * 3, false);
        const maix::err::Err e = impl_->disp.show(img);
        if (e != maix::err::ERR_NONE)
        {
            throw std::runtime_error("display show failed: err=" +
                                     std::to_string(static_cast<int>(e)));
        }
    }

} // namespace display
