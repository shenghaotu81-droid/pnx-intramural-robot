// Controller 类：构造传 Config，update()，reset()
#pragma once

#include "controller/config.hpp"
#include "controller/yaw_filter.hpp"
#include "controller/types.hpp"

namespace controller
{
    class Controller
    {
      public:
        explicit Controller(const ControllerConfig &config);

        // 计算本帧速度指令：先滤 yaw，再算前馈 + 位置 P + 航向 P；search 时直接输出期望角速度
        /* update() 内部顺序：
        1. 先滤 yaw（有 target_heading ）。
        2. 算前馈（有表观速度：表观 + ω×p + 车速（ω×p = ω·(-p.y, p.x)）；TAG 没表观速度，前馈就是 0）。
        3. 位置 P + 前馈（期望位置和实测位置都有）。
        4. 航向 P（期望朝向和平滑后的 yaw 都有）；search 时期望角速度直接输出。
        5. 组 Command 返回。
        */
        Command update(const CurrentState &current, const DesiredState &desired);

        // 清掉滤波器状态（以后加 I 时还清积分器），G3 切状态 / 丢目标时调
        // reset() 里面当前只调 yaw_filter_.reset()
        void reset();

      private:
        ControllerConfig config_;
        YawFilter yaw_filter_;
    };

} // namespace controller