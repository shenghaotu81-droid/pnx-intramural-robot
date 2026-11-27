# 高速板端采集

`capture_cpp` 是 MaixCAM／MaixCAM-Pro 的 MaixCDK C++ 应用，用于校内赛的动态 q 标定。相机固定请求 **1440×1080、30 fps、NV21、默认单缓冲**，启动后预热 10 帧。JPG 和 H.265 的可见尺寸都是 1440×1080。录像帧率以录制界面和随视频保存的逐帧时间为准。**v1.3.0 已完成板端验收。** 单缓冲手持实拍 62.204 秒、1,563 帧，实际 **25.11 fps**，P95 帧间隔 47.672 ms，最长 51.490 ms。 [板测对照与闭环报告](docs/board-acceptance-v1.3.0.md)

## 使用

从板端菜单打开 **高速图像采集（Capture C++）**。对准运动区域后，先按 **Start REC**。等顶部的 **Preparing video** 消失、录制计时开始，再让 MARK 匀速通过。应用在编码器就绪后先归还启动期间可能积压的相机帧。

| 按钮 | 操作 |
| --- | --- |
| **Photo** | 拍一张 JPG。录像可以继续，正在保存上一张时界面会提示。 |
| **Start REC / Stop REC** | 开始或结束一段 H.265 录像。录制时显示经过时间和实际写入帧率。 |
| **Exit** | 收尾当前录像并退出。 |

应用自动创建 **`/root/capture_cpp/`**。照片使用 `photo_*.jpg`，录像使用 `video_*.h265`，同名 `video_*.csv` 保存逐帧时间。每段录像还生成独立的 `video_*.timing.tsv` 性能日志。Python 版的静态采集和内参素材位于 `/root/capture/`。

做 q 标定时按文件名配对录像和 CSV。`frame_index` 从 0 开始，`t_us` 是相对首帧的单调采集时间，单位微秒。相邻两行的 `t_us` 差除以 `1e6` 得到秒。`vpss_pts` 保存驱动原始值，便于核对重复帧和间隔异常。H.265 裸流按码流的标称 30 fps 播放。MARK 在 0.5 m/s 匀速段内，每帧位移约为 `0.5 × Δt` 米，实际 `Δt` 由 CSV 提供。

分析采集速度时，在仓库根目录运行以下命令。脚本会自动寻找同名计时日志。旧版 CSV 也可以单独分析。 [计时字段和长间隔读法](docs/timing.md)

```bash
for csv in "$HOME"/Downloads/maix-capture-cpp/video_*.csv; do
    python3 vision/tools/capture_cpp/scripts/profile_capture.py "$csv"
done
```

## 构建与上板

可直接使用已校验的 1.3.0 安装包。电脑与板子连入同一局域网，在电脑的仓库根目录执行以下命令，然后用板端应用商店扫码。 [交付包](releases/capture_cpp_v1.3.0.zip) · [校验清单](releases/SHA256SUMS)

```bash
cd vision/tools/capture_cpp/releases
python3 -m pip install maixtool
sha256sum -c SHA256SUMS
maixtool deploy --pkg capture_cpp_v1.3.0.zip
```

使用 Sipeed MaixCDK `83f8b8d`，本项目使用该版本的 MaixCAM 原生帧 API、随附 FFmpeg、OpenCV 及 Sophgo middleware。CMake 已声明这些依赖。包内的 `libvpu.so` 和 `libsys.so` 提供 VPSS 取帧与缓存映射，`libisp.so` 和 `libae.so` 提供启动时查询 ISP 和曝光所需的接口。下面假设 `MaixCDK` 与比赛仓库并排存放。电脑需要 Python、CMake，以及官方快速入门列出的构建依赖。 [MaixCDK 快速入门](https://wiki.sipeed.com/maixcdk/doc/)

```bash
# 从比赛仓库根目录执行
export MAIXCDK_PATH="$(cd ../MaixCDK && pwd)"
python3 -m pip install maixtool
cd vision/tools/capture_cpp
maixcdk release -p maixcam --toolchain-id default
```

自行构建的安装包位于 `dist/capture_cpp_v1.3.0.zip`。电脑与板子在同一局域网时，可以在应用商店扫码安装。 [设备端扫码和命令行安装说明](https://wiki.sipeed.com/maixpy/doc/zh/basic/app.html)

```bash
maixtool deploy --pkg dist/capture_cpp_v1.3.0.zip
```

也可以用 SSH 安装。在应用源码目录执行，将示例地址 `192.168.1.50` 换成板子的实际地址。安装后从板端菜单打开应用。

```bash
BOARD=192.168.1.50
scp releases/capture_cpp_v1.3.0.zip "root@$BOARD:/root/"
ssh "root@$BOARD" 'app_store_cli install /root/capture_cpp_v1.3.0.zip'
```

采集结束后导出素材、标定 CSV 和计时日志。

```bash
mkdir -p "$HOME/Downloads/maix-capture-cpp"
scp -r "root@$BOARD:/root/capture_cpp/." "$HOME/Downloads/maix-capture-cpp/"
```

## 单缓冲与双缓冲对照

板端菜单默认单缓冲。若要比较 VPSS 在多一个可用缓冲时的交付节奏，先退出菜单中正在运行的采集应用，再通过 SSH 在板上运行下列命令。将 `2` 改为 `1` 可用相同启动方式采集对照组。

```bash
cd /maixapp/apps/capture_cpp
CAPTURE_CPP_CAMERA_BUFFERS=2 ./capture_cpp
```

两组尽量保持相同光照、运动、存储介质和录像时长。每份 TSV 都包含 `camera_buffers`、原生 VPSS 取帧耗时 `pop_us` 和按需映射耗时 `pixel_map_us`。用真实 fps、帧间隔及首帧软件与硬件的时间差来比较两组。1.3.0 还在首行记录 ISP 配置帧率和曝光时间，供区分相机配置与实际交付节奏。 [单缓冲板测结果](docs/board-acceptance-v1.3.0.md)

## 文档

- [MaixCDK API 与实现](docs/maixcdk-api.md)
- [设计核验、验证记录与实机验收](docs/validation.md)
- [1.0.0 实拍基线与 1.1.0 优化依据](docs/performance.md)
- [分版本的板端证据与 SHA-256 校验](../evidence/capture_cpp/README.md)
