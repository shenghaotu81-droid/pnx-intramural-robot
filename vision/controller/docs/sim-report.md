# G5 控制器仿真阻塞报告

控制器独立配置成功，生产库编译失败。任务在仿真 harness 实现前停止。

## 严重问题 1 · `YawFilter::smoothed_yaw()` 重复定义

`smoothed_yaw()` 在类体内和 `.cpp` 中各定义了一次。编译 `yaw_filter.cpp` 时，同一翻译单元包含两处定义，`controller` 库构建中止。见 [`yaw_filter.hpp:22`](../include/controller/yaw_filter.hpp#L22) 和 [`yaw_filter.cpp:45`](../src/yaw_filter.cpp#L45)。

```sh
cmake -S vision/controller -B vision/controller/build
cmake --build vision/controller/build --parallel 2
```

环境为 CMake 3.28.3、GNU C++ 13.3.0。配置命令退出码为 0，构建命令退出码为 2。关键诊断如下。

```text
src/yaw_filter.cpp:45:12: error: redefinition of ‘double controller::YawFilter::smoothed_yaw() const’
include/controller/yaw_filter.hpp:22:16: note: ‘double controller::YawFilter::smoothed_yaw() const’ previously defined here
```

## 严重问题 2 · 位置和航向反馈方向与车体系约定相反

车体系 `+x` 指向车头，yaw 和角速度均以逆时针为正。正向平移会减小静止目标在车前的 `x`，正向旋转会减小静止目标相对车头的 yaw。见 [控制器接口约定](../README.md#接口)。

| 控制环 | 可复核的初始状态 | 当前控制律的输出及效果 |
| --- | --- | --- |
| TAG 后退 | TAG 在车前 `0.5 m`，期望在车前 `1.0 m`，车静止且 TAG 无速度输入。 | `vx = Kp × (1.0 − 0.5)`。正增益令车辆向前，TAG 的相对距离减小，偏差随之增大。见 [`controller.cpp:38`](../src/controller.cpp#L38) 和 [`controller.cpp:43`](../src/controller.cpp#L43)。 |
| TAG 对准 | TAG 相对 yaw 为 `+0.1 rad`，期望为 `0 rad`，首帧滤波直接采纳量测。 | `ω = Kh × (0 − 0.1)`。正增益令车辆顺时针旋转，TAG 的相对 yaw 增大，偏差随之增大。见 [`controller.cpp:53`](../src/controller.cpp#L53)、[`controller.cpp:57`](../src/controller.cpp#L57) 和 [`yaw_filter.cpp:17`](../src/yaw_filter.cpp#L17)。 |

两个环的默认增益均为 `+1.0 s⁻¹`。MARK 跟随复用这两个反馈项，且 MARK 的前馈负责匹配目标平移速度。因此，在理想前馈匹配目标速度、车体暂不旋转的条件下，位置反馈方向问题仍存在。见 [`config.hpp:9`](../include/controller/config.hpp#L9) 和 [`controller.cpp:34`](../src/controller.cpp#L34)。

## 验证状态

仿真 harness 尚未实现，CTest 尚未运行。MARK 往复与换向、TAG 对准、TAG 后退、原地扫转均无闭环测量。增益扫描、`±0.05 m/s` 和 `±0.10 m/s` 前馈偏差实验、兑换窗口对照与积分项评估均待验证。恢复条件为生产库能够独立构建，并核定位置和航向反馈方向与车体系接口一致。
