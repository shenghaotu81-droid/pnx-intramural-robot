# MaixPy API 与实现说明

## 相机与拍照

入口以 `camera.Camera(1440, 1080, image.Format.FMT_BGR888, fps=30, buff_num=1)` 创建相机。仓库已有相同宽高、格式和缓冲数的出图记录，选择 BGR888 单缓冲沿用这条路径。MaixCDK 在驱动通道和 Python 图像之间处理内部宽对齐。程序用 `Camera.read(block_ms=150)` 取图，逐帧核对 `Image` 的宽高和格式。分辨率固定在入口的常量中。[仓库板测背景](../../../docs/resolution-change-impact.md) · [相机 Python API](https://wiki.sipeed.com/maixpy/api/maix/camera.html#read) · [MaixCAM 相机后端](https://github.com/sipeed/MaixCDK/blob/main/components/vision/port/maixcam/maix_camera_mmf.cpp)

拍照将本轮完整 BGR888 帧交给 `Image.save(path, quality=95)`。MaixCDK 对 BGR888 直接调用 `cv::imwrite`，由 `.jpg` 后缀选择 JPEG。应用检查返回值为 `err.Err.ERR_NONE`，成功后将同目录的临时文件改名。[Image.save](https://wiki.sipeed.com/maixpy/api/maix/image.html#save) · [MaixCDK Image 实现](https://github.com/sipeed/MaixCDK/blob/main/components/vision/src/maix_image.cpp)

## 录像

MaixCAM 的 `video.Encoder` 要求输入 `image.Format.FMT_YVU420SP`，即 NV21。录像开始后分配 1472×1080 的 BGR888 编码画布，每帧把 1440×1080 原图复制到左边，右侧保留 32 像素黑色填充。`Image.to_format(FMT_YVU420SP)` 将整张画布转成 NV21，底层先转 YV12，再交错 V、U 平面。每段录像创建一个 1472×1080 的 `Encoder`，显式传入 `VIDEO_H265_CBR` 和 `block=True`。[MaixPy 录像指南](https://wiki.sipeed.com/maixpy/doc/en/video/record.html) · [Encoder 和 Frame API](https://wiki.sipeed.com/maixpy/api/maix/video.html#encoder) · [格式转换源码](https://github.com/sipeed/MaixCDK/blob/main/components/vision/src/maix_image.cpp)

`hevc_crop.py` 在 `encode(nv21)` 返回的 Annex B 字节中找到 SPS，将右裁剪增加 32 像素，保留原有裁剪和其他 NAL 单元。播放器输出 1440×1080，原画面无缩放。应用逐包写入码流，并在发布文件前检查是否见到了 SPS。板端只需要 MaixPy 和 Python 标准库。[H.265 裁剪规范](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-H.265-202407-S%21%21PDF-E&lang=e&type=items) · [绿屏分析与修复报告](green-video-report.md)

`Encoder` 的无路径模式直接输出编码字节。每段写入 `/root/capture/` 内的隐藏临时文件，`Stop REC` 或正常退出时销毁编码器、关闭文件并发布 `.h265` 文件。编码或写入报错时清理该段临时文件，界面回到可开始录像的状态。重启录像会创建新的硬件编码通道。系统掉电或进程被强制终止时，未收尾的临时文件可能留在目录中。[Encoder 构造与析构源码](https://github.com/sipeed/MaixCDK/blob/main/components/vision/port/maixcam/maix_video_mmf.cpp)

## 屏幕和触摸

`display.Display()` 使用设备默认屏幕。`CaptureUI` 用 `Display.width()` 和 `height()` 创建画布，`Image.resize()` 把原帧按 4:3 缩放到预览区。`draw_image()`、`draw_rect()` 和 `draw_string()` 在画布上合成预览、按钮和状态，再交给 `Display.show()`。同一套按钮矩形同时用于绘制和触控命中。[Display API](https://wiki.sipeed.com/maixpy/api/maix/display.html) · [显示源码](https://github.com/sipeed/MaixCDK/blob/main/components/vision/src/maix_display.cpp) · [Image API](https://wiki.sipeed.com/maixpy/api/maix/image.html)

`TouchScreen.read()` 返回 `[x, y, pressed]`。应用在未按下切换为按下时判定按钮，长按只执行一次。触屏后端负责把板子的触摸坐标转换为屏幕方向。`app.need_exit()` 也由主循环检查，退出时先收尾录像，再关闭触屏、显示和相机。[触屏指南](https://wiki.sipeed.com/maixpy/doc/en/vision/touchscreen.html) · [MaixCAM 触屏源码](https://github.com/sipeed/MaixCDK/blob/main/components/vision/port/maixcam/maix_touchscreen_maixcam.hpp) · [app API](https://wiki.sipeed.com/maixpy/api/maix/app.html)

## Python 应用打包

`app.yaml` 提供 `id`、名称、版本、作者、描述及 `files` 清单。`maixtool release` 以 `main.py` 为 Python 入口，并自动把 `app.yaml` 放入 ZIP。文档保留在源码目录。安装后，启动器通过 `python3` 执行 `/maixapp/apps/capture/main.py`。[应用格式与启动规则](https://wiki.sipeed.com/maixcdk/doc/convention/app.html) · [maixtool 打包源码](https://github.com/sipeed/MaixCDK/blob/main/tools/maixtool/maixtool/app_release.py)
