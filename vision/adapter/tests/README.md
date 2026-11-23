# adapter tests

`adapter_test` 用合成的 `AdapterConfig` 和 `AdapterInput` 调用 `Adapter::adapt()`，运行 10 组共 35 个用例。测试只需要 adapter 的构建依赖，可在没有相机和 pipeline 的环境里运行。

| 测试组 | 覆盖范围 |
| --- | --- |
| 位置变换 | 相机前、右、下三个基向量到车体平面的映射，非零平移对位置的影响 |
| 速度变换 | 前向与右向速度的映射，使用非零平移验证速度只受旋转影响 |
| yaw | 零旋转、正负 yaw、纯俯仰和纯横滚 |
| 缺数据透传 | 位置、速度和 `rvec` 同时缺失时，对应输出均为 `nullopt` |
| 期望状态 | FOLLOW 跟 MARK 和 TAG 时选不同位置，SEARCH、ALIGN、BACKUP 各按配置填写期望字段 |
| 车速透传 | 车体线速度和角速度直接进入 `CurrentState` |
| 带俯仰的相机外参 | 向下倾斜 30° 后，位置和速度仍按配置变换，yaw 仍从相机系 `rvec` 提取 |
| yaw 接近 ±π | 取值区间两端及跨过两端后的回绕 |
| 观测组合与缺失 | 三项观测同时出现，复合旋转的 yaw，逐项及全部缺失 |
| 模式与目标独立性 | 变更配置后对 MARK 和 TAG 分别运行 SEARCH、ALIGN、BACKUP，逐项核对输出 |

## 运行

在仓库根目录运行独立 Release 构建和 CTest。

```sh
cmake -S vision/adapter -B /tmp/adapter-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/adapter-test
ctest --test-dir /tmp/adapter-test --output-on-failure
```

`/tmp/adapter-test/tests/adapter_test` 会打印全部 10 组结果。每个数值分量的绝对误差须小于 `1e-9`，可选字段还会检查有值或为空。任一检查失败时会显示用例、字段、期望值和实际值，并返回 `1`。全部通过时返回 `0`，检查在 Release 构建中同样生效。

配置、逐项预期和验证结果见[测试报告](../docs/test-report.md)。
