# MaixCAM 比赛相机分辨率与开通道故障报告

**比赛继续用 1440×1080、BGR888、请求 30 fps、单缓冲。** 本次板测以这组参数运行 `camera_display`，1440×1080 能打开并正常出图，1280×960 在同样格式、帧率和缓冲配置下也通过。现有比赛 `camera.yaml` 与验证成功的采集配置一致。前一版以宽对齐为由更换分辨率的建议撤回，1280×960 和 1408×1056 的迁移方案作废。[比赛相机配置](../camera/config/camera.yaml)

板测结论来自现场反馈。比赛仓库以 `b95244b` 为更新基线。本地 MaixCDK 当前为 `83f8b8d4`，用 Git 直接读取 1440×1080 预设提交 `a761e44eab14` 和 1280×960 预设提交 `cb09f688881`。三个版本的 MaixCAM 相机后端、随附预编译库和 `camera_display` 示例内容相同。SDK 相对链接按两个仓库并排存放解析。板端程序实际构建所用的提交及 `.so` 版本未随实测反馈提供。[1440×1080 预设提交](https://github.com/shenghaotu81-droid/MaixCDK/commit/a761e44eab14ab79ad78d0ec44c76b41b3d763eb) · [1280×960 预设提交](https://github.com/shenghaotu81-droid/MaixCDK/commit/cb09f688881e9e33f9929d13bcc050ee7c992ee4)

## 1. 两个程序在同样宽高下走的路径

| 程序 | 请求格式 / 帧率 / 缓冲数 | 现场结果或仓库状态 |
|---|---|---|
| `camera_display` | BGR888 / 30 fps / 1（命令行覆盖示例默认值） | 1440×1080 和 1280×960 均打开并正常出图。[命令行参数与创建相机](../../../MaixCDK/examples/camera_display/main/src/main.cpp#L79) |
| `app_camera` | YVU420SP（NV21） / 默认 30 fps / 3 | 现场反馈在这两档均报 `mmf add vi channel failed`。如通过菜单切档，实际传入的宽高需用日志确认。[默认帧率](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L232) · [创建相机](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L312) |
| 比赛相机封装 | **BGR888 / 30 fps / 1** | 配置已与成功案例一致。封装先构造 SDK 对象，再以同一组参数显式 `open`。本次板测覆盖的是 SDK 示例的相机与显示链路。[配置](../camera/config/camera.yaml) · [封装入口](../camera/src/camera.cpp#L46) |

### 格式和缓冲怎样影响 VI / VPSS

1. **格式在建通道前就分流。** `Camera::open()` 把 BGR888 的 `_format_impl` 设为 RGB888，YVU420SP 则直接保留。传给 `mmf_add_vi_channel_v2` 的分别是 `PIXEL_FORMAT_RGB_888` 和 `PIXEL_FORMAT_NV21`。这里的格式是 VPSS 输出通道格式。传感器侧 VI 及 VPSS 输入格式由 `_mmf_vi_init` 按板型和传感器设置，另行传给 `mmf_vi_init_v2`。RGB→BGR 换序发生在成功读到驱动帧之后。[格式分支](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L789) · [格式枚举](../../../MaixCDK/components/vision/include/maix_image_def.hpp#L24) · [VI 输入格式](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L513) · [初始化 VI](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L741) · [读帧换序](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1076)
2. **缓冲数同时改变两级池配置。** `buff_num=1` 使 `priv->vi_pool_num` 和输出通道 `pool_num` 都取 1。传 3 时两者通常都取 3，SDK 另有条件满足时将通道池设为 4 的分支。前者在首次初始化传给 `mmf_vi_init_v2`，后者作为最后一项传给 `mmf_add_vi_channel_v2`。调用里的 `depth=2` 与 `pool_num` 是独立参数。随附 `libmaixcam_lib.so` 的 v2 调用链先设置并启用 VPSS 通道、绑定 VI，再按对齐后的宽高和格式计算缓冲块大小，以 `pool_num` 为块数创建池并绑定到通道（反汇编 `0x1ece4`、`0x1ed14`、`0x1ed66`–`0x1eda8`）。NV21 的帧布局为 Y 和交错 VU 两平面，RGB888 为单平面打包格式。NV21 单块通常更小，但三个 NV21 块的像素量约为一个 RGB888 块的 1.5 倍，VI 池也有独立开销。**缓冲申请或挂池失败是有源码依据的可能性，具体内存状态未知。** [池参数](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L848) · [v2 参数转发](../../../MaixCDK/components/maixcam_lib/include/maixcam/sophgo_middleware.hpp#L260) · [缓冲布局计算](../../../MaixCDK/components/3rd_party/sophgo-middleware/sophgo-middleware/v2/include/cvi_buffer.h#L121) · [随附库](../../../MaixCDK/components/maixcam_lib/lib_maixcam/libmaixcam_lib.so)
3. **64 像素是内部对齐粒度。** 本地库在 v2 通道设置前用 `(width+63)&~63` 对齐输出宽度（`0x1ec8e`、`0x1ec98`）。1440 的内部通道宽为 1472，1280 经过该步仍为 1280。SDK 读取时按请求宽高分配应用层 `Image`，BGR 分支每行按驱动报告的宽度取源行、写入请求宽度的像素。因此内部宽 1472 与应用交付宽 1440 可以同时成立，板测的正常画面支持继续采用现有读帧路径。实际驱动报告的宽和步长仍无现场记录。旧的外部缓冲容量检查发生在读帧阶段，而比赛封装使用无参读取。[SDK 默认库选择](../../../MaixCDK/components/maixcam_lib/CMakeLists.txt#L26) · [SDK 读帧分配](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1008) · [BGR 换序与逐行写入](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1076) · [比赛读帧](../camera/src/camera.cpp#L72)

### 故障定位的证据边界

- 新建相机时，`mmf_add_vi_channel_v2` 返回非零会抛出 `mmf add vi channel failed`，发生在 SDK 等首帧和应用创建显示器、编码器之前。随附库中停用通道、设置或启用通道、VI→VPSS 绑定、创建 VB 池和挂池均有失败路径。若对已打开的 SDK 对象调用 `open`，删除旧通道失败也复用同一条报错。`app_camera` 的菜单切档路径会反初始化应用后再创建相机。**目前只有上层错误字符串，无法定位到具体调用或返回码。** [SDK 开通道与首帧](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L860) · [app 初始化顺序](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L324) · [切档反初始化与重建](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L1084)
- **本地预设另有一个已确认的参数解析缺口。** 两个提交都只在预设表加了条目。菜单点击保存的是名称标签，`ui_get_resulution()` 缺少 `1.5MP(4:3)` 和 `1.2MP(4:3)`，走到 `resolution not support` 时仍返回 0、未填宽高。调用者把未经初始化的 `w/h` 打印为 `Resolution setting: ...` 后用于重建相机。若现场使用这两份构建并从菜单切档，菜单显示的尺寸不一定等于传入 SDK 的尺寸。是否触发了这条路径需要现场日志确认。[菜单标签](../../../MaixCDK/projects/app_camera/main/app/ui_screen.c#L925) · [选择回调](../../../MaixCDK/projects/app_camera/main/app/ui_event_handler.c#L1053) · [尺寸解析](../../../MaixCDK/projects/app_camera/main/app/ui_event_handler.c#L1263) · [切档传参](../../../MaixCDK/projects/app_camera/main/app/app.cpp#L779)
- **综合判断。** 相同请求尺寸已在比赛所用的 BGR888 / 单缓冲组合下出图，足以撤销分辨率迁移。源码确认 `app_camera` 的 NV21 / 三缓冲组合改变 VPSS 输出属性和两级缓冲池配置，是优先排查方向。现有对照同时改变了两个参数，尚未区分格式限制、池资源或两者交互。另需确认两次测试的 `raw`、切档方式和运行库版本。示例默认 `raw=true`，比赛封装固定为 `false`，`app_camera` 用 `capture_raw_enable`；SDK 的 raw 分支会把 VI 压缩模式设为 `COMPRESS_MODE_NONE`。若要判明根因，可在同版 `camera_display` 固定尺寸、30 fps 和 raw 开关，交叉比较 BGR888 / NV21 与 1 / 3 缓冲，并记录首条底层错误和返回码。[示例传参](../../../MaixCDK/examples/camera_display/main/src/main.cpp#L79) · [比赛封装](../camera/src/camera.cpp#L53) · [raw 对 VI 的影响](../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L741)

## 2. 现有比赛配置与管线的尺寸关系

| 位置 | 1440×1080 下的行为 | 源码依据 |
|---|---|---|
| 相机配置和默认值 | YAML 与 `CameraConfig` 的宽高均为 1440×1080。加载器逐字段覆盖默认值，格式白名单只接受 BGR888。封装按配置开相机、跳过 10 帧，再校验 SDK 返回的 `Image` 格式和宽高。30 fps 是请求值。 | [配置](../camera/config/camera.yaml) · [默认值](../camera/include/camera/config.hpp#L10) · [加载器](../camera/src/config_loader.cpp#L9) · [相机实现](../camera/src/camera.cpp#L53) |
| 比赛 app 和配置入口 | 当前分支 `vision/app/` 只有 README，顶层 CMake 只纳入 pipeline、controller、adapter。**无法在此仓库确认已部署 app 的配置路径、打包副本或命令行覆盖**。集成时核实相机实际加载的文件，以及传给 `Pipeline(config_dir)` 的目录。该目录在构造时一次性加载配置。 | [app 现状](../app/README.md) · [顶层构建](../CMakeLists.txt) · [Pipeline 构造](../pipeline/src/pipeline/pipeline.cpp#L19) |
| 标定文件和引用 | 当前 `pose.yaml` 相对自身目录引用 `calibration.yaml`，其中 `image_width/height` 已是 1440×1080。直接使用对应像素坐标系的 K 与畸变。是否适合比赛真机取决于相机、镜头、对焦和有效视场。加载器按文件读取 K，`reprojection_error` 仅记录标定结果。 | [当前标定](../pipeline/config/calibration.yaml) · [pose.yaml](../pipeline/config/pose.yaml) · [标定读取](../pipeline/src/config/pose/pose_config.cpp#L19) |
| Pipeline 和 PnP | **保留检查**。`process` 把 `input.frame.size()` 传给 MARK 和可选 TAG。已有 MARK 或已知边长的 TAG 到达 `estimate_planar` 时，尺寸不等或四角不合法均返回 `IMAGE_SIZE_OR_QUAD_INVALID`，该观测没有位姿。空 MARK 提前返回。TAG 缺物理边长先报 `METRIC_SIZE_MISSING_OR_INVALID`。因此空场无法验证标定接对。MARK 四点模型和单位、Tag ID 和实际边长均不随像素数缩放。 | [帧分发](../pipeline/src/pipeline/pipeline.cpp#L29) · [PnP](../pipeline/src/pose/estimation/pose_estimator.cpp#L18) · [四角边界与凸性](../pipeline/src/pose/validation/pose_geometry.cpp#L15) · [TAG 位姿](../pipeline/src/pose/tag/tag_detector.cpp#L112) |
| MARK 工作图和粗检 | 以 960×720 工作图粗检，原图仍保留 1440×1080。粗角分别按横纵比例及半像素中心换回原图。`approximation_epsilon=2.0` 以工作图像素计，短角片判据 `22/80` 是比例，两处白阈 200 是灰度值。 | [配置](../pipeline/config/detector.yaml) · [默认值](../pipeline/include/vision/config/detector_config.hpp) · [入口](../pipeline/src/detection/pipeline/detector.cpp#L33) · [resize](../pipeline/src/detection/preprocess/preprocess.cpp#L7) · [坐标换算](../pipeline/include/vision/types/prepared_frame_types.hpp#L33) · [粗匹配](../pipeline/src/detection/geometry/match_quad.cpp#L157) |
| MARK 原图精修与发布 | 有粗匹配才对原图分割并取逐点轮廓，同帧复用。碰原图边界的角片会被拒绝。拟合的切线半窗是 3 个轮廓索引，端部裁剪为 2 个像素弧长，采样探针也有下限。最终四角按原图边界检查并发布。镜像、翻转或换安装后须对照实物核验角序。 | [原图轮廓](../pipeline/src/detection/corners/corner_observation.cpp#L13) · [拟合](../pipeline/src/detection/corners/corner_edge_fit.cpp#L47) · [发布](../pipeline/src/detection/pipeline/decode_stage.cpp#L42) |
| AprilTag | 启用后直接把原帧转灰度，传实际 `cols/rows/step`，`quad_decimate=1`。当前白名单为空，默认不构造 TagDetector。 | [启用条件](../pipeline/src/pipeline/pipeline.cpp#L19) · [解码和角点](../pipeline/src/pose/tag/tag_detector.cpp#L28) |
| 跟踪、adapter、controller、电控 | G4 用米制位姿和实际时间差，量测协方差为基础 `camera_mse_m2` 加本帧位姿分歧。整条比赛链路的发送间隔需要独立测量。v0.3 速度指令名义 30 Hz、要求至少 20 Hz，超过 100 ms 未收到指令会触发控制侧超时。 | [量测](../pipeline/src/tracking/measurement/measurement_adapter.cpp#L22) · [噪声参数](../pipeline/config/tracking_profile.yaml) · [调参依据](../pipeline/docs/tuning-calibration.md) · [adapter](../adapter/config/adapter.yaml) · [controller](../controller/config/controller.yaml) · [协议 v0.3](../../protocol/Serial-Communication-Protocol-v0.3.md#L323) |
| 板端 display | `show()` 每次取得设备逻辑宽高。`render()` 把全帧铺满，按实际横、纵倍率缩放叠加坐标。1440×1080 到 Pro 的 640×480 屏两轴均为 4/9。app 交付原帧像素坐标，米制轨迹投影使用当前 K 和畸变。 | [display 接口](../display/include/display/display.hpp#L25) · [绘制](../display/src/render.cpp#L56) · [上屏](../display/src/display.cpp#L41) |

已检测到四角但标定尺寸不符时，跟踪器会跳过没有位姿的观测。尚未建轨时没有位置，已有轨迹可输出预测状态 `COASTING`。比赛整链验证时应同时核对四角、位姿和轨迹状态。相机帧借用 SDK 的 BGR 内存，顺序循环应在下一次读取前完成处理和显示。异步保存所需的 `clone()` 会增加一整帧内存，1440×1080 紧密 BGR 像素约为 4.45 MiB。[量测筛选](../pipeline/src/tracking/measurement/measurement_adapter.cpp#L22) · [建轨与预测](../pipeline/src/tracking/lifecycle/target_tracker.cpp#L70) · [帧的借用约定](../camera/include/camera/camera.hpp#L15)

### 比赛标定的适用条件

现有 K 的 `fx/fy ≈ 2394.37/2394.81`、`cx/cy ≈ 705.00/565.41` 来自作业标定录像，约 0.095 px 是那次拟合误差。保留分辨率本身无需改写标定宽高或缩放内参。若比赛板使用不同相机、镜头、焦距或有效视场，应在比赛采集路径下重新标定。旧参数对比赛板的光学适用性尚未核实。[标定报告](../pipeline/tools/calibration/REPORT.md) · [求解用 K 和畸变](../pipeline/src/pose/estimation/pose_estimator.cpp#L77)

若能确认使用同一镜头、焦距和源图，并已知旧像素到新像素的变换，可以从原标定推导新内参。若 `u′=sx·u+tx`、`v′=sy·v+ty`，则 `fx′=sx·fx`、`fy′=sy·fy`、`cx′=sx·cx+tx`、`cy′=sy·cy+ty`。归一化畸变系数可沿用。偏移应计入裁剪原点及采样约定，例如像素中心式 resize 对应 `tx=(sx−1)/2`。同为 4:3 只能确定输出比例，不能确定取景窗口或上述变换。派生 K 须用独立画面和已知几何验证，旧 RMSE 不能抄作新结果。[仓库的像素中心约定](../pipeline/include/vision/types/prepared_frame_types.hpp#L33) · [旧标定的适用范围](../pipeline/tools/calibration/REPORT.md)

现有标定工具可复用。它读视频元数据宽高，按给定帧号取原帧，检测对称圆点阵，调用 `calibrateCamera` 并输出 K、畸变、尺寸和 RMSE。为本次采集单独给出 `--config`，换成新录像及对应的 `frame_list`，用独立的 `--output` 保存比赛标定。示例 YAML 还指向旧 `src/tushenghao/...` 路径，旧帧号也专属旧录像。同一块 7×7、圆心距 0.03 m 的实物板可以沿用。换板时按实物填写。标定录像需保留送入管线的取景与尺寸，屏幕录制或带裁剪的录像需要另核对。[输入样例](../pipeline/tools/calibration/configs/calibration_input.yaml) · [配置覆盖](../pipeline/tools/calibration/input/calibration_config.cpp#L43) · [选帧](../pipeline/tools/calibration/frame_picker.md) · [视频尺寸](../pipeline/tools/calibration/frames/calibration_video.cpp#L6) · [求解和输出](../pipeline/tools/calibration/solution/calibration_result.cpp#L38)

## 3. 旧录像缩放对照

前一轮用仓库已有的 Release `build/detection_replay` 跑完 `marker_video.avi` 的 **1676 帧**，各输入尺寸使用同一份 `detector.yaml`（工作图 960×720）。FFmpeg 6.1.1 解码为 BGR24，原尺寸直接送入，其余用 `scale=W:H:flags=area`，通过管道传递无损 rawvideo。原尺寸检出的 1088 个帧号与归档中取得 MARK 位姿的 1088 个帧号逐一相同。下表记录历史回放数据。[录像清单](../pipeline/docs/homework-setup/data/raw/MANIFEST.md) · [回放工具和角点输出顺序](../pipeline/tests/validation/detection_replay.cpp#L12) · [原结果汇总](../pipeline/docs/homework-setup/outputs/marker/summary.yaml)

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

仅在共同检出的帧上按物理角序配对，将各角用像素中心映射还原到 1440×1080，统计相对原尺寸检测结果的欧氏位移。这些位移量化的是检测对软件重采样的敏感性。录像缺少逐帧真值，板端取景、开通道和持续帧率由各自的实测判断。

## 4. 离线回归、测试和文档

旧录像和离线回归继续使用对应的 1440×1080 标定。`marker_app --video` 固定取 `VISION_SOURCE_ROOT/config`，库调用者可以指定 `Pipeline(config_dir)`。如果比赛经独立采集得到新内参，可以通过比赛配置目录加载对应标定，保留现有离线基线。[离线入口](../pipeline/tools/apps/marker/main.cpp#L17) · [库接口](../pipeline/include/vision/pipeline.hpp#L43)

| 位置 | 如何处理 |
|---|---|
| 离线运行、叠加和导出 | 无需写死新宽高。解码出的 Mat 进入同一条管线，输出 AVI 按首帧 `image.size()` 和容器 FPS 开启。调试画布克隆原帧，绿框用原图角点，位姿和轨迹重投影依赖当前 K、畸变。新旧录像应各配自己的标定和输出目录。`offline.yaml` 只管输出，改它不能选择内参。[runner](../pipeline/src/app/offline/offline_runner.cpp#L21) · [绘制](../pipeline/src/app/rendering/debug_renderer.cpp#L42) · [导出](../pipeline/src/app/export/video_exporter.cpp#L6) · [离线配置](../pipeline/config/offline.yaml) |
| 相机默认值测试 | `config_load_test.cpp` 的生产默认值期望为 1440×1080。宽 960 的 partial 夹具刻意遗漏高，full 夹具显式测 1280×720，均是加载器行为测试。该套件只检查配置，不编译硬件读帧实现。[测试源码](../camera/tests/config_load_test.cpp#L13) · [夹具说明](../camera/tests/README.md) |
| 管线集成和离线链测试 | `pipeline_sequence` 和 `pipeline_tag_no_profile` 读旧 `marker_video.avi`，后者在 CMake 配置期复制源树的 calibration/detector。测试还显式断言首帧与标定等大。`offline_chain_test` 复制默认标定、用其 K 投影，却固定创建 1440×1080 合成视频并断言位姿与建轨。源树改默认时须隔离这些旧夹具，或把新测试的画布、投影、标定和期望一起迁移。[CTest 配置](../pipeline/tests/pipeline/CMakeLists.txt) · [尺寸断言](../pipeline/tests/pipeline/pipeline_api_test.cpp#L79) · [离线链](../pipeline/tests/app/offline/offline_chain_test.cpp#L15) |
| 算法夹具和验证工具 | `block3_fixture.hpp` 有固定 1440×1080 画布和历史角点。match_quad / edge_fit 另测不同工作尺寸。独立保留可作为坐标换算回归。`replay_metrics.py` 合成真值时读取默认 K 却写固定 1440×1080。`ippe_solution_count` 直接调 OpenCV 并从相邻 `pose.yaml` 取 K，未经过生产位姿求解器的尺寸检查。用于新录像时应配对尺寸、K 和真值。旧 frame784、冻结轮廓、按 1676 帧比较的脚本及跟踪回放仍属于旧数据。Pose 合约测试用所载 K 生成像点、读标定尺寸，可验证运算一致性。它不能验证实拍光学标定。[检测夹具](../pipeline/tests/support/detection/block3_fixture.hpp#L49) · [合成真值](../pipeline/tests/detection/replay_metrics.py#L128) · [IPPE 对照](../pipeline/tests/validation/ippe_solution_count.cpp#L18) · [旧帧对照](../pipeline/tests/validation/compare_pose_boundary.py#L21) · [Pose 夹具](../pipeline/tests/support/pose/pose_test_scene.hpp) |
| 显示和配置夹具 | `display/tests/render_test.cpp` 的 1440×1080 → 640×480、非等比输入和 ROI 是渲染样例。保持即可。配置测试中 960×720 是检测器缺省值，640×0 测标定宽高独立加载。只换相机输出无需更新这些断言。[显示测试](../display/tests/render_test.cpp#L65) · [显示测试说明](../display/tests/README.md) · [配置测试](../pipeline/tests/config/config_load_test.cpp#L16) |
| 面向当前使用者的说明 | 相机 README 和 display 示例里的输入 1440×1080 与比赛配置一致。比赛相机封装直接交付 BGR，`pipeline/README` 的 RGB→BGR 是早期接入规划。工作图 960×720 是粗检尺寸，640×480 是 Pro 的屏幕尺寸。[相机说明](../camera/README.md#L43) · [显示示例](../display/README.md#L74) · [管线 README](../pipeline/README.md#L45) |
| 历史数据与报告 | 已核对原始 AVI，标定录像 6966 帧、MARK 录像 1676 帧，均为 1440×1080，容器约 70.4 FPS。保留 `homework-setup` 的采集设置 YAML、截图、清单、实物尺寸图及 `outputs/marker` 的 observations/tracks/summary/overlay 等产物。标定工具的 README 样例、REPORT、acceptance、带日期的 evidence 记录旧采集。camera 的 review、read-refactor、test-report 及 display 的 review、pipeline 的配置/重构报告记录当时的实现与样例，可加适用版本说明，历史数字应保留。[录像清单](../pipeline/docs/homework-setup/data/raw/MANIFEST.md) · [采集元数据](../pipeline/docs/homework-setup/data/camera/capture_settings.yaml) · [旧输出](../pipeline/docs/homework-setup/outputs/marker/summary.yaml) · [标定样例](../pipeline/tools/calibration/README.md) · [验收说明](../pipeline/tools/calibration/docs/acceptance.md) · [标定快照](../pipeline/tools/calibration/docs/evidence/calibration_2026-10-11.yaml) · [相机审阅](../camera/docs/camera-review.md) · [相机测试记录](../camera/docs/test-report.md) · [显示审阅](../display/docs/display-review.md) |
