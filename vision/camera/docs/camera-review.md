# MaixCAM 相机封装审阅报告

`camera.cpp` 对 `Camera` 构造、`open`、`read`、`skip_frames` 的参数和重载选择正确。MaixCAM 正常取帧时，返回的 `Image` 借用 OpenCV 的像素内存，两处 `delete img` 均符合 SDK 的所有权约定。**默认 1440×1080 配置可能在成功丢弃 10 帧后，仍读不到第一帧。** 预热由 SDK 分配像素内存，正式读取使用封装提供的缓冲区，只有后者会触发驱动尺寸与缓冲区容量的检查。

审阅基线为项目 `19dd5ac7300a` 和本地 `/home/tushenghao/projects/MaixCDK` 的 `30f4b8b7e3f6`。项目指定 MaixCAM，SDK 在 `PLATFORM_MAIXCAM` 下编译 `port/maixcam/maix_camera_mmf.cpp`。以下以该后端、普通彩色采集和仓库自带的相机配置为准。[项目平台说明](../../README.md) · [SDK 平台选择](../../../../MaixCDK/components/vision/CMakeLists.txt#L28) · [相机配置](../config/camera.yaml)

## 优先核查的读帧问题

1. **默认分辨率可能在预热后的第一次 `Camera::read()` 失败。** 封装分配连续的 `CV_8UC3`，传入 `1440 × 1080 × 3 = 4,665,600` 字节。SDK 用请求宽高构造 `Image`，再按驱动帧的 `u32Width × u32Height × 3` 检查容量。Sophgo 的通道配置源码把输出宽度向上对齐，本版本随附的 MaixCAM 库也将通道宽度向上取整到 64 的倍数。若驱动回报对齐后的 1472×1080，SDK 就要求 4,769,280 字节，多出 103,680 字节。此时 SDK 释放包装和驱动帧，封装最终抛出 `std::runtime_error`。预热的 `read()` 传空缓冲区，会绕过这项容量检查。设备验收应观察**预热后的首个正式帧**，并核对驱动的宽、高和 `buff size not enough` 日志。本次没有板端返回值。[封装传参](../src/camera.cpp#L77) · [SDK 实际帧尺寸与容量检查](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1008) · [通道宽度对齐示例源码](../../../../MaixCDK/components/3rd_party/sophgo-middleware/sophgo-middleware/v2/sample/vio/sophgo_middleware.c#L478)
2. **把传入的字节数直接改大也会触发错误。** SDK 先构造外部缓冲区 `Image`。对未压缩格式，正的 `data_size` 必须恰好等于请求宽高对应的像素字节数。在上述条件下，一个检查要求等于 4,665,600，另一个要求至少 4,769,280。封装事后核对的 `img->width()/height()` 来自请求宽高，无法独立证明驱动返回了相同尺寸。[Image 大小校验](../../../../MaixCDK/components/vision/src/maix_image.cpp#L371) · [Image 构造位置](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1020) · [封装输出校验](../src/camera.cpp#L85)

上述 64 像素对齐另经本地预编译库只读核对。`components/maixcam_lib/lib_maixcam/libmaixcam_lib.so` 中 `mmf_vi_aligned_width(int)` 返回 64，通道设置处（库内偏移 `0x1ec8e`、`0x1ec98`）对请求宽度执行 `(width + 63) & ~63`。驱动实际返回的帧尺寸仍取决于板端固件。[SDK 对预编译库的选择](../../../../MaixCDK/components/maixcam_lib/CMakeLists.txt#L25)

## API 与内存所有权

| 核验项 | 本项目调用与 MaixCAM 行为 | 源码定位 |
| --- | --- | --- |
| 构造 | 八个实参依次为宽、高、`FMT_BGR888`、`nullptr`、`double fps`、`int buff_num`、`open=false`、`raw=false`。BGR888 在支持列表中。构造时保存参数并关闭彩条，随后由封装显式打开一次。 | [封装](../src/camera.cpp#L45) · [SDK 声明](../../../../MaixCDK/components/vision/include/maix_camera.hpp#L82) · [SDK 构造](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L78) |
| `open` | 五个实参次序正确，返回类型为 `maix::err::Err`。`ERR_NONE` 表示成功，其他返回码经 `to_str` 进入 `std::runtime_error`。SDK 创建通道后还会取出并释放一帧，再设置打开标志。 | [封装](../src/camera.cpp#L55) · [SDK 声明](../../../../MaixCDK/components/vision/include/maix_camera.hpp#L106) · [就绪检查](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L873) |
| `skip_frames(n)` | 返回 `void`，在构造期间调用一次。SDK 循环执行无参阻塞 `read()` 并逐帧 `delete`，临时像素也随之释放。`n=0` 时循环零次。默认 10 次成功读取给自动曝光留下调整时间，SDK 没有判断曝光是否已收敛。预热不推进封装的帧号。 | [封装](../src/camera.cpp#L59) · [无参转发](../../../../MaixCDK/components/vision/include/maix_camera.hpp#L145) · [丢帧实现](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1355) |
| `read(buff, size, true, -1)` | 选中外部缓冲区重载，`total() * elemSize()` 给出连续 Mat 的字节数。负超时取三个帧周期，30 fps 时底层取帧等待约 100 ms。阻塞模式下空结果经 `check_null_raise` 抛 SDK 异常，封装转为 `std::runtime_error`，也为直接返回空指针保留了检查。 | [封装](../src/camera.cpp#L68) · [SDK 声明](../../../../MaixCDK/components/vision/include/maix_camera.hpp#L119) · [阻塞分支](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1175) |
| BGR 与拷贝 | 请求 BGR888 时，MMF 将采集通道设为 RGB888，再把每个像素按 B、G、R 顺序写到传入缓冲区。帧交给 OpenCV 和管线时只复制 Mat 头，省掉一次整帧复制。驱动缓冲区到 Mat 仍有一次逐像素读写。 | [通道格式](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L794) · [BGR 写入](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1076) · [帧交付](../src/camera.cpp#L95) |
| `Image` 的 `new/delete` | 正常分支用 `new Image(..., buff, buff_size, false)`，令 `_data = buff`、`_is_malloc = false`。SDK 在返回前释放驱动帧。调用方在格式或尺寸校验失败、成功返回两个分支各删除一次包装。析构只在 `_is_malloc` 为真时释放像素，所以 Mat 数据在包装销毁后仍有效。 | [SDK 包装与返回](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1020) · [Image 借用内存与析构](../../../../MaixCDK/components/vision/src/maix_image.cpp#L462) · [封装释放](../src/camera.cpp#L89) |

## 启动与错误处理

- **启动阶段可能直接抛出 SDK 异常。** SDK 的格式、分配、VI 初始化和通道检查可以抛 `maix::err::Exception`，预热读取失败也会抛出该异常。它直接继承 `std::exception`。封装只在公共 `read()` 里捕获它，因此仅捕获 `std::runtime_error` 的调用方会漏接这类构造失败。README 对构造只承诺“抛异常”，实现注释所述的转换覆盖 `open()` 的错误返回码。[封装构造](../src/camera.cpp#L49) · [SDK 通道初始化](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L873) · [SDK 异常继承](../../../../MaixCDK/components/basic/include/maix_err.hpp#L51)
- **SDK 首次打开超时的回收路径有缺口。** 首个就绪帧超时会在通道建好后直接返回 `ERR_RUNTIME`，此时 `_is_opened` 仍为假。封装的 `unique_ptr` 会析构 SDK 对象，但 SDK 析构只在 `is_opened()` 为真时调用 `close()`。该分支可能留下 MMF 通道，影响同一进程重试。成功打开后的正常析构及预热失败时的栈展开均能进入 `close()`。[SDK 就绪检查与关闭](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L886) · [SDK 析构](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L135)
- **SDK 打开路径使用了已失效的板型字符串指针。** `sys::device_id()` 按值返回 `std::string`，`open()` 保存 `sys::device_id().c_str()` 后，临时字符串在该语句结束时销毁。后续 `_mmf_vi_init(board_id, ...)` 用 `strcmp` 选板型，构成未定义行为。该问题属于当前 SDK 版本，实际启动表现未经板测。[返回类型](../../../../MaixCDK/components/basic/include/maix_sys.hpp#L55) · [取得指针](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L823) · [板型比较](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L539)

## 框架契合度

- **帧的使用期限。** Pimpl 将 SDK 类型留在实现文件。`Frame.image` 和内部 Mat 共享同一块像素，旧帧即使有自己的 Mat 引用，也会看到下一次采集写入的新内容。预期的顺序是成功读一帧，把 `{image, frame_id, timestamp_us}` 交给 `Pipeline::process`，处理完再读。现有管线同步使用图像，检测器跨帧只保留输入戳，检测结果也不持有原图。跨帧保留或异步消费需要在下次读取前取得独立像素副本。[Frame 接口](../include/camera/camera.hpp#L15) · [管线入口](../../pipeline/src/pipeline/pipeline.cpp#L29) · [检测器留存状态](../../pipeline/src/detection/pipeline/detector.cpp#L18) · [检测结果类型](../../pipeline/include/vision/types/detector_types.hpp#L30)
- **帧号与时钟。** 每个相机实例的首个成功帧为 0，随后每次成功交付加 1。取帧失败或校验失败不消耗帧号。`steady_clock` 在 SDK 读取和像素转换完成后取样，单位为微秒，表示应用接收时刻。它符合管线对帧号递增、时间不倒退的检查。传感器曝光到这个时间戳之间包含排队和像素转换耗时。[封装计时与编号](../src/camera.cpp#L28) · [管线输入契约](../../pipeline/include/vision/pipeline.hpp#L18) · [帧序校验](../../pipeline/src/detection/pipeline/detector.cpp#L34)
- **配置与标定。** 默认 BGR888、1440×1080 与管线的 `CV_8UC3` 输入、仓库中的标定尺寸一致。更改分辨率时若沿用原标定，位姿估计会返回 `IMAGE_SIZE_OR_QUAD_INVALID`。加载器只对格式白名单做校验，直接构造时也只再次检查格式。例如 `fps=0` 会进入 SDK 的除法，`skip_frames<0` 会静默跳过预热。默认的 30 fps 和单缓冲是请求值。MMF 在 `buff_num=1` 时省略额外的软件节流，实际吞吐、帧龄与曝光收敛仍需实测。[配置读取](../src/config_loader.cpp#L15) · [标定尺寸](../../pipeline/config/calibration.yaml#L16) · [位姿尺寸检查](../../pipeline/src/pose/estimation/pose_estimator.cpp#L53) · [SDK 节流](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1184)
- **后端边界。** 当前封装校验返回图像的格式和尺寸，随后发布自己的 Mat。这依赖后端确实写入了传入地址。例如同一 SDK 的 MaixCAM2 后端忽略 `buff`，由 `Frame::to_image()` 另配内存。若在该后端直接复用封装，即使格式和尺寸相符，也可能把尚未写入本帧的 Mat 交给管线。[封装校验与发布](../src/camera.cpp#L89) · [MaixCAM2 read](../../../../MaixCDK/components/vision/port/maixcam2/maix_camera_maixcam2.cpp#L673) · [MaixCAM2 像素分配](../../../../MaixCDK/components/vision/port/maixcam2/maix_pipeline_maixcam2.cpp#L305)

## 验证记录

以 GCC 13.3.0、主机 OpenCV 4.6.0、本地 MaixCDK 头文件及其已有的生成配置执行以下只读语法检查，退出码为 0。此步骤核验了 `camera.cpp` 的实参类型、重载选择和返回类型。

```sh
# 在本项目根目录运行
sdk=/home/tushenghao/projects/MaixCDK
g++ -std=c++17 -fsyntax-only -Ivision/camera/include \
  -I"$sdk/components/vision/include" -I"$sdk/components/basic/include" \
  -I"$sdk/examples/hello_world/build/config" -I/usr/include/opencv4 \
  vision/camera/src/camera.cpp
```

仓库的 `vision/CMakeLists.txt` 当前只纳入 pipeline、controller 和 adapter。独立的 `camera` 目标列出了 `camera.cpp`，只声明了 OpenCV 依赖，板端构建还需提供 MaixCDK 的头文件与链接依赖。已有 CTest 针对配置加载器，未编入相机实现。`vision/app` 仍标为待写，因此顺序主循环属于接口设计。本次验证止于源码与主机语法检查，板端交叉链接、真实采集和端到端运行尚无本次实测结果。[视觉构建入口](../../CMakeLists.txt) · [相机构建目标](../CMakeLists.txt#L4) · [测试目标](../tests/CMakeLists.txt#L1) · [app 状态](../../app/README.md#L9)
