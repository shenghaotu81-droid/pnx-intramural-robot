# 采集工具索引

| 要做的事 | 入口 |
| --- | --- |
| 构建、安装、导出 Python 静态采集素材 | [board_capture 使用说明](board_capture/README.md) |
| 查询 MaixPy 接口和录像行距故障 | [API](board_capture/docs/maixpy-api.md) · [绿屏报告](board_capture/docs/green-video-report.md) · [验证记录](board_capture/docs/validation.md) |
| 构建、安装、导出 C++ 动态采集素材 | [capture_cpp 使用说明](capture_cpp/README.md) |
| 查询 NV21 原生帧、H.265、触屏和文件收尾 | [MaixCDK 接口与实现](capture_cpp/docs/maixcdk-api.md) |
| 比较实测帧率、定位长间隔 | [旧版基线](capture_cpp/docs/performance.md) · [VPSS 取帧与 1.2.0 优化](capture_cpp/docs/pop-performance.md) · [计时字段](capture_cpp/docs/timing.md) · [离线统计脚本](capture_cpp/scripts/profile_capture.py) |
| 核对 1.3.0 应用闭环、首帧时间与最长间隔 | [25.11 fps 板端验收](capture_cpp/docs/board-acceptance-v1.3.0.md) · [改动依据](capture_cpp/docs/final-round.md) · [1.3.0 原件](evidence/capture_cpp/v1.3.0/) |
| 验证 C++ 安装包、视频尺寸和帧数 | [构建记录与板上验收](capture_cpp/docs/validation.md) |
| 查找历次原始样片、板端日志和校验清单 | [MaixPy 证据](evidence/board_capture/README.md) · [C++ 证据](evidence/capture_cpp/README.md) |
