# MaixCDK API 与实现

## 采集与编码

`Camera(1440, 1080, FMT_YVU420SP, nullptr, 30, buffers, true, false)` 打开相机，默认缓冲数 1。1.2.0 从 `Camera::get_channel()` 取通道号，使用与该 SDK 相同的组号 0、150 ms 超时调用 `CVI_VPSS_GetChnFrame`。成功后立即记录软件时间，保留描述符自带的 PTS 和 TimeRef。`Nv21Frame` 拥有该引用，归还时调用 `CVI_VPSS_ReleaseChnFrame`。数据面接受原生宽 1440 或 1472、高 1080、未压缩线性 NV21，并核验地址、行距、平面长度与裁剪偏移。 [相机 API](https://wiki.sipeed.com/maixcdk/api/maix/camera.html) · [固定版本相机后端](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_camera_mmf.cpp#L1196)

开始录像前按原生宽度建无路径 H.265 CBR 编码器，30 fps、6 Mbps、GOP 30。采集线程把描述符包装为非拥有的 `pipeline::Frame` 交给 `Encoder::push`，成功后立即归还 VPSS 帧。取流线程复制压缩片段并释放 SDK 流。下一次提交要等上一笔输入已取流并入队，同一 VENC 通道只有一笔待取输出。独立的写线程处理 SPS 裁剪和三个输出文件，1.3.0 的压缩队列上限是 16 帧。SDK 依赖由 `vision`、`opencv`、`FFmpeg`、`sophgo-middleware` 组件提供。 [原生帧包装及释放语义](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_pipeline_maixcam.cpp#L257-L300) · [SDK 录像器归还顺序](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_video_mmf.cpp#L3578-L3590) · [编码后端](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_video_mmf.cpp#L1343)

原生帧报告宽 1440 时，VENC 直接按 1440 编码，描述符仍携带实际行距。报告宽 1472 时，VENC 按 1472 编码，应用调用 SDK 随附的 FFmpeg `hevc_metadata` 位流过滤器设置 SPS 右裁剪 32 像素。过滤器只处理含 SPS 的访问单元。最终可见尺寸为 1440×1080，裁剪不重编码。两种情况都经原生帧接口，绕开 `mmf_enc_h265_push` 的紧密内存复制分支。 [FFmpeg HEVC 元数据过滤器](https://ffmpeg.org/ffmpeg-bitstream-filters.html#hevc_005fmetadata) · [已定位的行距故障](../../board_capture/docs/green-video-report.md)

1.3.0 在编码器初始化后读取一次 ISP 管道 0 的 `CVI_ISP_GetPubAttr`（配置帧率）及 `CVI_ISP_QueryExposureInfo`（曝光时间）。两项可独立失败，失败项在首行以 −1 表示，录像继续。应用随后按请求的相机缓冲数取出并归还候选帧，用下一个有效帧建立录像时间原点。SDK 的相机后端按请求缓冲数配置通道池，改变传感器帧率则涉及特定型号的寄存器。此次只查询配置。 [固定版本的相机开通道](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_camera_mmf.cpp#L847-L890) · [ISP 帧率设置实现](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_camera_mmf.cpp#L1412-L1441)

## 预览和拍照

纯录像使用驱动的物理描述符。CPU 需要像素时，从 Y 平面物理地址开始调用一次 `CVI_SYS_MmapCache`，按两平面的物理地址差定位 VU，归还前解除映射。固定版本随包 `libsys.so` 的 SHA-256 为 `c1faab8e19593aa5cca7050c9c89d64846e16a4a207784b0e12de81587c31ffe`，该函数内部已对映射范围执行缓存失效。 [映射实现](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/3rd_party/sophgo-middleware/sophgo-middleware/v2/modules/sys/src/cvi_sys.c#L585-L594) · [二进制核验](../../evidence/capture_cpp/validation/libsys-mmapcache.txt)

预览分别采样有效的 Y 和交错 VU 行，向 UI 发布一份紧密 NV21 小图。录制时小图最大 240×180，每 125 ms 至多更新一次，待机按预览框尺寸采样。UI 线程用 `COLOR_YUV2RGB_NV21` 转色，在 1.3.0 中，待机预览直接写 RGB 画布的预览区域，录像小图复用 RGB 缓冲后按最近邻放大到同一区域。Photo 和 Exit 在初始化时绘制，录像按钮在切换状态时重绘。触屏按下沿触发一次。 [显示 API](https://wiki.sipeed.com/maixcdk/api/maix/display.html) · [触屏 API](https://wiki.sipeed.com/maixcdk/api/maix/touchscreen.html)

拍照才复制一份紧密排列的 1440×1080 NV21，再在后台转成 BGR 并用 OpenCV 编码 JPG。预览和单拍转换均在录像编码链路之外。应用只允许一张照片在后台保存，避免积压多张全尺寸图。

## 文件与时间

`/root/capture_cpp/` 下，H.265、标定 CSV 和分段计时 TSV 录制时先写隐藏 `.part` 文件。正常停止或退出会等最后一帧取流、写入完成，关闭并发布 CSV、TSV，最后发布同名录像。写入或编码出现致命错误时清理未完成的文件。每个成功写入的访问单元对应一行 `t_us` 和驱动原始 `vpss_pts`。本地单调时间在 `CVI_VPSS_GetChnFrame` 成功返回后立即采样，和文件名用的墙上时间分别记录。
