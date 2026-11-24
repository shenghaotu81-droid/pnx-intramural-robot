# Pipeline 第二步实施报告

逐帧算法入口已移入 `mark::Pipeline::process`。离线程序通过同一实例处理整段视频。旧版两次、新版一次的 1676 帧回放均与 Git 保存的基线相符。以下路径相对 `vision/pipeline/`。

## 做了什么

| 文件和行号 | 职责 |
|---|---|
| `src/pipeline/pipeline.cpp:9–46` | 构造时各加载一次 detector、pose、tracking 配置，白名单非空才创建 Tag 检测器。每帧先检测，再生成至少一条 MARK 观测，追加 Tag，最后用完整观测列表推进跟踪。 |
| `include/vision/pipeline.hpp:49–64` | 两个 getter 分别借出渲染配置与本帧检测缓存。求解器取得配置副本。调用方串行读写实例。 |
| `tools/apps/marker/main.cpp:18–22`、`include/vision/config/app_config.hpp:2–20` | main 从 offline.yaml 加载输出选项，AppConfig 仅保存这些选项，头文件显式包含 filesystem。 |
| `src/app/offline/offline_runner.cpp:22–31,50–83`、`src/app/CMakeLists.txt:4–10` | runner 在循环外构造 Pipeline，按原公式计算微秒时刻，将输入帧戳用于观测导出。汇总、轨迹文件和渲染分别消费本帧检测、位姿与完整跟踪结果。vision_app 链接 vision_pipeline。 |
| `tests/app/offline/offline_chain_test.cpp:14–17,66–68,100–102` | 临时算法配置目录补 detector.yaml，两处调用只传离线输出选项。 |
| `tests/CMakeLists.txt:10`、`tests/pipeline/CMakeLists.txt:1–15`、`tests/pipeline/pipeline_api_test.cpp:23–71` | 增加两个直接调用库的 CTest 用例。Tag 用例在构建目录组合测试用 pose/tracking YAML 与现有标定、检测配置。 |
| `README.md:24,30–47`、`docs/flow-guide.md:5–30,45,66`、`docs/INDEX.md:3–5` | 给出按帧调用示例，标明配置归属、借用期限、离线读写职责和板上调用方的接入状态。 |

## 验证结果

| 项目 | 实测结果 |
|---|---|
| 默认 Release 构建 | 通过。从仓库根运行 `cmake -S vision -B build`、`cmake --build build`、`ctest --test-dir build --output-on-failure`，13/13 通过（原 11 项加新 2 项）。 |
| 独立链接与短序列 | 通过。`pipeline_api_test` 只链接 vision_pipeline。一次构造处理三帧，验证建轨事件、第二帧 NIS 和速度、缺检帧的空 MARK 与 COASTING。检测缓存、观测、跟踪输出的帧号和时刻均匹配输入，输入像素差为零。见 `tests/pipeline/CMakeLists.txt:1–6` 和 `tests/pipeline/pipeline_api_test.cpp:23–56`。 |
| Tag 与空 profile | 通过。ID 0 在白名单内且没有边长时，Tag 图产生两条观测，空 MARK 在前，APRILTAG 在后，后者带 METRIC_SIZE_MISSING_OR_INVALID。真实 MARK 帧仍有位姿，两帧跟踪都接受输入且轨迹为空；单独运行并比对 stdout，提示恰好一行。见 `tests/pipeline/tag/pose.yaml:5`、`tests/pipeline/tag/tracking.yaml:4`、`tests/pipeline/pipeline_api_test.cpp:59–71`、`src/pipeline/pipeline.cpp:9–25`。 |
| 仅关工具 | 通过。单独配置 `VISION_BUILD_TOOLS=OFF`、`BUILD_TESTING=ON`，完整构建包含 vision_app，CTest 13/13 通过。开关见 `src/app/CMakeLists.txt:1–6`。 |
| 工具与测试都关 | 通过。独立主机 CMake 消费者引入 vision 工程，配置时断言 vision_app、marker_app、vision_calibration、calibration_tool 四个 target 均缺席。消费者只链接 vision_pipeline，实际处理一帧并核对跟踪接受标志、空 MARK 观测和缓存帧号。对应 `CMakeLists.txt:15–22`、`tools/CMakeLists.txt:1–4`。 |
| 1676 帧回放 | 通过。旧版取自父提交 `99d95d5`。同机使用相同视频与配置，在三个独立工作目录运行旧、旧、新三个回放。三次均为 MEASURED 1088、COASTING 588、TRACK_CREATED 1，汇总与 `docs/homework-setup/outputs/marker/summary.yaml:3–30` 相符。 |

分别将三次运行的输出与 `git show 99d95d5:vision/pipeline/docs/homework-setup/outputs/marker/<文件名>` 返回的原始字节比较。observations.yaml、tracks.yaml、overlay.avi 全部相同；summary.yaml 只剔除唯一的 `elapsed_s` 行，其余字节相同。三个完整文件的 SHA-256 如下。

| 文件 | SHA-256 |
|---|---|
| observations.yaml | `5286f978ce96dc23ee8f626e654c547099f6481c5a9049e95f7772c85d10d2b8` |
| tracks.yaml | `c4953ba538ea28d381050f660406d25ed4825a241b149145ae4c6ccb3591a187` |
| overlay.avi | `29145bd7d42d86744578c65af97924ab6ffb1c42c94ff4de4708a4f0c655c8d8` |

## 与计划的出入

- 清理 AppConfig 的间接 include 还涉及配置测试。它直接调用检测配置 loader，已在 `tests/config/config_load_test.cpp:1,15` 给该调用补上自己的头文件依赖。
- 实施指令引用的提示在 profile 后多一个空格。沿用父提交 `99d95d5` 的 `src/app/offline/offline_runner.cpp:44` 原文，现位于 `src/pipeline/pipeline.cpp:13`。
- 本机基线 YAML 工作树为 CRLF，Git 对象为 LF（仓库根 `.gitattributes:5`）。严格字节比对采用 Git 对象，并交叉核对同机旧版输出。生成文件未经换行转换。

## 顺手修正

`tools/INDEX.md:9–13`、`docs/flow-guide.md:37–40` 的旧路径改为当前构建入口和素材位置。`tools/apps/marker/main.cpp:1,19`、`src/app/CMakeLists.txt:4` 的注释改为实际调用关系。README 中的“只跟踪 MARK”限定为当前 tracking.yaml 的来源设置，板上 app 标为计划调用方（`README.md:45`）。

## 严重问题与遗留事项

本次构建、测试和回放均通过。板上相机接入、MaixCDK 依赖与实机验证仍由后续板上工程完成，当前 app 状态见 `../app/README.md:9–11`。
