# 高速板端采集

`capture_cpp` 是 MaixCAM／MaixCAM-Pro 的 MaixCDK C++ 应用，用于校内赛的动态 q 标定。相机固定请求 **1440×1080、30 fps、NV21、单缓冲**，启动后预热 10 帧。JPG 和 H.265 的可见尺寸都是 1440×1080。录像帧率以录制界面和随视频保存的逐帧时间为准。

## 使用

从板端菜单打开 **高速图像采集（Capture C++）**。对准运动区域后，在 MARK 开始匀速运动前按 **Start REC**。

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

使用 Sipeed MaixCDK `83f8b8d`，本项目用该版本的 MaixCAM 原生帧 API 和 SDK 随附 FFmpeg。下面假设 `MaixCDK` 与比赛仓库并排存放。电脑需要 Python、CMake，以及官方快速入门列出的构建依赖。 [MaixCDK 快速入门](https://wiki.sipeed.com/maixcdk/doc/)

```bash
# 从比赛仓库根目录执行
export MAIXCDK_PATH="$(cd ../MaixCDK && pwd)"
python3 -m pip install maixtool
cd vision/tools/capture_cpp
maixcdk release -p maixcam --toolchain-id default
```

安装包位于 `dist/capture_cpp_v1.1.0.zip`。电脑与板子在同一局域网时，可以在应用商店扫码安装。 [MaixCDK 应用安装说明](https://wiki.sipeed.com/maixcdk/doc/convention/app.html)

```bash
maixtool deploy --pkg dist/capture_cpp_v1.1.0.zip
```

也可以用 SSH 安装，将示例地址 `192.168.1.50` 换成板子的实际地址。安装后从板端菜单打开应用。

```bash
BOARD=192.168.1.50
scp dist/capture_cpp_v1.1.0.zip "root@$BOARD:/root/"
ssh "root@$BOARD" 'app_store_cli install /root/capture_cpp_v1.1.0.zip'
```

采集结束后导出素材、标定 CSV 和计时日志。

```bash
mkdir -p "$HOME/Downloads/maix-capture-cpp"
scp -r "root@$BOARD:/root/capture_cpp/." "$HOME/Downloads/maix-capture-cpp/"
```

## 文档

- [MaixCDK API 与实现](docs/maixcdk-api.md)
- [设计核验、验证记录与实机验收](docs/validation.md)
- [1.0.0 实拍基线与 1.1.0 优化依据](docs/performance.md)
