# 显示库接口说明

## 定位

display 库是一个纯画画的渲染器：把 app 给的数据画到帧上，推到屏幕。不做任何计算（不检测、不跟踪、不控制），不做任何判断（不决定画什么、不决定什么时候画）。调了就画，不调就不画。

- 比赛模式黑屏不是 display 的事：app 不调 display，屏幕自然黑。
- 节流不是 display 的事：app 每 N 帧调一次 display，就是节流。

## 输入

`DisplayInput` 的字段与 app 传入的数据一一对应。`Velocity` 的形状与控制器 `Command`（vx、vy、ω）和 0x0015 回传（vx、vy、ω）一致，app 直接传，不用换算。

```cpp
namespace display
{

    // 线速度 + 角速度。cmd 和 act 共用这个形状。
    struct Velocity
    {
        cv::Vec2d linear;   // vx, vy
        double angular;     // ω
    };

    struct DisplayInput
    {
        // 必填
        cv::Mat frame;                  // BGR, CV_8UC3，1440x1080，来自相机库
        uint64_t frame_id;              // 帧号
        std::string mode;               // G3 状态名，如 "FOLLOW"
        Velocity command;               // 发给 C 板的速度（vx, vy, ω）
        double loop_hz;                 // 主循环频率，app 算好传进来

        // 可选：缺失时对应元素不画
        std::optional<std::array<cv::Point2f, 4>> target_quad;  // 当前目标四边形，2D 像素点，帧坐标
        std::optional<cv::Point2f> track_pixel;                 // 跟踪点像素，帧坐标
        std::optional<cv::Point2f> velocity_pixel;              // 速度箭头端点像素，帧坐标
        std::optional<std::string> track_state;                 // "MEASURED" 或 "COAST"
        std::optional<cv::Vec2d> target_position;               // 车体系目标位置 x, y，米
        std::optional<cv::Vec2d> target_velocity;               // 车体系目标速度 vx, vy，米/秒
        std::optional<Velocity> actual;                         // C 板实际速度（vx, vy, ω），0x0015 回传
    };

    class Display
    {
      public:
        // 打开 MaixCDK 屏幕（默认屏，BGR888）。打开失败抛 std::runtime_error。
        Display();
        ~Display();
        Display(const Display &) = delete;
        Display &operator=(const Display &) = delete;

        // 画一帧并推屏。MaixCDK 推屏失败抛 std::runtime_error。
        void show(const DisplayInput &input);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // 纯 OpenCV 绘制：输入一帧数据，输出画好的屏幕图（640x480）。
    // 不碰 MaixCDK，可在笔记本上编和测。Display::show() 内部调它。
    cv::Mat render(const DisplayInput &input, const cv::Size &screen_size);

}  // namespace display
```

## 行为

`show()` = `render()` + 转 Image + 推屏。

`render()` 的步骤：

1. 把 `frame` 按等比缩到屏幕尺寸（1440x1080 → 640x480，倍数 0.444，两边都是 4:3，无变形、无黑边）。
2. 把叠加层的点（`target_quad` 四个角点、`track_pixel`、`velocity_pixel`）按同一倍数 0.444 缩到屏幕坐标。
3. 在缩好的图上画：绿框、橙点、橙箭头、四角文字。
4. 返回 640x480 的 BGR Mat。

`show()` 把 `render()` 返回的 Mat 包成 `maix::image::Image`（FMT_BGR888，不做颜色转换），调 MaixCDK `Display::show()` 推屏。

**可选字段缺失时**：对应元素直接不画（无四边形就不画框，无跟踪点就不画点和箭头，无实际速度就不画右下角那块），不报错。

**app 侧的责任**：`track_pixel` 和 `velocity_pixel` 由 app 算好传进来（`cv::projectPoints`，K/dist 取自 `pipeline.pose_config()`，旋转和平移都是单位量——位置已经在相机系）。`loop_hz` 由 app 算好传进来（主循环迭代间隔，平滑后）。display 不做投影，不算频率。

## 布局

屏幕 640x480（MaixCAM-Pro 2.4 寸 IPS 电容触摸屏，见 Sipeed 官网）。四块文字全部贴边，中间画图形。左上贴上沿和左沿，右上贴上沿和右沿，左下贴下沿和左沿，右下贴下沿和右沿。

```
┌──────────────────────────────────────┐
│ frame 123 30Hz              cmd      │
│ MEASURED                    0.50     │
│ FOLLOW                      0.20     │
│                             0.10     │
│                                      │
│        绿框 + 橙点 + 橙箭头           │
│                                      │
│ p 0.50 0.20                 act      │
│ v 0.30 0.10                 0.40     │
│                             0.10     │
│                             0.05     │
└──────────────────────────────────────┘
```

各块的行顺序（写死，不可调）：

- 左上（状态），3 行：第 1 行 `frame <帧号> <Hz>Hz`；第 2 行跟踪状态（`MEASURED` 或 `COAST`）；第 3 行 mode（如 `FOLLOW`）。
- 右上（控制），4 行：第 1 行 `cmd`；第 2 行 vx；第 3 行 vy；第 4 行 ω。
- 左下（目标），2 行：第 1 行 `p <x> <y>`；第 2 行 `v <vx> <vy>`。
- 右下（实际），4 行：第 1 行 `act`；第 2 行 vx；第 3 行 vy；第 4 行 ω。
- 中间：绿框（当前目标四边形）、橙点（跟踪点，MARK 左上角 (0,0) 的投影）、橙箭头（从跟踪点出发，方向为目标表观运动）。

## 格式与单位

- 位置、速度、cmd、act 的数：2 位小数（如 `0.50`）。
- Hz：整数 + 后缀 `Hz`（如 `30Hz`）。
- frame_id：整数。
- 单位只写在本文档里，不上屏（Hz 除外）。位置单位米，速度单位米/秒，角速度单位弧度/秒，频率单位赫兹。x、y 的顺序固定为 x 在前、y 在后，不写轴字母，靠约定。
- 2 位小数填 desired 的最大取整误差 5mm，相对 500mm 跟随距离和 80mm MARK 可忽略，可直接照着屏幕上的数填配置。

## 绘制参数

这些是定死的常量，硬编码在 render.cpp 里，无配置文件：

| 项目 | 值 |
| --- | --- |
| 字体 | `cv::FONT_HERSHEY_SIMPLEX` |
| 字号 | fontScale 1.2，thickness 2 |
| 文字颜色 | 白 `{255,255,255}`（BGR） |
| 目标四边形 | 绿 `{0,255,0}`，thickness 2 |
| 跟踪点 | 橙 `{0,180,255}`，圆半径 5，thickness 2 |
| 速度箭头 | 橙 `{0,180,255}`，thickness 2 |

字号 1.2 是保守值：最长行（`frame 123 30Hz`，14 字符）约 250px（640 宽放得下），右上 4 行块约占屏高 29%，字高约 30px（约 2.3mm，40cm 处可读）。上板后实测可读性再确认。

不做半透明背景：叠加层是细线 + 角落文字，不挡图；真有遮挡问题再加。

## 构建

只编给板子用：笔记本上交叉编译（`maixcdk build`），出板子用的 RISC-V 可执行文件，检查就是交叉编译能不能通过。不编 x86 给笔记本跑的那份。MaixCDK 的调用只出现在实现文件里，app 和其他库不 include MaixCDK 头文件。`render()` 是纯 OpenCV，可在笔记本上编和测（见 tests）。
