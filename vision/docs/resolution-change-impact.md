# MaixCAM 比赛相机更换分辨率影响报告

调研基线为比赛仓库 `8b99135e1c59` 和本地 MaixCDK `a761e44eab14` 的 MaixCAM / Pro 后端。SDK 源码链接按两个仓库在本机并排存放解析。核对范围包括源码、随附库、两段旧 AVI 的容器尺寸和旧 MARK 录像的离线对照。板上 1440×1080 打开失败来自现场反馈。本次未取得板端底层错误码，也未在板上测试候选档。[SDK 分支对应的提交](https://github.com/shenghaotu81-droid/MaixCDK/tree/a761e44eab14ab79ad78d0ec44c76b41b3d763eb)

**比赛相机实际交付的宽高，必须与用于位姿求解的标定匹配。** 只改相机 YAML，可能已经能看见图像和 MARK 绿框，但有四角时 PnP 仍会拒绝求解。MARK 工作图可以先保留 960×720，display 可以继续按屏幕实际尺寸渲染。建议首轮比较 **1408×1056、1280×960、960×720**，分别观察原图细节、处理开销和工作图同尺寸三种情况，暂不指定比赛档。[相机读取](../camera/src/camera.cpp#L72) · [管线入口](../pipeline/src/pipeline/pipeline.cpp#L29) · [位姿尺寸检查](../pipeline/src/pose/estimation/pose_estimator.cpp#L53)

## 1. 板上故障能从源码确认到哪一层

- 比赛封装先构造 SDK 对象（`open=false, raw=false`），再用配置宽高显式 `open`。MaixCAM 把请求的 BGR888 映射为 RGB888 硬件通道，向 `mmf_add_vi_channel_v2` 传入宽高。首次开通道返回非零，会抛出现场所见的 `mmf add vi channel failed`。这一步在预热、应用取帧、OpenCV 缩图和 display 之前。[比赛封装](../camera/src/camera.cpp#L46) · [SDK 格式与开通道](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L794) · [v2 转发](../../../MaixCDK/components/maixcam_lib/include/maixcam/sophgo_middleware.hpp#L264)
- Sophgo 示例把输出宽设为 `ALIGN(width, DEFAULT_ALIGN)`，高原样交给 VPSS，头文件定义 `DEFAULT_ALIGN=64`。本地 SDK 默认链接预编译库。只读反汇编也确认 `mmf_vi_aligned_width(int)` 返回 64，通道设置处 `0x1ec8e` 加 63、`0x1ec98` 清低 6 位。因此 1440×1080 对应 **1472×1080**。为这块比赛板选宽度为 64 倍数，可消除请求宽与对齐宽的差值。**仍不确定**本次失败的具体底层调用和返回码。该报错汇总了底层失败，单凭它也无法推断任一对齐档必能打开。示例 C 文件与实际 v2 库是不同实现。[示例通道设置](../../../MaixCDK/components/3rd_party/sophgo-middleware/sophgo-middleware/v2/sample/vio/sophgo_middleware.c#L472) · [对齐常量](../../../MaixCDK/components/3rd_party/sophgo-middleware/sophgo-middleware/v2/uapi/linux/cvi_defines.h#L63) · [库选择](../../../MaixCDK/components/maixcam_lib/CMakeLists.txt#L12) · [随附库](../../../MaixCDK/components/maixcam_lib/lib_maixcam/libmaixcam_lib.so)
- 开通道后还有独立的读帧问题。当前封装使用无参 `read()`。SDK 按请求宽高分配输出 `Image`，BGR 分支按驱动报告的 `u32Width × 3` 跨源行，逐像素换序并紧密写出。封装校验的 `Image` 宽高只能证明 SDK 对外的尺寸。源行距需用驱动的 `u32Stride[0]` 或带清晰竖线的实拍图核实。旧报告里的外部缓冲区容量错误发生在另一阶段，调大应用缓冲或在取帧后 resize 都无法处理本次启动失败。[SDK 取帧与分配](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1008) · [BGR 换序](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1076) · [读帧改造记录](../camera/docs/read-refactor-report.md)

## 2. 生产路径逐项影响

| 位置 | 换分辨率时的处理 | 源码依据 |
|---|---|---|
| 相机配置 | **必改比赛使用的 `width`、`height` 两项**。当前为 1440、1080。首次比较可固定 `BGR888 / fps=30 / buff_num=1 / skip_frames=10`，减少变量。30 是请求值。 | [camera.yaml](../camera/config/camera.yaml) |
| 相机默认值和加载器 | **比赛默认档也要迁移时，同步改 `CameraConfig`**。缺字段逐个回落到 1440 或 1080，直接默认构造也使用旧值。完整 YAML 显式给足两项就能覆盖。加载器只对白名单格式做检查，没有正数、4:3 或宽对齐校验，加载成功不能证明能开机。现有 `open/read` 按配置走，保留实际帧类型和宽高校验。 | [默认值](../camera/include/camera/config.hpp#L10) · [加载器](../camera/src/config_loader.cpp#L9) · [相机实现](../camera/src/camera.cpp#L53) |
| 比赛 app 和配置入口 | 当前分支 `vision/app/` 只有 README，顶层 CMake 只纳入 pipeline、controller、adapter。**无法在此仓库确认已部署 app 的配置路径、打包副本或命令行覆盖**。集成时核实相机实际加载的文件，以及传给 `Pipeline(config_dir)` 的目录。该目录在构造时一次性加载配置。 | [app 现状](../app/README.md) · [顶层构建](../CMakeLists.txt) · [Pipeline 构造](../pipeline/src/pipeline/pipeline.cpp#L19) |
| 标定文件和引用 | **比赛必须提供匹配新帧的 `camera_matrix`、`distortion_coefficients`、`image_width/height`**。当前 `pose.yaml` 相对自身目录引用 `calibration.yaml`。新比赛目录沿用文件名即可。另存为新文件名才需改比赛 `pose.yaml` 的引用。加载器不换算 K，也不使用 `reprojection_error` 求解。旧录像还依赖旧标定，见第 4 节。 | [当前标定](../pipeline/config/calibration.yaml) · [pose.yaml](../pipeline/config/pose.yaml) · [标定读取](../pipeline/src/config/pose/pose_config.cpp#L19) |
| Pipeline 和 PnP | **保留检查**。`process` 把 `input.frame.size()` 传给 MARK 和可选 TAG。已有 MARK 或已知边长的 TAG 到达 `estimate_planar` 时，尺寸不等或四角不合法均返回 `IMAGE_SIZE_OR_QUAD_INVALID`，该观测没有位姿。空 MARK 提前返回。TAG 缺物理边长先报 `METRIC_SIZE_MISSING_OR_INVALID`。因此空场无法验证标定接对。MARK 四点模型和单位、Tag ID 和实际边长均不随像素数缩放。 | [帧分发](../pipeline/src/pipeline/pipeline.cpp#L29) · [PnP](../pipeline/src/pose/estimation/pose_estimator.cpp#L18) · [四角边界与凸性](../pipeline/src/pose/validation/pose_geometry.cpp#L15) · [TAG 位姿](../pipeline/src/pose/tag/tag_detector.cpp#L112) |
| MARK 工作图和粗检 | **可先保留 960×720**，YAML 与 C++ 缺省值一致。入口接受非空二维 BGR `CV_8UC3`。预处理保留原图，另按配置 resize。粗角分别按横纵比例及半像素中心换回原图。即使选 960×720，现有流程仍调用 resize。工作图的 `approximation_epsilon=2.0` 以工作图像素计，短角片判据 `22/80` 是比例，两处白阈 200 是灰度值。固定工作尺寸时无需按相机比例换算 epsilon。若另调工作图，应复核 epsilon 和粗检耗时。曝光、插值会影响分割，阈值按样片复核。 | [配置](../pipeline/config/detector.yaml) · [默认值](../pipeline/include/vision/config/detector_config.hpp) · [入口](../pipeline/src/detection/pipeline/detector.cpp#L33) · [resize](../pipeline/src/detection/preprocess/preprocess.cpp#L7) · [坐标换算](../pipeline/include/vision/types/prepared_frame_types.hpp#L33) · [粗匹配](../pipeline/src/detection/geometry/match_quad.cpp#L157) |
| MARK 原图精修与发布 | **无固定帧尺寸要改，需用远端小 MARK 复测**。有粗匹配才对原图分割并取逐点轮廓，同帧复用。碰原图边界的角片会被拒绝。拟合的切线半窗是 3 个轮廓索引，端部裁剪为 2 个像素弧长，采样探针也有下限。低分辨率可能丢失边支持，即使粗框仍在。最终四角按原图边界检查并发布。单纯缩放保留 `physical_to_screen`。镜像、翻转或换安装后须对照实物核验角序。 | [原图轮廓](../pipeline/src/detection/corners/corner_observation.cpp#L13) · [拟合](../pipeline/src/detection/corners/corner_edge_fit.cpp#L47) · [发布](../pipeline/src/detection/pipeline/decode_stage.cpp#L42) |
| AprilTag | **检测代码无需改尺寸**。直接把原帧转灰度，传实际 `cols/rows/step`，`quad_decimate=1`。当前白名单为空，默认不构造 TagDetector。启用时原帧像素数影响计算量和远处码元的可辨性，需单独测检出率和位姿。改 MARK 工作尺寸不会改变 TAG 输入。 | [启用条件](../pipeline/src/pipeline/pipeline.cpp#L19) · [解码和角点](../pipeline/src/pose/tag/tag_detector.cpp#L28) |
| 跟踪、adapter、controller、电控 | **不改坐标单位、模型或协议字段**。G4 用米制位姿及实际时间差，量测协方差为基础 `camera_mse_m2` 加本帧位姿分歧。基础 R 原由焦距与像素误差估算，建议用新模式的静态位置散布和动态 NIS 复核。q 与初速方差无分辨率换算。adapter 的安装外参、控制增益在安装不变时无需随尺寸改。v0.3 速度指令名义 30 Hz、要求至少 20 Hz，超过 100 ms 未收到指令会触发控制侧超时。选档要看整链发送间隔。 | [量测](../pipeline/src/tracking/measurement/measurement_adapter.cpp#L22) · [跟踪配置](../pipeline/config/tracking.yaml) · [噪声参数](../pipeline/config/tracking_profile.yaml) · [调参依据](../pipeline/docs/tuning-calibration.md) · [adapter](../adapter/config/adapter.yaml) · [controller](../controller/config/controller.yaml) · [协议 v0.3](../../protocol/Serial-Communication-Protocol-v0.3.md#L323) |
| 板端 display | **渲染与面板尺寸无需改**。`show()` 每次取得设备逻辑宽高。`render()` 把全帧铺满，再用 `screen.width/frame.cols` 和 `screen.height/frame.rows` 分别缩放四角、跟踪点与速度箭头端点。Pro 的 640×480 是屏幕尺寸。4:3 原帧到该屏两轴同比，其他屏幕比例可能拉伸。app 应交付当前原帧像素坐标，米制轨迹投影时用当前 K、畸变。提前缩成屏幕坐标会再缩一次。文字和线宽用屏幕像素。 | [display 接口](../display/include/display/display.hpp#L25) · [绘制](../display/src/render.cpp#L56) · [上屏](../display/src/display.cpp#L41) |

现场排查时可以同时核对四角、位姿和轨迹状态。已检测到四角但标定尺寸不符时，跟踪器会跳过没有位姿的观测。尚未建轨时没有位置，已有轨迹可输出预测状态 `COASTING`。此时屏幕上的框或预测点都不能单独说明新标定已生效。相机帧借用 SDK 的 BGR 内存，顺序循环应在下一次读取前完成处理和显示。异步保存所需的 `clone()` 会增加一整帧内存，容量随表中尺寸变化。[量测筛选](../pipeline/src/tracking/measurement/measurement_adapter.cpp#L22) · [建轨与预测](../pipeline/src/tracking/lifecycle/target_tracker.cpp#L70) · [帧的借用约定](../camera/include/camera/camera.hpp#L15)

### 新标定怎样取得

现有 K 的 `fx/fy ≈ 2394.37/2394.81`、`cx/cy ≈ 705.00/565.41`，来自作业标定录像，约 0.095 px 是那次拟合误差。没有证据表明这就是比赛板上的传感器、镜头、对焦和有效视场。**优先在比赛硬件、最终开通道方式和最终尺寸下重新标定**。只改标定 YAML 的宽高能通过尺寸检查，却会让错误的射线进入 PnP。这时程序可能给出数值正常而有偏差的位姿。[标定报告](../pipeline/tools/calibration/REPORT.md) · [求解用 K 和畸变](../pipeline/src/pose/estimation/pose_estimator.cpp#L77)

若能确认使用同一镜头、焦距和源图，并已知旧像素到新像素的变换，可以从原标定推导新内参。若 `u′=sx·u+tx`、`v′=sy·v+ty`，则 `fx′=sx·fx`、`fy′=sy·fy`、`cx′=sx·cx+tx`、`cy′=sy·cy+ty`。归一化畸变系数可沿用。偏移应计入裁剪原点及采样约定，例如像素中心式 resize 对应 `tx=(sx−1)/2`。同为 4:3 只能确定输出比例，不能确定取景窗口或上述变换。派生 K 须用独立画面和已知几何验证，旧 RMSE 不能抄作新结果。[仓库的像素中心约定](../pipeline/include/vision/types/prepared_frame_types.hpp#L33) · [旧标定的适用范围](../pipeline/tools/calibration/REPORT.md)

现有标定工具可复用。它读视频元数据宽高，按给定帧号取原帧，检测对称圆点阵，调用 `calibrateCamera` 并输出 K、畸变、尺寸和 RMSE。为本次采集单独给出 `--config`，换成新录像及对应的 `frame_list`，用独立的 `--output` 保存比赛标定。示例 YAML 还指向旧 `src/tushenghao/...` 路径，旧帧号也专属旧录像。同一块 7×7、圆心距 0.03 m 的实物板可以沿用。换板时按实物填写。标定录像需保留送入管线的取景与尺寸，屏幕录制或带裁剪的录像需要另核对。[输入样例](../pipeline/tools/calibration/configs/calibration_input.yaml) · [配置覆盖](../pipeline/tools/calibration/input/calibration_config.cpp#L43) · [选帧](../pipeline/tools/calibration/frame_picker.md) · [视频尺寸](../pipeline/tools/calibration/frames/calibration_video.cpp#L6) · [求解和输出](../pipeline/tools/calibration/solution/calibration_result.cpp#L38)

## 3. 候选分辨率

精确 4:3 且宽按 64 对齐的整数尺寸可写成 **64n × 48n**。下表以旧请求 1440×1080 为计算基准。“边长”表示**同一视场、距离和姿态**下，一个标志边长能占原来多少像素。“面积”也等于紧密 BGR 帧字节数的比例。单帧用 `3×宽×高 / 2²⁰` MiB 计算，不含驱动池、工作图、轮廓、渲染和编码内存。实际耗时不能按面积直接换算。

| 候选（宽/64） | 边长 | 面积 | BGR 单帧 | 主要取舍 |
|---|---:|---:|---:|---|
| 1408×1056（22） | 97.8% | 95.6% | 4.25 MiB | 旧尺寸以下最接近的对齐 4:3 档。优先对照远处细角，带宽和内存节省很小。 |
| 1280×960（20） | 88.9% | 79.0% | 3.52 MiB | 少处理约 21% 原图像素，粗检仍缩到 960×720。适合作折中对照，高度 960 超过 SDK 的 720p 高帧率模式门槛。 |
| 1152×864（18） | 80.0% | 64.0% | 2.85 MiB | 介于 1280 档与工作图之间，还能在比工作图更大的原图上精修。 |
| 1024×768（16） | 71.1% | 50.6% | 2.25 MiB | 原图像素约减半。粗检前只略微缩小，原图角片的细节余量也较小。 |
| 960×720（15） | 66.7% | 44.4% | 1.98 MiB | 原图恰等于默认工作图，坐标倍率为 1。全帧 BGR 换序、原图精修、TAG 工作量较低，远处边缘和码元需实测。 |
| 640×480（10） | 44.4% | 19.8% | 0.88 MiB | 与 Pro 逻辑屏同尺寸，适合测通道和低负载下限。默认 MARK 粗检会放大 1.5 倍，插值无法恢复细节。 |
| 1472×1104（23） | 102.2% | 104.5% | 4.65 MiB | 旧尺寸以上最接近的对齐 4:3 档。比旧帧更重，仅在传感器有效高度和 VPSS 资源有余量时值得测试。现场的 1472×1080 不满足 4:3。 |

**如何选档。** 关注最远工作距离时先比较 1408 与 1280。循环耗时吃紧时加测 960。若 960 的角片支持不足而 1280 的发送间隔过长，1152 或 1024 提供中间档。固定 960×720 工作图时，降相机分辨率主要减少 SDK 换序、原图精修和可选 TAG 的像素量，粗检仍扫 691,200 像素。是否切换传感器模式、裁掉多少视场、检测率以及持续帧率都需实测。[工作图生成](../pipeline/src/detection/preprocess/preprocess.cpp#L7) · [SDK 传感器模式选择](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L572)

### 同一段录像的实测对照

用仓库已有的 Release `build/detection_replay` 跑完 `marker_video.avi` 的 **1676 帧**，各档使用同一份 `detector.yaml`（工作图 960×720）。FFmpeg 6.1.1 解码为 BGR24，原尺寸直接送入，其余用 `scale=W:H:flags=area`，通过管道传递无损 rawvideo。原尺寸检出的 1088 个帧号与归档中取得 MARK 位姿的 1088 个帧号逐一相同。[录像清单](../pipeline/docs/homework-setup/data/raw/MANIFEST.md) · [回放工具和角点输出顺序](../pipeline/tests/validation/detection_replay.cpp#L12) · [原结果汇总](../pipeline/docs/homework-setup/outputs/marker/summary.yaml)

| 输入尺寸 | 检出帧 / 1676 | 相对原尺寸少检 / 新增帧 | 对应角点位移中位数 / P95（原图 px） |
|---|---:|---:|---:|
| 1440×1080 原录像 | 1088 | 基准 | 0 / 0 |
| 1408×1056 | 1088 | 0 / 0 | 0.53 / 1.12 |
| 1280×960 | 1088 | 0 / 0 | 0.57 / 1.22 |
| 1152×864 | 1088 | 0 / 0 | 0.71 / 1.39 |
| 1024×768 | 1088 | 0 / 0 | 0.83 / 1.71 |
| 960×720 | 1088 | 0 / 0 | 0.92 / 1.91 |
| 640×480 | 1086 | 2 / 0 | 1.70 / 3.24 |
| 1472×1104 | 1088 | 0 / 0 | 0.49 / 1.11 |

仅在共同检出的帧上按物理角序配对，将各角用像素中心映射还原到 1440×1080，统计相对原尺寸检测结果的欧氏位移。**1408、1280、960 三档检出的帧完全相同，角点偏移却可区分。** 这些位移反映对重采样的敏感性，原图角点本身也有误差。录像缺少逐帧真值，故表中计数不能解释为召回率。1472 档是从旧素材放大而来。这个对照没有测到新硬件视场、位姿绝对误差、TAG 表现或板端吞吐。

## 4. 离线回归、测试和文档

**保留旧录像与旧标定的配对。** 当前 `marker_app --video` 固定取 `VISION_SOURCE_ROOT/config`，没有命令行配置目录选项。库本身可以接收不同的目录。比赛集成使用独立配置目录，是保住现有离线基线、同时使用真机标定的低改动方案。若决定更换源树默认标定，则应先为下列旧数据消费者指定旧配置，否则会同时改变比赛和回归的含义。[离线入口](../pipeline/tools/apps/marker/main.cpp#L17) · [库接口](../pipeline/include/vision/pipeline.hpp#L43)

| 位置 | 如何处理 |
|---|---|
| 离线运行、叠加和导出 | 无需写死新宽高。解码出的 Mat 进入同一条管线，输出 AVI 按首帧 `image.size()` 和容器 FPS 开启。调试画布克隆原帧，绿框用原图角点，位姿和轨迹重投影依赖当前 K、畸变。新旧录像应各配自己的标定和输出目录。`offline.yaml` 只管输出，改它不能选择内参。[runner](../pipeline/src/app/offline/offline_runner.cpp#L21) · [绘制](../pipeline/src/app/rendering/debug_renderer.cpp#L42) · [导出](../pipeline/src/app/export/video_exporter.cpp#L6) · [离线配置](../pipeline/config/offline.yaml) |
| 相机默认值测试 | 若同步改 C++ 缺省值，更新 `config_load_test.cpp` 显式列出的默认期望及相关文字。宽 960 的 partial 夹具刻意遗漏高，full 夹具显式测 1280×720，均是加载器行为测试。格式错误、空文件等夹具与上板选档无关。该套件不编译硬件读帧实现。[测试源码](../camera/tests/config_load_test.cpp#L13) · [夹具说明](../camera/tests/README.md) |
| 管线集成和离线链测试 | `pipeline_sequence` 和 `pipeline_tag_no_profile` 读旧 `marker_video.avi`，后者在 CMake 配置期复制源树的 calibration/detector。测试还显式断言首帧与标定等大。`offline_chain_test` 复制默认标定、用其 K 投影，却固定创建 1440×1080 合成视频并断言位姿与建轨。源树改默认时须隔离这些旧夹具，或把新测试的画布、投影、标定和期望一起迁移。[CTest 配置](../pipeline/tests/pipeline/CMakeLists.txt) · [尺寸断言](../pipeline/tests/pipeline/pipeline_api_test.cpp#L79) · [离线链](../pipeline/tests/app/offline/offline_chain_test.cpp#L15) |
| 算法夹具和验证工具 | `block3_fixture.hpp` 有固定 1440×1080 画布和历史角点。match_quad / edge_fit 另测不同工作尺寸。独立保留可作为坐标换算回归。`replay_metrics.py` 合成真值时读取默认 K 却写固定 1440×1080。`ippe_solution_count` 直接调 OpenCV 并从相邻 `pose.yaml` 取 K，未经过生产位姿求解器的尺寸检查。用于新录像时应配对尺寸、K 和真值。旧 frame784、冻结轮廓、按 1676 帧比较的脚本及跟踪回放仍属于旧数据。Pose 合约测试用所载 K 生成像点、读标定尺寸，可验证运算一致性。它不能验证实拍光学标定。[检测夹具](../pipeline/tests/support/detection/block3_fixture.hpp#L49) · [合成真值](../pipeline/tests/detection/replay_metrics.py#L128) · [IPPE 对照](../pipeline/tests/validation/ippe_solution_count.cpp#L18) · [旧帧对照](../pipeline/tests/validation/compare_pose_boundary.py#L21) · [Pose 夹具](../pipeline/tests/support/pose/pose_test_scene.hpp) |
| 显示和配置夹具 | `display/tests/render_test.cpp` 的 1440×1080 → 640×480、非等比输入和 ROI 是渲染样例。保持即可。配置测试中 960×720 是检测器缺省值，640×0 测标定宽高独立加载。只换相机输出无需更新这些断言。[显示测试](../display/tests/render_test.cpp#L65) · [显示测试说明](../display/tests/README.md) · [配置测试](../pipeline/tests/config/config_load_test.cpp#L16) |
| 面向当前使用者的说明 | 确定新默认后，同步相机 README 的宽高、FPS 表述及 `config.hpp` 的 FPS 注释。display 头文件和 README 示例都写了 1440×1080，README 还举了 4/9 比例。保留示例时注明属于旧输入。`flow-guide` 的配置表应注明是哪套标定。`pipeline/README` 的 RGB→BGR 规划与当前相机封装直接交付 BGR 有差异。工作图 960×720、面板 640×480 各有独立含义。[相机说明](../camera/README.md#L43) · [显示契约注释](../display/include/display/display.hpp#L25) · [显示示例](../display/README.md#L74) · [管线导读](../pipeline/docs/flow-guide.md#L47) · [管线 README](../pipeline/README.md#L45) |
| 历史数据与报告 | 已核对原始 AVI，标定录像 6966 帧、MARK 录像 1676 帧，均为 1440×1080，容器约 70.4 FPS。保留 `homework-setup` 的采集设置 YAML、截图、清单、实物尺寸图及 `outputs/marker` 的 observations/tracks/summary/overlay 等产物。标定工具的 README 样例、REPORT、acceptance、带日期的 evidence 记录旧采集。camera 的 review、read-refactor、test-report 及 display 的 review、pipeline 的配置/重构报告记录当时的实现与样例，可加适用版本说明，历史数字应保留。[录像清单](../pipeline/docs/homework-setup/data/raw/MANIFEST.md) · [采集元数据](../pipeline/docs/homework-setup/data/camera/capture_settings.yaml) · [旧输出](../pipeline/docs/homework-setup/outputs/marker/summary.yaml) · [标定样例](../pipeline/tools/calibration/README.md) · [验收说明](../pipeline/tools/calibration/docs/acceptance.md) · [标定快照](../pipeline/tools/calibration/docs/evidence/calibration_2026-10-11.yaml) · [相机审阅](../camera/docs/camera-review.md) · [相机测试记录](../camera/docs/test-report.md) · [显示审阅](../display/docs/display-review.md) |

## 5. 用本地 MaixCDK 缩小选档范围

SDK 的尺寸查询有两处。全局 `camera::get_sensor_size()` 按识别到的型号查表，例如 GC4653/OS04A10 返回 2560×1440，GC02M1/OV2685 返回 1600×1200。打开后的 `cam.get_sensor_size()` 按本次选中的传感器模式取尺寸。把这两项和 `cam.width()/height()` 的输出通道尺寸分别记录，可区分传感器采样尺寸与 VPSS 输出尺寸。若实物恰是 1600×1200 模式，这也是可加测的原生比例候选（宽为 25×64，单帧 BGR 5.49 MiB）。型号和模式尚不确定。[型号尺寸表](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L395) · [实例尺寸查询](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1822)

1. **先用已有示例筛掉开不了的档。** 本地 `examples/camera_display` 支持从命令行传宽、高、格式、FPS、缓冲数、raw、阻塞和超时。在板上已有同版本示例的前提下，`./camera_display 1408 1056 1 30 1 0 1 -1` 可作第一组，替换前两项即可比较其余候选。该版本格式枚举 1 是 BGR888。示例会开屏、跳过 5 帧并连续读取。比赛封装跳过 10 帧，示例循环耗时还包含显示。另一个 `projects/app_camera` 的本地分支虽把 1440×1080 列入菜单，实际构造时请求 YVU420SP、3 缓冲并接编码器，菜单项不能证明比赛的 RGB 通道可用。[示例参数与取帧](../../../MaixCDK/examples/camera_display/main/src/main.cpp#L79) · [格式枚举](../../../MaixCDK/components/vision/include/maix_image_def.hpp#L24) · [相机应用预设](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L100) · [相机应用初始化](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L324)
2. **实测视场后再判断能否共用一套标定。** 记录传感器名、板端 SDK/固件、实际传感器模式、镜像/翻转和两档画面里同一组固定标记的位置。SDK 按传感器、宽高和请求 FPS 选模式，例如 GC4653 在宽 ≤1280、高 ≤720 且请求 >30 FPS 才走 720p 高速分支。小图本身不保证高帧率。开通道传 `fit=2`，图像 Fit 枚举中 2 表示 cover，实际生效的裁剪区域仍需验证。若后续需要明确可重复的 4:3 视场，可以研究在可打开的通道上设固定 ROI 并按该 ROI 标定。MaixCAM 的 `set_windowing` 要求先打开相机，窗口宽也须为 64 的倍数，不能用它挽救已经失败的 `open`。[传感器识别及尺寸](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L395) · [GC4653 模式选择](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L609) · [通道 fit 参数](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L879) · [Fit 枚举](../../../MaixCDK/components/vision/include/maix_image_def.hpp#L243) · [窗口实现](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1777)
3. **按比赛链路定档。** 尺寸初筛记录开通道结果和底层错误码、预热后首帧、持续读帧失败次数、实际宽高/步长、色序和四边取景。对通过的档位，在相同灯光、距离、姿态下分别记录 MARK 粗检与四角成功率、启用时的 TAG 成功率、用对应标定得到的已知距离偏差及静态抖动。比较关闭显示和比赛显示频率下的取帧、管线、整循环耗时和实际发送间隔，关注长尾及连续丢帧。旧录像在内存中缩小只能估计采样损失。传感器裁剪、通道资源、板端延迟和最终比赛档仍以板测为准。[相机时间戳](../camera/src/camera.cpp#L87) · [标定与精度核验依据](../pipeline/docs/tuning-calibration.md) · [速度指令时序](../../protocol/Serial-Communication-Protocol-v0.3.md#L323)
