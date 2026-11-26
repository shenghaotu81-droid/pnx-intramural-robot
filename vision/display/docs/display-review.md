# display 实现审查与测试状态

**阻断项：MaixCAM 后端推帧失败时，项目的 `Display::show()` 可能正常返回。** 审查基线为本项目 `ef856e2` 和本地 MaixCDK `30f4b8b7e3f6`，跟踪的是 MaixCAM-Pro 默认屏的 `PLATFORM_MAIXCAM` 路径。结论来自源码，未在板上复现。按停工约定，本次新增 `render()` 用例 0 个，未构建或运行测试。

## 推帧错误在哪里丢失

1. 项目把 BGR888 图交给 SDK。SDK 进入格式转换分支，得到 RGB888 图，再调 `_impl->show(*show_img, fit)`。[项目封装](../src/display.cpp#L44) · [SDK 格式分支][sdk-show]
2. 默认视频层收到 RGB888 后调用 `mmf_vo_frame_push_with_fit`。该函数返回非零时，后端记录 `mmf_vo_frame_push failed`，并返回 `ERR_RUNTIME`。[MMF 的 RGB888 分支][mmf-push]
3. SDK 忽略 `_impl->show` 的返回值，删除临时图，返回先前的 `e`。屏幕已打开时，`e` 一直是 `ERR_NONE`。项目的 `e != ERR_NONE` 判断因此不会抛错，调用方无法从这次返回判断帧是否已送出。即使在项目侧提前转为 RGB888，SDK 的直通分支也同样忽略返回值。[SDK 返回路径][sdk-show] · [项目错误检查](../src/display.cpp#L52)

头文件承诺“MaixCDK 推屏失败抛 `std::runtime_error`”。要兑现这条承诺，首先要让 SDK 传回后端错误码。修复的验收条件是让 MMF 推帧返回错误，再确认项目调用方收到 `std::runtime_error`。仅靠 OpenCV 的绘制测试覆盖不到这条路径。[接口约定](../include/display/display.hpp#L55)

## MaixCDK API 逐项核对

| 核对项 | 项目调用和 SDK 行为 | 源码 |
| --- | --- | --- |
| 默认构造与打开异常 | SDK 默认宽高均为 −1、格式 RGB888、设备名为空、自动打开。空设备名在 MaixCAM 选择 MMF。构造中的 `open()` 返回错误时，`check_raise` 抛出 `maix::err::Exception`，它继承 `std::exception`。项目捕获后以 `display open failed: ` 加原诊断重新抛出 `std::runtime_error`。头文件注释中的默认屏 BGR888 有误。 | [项目构造](../src/display.cpp#L20) · [默认参数][sdk-ctor-decl] · [构造与打开][sdk-ctor] · [异常类型][sdk-error] · [check_raise][sdk-check] |
| `width()` / `height()` | 两者返回 `int` 并转发给后端。MMF 从面板配置取尺寸，计入旋转，保存逻辑宽高。Pro 的面板项为 480×640，默认逻辑宽高为 640×480。项目按宽、高顺序传给 `cv::Size`，每次调用都查询设备。 | [项目取尺寸](../src/display.cpp#L43) · [SDK 转发][sdk-size] · [面板尺寸][mmf-panel] · [旋转][mmf-rotate] · [MMF 初始化][mmf-ctor] · [宽高访问器][mmf-size] |
| BGR888 → RGB888 | 项目按 Mat 的 BGR 通道顺序标记 `FMT_BGR888`。MaixCAM SDK 将其交给 `to_format(FMT_RGB888)`，为结果分配图像，选择 `cv::COLOR_BGR2RGB` 并执行 `cvtColor`。后端调用返回后，SDK 删除临时图。 | [项目包装](../src/display.cpp#L47) · [SDK show][sdk-show] · [通道转换][sdk-bgr] · [目标分配][sdk-dst] · [执行转换][sdk-cvt] |
| `Image` 的 `data_size` | 对未压缩格式，正的 `data_size` 必须精确等于宽 × 高 × 每像素字节数，否则抛 `ERR_ARGS` 对应的 SDK 异常。BGR888 每像素 3 字节。按接口传入 `CV_8UC3` 时，`resize` 写入新建的连续 Mat，项目传 `cols * rows * 3` 与其字节数一致。640×480 的实参应为 921,600 字节。 | [渲染目标](../src/render.cpp#L55) · [构造实参](../src/display.cpp#L50) · [字节数表][sdk-bpp] · [精确匹配检查][sdk-bytes] |
| `copy=false` 的所有权和期限 | 传入非空指针时，SDK 直接保存该地址并置 `_is_malloc=false`。析构只释放自有内存。项目先构造 `screen`，再构造借用它的 `img`，后者先析构。转换读取 BGR 数据时 Mat 仍存活，交给 MMF 的 RGB 临时图有独立像素。 | [项目变量作用域](../src/display.cpp#L44) · [借用分支][sdk-borrow] · [析构][sdk-free] · [SDK 消费图像][sdk-show] |
| `ERR_NONE` 与异常 | `ERR_NONE` 的值为 0。SDK 自动重开屏幕返回错误或格式转换返回空指针时，公共 `show()` 会返回非零，项目将该码写进 `std::runtime_error`。MMF 推帧错误会在上一节所述路径丢失。另外，RGB 图像分配失败可以直接抛出 `maix::err::Exception`，项目的 `show()` 没有捕获它。 | [项目检查](../src/display.cpp#L52) · [SDK 错误码与异常][sdk-error] · [重开与转换][sdk-show] · [图像分配][sdk-alloc] |

构造异常还有一处 SDK 资源回收风险。SDK 先用 `new` 创建 MMF 后端，再调用 `open()`。若后端已构造而 `open()` 报错抛出，SDK 的 `Display` 尚未完成构造，析构函数里释放后端的路径不会执行。项目捕获异常能提供打开失败的诊断，但同一进程反复尝试打开可能积累资源。[SDK 构造与析构][sdk-ctor] · [MMF 初始化][mmf-ctor] · [后端打开失败分支][mmf-open]

## render 接口与测试状态

`render.hpp` 声明的输出尺寸与实现一致。实现将整帧缩放到传入的 `screen_size`，对角点、圆心和箭头端点分别按 `sx`、`sy` 缩放后取整，随后在输出 Mat 上绘制。箭头需要跟踪点和端点同时存在。文字使用固定行位，缺跟踪状态时 mode 仍在左上第 3 行，位置和速度各占左下固定一行，右下四行由 `actual` 整块控制。源码中的 BGR 颜色、线宽、两位小数及整数 Hz 与 README 一致。[渲染接口](../include/display/render.hpp#L13) · [图形](../src/render.cpp#L55) · [文字](../src/render.cpp#L85)

README 已按上述行为修正屏幕格式、缩放方式和错误传播，并把示意图的左右两块底端对齐。头文件中关于默认格式和推屏异常的两处注释仍与本版本行为有出入。跳过 `show()` 只会停止提交新帧，库没有自动清屏操作。[README](../README.md) · [头文件](../include/display/display.hpp#L47)

现有 CMake 测试目标只编译 `render.cpp` 和测试入口，适合在主机上测像素。`render_test.cpp` 当前只有占位注释。恢复测试工作时，以下用例能覆盖彼此独立的回归，本次均未执行。[测试目标](../tests/CMakeLists.txt) · [测试入口](../tests/render_test.cpp)

| 待测场景 | 判定方法与理由 |
| --- | --- |
| 非等比缩放 | 使用 `sx ≠ sy` 的帧和屏幕，断言输出尺寸、BGR 类型，并在预计的四条边、跟踪圆周、箭杆及箭头端部取样。默认的 4:3 输入输出会掩盖横纵比例混用。 |
| 可选图形 | 在纯色底图上单独给四边形、单独给跟踪点、仅给箭头端点及两点齐全。检查绿色和橙色的出现区域，区分点与箭头的依赖。 |
| 固定文字行 | 分别切换跟踪状态、位置、速度和实际速度，在各预留行检查白字像素的有无。对相邻保留行比较像素位置，可发现缺字段导致的行位移动，无需 OCR。 |
| 输入帧完整性 | 在输入输出同尺寸和缩放两种情况下，对含叠加层的渲染逐像素比较输入与其事前副本。同尺寸用例可发现直接在共享 Mat 上绘图的回归。 |

[sdk-ctor-decl]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/include/maix_display.hpp#L37-L48
[sdk-ctor]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_display.cpp#L37-L95
[sdk-size]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_display.cpp#L166-L174
[sdk-show]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_display.cpp#L186-L222
[mmf-panel]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/port/maixcam/maix_display_mmf.hpp#L37-L68
[mmf-rotate]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/port/maixcam/maix_display_mmf.hpp#L109-L121
[mmf-ctor]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/port/maixcam/maix_display_mmf.hpp#L162-L196
[mmf-size]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/port/maixcam/maix_display_mmf.hpp#L232-L240
[mmf-open]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/port/maixcam/maix_display_mmf.hpp#L252-L285
[mmf-push]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/port/maixcam/maix_display_mmf.hpp#L341-L355
[sdk-error]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/basic/include/maix_err.hpp#L23-L59
[sdk-check]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/basic/src/maix_err.cpp#L68-L79
[sdk-bpp]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/include/maix_image_def.hpp#L26-L75
[sdk-bytes]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L371-L395
[sdk-alloc]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L397-L402
[sdk-borrow]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L462-L486
[sdk-free]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L499-L508
[sdk-bgr]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L968-L976
[sdk-dst]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L1073-L1085
[sdk-cvt]: https://github.com/sipeed/MaixCDK/blob/30f4b8b7e3f66ded9cfa64fb081b46e30bc27248/components/vision/src/maix_image.cpp#L1135-L1147
