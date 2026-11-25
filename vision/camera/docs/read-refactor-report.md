# camera 无缓冲读帧重构审核报告

**审核结论：暂停实施。** 无参 `read()` 能绕过 1440/1472 宽度差异触发的外部缓冲区容量检查，SDK 输出的 BGR 图像也可以直接包成 `cv::Mat`。阻断实施的是取帧后的分配失败路径。按方案，每次正式读取都要在已持有驱动帧时分配整幅图像。分配一旦抛异常，当前 SDK 会跳过驱动帧的归还。另有一处需要明确的接口约定：只捕获 `skip_frames()` 足以转换预热异常，但构造和显式 `open()` 仍可能直接抛 SDK 异常。

核验基线为项目 `fe4ba66`、本地 MaixCDK `30f4b8b7` 的 MaixCAM MMF 后端，以及宿主 OpenCV 4.6.0。此次按指令在审核阶段停止，交付物仅为本报告。

## 阻断项：像素分配失败会跳过驱动帧归还

1. `_mmf_vi_frame_pop` 先通过 `CVI_VPSS_GetChnFrame` 取得帧，再映射设备内存。`_mmf_read` 随后在空 `buff` 分支执行 `new Image(width_out, height_out, format_out)`。[取出并映射驱动帧](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L949) · [分配输出 Image](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1008)
2. 对默认 1440×1080 BGR888，这个构造函数每次为 4,665,600 字节像素另加 4,096 字节对齐余量调用 `malloc`。分配失败会直接抛 `maix::err::Exception(ERR_NO_MEM, ...)`。[Image 构造入口](../../../../MaixCDK/components/vision/src/maix_image.cpp#L488) · [大小计算及分配失败分支](../../../../MaixCDK/components/vision/src/maix_image.cpp#L387)
3. 此时控制流直接退出 `_mmf_read`。正常返回及 `_error` 分支中的 `_mmf_vi_frame_free` 均不会执行，映射和驱动帧未在该次读取中释放。外层 `catch` 可以改写异常类型，却拿不到 SDK 局部变量 `pframe`，因而无法补做归还。默认 `buff_num=1`，未归还的帧可能使后续读取超时。源码能确定清理被跳过，后续是否超时未经板测。[归还函数](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L977) · [正常与错误出口](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1147) · [默认配置](../config/camera.yaml#L7)

现有预热已走这条分配路径，正式读取则复用预分配的 Mat。拟议改动会让整帧分配失败路径从启动阶段延伸到每一帧。外层 `current_img_` 只能接管成功返回的 `Image*`。要保证这一错误之后仍能重试，清理必须覆盖 SDK 取得驱动帧至返回之间的异常出口，这超出了指令限定的封装改动范围。[现有预热及 Mat 分配](../src/camera.cpp#L59) · [现有正式读取](../src/camera.cpp#L74)

## 其余方案核验

| 审核项 | 结论及适用条件 |
| --- | --- |
| 无缓冲重载与容量 | `read()` 转发为 `read(nullptr, 0, true, -1)`。外部缓冲区的容量判断只出现在 `if (buff)` 分支，空缓冲区分支按请求宽高创建 Image。原报告指出的两项容量要求不再冲突。[重载](../../../../MaixCDK/components/vision/include/maix_camera.hpp#L145) · [MMF 分支](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1020) |
| 源行距与输出行距 | SDK 按驱动报告的 `u32Width * 3` 定位 RGB 源行，每行转换请求宽度个像素，按 B、G、R 紧密写入目的地址。因此 1440 宽的输出行距为 4,320 字节，Mat 默认步长正确。若驱动报告 1472 且源行距为 4,416 字节，也能正确逐行取像。SDK 此处分支未读取 `u32Stride[0]`，源行距仍需用板端数据核实。[BGR 转换循环](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1076) · [驱动帧宽与 stride 字段](../../../../MaixCDK/components/3rd_party/sophgo-middleware/sophgo-middleware/v2/uapi/linux/cvi_comm_video.h#L365) |
| 像素所有权与失效时刻 | `cv::Mat(h, w, CV_8UC3, img->data())` 只建视图，像素由 SDK Image 持有。按方案，尺寸或格式不符时删除临时 Image，成功时由 `current_img_` 接管，正常路径的释放次数正确。旧 Mat 及其浅拷贝从**下一次调用 read 的入口**即成为悬空视图，即使该次读取失败也是如此。销毁 Camera 同样使当前视图失效。现有实现中，返回的 Mat 引用计数可让像素存储活过 Camera 析构。这是签名不变时仍需告知调用方的生命周期变化。[当前 Frame 赋值](../src/camera.cpp#L95) · [SDK Image 析构](../../../../MaixCDK/components/vision/src/maix_image.cpp#L499) · [OpenCV 外部数据构造说明](/usr/include/opencv4/opencv2/core/mat.hpp#L908) |
| 管线顺序 | 若调用方在 Camera 存活期间先处理完当前帧，再发起下一次读取，借用视图可以满足现有同步 `Pipeline::process`。仓库中的板端 app 仍待实现，因此“所有调用方均按此顺序使用”只能作为接口前提。[管线入口](../../pipeline/src/pipeline/pipeline.cpp#L29) · [app 状态](../../app/README.md#L9) |

## 构造异常的约定

`skip_frames()` 循环调用 SDK 的无参 `read()`。仅对这一行捕获 `const maix::err::Exception&` 并抛 `std::runtime_error`，能覆盖指令指定的预热 SDK 异常。对于整个构造过程，存在更早的失败路径。[预热实现](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1355)

- 即使传 `open=false`，SDK 构造函数也会校验格式、分配私有状态并检查分配结果。该开关只控制构造末尾的自动打开。[SDK 构造检查](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L78) · [自动打开分支](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L128)
- 显式 `open()` 创建通道失败时会通过 `check_raise` 抛出 SDK 异常，封装的返回值判断没有执行机会。该异常直接继承 `std::exception`，调用方只捕获 `std::runtime_error` 会漏接。[封装检查](../src/camera.cpp#L52) · [SDK 打开分支](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L873) · [异常继承](../../../../MaixCDK/components/basic/include/maix_err.hpp#L51)

当前 README 仅承诺构造失败抛异常。若指令中的 `std::runtime_error` 约定只针对读帧和预热，所提捕获范围足够。若要统一所有启动阶段的 SDK 异常，指令第 90 行“只包 `skip_frames`”就需要调整。[相机行为约定](../README.md#L29)

## 建议方案与验收条件

建议保留无缓冲读取和直接包装 Mat 的设计，先修复 MaixCDK 取帧后的异常清理，再让项目的正式读帧切换到这条路径。**第一步需要修改或升级板端实际使用的 MaixCDK**，当前任务只授权在报告中设计这项改动。

1. **在 SDK 内保证每次成功取出的帧都归还一次。** 在 `_mmf_read` 成功取得 `pframe` 后、构造 Image 前，用无需堆分配的局部 RAII 对象持有该帧，析构时调用 `_mmf_vi_frame_free(ch, &pframe)`。让这个对象统一负责成功、格式错误和异常出口，移除这些分支中重复的手动归还。局部 `unique_ptr<Image>` 持有输出图像，成功返回时才把所有权交给调用方。此外，`_mmf_vi_frame_pop` 为帧描述符调用 `malloc` 后也应先判空，再执行 `memset`，覆盖更早的分配失败。[帧描述符分配](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L949) · [驱动帧释放函数](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L977) · [现有出口](../../../../MaixCDK/components/vision/port/maixcam/maix_camera_mmf.cpp#L1141)
2. **在封装内明确像素的借用期限。** 按原方案在 `read()` 入口释放上一帧，使常态只需一份 SDK 输出像素。用局部 `unique_ptr` 立即接管无参 `read()` 的结果，校验非空、BGR888 和尺寸，再用 `img->data()` 构造 Mat。全部成功后将指针移入 `current_img_` 并分配帧号，校验失败由局部对象自动清理。头文件和 README 应写清借用条件。Camera 必须存活，一旦发起下一次读取，旧视图立即失效，即便该次读取随后抛错。需要保留最后一帧的调用方应在此之前 `clone()`。
3. **让调用方能用同一种方式处理启动失败。** 建议把 SDK Camera 构造、显式 `open()`、`skip_frames()` 三个阶段直接抛出的 `maix::err::Exception` 都转为带阶段名称和原始 `what()` 的 `std::runtime_error`，并继续检查 `open()` 的非成功返回码。这一建议扩展了指令第 90 行限定的捕获范围，需要在实施前明确采用。
4. **用一次失败后的下一帧验收错误恢复。** 在 SDK 测试构建中让一次已取帧后的像素分配受控失败，核对该帧的解除映射、通道归还各执行一次。随即用同一相机、`buff_num=1` 再读一帧，应成功且失败调用不消耗封装帧号。板端正常路径再记录预热后的首帧宽、高、源 stride，检查跨行图案和红蓝色块。若源 stride 与 `u32Width * 3` 不同，SDK 的 BGR 源行寻址应采用经验证的实际 stride，输出仍按每行 4,320 字节交给 Mat。

## 验证与板端待确认项

本轮完成源码核验。按指令的审核停止条件，`src/camera.cpp`、公共头文件和 README 均未修改。重构版本的 `g++ -std=c++17 -fsyntax-only` 未执行，无退出码。CTest 本轮未运行，现有测试目标仅编入配置加载器。[加载器测试目标](../tests/CMakeLists.txt#L1)

仍须以板端首个正式帧确认原 1472 风险是否触发。对照请求尺寸、同帧 `u32Width`、`u32Height`、`u32Stride[0]` 和原路径的容量日志，才能判定是容量冲突还是其他启动错误。后续验证无缓冲路径时，还应核对画面逐行排列和颜色。预热成功本身无法回答这些问题。[原始审阅报告](camera-review.md#优先核查的读帧问题)
