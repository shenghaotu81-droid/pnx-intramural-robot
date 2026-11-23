// YawFilter 类：门控 + EMA
#pragma once

#include "controller/config.hpp"

namespace controller
{
    // update 的逻辑：首帧直接采纳；之后算 wrap 后的差，超阈值拒收 hold，否则 EMA 更新
    // 状态：两个成员（平滑值 + 是否初始化过）
    class YawFilter
    {
      public:
        explicit YawFilter(const YawFilterConfig &config);

        // 输入原始 yaw，返回滤波后的 yaw；门控拒收时返回上一个平滑值
        double update(double raw_yaw);

        // 清掉状态，下一次 update 重新初始化（丢目标 / 切模式时调）
        void reset();

        // 获取当前平滑值
        double smoothed_yaw() const;

      private:
        YawFilterConfig config_;
        double smoothed_yaw_ = 0.0;
        bool initialized_ = false;
    };

    double wrap_angle(double angle); // 把角度包到 [-π, π]

} // namespace controller