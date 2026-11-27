# 1440×1080 H.265 绿屏分析与修复

## 样片证据

分析了 `初代/` 中的 `photo_20261126_163902_417431_dca1f089.jpg`、`video_20261126_163905_725551_03dd25b4.h265` 和同名 MP4。两个视频都能用 FFmpeg 软件解码完整读取 116 帧。按解码后的 YUV420P 数据逐帧做 SHA-256，比对结果为 116／116 一致。原始 H.265 的 SPS 声明 1440×1080、8 位 4:2:0，绿屏已经存在于原始码流。 [初代原件与验证材料](../../evidence/board_capture/README.md)

| 样片 | 亮度 Y | 色度 U/V |
| --- | --- | --- |
| 正常 JPG | 灰度图完整，可见笔记本屏幕、键盘和背景边缘 | 彩色图颜色正常 |
| H.265 第 1、4 帧 | 顶部是重复条纹，下方为噪声 | 两个平面均呈噪声分布 |
| H.265 第 2 帧 | 第 400 行到末行的所有样本均为 0 | 两个平面各约 98.3% 的样本为 0 |
| H.265 共 77 帧 | 自画面顶部数第 335 行出现最大亮度断层，第 401 行到末行的平均 Y 小于 6 | U 与 V 的帧均值均小于 4.3 |

单独调换 U/V 或将平面色度当作交错色度读取，都无法造成上述 Y 平面损坏。大段亮度归零与重复条纹指向编码器收到的输入缓冲内容异常。改封装、换播放器或调色无法恢复旧片中缺失的像素。

## 编码器行距分支

本地 MaixCDK `83f8b8d` 的 `Image::to_format(FMT_YVU420SP)` 在生成 YV12 后会显式交错 V、U。随后无路径的 H.265 `Encoder.encode(Image)` 把 NV21 指针和图像宽高传给 `mmf_enc_h265_push`。 [V/U 交错源码](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/src/maix_image.cpp#L1094) · [输入硬件编码器源码](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/vision/port/maixcam/maix_video_mmf.cpp#L873)

反汇编该提交随附的 `libmaixcam_lib.so`，SHA-256 为 `a55c72a05754a7ba8e040a3e2fdbc358d4ed530e248f319b513bc19ec1bb25a5`。分配路径按 64 字节对齐 NV21 行距，1440 字节紧密行进入 1472 字节目的缓冲。`mmf_enc_h265_push` 比较输入宽度与目的行距（`0x1e278`–`0x1e27a`）。两者不同时，循环每次都将目的偏移算成 `stride * height`（`0x1e294`–`0x1e2a8`），即 VU 区起点。循环递增源行指针，目的地址始终相同（`0x1e2ac`–`0x1e2b0`）。这会给 VENC 留下未写入的 Y 数据和大部分 VU 数据。宽度等于行距时，`0x1e2b2`–`0x1e2c6` 会整块复制 `width * height * 3 / 2` 字节。 [MaixCAM 随附库](https://github.com/sipeed/MaixCDK/blob/83f8b8d456813932526601a40757bdafe452e718/components/maixcam_lib/lib_maixcam/libmaixcam_lib.so)

这个缺陷解释了为何原始 H.265 的亮度和色度都坏，而使用 BGR888 直接保存的 JPG 正常。码流不包含板上共享库的版本或输入缓冲区内容，无法从样片核对库哈希或追溯每一行错误字节的来源。

## 应用改动

- 延用仓库已有出图记录的 1440×1080 BGR888 单缓冲采集。每段录像复用一张 1472×1080 的 BGR 画布，将原图复制到左侧并保留右侧 32 像素黑边。转换后的 NV21 图和编码器都使用 1472 宽，走整块复制分支。
- 逐包在 SPS 设置 H.265 一致性裁剪窗口，在原有右裁剪基础上增加 32 像素。其余编码参数、切片数据和原有裁剪保留。按 H.265 规范解码输出仍为 1440×1080，码流内部宽度为 1472。编码期间无需额外进程或转码工具。 [ITU-T H.265 规范](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-H.265-202407-S%21%21PDF-E&lang=e&type=items)

相机若直接交付宽度为 1440 的 NV21 `Image`，仍会进入同一个行距补齐分支。对这个缺陷，有效的绕过点在 `Encoder` 的输入宽度。验证方法和结果见 [验证记录](validation.md)。
