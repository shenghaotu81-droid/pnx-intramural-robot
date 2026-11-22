# vision

校内赛机器人视觉侧代码（跑在 MaixCAM 上）。

## 结构

- `pipeline/`：视觉管线库，密封。检测 → 位姿 → 跟踪，内部不动。
- `app/`：应用层，状态机 + 控制器，用 pipeline 的输出做决策并发速度指令。

## pipeline 对外输出

- yaw：未处理，原样取 G2 的名义值（RMSE 最小解）。
- 位置 / 速度 / P：卡尔曼滤波后的值。

## 构建

pipeline 可单独构建验证：`cmake -S vision/pipeline -B build && cmake --build build`，再 `ctest --test-dir build`（11/11 通过）。

vision 层的 CMakeLists（把 pipeline 和 app 串起来）待补全。

当前结果可视化呈现（来自 pipeline）：[outputs](pipeline/docs/homework-setup/outputs/marker/overlay.avi)
