# vision

校内赛机器人视觉侧代码（跑在 MaixCAM 上）。

## 结构

- `pipeline/`：视觉管线库，密封。检测 → 位姿 → 跟踪，内部不动。
- `controller/`：通用控制器库。给定期望状态算速度，与系和语义无关。
- `adapter/`：适配层库。把 pipeline 的相机系输出转成车体系，按 mode 拼出 G5 要的 CurrentState/DesiredState，只做几何 + 语义，不做判断。
- `app/`：应用层，状态机 + 串口，用 pipeline、adapter 和 controller 做决策并发速度指令。

## G1–G5 是什么

G1–G5 是管线里五个环节的代号：

- **G1 检测**：从图像里找出 MARK/TAG 的四个角点。链路：二值化 → 轮廓 → 多边形近似 → 角点拟合 → 排序 → 几何验证 → 发布。
- **G2 位姿**：用 PnP（IPPE）从角点算目标在相机系下的位姿。IPPE 给出两个解，取 RMSE 最小的当名义值，另一个解与名义值的差距记成 S，加进不确定度；不做解歧义，yaw 原样输出。
- **G3 状态机**：决策层，在 `app/` 里。决定现在该干什么（跟 MARK、找 TAG、对准、后退、退出），发动作指令，等 RFID、卡 1 分钟时限。自己不算速度，调 G5 算。
- **G4 跟踪**：卡尔曼滤波，在 `pipeline/` 里。对 G2 的位置做滤波，给出滤波后的位置、表观速度和协方差 P；检测丢帧时靠预测续着。只管位置和速度，不管朝向。
- **G5 通用控制器**：在 `controller/` 里。输入当前状态 + 期望状态（同一 2D 系），输出期望速度 (vx, vy) + 角速度 ω。内部做 P 控制 + 前馈 + 旋转补偿（ω × p）+ 朝向平滑；不做坐标变换，不解释输入是什么意思。

## pipeline 对外输出

- yaw：未处理，原样取 G2 的名义值（RMSE 最小解）。
- 位置 / 速度 / P：卡尔曼滤波后的值。

## 构建

`vision/CMakeLists.txt` 可一起构建 pipeline、controller 和 adapter。运行三个模块的测试时，分别从各模块目录独立构建并执行 CTest。例如 pipeline 使用 `cmake -S vision/pipeline -B build/pipeline-test && cmake --build build/pipeline-test && ctest --test-dir build/pipeline-test --output-on-failure`。

当前结果可视化呈现（来自 pipeline）：[outputs](pipeline/docs/homework-setup/outputs/marker/overlay.avi)
