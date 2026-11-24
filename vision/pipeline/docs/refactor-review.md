# Pipeline 搬链方案评审

## 结论

**基本可行需改动：保留“Pipeline 串算法、offline_runner 管读写”的方案和两个 getter，补齐头文件依赖、测试配置夹具、帧戳留存及验收覆盖后即可实施，不需要增加框架或再拆一层。**

评审依据为当前工作树：分支 `refactor/vision-pipeline`，HEAD `dfef42f`。开始评审时 `vision/pipeline/README.md` 已有未提交修改，本文引用其工作树行号，未改动该文件。本次仅静态阅读代码和文档；没有构建、运行测试或回放，以遵守“只允许写报告”的要求。背景中的 11/11 是已有记录，不是本次验证结果。

下文除明确写出的外部路径外，文件路径均相对仓库根目录；行号为评审时版本。

## 逐项回答

### 1. 可行性：主线正确，六步清单需要补全

现有四段算法确实集中在 `vision/pipeline/src/app/offline/offline_runner.cpp:73–102`，所需输入只有图像、帧号、时间戳和持久算法对象。`PipelineInput` / `PipelineOutput` 已覆盖这条链的输入输出（`vision/pipeline/include/vision/pipeline.hpp:19–35`）。不必把导出和渲染搬进库。

逐步判断：

| 原步骤 | 判断与必要补充 |
|---|---|
| 1. 加 getter 和成员 | 成立。`pose_config_` 必须声明在 `solver_` 前面；`last_detection_` 保存最近一次检测结果即可。依据：`vision/pipeline/include/vision/pipeline.hpp:52–56`、`vision/pipeline/include/vision/pose/pose_estimator.hpp:13–14`。 |
| 2. 实现构造与 process | 成立，但要在初始化列表里构造算法成员，保留 MARK 空观测、TAG 顺序和每帧 tracker.step。原链见 `vision/pipeline/src/app/offline/offline_runner.cpp:33–45,79–102`。 |
| 3. 改 offline_runner | 成立。Pipeline 在循环外构造一次；循环内保留时间戳生成、观测文件帧戳、各项汇总、导出和渲染。依据：同文件 `62–127`。 |
| 4. vision_app 链接 vision_pipeline | 正确。新 target 已公开链接 detection/pose/tracking/config；依赖方向应是 app → pipeline → 算法库。原直接链接留着也合理，renderer 本身仍使用 pose/tracking 函数。依据：`vision/pipeline/src/pipeline/CMakeLists.txt:2–6`、`vision/pipeline/src/app/CMakeLists.txt:5–9`、`vision/pipeline/src/app/rendering/debug_renderer.cpp:42,62`。 |
| 5. 构建、测试、回放 | 必要但不完整，需补 Tag 集成、无 profile、独立库消费者和双开关关闭构建，详见第 6 项。 |
| 6. 提交 | 应在代码、文档与验收完成后实施。本次任务只评审，不提交。 |

**决议 C 不能只写在“背后决议”里，应并入第 3 步的实际改动清单。** 否则 main 仍加载一次 detector 配置，而 Pipeline 再加载一次。实际读取点是 `vision/pipeline/tools/apps/marker/main.cpp:21–22`，旧消费点是 `vision/pipeline/src/app/offline/offline_runner.cpp:33`。

“不碰 config 四个库”也要说准确：**不改四个库的算法与 loader 实现，但允许清理 `include/vision/config/app_config.hpp` 的应用组合字段。** 若把“不碰 config”解释成任何文件都不能改，就与决议 C 自相矛盾。字段确实位于该头文件 `15–18`，不需要改配置解析实现。

### 2. 漏洞与遗漏：帧戳、初始化、Tag 条件和提示语

**除 raw 和 pose_config，还要保留离线文件的 stamp。** 观测文件在 `vision/pipeline/src/app/offline/offline_runner.cpp:115–119` 单独写帧戳；它不是只遍历 poses。建议 offline_runner 从本次 `PipelineInput.frame_id / timestamp_us` 构造这个小值对象。也可用 `output.tracking.input_stamp_`，但直接取输入更直观，导出无需依赖跟踪结果。类型已经提供该字段（`vision/pipeline/include/vision/types/tracking_types.hpp:33–39`）。

尤其不要从 `last_detection()` 反推帧戳：Detector 遇到非法输入会返回默认 FrameResult，未填本次帧号与时间戳（`vision/pipeline/src/detection/pipeline/detector.cpp:35–50`），旧链一直从输入造 stamp（`vision/pipeline/src/app/offline/offline_runner.cpp:79–82`）。

其余离线需要的东西都拿得到：

- `summary.add` 需要检测数、状态和 diagnostics，由 getter 提供（`vision/pipeline/src/app/reporting/run_report.cpp:7–17`）。
- 位姿计数用 `output.poses`；轨迹计数、事件和 tracks.yaml 用完整的 `output.tracking`，不能只取 `tracks_` 后丢掉 events/NIS（`vision/pipeline/src/app/offline/offline_runner.cpp:93–119`；`vision/pipeline/include/vision/types/tracking_types.hpp:33–39`）。
- 原图、FPS、输出路径、VideoExporter、计时留在 offline_runner，位置见同文件 `26–31,47–62,122–140`。

**构造顺序按成员声明顺序执行，不按初始化列表的书写顺序。** 推荐顺序为 `detector_ → pose_config_ → solver_ → tag_detector_ → tracker_ → last_detection_`。`solver_(pose_config_)` 传副本，不能 `std::move(pose_config_)`，否则 getter 保存的配置可能被掏空。PoseEstimator 按值接收后移入自身，并克隆 K/dist（`vision/pipeline/src/pose/estimation/pose_estimator.cpp:10–14`）。

Tag 建议直接在初始化列表按以下条件创建：

```cpp
tag_detector_(pose_config_.tag_.allowed_ids_.empty()
                  ? nullptr
                  : std::make_unique<pose::TagDetector>(pose_config_.tag_))
```

这与旧链 `vision/pipeline/src/app/offline/offline_runner.cpp:38–40` 一致，也保持 Tag 在加载 tracking 配置前构造。放到构造函数体里创建也能工作，但那时 tracker 已构造完成，初始化失败顺序会变化。**不能改成按 sizes 是否为空启用**：缺边长仍应解码并输出无米制估计的 Tag 观测（`vision/pipeline/src/pose/tag/tag_detector.cpp:107–117`）。

**“Tracking profile 未配置”提示搬进构造可以接受，暂不值得为一句话引入日志回调。** 但不能先在函数体里读局部 tracking_config 再赋值 tracker：TargetTracker 无默认构造且禁拷贝，见 `vision/pipeline/include/vision/tracking/target_tracker.hpp:12–18`。最小实现是在 pipeline.cpp 的匿名命名空间放一个加载辅助函数：加载 tracking.yaml → 无 profile 时打印原提示 → 返回配置；`tracker_` 在初始化列表调用它。这样只读一次，且提示仍发生在 tracker 构造前，与旧链 `offline_runner.cpp:42–45` 一致。

长期从职责看，提示应由调用方决定；如果以后确需库完全静默，再加一个简单的只读状态查询给 app 使用即可。这是后续选择，不要求第二步加第三个 getter、日志框架或重读 tracking.yaml。

### 3. getter 设计：成立，简单且够用

**A、B 都可以采用。** 当前诊断消费者明确，没必要为了 raw 增加回调、观察者或第二套结果协议。

`last_detection_` 是结果缓存，不是离线导出设施。FrameResult 只包含帧戳、状态、检测数组和诊断字符串，不持有整张图（`vision/pipeline/include/vision/types/detector_types.hpp:30–37`）。在每次 process 中用检测结果覆盖它，然后直接给位姿求解使用，避免另存一份 raw。

渲染实际读取 `raw.frame_id_` 和每个 detection 的 `corners_`；配置读取 MARK 模型点、K 和 dist，用于 MARK 与轨迹投影（`vision/pipeline/src/app/rendering/debug_renderer.cpp:24–35,44–52,69–86`）。汇总还读取 status 和 diagnostics，所以只暴露角点不能替代完整 FrameResult（`vision/pipeline/src/app/reporting/run_report.cpp:7–17`）。

`pose_config_` 与 solver 内的配置重复是合理的小成本。现有 PoseEstimator 无配置 getter（`vision/pipeline/include/vision/pose/pose_estimator.hpp:12–35`），现有 offline_runner 本来就保留配置再传给 solver（`vision/pipeline/src/app/offline/offline_runner.cpp:35–36`）。这次只是把相同所有权关系挪到 Pipeline 内。

getter 的约定建议写成四句话：

1. `last_detection()` 在本次 process 正常返回后立即读取；第一次调用前没有有效帧结果。
2. 下一次 process 会覆盖缓存。指向 FrameResult 对象本身的引用不因赋值自动悬空，但内容会变；其内部 vector 元素的引用/迭代器可能失效。要跨帧保存就按值拷贝。
3. Pipeline 销毁后两个引用都失效；同一个实例串行调用，不能一边 process 一边从另一线程读取 getter。
4. `pose_config()` 返回借用的只读配置。不要通过复制出来的 cv::Mat 头去写它的底层矩阵。

第 4 点不是要求增加深拷贝 getter：PoseConfig 含 `cv::Mat`（`vision/pipeline/include/vision/config/pose_config.hpp:11–16`），solver 构造已单独 clone（`vision/pipeline/src/pose/estimation/pose_estimator.cpp:10–14`）；误改 getter 矩阵会造成渲染配置与求解配置不一致。给清晰使用约定即可。

不要承诺异常后缓存和 tracker 总是同一帧：检测先执行，Tag 可以抛异常（`vision/pipeline/src/pose/tag/tag_detector.cpp:49–64`），tracker 只对自身做提交保护（`vision/pipeline/src/tracking/lifecycle/target_tracker.cpp:174–192`）。本次无需引入整条 Pipeline 的事务或回滚。

### 4. AppConfig 清理：三处额外文件基本对，但具体改法漏了两件事

对当前仓库可检索源码搜索 `AppConfig` 和 `detector_config_`，相关位置为：

| 位置 | 处理 |
|---|---|
| `vision/pipeline/include/vision/config/app_config.hpp:3,15–21` | 删除 detector include 和字段；**新增显式 `<filesystem>` include**。 |
| `vision/pipeline/tools/apps/marker/main.cpp:13,20–22` | 聚合初始化只保留 offline 配置，同时改配置归属注释。 |
| `vision/pipeline/src/app/offline/offline_runner.cpp:20–33` | 删除旧 detector 字段消费；这是方案本来就要改的文件。 |
| `vision/pipeline/tests/app/offline/offline_chain_test.cpp:17–18,67–70,102–105` | 改两处初始化；**临时配置目录也复制 detector.yaml**。 |
| `vision/pipeline/src/app/offline/offline_runner.hpp:2,8–9` | 参数仍为 AppConfig，无需改函数签名。 |

**头文件依赖是实质遗漏。** `app_config.hpp:21` 使用 `std::filesystem::path`，目前由 `detector_config.hpp:3` 间接提供声明。删 include 后必须自行包含 `<filesystem>`。`vision/pipeline/src/config/application/offline_config.cpp:1–5` 先包含 app_config，再包含 filesystem，不能靠后面的 include 补救。

**测试不是只改两处大括号就够。** unique_chain 原来从源配置读 detector 参数，却把临时目录交给 run_offline（`offline_chain_test.cpp:67–70`）。新 Pipeline 会统一从临时目录读三个 YAML，而该目录目前只复制 pose、tracking、calibration（同文件 `17–18`），必然缺 detector.yaml；loader 对缺文件抛错（`vision/pipeline/src/config/detector/detector_config.cpp:13–16`）。补复制源 detector.yaml 即可，无需改测试目标或算法。

因此，“除 offline_runner 外还有 main/header/test 三个源码文件”基本成立；“测试只改两处构造、头文件只删不补”不成立。当前搜索没有发现其他该字段的源码使用点，不据此推断仓库外调用者不存在。接口变化意味着以后测试不能通过 AppConfig 注入 detector 参数，应在测试配置目录中表达差异。

### 5. 文档：重点解释谁调用谁，不再复制一份长接口

必须更新 README、flow-guide 和外部接口草稿；INDEX 补导航即可。详细清单见后文。

有三处特别值得纠正：

- `vision/pipeline/README.md:34` 把板上 app 和 offline_runner 写成两个现有调用方，但 `vision/app/README.md:9–11` 明确“待写”。第二步完成后应写“离线已接入，板上 app 计划调用同一入口”。
- `vision/pipeline/README.md:32` 的“跟踪器只吃 MARK”应限定为当前配置。代码按 `TrackingConfig.source_` 过滤（`vision/pipeline/src/tracking/measurement/measurement_adapter.cpp:21–32`），当前 `vision/pipeline/config/tracking.yaml:3` 才把它设为 MARK。不要为了贴合文案，在 Pipeline 新加 MARK 过滤。
- 外部 `管线按帧接口说明-草稿.md:65–67` 仍写新增骨架、main/tools/config 全不动，已与骨架完成和决议 C 冲突。它是规划草稿，不能继续当实际改动清单。

### 6. 验收：回放是主证据，但覆盖不了整条新接口

**基线确实存在并受 Git 跟踪。** `vision/pipeline/docs/homework-setup/outputs/marker/summary.yaml:3–29` 记录 1676 帧、1088 个检测、MEASURED 1088 / COASTING 588，与任务说明一致。本次未执行回放，不能据此声称当前重跑已与基线相同。

汇总的实际 YAML 键是 **`elapsed_s`**，不是 C++ 成员名 `elapsed_s_`。序列化输出只有 frames、detections、四类计数和 elapsed_s；正常同输入运行时，代码中没有其他每次自动变化的字段（`vision/pipeline/src/app/reporting/run_report.cpp:24–40`）。对比时只排除 elapsed_s，不要顺手忽略 reasons、状态或其他差异。

`observations.yaml`、`tracks.yaml` 应在相同输入、配置及工具链下逐字节比较，汇总计数不能替代逐帧比较。特别检查观测顺序、空观测、stamp、events、NIS 都没变；这些是旧链实际交付的数据（`vision/pipeline/src/app/offline/offline_runner.cpp:84–119`）。

**overlay.avi 的逐字节一致可以保留为同环境严格验收，但不能当跨环境必然保证。** exporter 固定 FFmpeg + MJPG，却没有固定其版本和所有编码实现细节（`vision/pipeline/src/app/export/video_exporter.cpp:6–13`）。本次没有测试证明此环境每次编码都字节一致；也不能反过来说“AVI 一定随机”。建议先在同环境确认旧版本重复运行稳定，再比较新旧版本。若视频文件不同，先定位编码/封装还是画面差异，补比解码后的帧数、尺寸、FPS、codec 和逐帧像素；不能把画面变化简单归因于编码器后放过。已有测试只检查 MJPG、尺寸、帧数，不比较渲染像素（`vision/pipeline/tests/app/offline/offline_chain_test.cpp:106–117`）。

第二步建议增加的最小验收：

| 验收 | 要验证的边界与依据 |
|---|---|
| 默认构建与现有 ctest | 保持现有测试全过，并修复临时 detector 配置；现有测试已覆盖 24 帧 MARK→跟踪、缺检 coast、overlay 开关不改两类 YAML（`vision/pipeline/tests/app/offline/offline_chain_test.cpp:73–85,132–147`）。 |
| 直接调用 Pipeline 的短序列 | 只链接 vision_pipeline，创建一次后处理多帧；检查 output 与 getter 同帧、空 MARK 仍有一条、完整 tracking 结果、输入图不被改。不只验证静态库能生成：当前 cpp 只有 TODO，也能生成 target（`vision/pipeline/src/pipeline/pipeline.cpp:1–5`）。 |
| 启用 Tag 的 Pipeline 集成 | 白名单非空时出现 APRILTAG；MARK 空观测仍在前面；无边长保留空米制估计。默认 `vision/pipeline/config/pose.yaml:9–11` 关闭 Tag，1676 帧回放不覆盖此路径；现有 Tag 测试直接调用 TagDetector，未走 Pipeline（`vision/pipeline/tests/pose/tag/tag_detector_test.cpp:35–49`）。 |
| 空 tracking profile | 处理帧仍返回位姿，跟踪遵循原行为，提示只在构造阶段一次。空 profile 的配置语义见 `vision/pipeline/src/config/tracking/tracking_config.cpp:9–35`，不能在 Pipeline 擅自绕过 tracker.step。 |
| 两个开关都 OFF | 验证 vision_pipeline 可被小消费者链接调用，vision_app / marker / 标定工具不参与；常编入口和跳过条件见 `vision/pipeline/CMakeLists.txt:15–16`、`vision/pipeline/src/app/CMakeLists.txt:1–3`。仅 tools OFF、测试 ON 时 vision_app 仍应可用。 |

这里的库构建验证是主机上验证分层，不能宣称等于 MaixCAM 上板成功。现有顶层仍无条件寻找 OpenCV 的 imgcodecs/videoio 等组件（`vision/pipeline/CMakeLists.txt:9`），pose 仍要求 AprilTag（`vision/pipeline/src/pose/CMakeLists.txt:5–26`）。关工具不等于所有离线相关第三方依赖也消失；依赖裁剪可留给上板工程。

回放输出应写到独立工作目录，不覆盖受跟踪的基线。当前 offline.yaml 输出路径是相对路径 `outputs/marker`（`vision/pipeline/config/offline.yaml:3–5`），利用运行工作目录隔离即可，不需要为本次验收增加 CLI 参数。

### 7. 其他补充：保持一条直线调用链

推荐最终结构：

```text
marker main：解析命令行、加载 offline.yaml
  └─ offline_runner：打开视频，构造一次 Pipeline
       ├─ 每帧读图、计算帧号/FPS 时间戳
       ├─ Pipeline.process(input)
       │    ├─ Detector.process
       │    ├─ MARK 位姿：max(1, detections.size())
       │    ├─ 可选 Tag 检测与位姿
       │    └─ TargetTracker.step
       └─ 用 output + 两个 getter 汇总、导出、渲染

未来板上 app：采图、准备 BGR 与帧戳 → 同一 Pipeline → 控制流程
```

这直接对应旧链 `vision/pipeline/src/app/offline/offline_runner.cpp:26–127` 和现有接口 `vision/pipeline/include/vision/pipeline.hpp:19–56`。Pipeline 不需要会话管理器、通用 stage 接口、可插拔执行图或事件总线。四个算法对象和一段明确的 process 足够。

其他风险分清“搬迁必须保留”和“以后再改”：

- **别在缺检测时提前 return。** 原链至少发一条 MARK 空观测，仍调用 tracker 推进 coast（`offline_runner.cpp:84–102`）。不要改变现有输入错误和失败语义来顺手“优化”。
- **状态不只有 tracker。** Detector 保存上一输入帧戳（`vision/pipeline/src/detection/pipeline/detector.cpp:18–23,58–59`）。同一 Pipeline 对应一条顺序帧流；重开视频/重置帧号时新建实例即可，目前无需额外 reset 协议。
- **直接库调用时，坏图行为不统一是已有事实。** Detector 返回 INVALID_INPUT，Tag 开启时会抛 TAG_INPUT_INVALID（`vision/pipeline/src/detection/pipeline/detector.cpp:38–50`；`vision/pipeline/src/pose/tag/tag_detector.cpp:49–52`）。接口应说明只接受约定的有效 BGR 图，不保证所有坏输入都转成统一输出；本次不改四库异常逻辑。
- **配置失败的先后次序会有小变化。** detector.yaml 原来在 main 打开视频前加载，搬后在 run_offline 验证视频后加载（`vision/pipeline/tools/apps/marker/main.cpp:21–24`；`vision/pipeline/src/app/offline/offline_runner.cpp:26–35`）。两者同时坏时先报哪个错可能变，不影响有效回放，不值得为此设计两阶段构造。
- **输出可靠性有既存薄弱点。** VideoExporter 未检查 writer 是否打开，两个 YAML FileStorage 也未显式检查打开状态（`vision/pipeline/src/app/export/video_exporter.cpp:12–18`；`vision/pipeline/src/app/offline/offline_runner.cpp:53–60`）。本次用实际回读验收发现输出失败；若要改，单列后续修复，不夹在搬链里。
- **旧注释也算调用说明。** main 首行仍称 offline_runner 是检测/位姿/跟踪主干，app CMake 注释同样如此（`vision/pipeline/tools/apps/marker/main.cpp:1`；`vision/pipeline/src/app/CMakeLists.txt:4`）。随对应文件改动纠正即可，不用全仓重命名 app 目录。

## 问题清单（按严重程度）

### 会错的：第二步必须修

1. **删 detector include 后缺 `<filesystem>`，头文件无法自包含。** 修法和证据见第 4 项：`app_config.hpp:3,21`、`detector_config.hpp:3`。
2. **unique_chain 临时目录缺 detector.yaml，新构造会报配置打不开。** 同文件不仅要改两处 AppConfig，还要补夹具复制：`offline_chain_test.cpp:17–18,67–70`。
3. **帧戳不能随算法链一起从 offline_runner 删除。** 观测流还需要它，且应从输入取得：`offline_runner.cpp:79–82,115–116`；无效 raw 不保留输入帧戳：`detector.cpp:45–50`。
4. **实现时必须避开的坑：** solver 声明早于 pose_config、move 掉 pose_config、在构造函数体赋值 tracker、漏空 MARK 或 tracker.step、move 掉要返回的 poses。现有构造约束和链路证据分别见第 2、3 项及 `offline_runner.cpp:84–102`。这是落地注意点，不是声称尚未实现的 pipeline.cpp 已存在这些 bug。

### 建议改的：让这次搬迁可验证、调用关系明确

1. 将决议 C 和测试夹具修正正式列入六步清单，不再笼统声称 config/main/tools 全不动（第 1、4、5 项）。
2. 为 getter 写清有效期、串行使用和异常边界，避免上一帧 output 搭配下一帧 raw（第 3 项）。
3. 补 Tag、空 profile、独立链接和双开关关闭的验收；默认录像没有 Tag 覆盖（第 6 项）。
4. 更新 README、flow-guide、草稿和旧路径；把计划中的板上调用方与已实现代码区分开（第 5 项及下表）。
5. 库构造打印先保持兼容；未来需要静默库时再移到调用方，不为此搭框架（第 2 项）。

### 可忽略或留后续：不阻塞第二步

- PoseConfig 保存两份、旧的三个显式 CMake 链接暂留、AppConfig 暂只包一个 offline 字段，都可以接受（第 1、3、4 项）。
- 同时坏视频和坏配置时错误优先级变化，不影响正常链路（第 7 项）。
- 上板依赖裁剪、统一坏帧语义、输出打开检查、app 目录命名，单列后续任务，不扩大搬链提交（第 6、7 项）。

## 需要更新的文档清单

外部草稿目录为：`/mnt/d/Personal/Study/26-27Fall/PNX/校内赛/视觉算法开发/开发计划路线板块/全板块链路/新仓库/重构接口/`。

| 文档/位置 | 第二步应更新什么 |
|---|---|
| `vision/pipeline/README.md:24,30–34` | 写清 main 只读 offline，Pipeline 加载三个算法配置；给一个“循环外构造、循环内 process”的短例子；列两个 getter 及引用有效期。板上 app 标为计划调用方。MARK 筛选限定为当前 tracking 配置。 |
| `vision/pipeline/README.md:7–16` | 明确链接 target 为 vision_pipeline、两开关关闭仍构建它。现有 `cmake -S vision/pipeline` 是合法独立构建入口，不是错误；若改用整个视觉工程入口则写 `-S vision`，两者不要共用已有的同一个 build 缓存。保留工作树中已有的开关解释。依据：`vision/CMakeLists.txt:4–7`、`vision/pipeline/CMakeLists.txt:1–23`。 |
| `vision/pipeline/docs/flow-guide.md:5–23,25–59` | 图上划分离线输入/输出与 Pipeline 算法链；改配置加载归属；写清输入 stamp 与诊断 getter。修正 `src/tushenghao` 旧构建和视频路径。 |
| `vision/pipeline/docs/flow-guide.md:76,120–133` | “所有结构成员使用下划线后缀”已不适用于 PipelineInput/Output（接口头 `19–35`），限定为已有算法类型或删除绝对说法。输出 schema 不变，只补“结果由 Pipeline 返回，app 导出”。 |
| `vision/pipeline/docs/INDEX.md:3–7` | 增加 README 按帧接口段和本评审的链接，方便找到入口；无需再建重复接口长文。 |
| 外部 `管线按帧接口说明-草稿.md:25–35,53–67,85–93` | 补 getter、生命周期、配置归属；区分已完成骨架与第二步；删除 main/config/tests 全不动的承诺；补实际变动清单和验收范围。`59` 行“不用改 CMakeLists”需限定为切换部署模式，不能理解为接入 Pipeline 不改链接。 |
| `vision/pipeline/tools/INDEX.md:9–13` | marker、calibration、tracking_replay 示例仍用 `src/tushenghao`，换成当前 `vision/pipeline` 路径。这是既有文档债，可随此次使用说明一起修。 |
| `vision/pipeline/tools/apps/marker/main.cpp:1,13,20`、`vision/pipeline/src/app/CMakeLists.txt:4`、`vision/pipeline/include/vision/pipeline.hpp:38–50` | 作为代码旁的调用说明同步更新；头文件补两个 getter 契约。 |

`vision/pipeline/docs/api-derivations.md` 的数学推导和基线输出无需因纯搬链改写；帧格式说明见该文 `5`，输出约定见 `flow-guide.md:124–131`。基线文件应保持原样作为比较对象。`vision/app/README.md:9–11` 仍是待写状态，无需将其改成已接入。外部草稿只列更新建议，本次没有修改。

## 建议采用的正确改法

1. **先补清单和依赖。** pipeline.hpp 加两个 getter、pose_config_ 与 last_detection_；app_config.hpp 删 detector 字段及 include，补 filesystem。保留 AppConfig 包装，不在此时顺手改 run_offline 签名（依据见第 3、4 项）。
2. **按声明顺序初始化。** detector 加载 detector.yaml；pose_config 加载 pose.yaml；solver 拷贝 pose_config；Tag 按 allowed_ids 创建；tracker 用只加载一次配置的辅助函数初始化，保留原提示。不用为可直接构造的 solver/tracker 增加 unique_ptr（依据见第 2 项）。
3. **process 只写四步直线代码。** 将输入转为 FrameInput；检测结果存 last_detection_；从输入建 stamp；按 `max(1,n)` 生成 MARK，再追加 TAG；将完整 poses 交给 TrackingFrame，调用 step，将完整结果和 poses 返回。不要先 move 掉 poses 再返回空容器，保持旧代码的复制语义就够（旧链依据：`offline_runner.cpp:71–102`）。
4. **offline_runner 收缩为调用方。** Pipeline 在读帧循环外构造；循环内保留原来的 long double + llround 时间戳算法，调用 process 后立即拿 raw 引用；用 output 计数、写文件、渲染。导出 stamp 由本次输入构造，计时边界不变。main 只加载 offline 配置，vision_app 链接 vision_pipeline（依据见第 1、2 项）。
5. **修测试与文档，再验收。** 两处 AppConfig 构造和临时 detector.yaml 一起修；完成第 6 项的少量针对性验收和文档同步。只在同环境证据充分后，把第二步称为“搬链完成”。

这次需要解决的是配置归属和调用归属；四个算法库的实现、输出 schema、导出渲染算法都保持原样。最终读代码时应能直接看出：一帧从哪里进来，经过哪四步，结果由谁写出。
