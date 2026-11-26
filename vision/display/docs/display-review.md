# display 实现审查与 render 测试结果

**主机上的五组 `render()` 检查全部通过。** `Display::show()` 的异常约定已按 SDK 可观察的返回值收窄。MaixCAM 后端推帧失败时，SDK 可能返回 `ERR_NONE`，因此封装正常返回并不能确认上屏成功。本次决定通过 README 和头文件说明这一限制，沿用现有 SDK 和封装实现。

生产实现的审查基线为本项目 `ef856e2` 和本地 MaixCDK `30f4b8b7e3f6`。以下 SDK 结论来自 MaixCAM-Pro 默认屏的 `PLATFORM_MAIXCAM` 源码路径，未做板端推屏实测。

## 推帧错误在哪里丢失

1. 项目把 BGR888 图交给 SDK。SDK 进入格式转换分支，得到 RGB888 图，再调 `_impl->show(*show_img, fit)`。[项目封装](../src/display.cpp#L50) · [SDK 格式分支][sdk-show]
2. 默认视频层收到 RGB888 后调用 `mmf_vo_frame_push_with_fit`。该函数返回非零时，后端记录 `mmf_vo_frame_push failed`，并返回 `ERR_RUNTIME`。[MMF 的 RGB888 分支][mmf-push]
3. SDK 忽略 `_impl->show` 的返回值，删除临时图，返回先前的 `e`。屏幕已打开时，`e` 一直是 `ERR_NONE`。项目的 `e != ERR_NONE` 判断因此不会抛错，调用方无法从这次返回判断帧是否已送出。即使在项目侧提前转为 RGB888，SDK 的直通分支也同样忽略返回值。[SDK 返回路径][sdk-show] · [项目错误检查](../src/display.cpp#L53)

头文件和 README 现在都约定，SDK 的公共 `show()` 返回非 `ERR_NONE` 时，封装抛出 `std::runtime_error`。SDK 或 OpenCV 直接抛出的异常按原类型传播。[接口声明](../include/display/display.hpp#L56) · [README](../README.md#行为)

## MaixCDK API 逐项核对

| 核对项 | 项目调用和 SDK 行为 | 源码 |
| --- | --- | --- |
| 默认构造与打开异常 | SDK 默认宽高均为 −1、格式 RGB888、设备名为空、自动打开。空设备名在 MaixCAM 选择 MMF。构造中的 `open()` 返回错误时，`check_raise` 抛出 `maix::err::Exception`，它继承 `std::exception`。项目捕获后以 `display open failed: ` 加原诊断重新抛出 `std::runtime_error`。 | [项目构造](../src/display.cpp#L27) · [默认参数][sdk-ctor-decl] · [构造与打开][sdk-ctor] · [异常类型][sdk-error] · [check_raise][sdk-check] |
| `width()` / `height()` | 两者返回 `int` 并转发给后端。MMF 从面板配置取尺寸，计入旋转，保存逻辑宽高。Pro 的面板项为 480×640，默认逻辑宽高为 640×480。项目按宽、高顺序传给 `cv::Size`，每次调用都查询设备。 | [项目取尺寸](../src/display.cpp#L44) · [SDK 转发][sdk-size] · [面板尺寸][mmf-panel] · [旋转][mmf-rotate] · [MMF 初始化][mmf-ctor] · [宽高访问器][mmf-size] |
| BGR888 → RGB888 | 项目按 Mat 的 BGR 通道顺序标记 `FMT_BGR888`。MaixCAM SDK 将其交给 `to_format(FMT_RGB888)`，为结果分配图像，选择 `cv::COLOR_BGR2RGB` 并执行 `cvtColor`。后端调用返回后，SDK 删除临时图。 | [项目包装](../src/display.cpp#L50) · [SDK show][sdk-show] · [通道转换][sdk-bgr] · [目标分配][sdk-dst] · [执行转换][sdk-cvt] |
| `Image` 的 `data_size` | 对未压缩格式，正的 `data_size` 必须精确等于宽 × 高 × 每像素字节数，否则抛 `ERR_ARGS` 对应的 SDK 异常。BGR888 每像素 3 字节。按接口传入 `CV_8UC3` 时，`resize` 写入新建的连续 Mat，项目传 `cols * rows * 3` 与其字节数一致。640×480 的实参应为 921,600 字节。 | [渲染目标](../src/render.cpp#L59) · [构造实参](../src/display.cpp#L51) · [字节数表][sdk-bpp] · [精确匹配检查][sdk-bytes] |
| `copy=false` 的所有权和期限 | 传入非空指针时，SDK 直接保存该地址并置 `_is_malloc=false`。析构只释放自有内存。项目先构造 `screen`，再构造借用它的 `img`，后者先析构。转换读取 BGR 数据时 Mat 仍存活，交给 MMF 的 RGB 临时图有独立像素。 | [项目变量作用域](../src/display.cpp#L45) · [借用分支][sdk-borrow] · [析构][sdk-free] · [SDK 消费图像][sdk-show] |
| `ERR_NONE` 与异常 | `ERR_NONE` 的值为 0。SDK 自动重开屏幕返回错误或格式转换返回空指针时，公共 `show()` 会返回非零，项目将该码写进 `std::runtime_error`。MMF 推帧错误会在上一节所述路径丢失。另外，RGB 图像分配失败可以直接抛出 `maix::err::Exception`，项目的 `show()` 没有捕获它。 | [项目检查](../src/display.cpp#L53) · [SDK 错误码与异常][sdk-error] · [重开与转换][sdk-show] · [图像分配][sdk-alloc] |

构造异常还有一处 SDK 资源回收风险。SDK 先用 `new` 创建 MMF 后端，再调用 `open()`。若后端已构造而 `open()` 报错抛出，SDK 的 `Display` 尚未完成构造，析构函数里释放后端的路径不会执行。项目捕获异常能提供打开失败的诊断，但同一进程反复尝试打开可能积累资源。[SDK 构造与析构][sdk-ctor] · [MMF 初始化][mmf-ctor] · [后端打开失败分支][mmf-open]

## render 接口与测试覆盖

`render.hpp` 声明的输出尺寸与实现一致。实现按 `sx`、`sy` 映射像素坐标，在缩放后的图上画叠加层。README 示意图与源码的四角文字行位一致。BGR 颜色、线宽、数值的两位小数和整数 Hz 也已逐项对照源码。文字测试按白字像素检查有无和位置，字符内容及数字格式由源码审阅核对。[渲染接口](../include/display/render.hpp#L15) · [图形](../src/render.cpp#L64) · [文字](../src/render.cpp#L86)

| 新增测试组 | 如何判定，能发现什么回归 |
| --- | --- |
| 输出尺寸与底图 | 用三块已知 BGR 颜色标出原帧 1/4 宽和 1/2 高的分界，在 640×480 和 720×360 输出上检查分界两侧与对角边缘。这样既能发现通道颠倒，也能发现输出宽高正确但底图裁切或加了黑边的情况。 |
| 非等比图形缩放 | 选 `sx=1/2`、`sy=3/4`，在预期屏幕坐标检查斜四边形四个顶点、四条边中点、圆周和箭杆。另在端点旁偏离箭杆的区域检查箭翼，可区分正确箭头、反向箭头和普通线段。 |
| 可选图形 | 在黑底上分别测全缺、仅端点、仅框、仅点、两点齐全。仅端点时与全缺的输出逐像素相同，仅点时圆周着色而圆外没有橙线。独立输入可发现框与跟踪点被错误绑定的情况。 |
| 固定文字行 | 全字段画面在四角 13 个行位都应有白字。逐一只保留跟踪状态、目标位置、目标速度或实际速度，并另测全部缺失。空行检查黑底，保留行与完整画面的同一行逐像素比较白字位置。缺 `actual` 时另外检查整个右下象限，防止残留标签。 |
| 输入帧完整性 | 同尺寸和缩放两条路径都在带行间距的 ROI 上绘图。与连续副本的结果比较可发现错误的行跨度处理。绘制后、主动改写返回图后均将整个父图与事前快照比较，可发现原地写入和返回图共享像素。 |

## 运行结果

在 Linux x86_64 主机上，使用 GCC 13.3.0、CMake 3.28.3、OpenCV 4.6.0，以 Release 配置构建 `display_render_test`。CTest 运行 **1/1 通过，0 失败**，详细输出中的五个测试组均通过。该目标编译测试入口和 `render.cpp`，覆盖范围止于返回的 BGR Mat。[测试源码](../tests/render_test.cpp) · [复现命令](../tests/README.md#运行) · [CMake 目标](../tests/CMakeLists.txt#L3)

另在临时目录中对五份独立的实现副本各引入一项改动，运行同一套测试。纵坐标误乘 `sx` 和箭头反向均被“非等比图形缩放”检出，跟踪点错误依赖四边形被“可选图形”检出，缺跟踪状态时 mode 上移被“固定文字行”检出，同尺寸时复用输入像素被“输入帧完整性”检出。五次运行均按预期返回失败。

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
