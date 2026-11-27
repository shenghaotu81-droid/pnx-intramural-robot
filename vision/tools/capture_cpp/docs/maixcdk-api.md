# MaixCDK API 与实现

## 采集与编码

`Camera(1440, 1080, FMT_YVU420SP, nullptr, 30, 1, true, false)` 打开 NV21 通道。`Camera::pop(150)` 取出一个带 VPSS 物理地址、行距和 PTS 的原生帧。`std::unique_ptr` 管理帧，析构时释放映射并归还 VPSS 帧。应用接受原生宽 1440 或 1472，两个平面都按实际 `u32Stride` 访问。JPEG 和预览使用每行左侧 1440 个像素。 [相机 API](https://wiki.sipeed.com/maixcdk/api/maix/camera.html) · [相机后端](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_camera_mmf.cpp#L1196) · [原生帧与流](https://wiki.sipeed.com/maixcdk/api/maix/pipeline.html)

按下录像键后，使用预热帧的原生宽度在首次录像取帧前创建无路径的 H.265 CBR `Encoder`，目标 30 fps、6 Mbps、GOP 30。`Encoder::push(Frame*)` 通过 `mmf_venc_push2` 将原生描述符送到 VENC。`Encoder::pop()` 取回一个访问单元，读取各片段后马上释放流对象。采集线程完成本帧所需的预览采样和拍照拷贝，成功推帧后立即归还 VPSS 帧。后台线程取流并写文件，同时采集线程可进入下一次相机取帧。下一次编码提交会等上一帧的输出完成，同一编码通道始终只有一笔未收尾的提交。SDK 自带的 `VideoRecorder` 同样在 `mmf_venc_push2` 成功后释放调用方的相机帧。热路径只拼接压缩后的片段，无全尺寸转色或整帧像素拷贝。此 SDK 的 MaixCAM 实现使用 MMF 自己的取流等待逻辑。 [Encoder API](https://wiki.sipeed.com/maixcdk/api/maix/video.html#push) · [SDK 原生帧归还顺序](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_video_mmf.cpp#L3578-L3590) · [MaixCAM 编码后端](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_video_mmf.cpp#L1343)

原生帧报告宽 1440 时，VENC 直接按 1440 编码，描述符仍携带实际行距。报告宽 1472 时，VENC 按 1472 编码，应用调用 SDK 随附的 FFmpeg `hevc_metadata` 位流过滤器设置 SPS 右裁剪 32 像素。过滤器只处理含 SPS 的访问单元。最终可见尺寸为 1440×1080，裁剪不重编码。两种情况都经原生帧接口，绕开 `mmf_enc_h265_push` 的紧密内存复制分支。 [FFmpeg HEVC 元数据过滤器](https://ffmpeg.org/ffmpeg-bitstream-filters.html#hevc_005fmetadata) · [已定位的行距故障](../../board_capture/docs/green-video-report.md)

## 预览和拍照

原生帧只映射了从 Y 开始的一段设备内存。`Nv21Frame` 使用两平面物理地址差和实际行距构造 OpenCV 视图，并在访问前校验地址落在映射范围内。MaixCAM 的 `Frame::to_image()` 在核验版本里未实现。 [MaixCAM Frame 源码](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_pipeline_maixcam.cpp#L315)

预览先分别缩小 Y 和交错 VU，再以 `COLOR_YUV2RGB_NV21` 转成屏幕用 RGB。录制时以至少 125 ms 的间隔更新预览，屏幕呈现和触控在独立于录像的线程，预览画面或界面状态变化时才重绘。显示用 `Display::show`，按钮坐标直接按显示尺寸计算，`TouchScreen::read` 的按下沿只触发一次。 [显示 API](https://wiki.sipeed.com/maixcdk/api/maix/display.html) · [触屏 API](https://wiki.sipeed.com/maixcdk/api/maix/touchscreen.html)

拍照才复制一份紧密排列的 1440×1080 NV21，再在后台转成 BGR 并用 OpenCV 编码 JPG。预览和单拍转换均在录像编码链路之外。应用只允许一张照片在后台保存，避免积压多张全尺寸图。

## 文件与时间

`/root/capture_cpp/` 下，H.265、标定 CSV 和分段计时 TSV 录制时先写隐藏 `.part` 文件。正常停止或退出会等最后一帧取流、写入完成，关闭并发布 CSV、TSV，最后发布同名录像。写入或编码出现致命错误时清理未完成的文件。每个成功写入的访问单元对应一行 `t_us` 和驱动原始 `vpss_pts`。本地单调时间在 `Camera::pop()` 返回后立即采样，和文件名用的墙上时间分别记录。
