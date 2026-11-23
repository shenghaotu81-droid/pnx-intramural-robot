# adapter

适配层库：把 pipeline 的相机系输出（G4 滤波后的位置/速度 + G2 的 rvec）转成车体系，按 G3 给的 mode 从配置里取期望值，拼成 G5 要的 CurrentState/DesiredState。只做几何变换 + 模式语义，不做判断。

## 状态

待写。
