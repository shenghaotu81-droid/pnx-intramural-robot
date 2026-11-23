# G5 控制器仿真测试

测试程序调用生产库的 `Controller::update()`，验证跟随 MARK、对准 TAG、后退和原地扫转。还检查单帧缺 yaw 时保留滤波值、调用方给出的外推位置和速度继续参与控制，以及恒定前馈速度偏差引起的稳态位置残差。数值和增益选择记录见 [仿真报告](../docs/sim-report.md)。

## 构建和运行

在仓库根目录执行。控制器独立构建默认启用 `BUILD_TESTING`，测试程序只链接 `controller` 并使用 C++17 标准库。

```sh
cmake -S vision/controller -B vision/controller/build
cmake --build vision/controller/build
ctest --test-dir vision/controller/build --output-on-failure
ctest --test-dir vision/controller/build -V
vision/controller/build/tests/controller_sim gain-sweep
vision/controller/build/tests/controller_sim feedforward-bias
```

也可以直接传入 `mark`、`tag-align`、`tag-retreat`、`search` 或 `yaw-hold` 单独运行。回归检查失败时进程返回 1。增益扫描是测量命令，扫描到超时或发散的增益时在该行输出状态，其余点继续运行。

## 模型和每帧步骤

世界系初始朝向为 0 时与车体系同向。车体系 `+x` 向前、`+y` 向左、正 yaw 逆时针。仿真保存车的世界位置、航向和车体系实际速度。每帧固定为 `dt = 1/30 s`。

1. 由当前仿真时刻求目标世界位置和速度。令 `R` 为车体系转世界系的旋转矩阵，目标相对位置为 `p = Rᵀ(p_target − p_car)`，相对 yaw 为目标世界 yaw 减车体 yaw，并包角。
2. MARK 的表观速度用 `v_app = Rᵀ v_target − v_car − ω × p` 构造。前馈实验把恒定偏差加到速度量测的世界系轨道方向，再计算 `v_app`。目标位置和实际运动始终用真值。TAG 的表观速度字段留空。用车的实际线速度和角速度填充 `CurrentState`，调用 `Controller::update()`。
3. 同一帧的真值和指令配对采样。线速度和角速度均经过 `u_actual += (1 − exp(−dt/τ)) × (u_command − u_actual)`，取 `τ = 0.10 s`。使用更新后的速度和本帧朝向积分车的世界位置，再积分朝向。
4. 所有增益从 `ControllerConfig` 传入。yaw 门控阈值和 EMA 沿用生产默认值 `0.17 rad` 和 `0.5`。各次仿真都重新构造控制器，车的初始速度为零。

| 场景 | 目标、初态和期望状态 |
| --- | --- |
| MARK 往复 | 固定世界 `x = 1.0 m`，沿世界 `y` 在 `−1.5 m` 和 `+1.5 m` 之间以 `0.5 m/s` 往复，端点处同一帧切换速度符号。车从 `(0, −1.8) m`、yaw `−0.15 rad` 起步，期望相对位置为 `(0.8, 0) m`、yaw 为 0。共 30 s，含四次换向。 |
| TAG 对准 | TAG 静止，车初始 yaw 为 `−0.30 rad`，期望相对 yaw 为 0，仅给航向期望。运行 8 s。 |
| TAG 后退 | TAG 静止且初始在车前 `0.50 m`，期望位于车前 `1.00 m`，仅给位置期望。运行 8 s。 |
| 原地扫转 | 目标字段全空，期望角速度为 `+0.60 rad/s`。运行 4 s，逐帧检查指令，并用一阶响应公式检查最终采样的实际角速度和朝向。 |
| 单帧缺 yaw | 先给 `0.10 rad` 的 yaw，下一帧只给外推的 MARK 位置和速度，第三帧恢复 yaw `0.08 rad`。检查角速度保持、位置环继续计算，以及恢复量测后的 EMA 输出。 |

MARK 和 TAG 的世界朝向均为 0。主场景假设理想观测，MARK 在反转帧及时更新速度。场景中的 3 m 行程、初始偏差和控制误差带是测试输入。

## 读数和通过条件

- `pos_peak_m` 是全程位置误差范数的最大值，包含初始跟上的过程。带 `tail` 的峰值取末段最后 1 s。航向误差使用真值包角，`rel_speed_tail_peak_mps` 使用目标相对车的真实平移速度范数。
- `turn_peak_m` 只统计第一次换向起的误差峰值。`turn_overshoot_m` 是沿每次换向后的新方向，车辆相对于该方向目标偏移越过平衡点的最大距离。`turn_settle_s` 取四次换向中最慢的一次，从换向帧起算，进入误差带后须保持到下一次换向或运行结束。`settle_s` 是 TAG 自起步到持续进入误差带的时间。
- 误差带为位置 `0.02 m`、航向 `0.02 rad`、相对平移速度 `0.02 m/s`、实际角速度 `0.02 rad/s`。MARK 和 TAG 回归要求默认增益与候选增益的最后 1 s 都在带内，MARK 还要求四次换向都能恢复。
- `*_slew_peak_*` 是相邻帧指令变化量除以 `dt` 的峰值，首帧指令不参与差分。线速度使用二维指令差的范数。
- 前馈偏差扫描在最后一段负向匀速、最后一段正向匀速各取末 1 s，测量 `实测目标相对位置 − 期望目标相对位置` 投影到世界 `+y` 的带符号均值。两段均值距 `−偏差/Kp` 均须不超过 `1 mm`，两段相对速度峰值须不超过 `2 mm/s`。
- 扫描输出中，`settled` 表示满足测量时窗内的误差带判据，`unsettled` 表示跑完时窗仍未满足判据。状态之外还要查看最大误差、超调和指令变化率。车辆位移超过 100 m，或位移和指令中出现非有限数时输出 `diverged`，后续指标为空。
