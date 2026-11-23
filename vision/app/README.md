# app

MaixCAM 上的应用层：状态机（G3） + 串口。

- 吃 pipeline 的输出（未处理 yaw + 滤波后的位置 / 速度 / P）做决策。
- 调 adapter/ 库把 pipeline 的输出转成车体系 2D 状态，并定期望状态。
- 控制器（G5，`controller/` 库，通用）算出速度，经串口通信协议（当前 protocol v0.3）给 C 板发速度指令。

## 状态

待写。
