# app

MaixCAM 上的应用层：状态机（G3） + 串口。

- 用 pipeline 的输出（G2 名义姿态 `rvec` 和滤波后的位置 / 速度 / P）做决策。
- 调 adapter 库计算车体系位置、速度和目标 yaw，并按模式给出期望状态。[adapter 计算说明](../adapter/README.md#做法)
- 控制器（G5，`controller/` 库，通用）算出速度，经串口通信协议（当前 protocol v0.3）给 C 板发速度指令。

## 状态

待写。
