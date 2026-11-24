# MARK 视觉链：构建与运行

三 L＋短 M 结构匹配 → 屏幕排序与固定安装映射 → 原图两条 L2 边求角 → 平面 IPPE 名义位姿 → 相机系 CV 跟踪。构建之外的运行细节见[流程导读](docs/flow-guide.md)。

## 构建

从仓库根目录执行，依赖 C++17、CMake ≥3.16、OpenCV ≥4.6、AprilTag ≥3.3。

```bash
sudo apt-get install --no-install-recommends build-essential cmake libopencv-dev libapriltag-dev
cmake -S vision -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

按帧库 target `vision_pipeline` 始终构建。`VISION_BUILD_TOOLS=OFF` 关闭应用和标定工具，`BUILD_TESTING=OFF` 关闭测试；两者同时关闭只构建算法库。关掉的是三块：`tools/apps`（marker_app）、`tools/calibration`（标定工具）、`src/app`（vision_app：offline_runner 等）；其中 `src/app` 需两个开关都关才跳过。各模块公开接口在 `include/vision/`。

## 运行

```bash
build/marker_app --video vision/pipeline/docs/homework-setup/data/raw/marker_video.avi
```

命令行只接受 `--video VIDEO`。`marker_app` 从源树 `config/` 读取 `offline.yaml` 的输出目录、AVI 文件名和 overlay 开关。`run_offline` 把同一目录传给 Pipeline 构造函数，由它加载 `detector.yaml`、`pose.yaml`（含标定引用、四点模型和 Tag 配置）和 `tracking.yaml`（含 profile 引用）。

默认输出 `outputs/marker/{observations.yaml,tracks.yaml,summary.yaml,overlay.avi}`。已有目录可复用，同名文件覆盖；更改 `video_filename` 可保留不同叠加视频，文件名使用 `.avi`。`overlay: false` 关闭视频编码与渲染，位姿/跟踪/汇总仍写出。视频固定 MJPG，关闭编码器后从临时 AVI 发布。

配置文件中出现的字段覆盖结构体默认值，省略的字段保留默认值。主配置或所引用的文件打不开时抛出异常。加载器直接读取手填值，不检查字段类型、范围或 schema。终端输入、视频读取、跨模块输入和运行期数值安全检查保留。资源路径相对配置目录解析，输出目录相对运行工作目录解析。[加载器验证报告](docs/config-loader-report.md)

## 按帧调用（库接口）

调用方链接 `vision_pipeline`，包含 `<vision/pipeline.hpp>`。每条帧流构造一次 Pipeline，再按帧号顺序串行调用。下面的 `capture` 是已打开的视频，`fps` 是已校验的正帧率。

```cpp
mark::Pipeline pipeline(config_dir);
cv::Mat image;
for (uint64_t id = 0; capture.read(image); ++id)
{
    const auto us = std::llround(static_cast<long double>(id) * 1e6L / fps);
    const auto out = pipeline.process({image, id, us});
    // out.tracking 含轨迹、事件、NIS 和输入帧戳。out.poses 含 MARK 和 APRILTAG 观测。
}
```

输入图像为 CV_8UC3、BGR 顺序，时间戳单位为微秒。离线调用方 `src/app/offline/` 读视频并负责导出、叠加和汇报。板上 `vision/app/` 计划将相机 RGB888 转为 BGR，使用捕获时刻调用同一接口。每帧先返回 MARK 观测（缺检也有一条空观测），再追加检测到的 APRILTAG。跟踪器按配置筛选观测来源，当前 `tracking.yaml` 选择 MARK。

离线渲染和汇总在 `process()` 正常返回后读取 `last_detection()`。它借用本帧缓存，下次调用会覆盖内容，跨帧保存请复制。`pose_config()` 借用构造时加载的只读配置，复制其中的 `cv::Mat` 头仍共享矩阵数据，请保持只读。两个引用随 Pipeline 销毁而失效，读取与处理帧应串行。

## 使用边界

R=diag(2e-8,2e-8,2.5e-7) m²、q=0.5 m²/s³、初速方差=0.25 m²/s² 是**初估、待标定**，依据见[调参与标定](docs/tuning-calibration.md)。空 profile 可关闭数值跟踪。录像没有真实位置/速度 ground truth，不能证明板端精度；coast 是预测，相机系速度包含自车运动。

固定安装映射需赛前按实物核验，不支持跨 90° 翻转或上下带交换。核验步骤、输出字段、标定工具及图纸见[文档索引](docs/INDEX.md)。
