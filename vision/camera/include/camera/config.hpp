// 相机库配置
// CameraConfig
#pragma once

#include <string>

namespace camera
{

    struct CameraConfig
    {
        int width = 1440;              // 图像宽，px
        int height = 1080;             // 图像高，px
        std::string format = "BGR888"; // 格式字符串，库里映射到 MaixCDK 格式枚举
        double fps = 30;               // 帧率，fps（1440x1080 下实际就是 30fps）
        int buff_num = 1;              // 缓冲帧数，1 延时最低
        int skip_frames = 10;          // 启动时丢帧数，等自动曝光收敛
    };

} // namespace camera
