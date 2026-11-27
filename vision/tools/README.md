# 板端采集工具

按标定任务选择应用。两个应用都支持 MaixCAM / MaixCAM-Pro、1440×1080 触屏预览、拍照和录像。

| 任务 | 应用 | 板端数据目录 |
| --- | --- | --- |
| 静态取样、相机内参 | [MaixPy 图像采集与部署](board_capture/README.md) | `/root/capture/` |
| 运动 MARK、动态 q 标定 | [C++ 采集与部署](capture_cpp/README.md) | `/root/capture_cpp/` |

动态录像导出后，用同名 H.265 和三列 CSV 配对做标定。性能诊断可另读同名 TSV。构建、部署、样片分析与校验入口见[工具索引](INDEX.md)。电脑端的内参解算和回放工具见[视觉管线工具索引](../pipeline/tools/INDEX.md)。

历次板端录像、配套日志与核验材料按应用保存。 [MaixPy 证据](evidence/board_capture/README.md) · [C++ 证据](evidence/capture_cpp/README.md)
