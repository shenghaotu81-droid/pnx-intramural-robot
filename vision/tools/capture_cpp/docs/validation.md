# 设计核验与验证记录

## 旧样片与目标

Python 迭代样片 `video_20261126_172653_855857_960bd3de.h265` 能解码 98 帧。FFprobe 显示可见 1440×1080、内部编码宽 1472。配套 MP4 有同样的 98 帧，标称 30 fps，播放时间 3.2666 秒。裸流及 MP4 均无法还原开始和结束采集的墙上时间。C++ 版记下每帧时间，便于区分编码播放速率、采集速率与采集间隔中的空档。

目标参数采用比赛相同的 1440×1080、请求 30 fps、单缓冲及 10 帧预热。比赛配置用 BGR888，C++ 采集应用为了直接送硬件编码而请求 NV21。仓库历史记录中的 NV21 三缓冲通道曾打开失败，当前单缓冲与三缓冲使用不同的缓冲池配置。 [比赛配置](../../../camera/config/camera.yaml) · [历史开通道记录](../../../docs/resolution-change-impact.md)

## 本机验证

| 检查 | 结果 |
| --- | --- |
| MaixCDK `83f8b8d` 交叉编译和打包 | `maixcam` Release 编译、链接和打包通过。ZIP 中的 `capture_cpp` 是 RISC-V musl 可执行文件，内置输出路径为 `/root/capture_cpp`。 |
| NV21 平面与拍照 | 用模拟的 1472 字节 Y/VU 行距、1440 有效像素验证了拷贝、预览转色和 1440×1080 JPG。模拟越界的 VU 地址被拒绝。 |
| 编码数据与时间 | 以两套 12 帧的合成 HEVC 包模拟硬件返回，将起始码拆在两个片段中。1440 直通和 1472 裁剪都生成可完整解码的 1440×1080 录像，与对应 FFmpeg 参考输出逐像素一致。两套 CSV 各有 12 行，帧号和微秒值对齐。 |
| 录像错误收尾 | 首包缺少 SPS 时，下一次提交或直接停止都能取到后台错误。空录像和失败录像都清理暂存文件。 |
| 1.1.0 并行收尾与计时 | 模拟取流阻塞 4 ms，检查调用方在取流前已归还相机帧。上一帧仍在输出时可以开始取下一帧，两次编码请求保持串行。停止后完整解码 12 帧，CSV 与 TSV 的帧号和 `t_us` 一致，日志反映了注入的等待。 |
| 550 帧实拍文件回放 | 在主机用原始 H.265 的 550 个访问单元模拟编码器输出，注入旧 CSV 的时间。直通和裁剪两条路径都生成 550 行 TSV，生成的三列 CSV 与原文件逐字节相同，整段解码后的像素 SHA-256 与原录像相同。 |

## 1.2.0 验证

1.0.0 和 1.1.0 的实机录像分别解出 550 与 1,238 帧，对应 14.32 与 19.79 fps。1.2.0 已用 MaixCDK `83f8b8d` 交叉编译并生成 RISC-V 安装包。可执行文件的运行库路径含 `$ORIGIN/dl_lib`，ZIP 内有直接使用的 `libsys.so` 和 `libvpu.so`。 [实拍原件](../../evidence/capture_cpp/README.md) · [1.2.0 优化报告](pop-performance.md)

主机测试使用真实 Sophgo 头文件，模拟 VPSS 和 VENC。两次原始录像分别按 1440 直通和 1472 裁剪回放。四组均保留原始三列 CSV 字节和解码像素哈希，TSV 逐行与 CSV 对齐。队列曾达到 8/8，停止后尾帧完整。另测无数据、非法平面布局、映射失败、单次映射同时采样预览与 JPG、先归还帧再取流、编码取流失败与限制文件大小造成的实际写失败。错误收尾清理未发布的录像文件组。

在 Linux 主机安装 Python 3、g++、pkg-config、FFmpeg 命令行及 `opencv4`、`libavcodec`、`libavutil` 的开发包。从仓库根目录执行。脚本优先读取 `MAIXCDK_PATH`，缺省使用相邻的 MaixCDK 仓库。临时可执行文件和回放输出在系统临时目录创建，测试结束自动清理。

```bash
python3 vision/tools/capture_cpp/tests/run_host.py
```

## 1.3.0 验证

1.2.0 已有单缓冲实拍。1,294 个软件帧号和时间戳逐行核对通过，H.265 和 MP4 完整解码到相同像素。1.3.0 的主机回放使用全部三版实拍原件，分别走 1440 直通和 1472 裁剪，共六组。每组的标定 CSV 和解码像素均与对应原件相同，压缩队列达到过 16/16，停止后尾帧完整。另验了按 1、2 个缓冲归还启动候选帧、首行诊断字段、准备阶段停止、磁盘写入和编码取流故障的清理。等尺寸转色与小图放大两种预览在有行距的 RGB 画布上，与原先先转色再拷贝的参考画面逐像素相同，预览框外边缘也一致。 [六组回放记录](../../evidence/capture_cpp/validation/host-replay-v1.3.0.txt) · [实测与收尾判据](final-round.md)

使用 MaixCDK `83f8b8d` 生成 1.3.0 的 MaixCAM Release 包。ZIP 通过 CRC 校验，包内 ELF 和 `app.yaml` 与构建输出和源码相同。可执行文件为 64 位 RISC-V musl，动态加载路径含 `$ORIGIN/dl_lib`。36 个随包库与已上板的 1.2.0 逐字节相同，`libisp.so` 导出 `CVI_ISP_GetPubAttr`，`libae.so` 导出 `CVI_ISP_QueryExposureInfo`。此版本的真实吞吐、启动时差和显示效果等待实机确认。 [可安装的交付包](../releases/capture_cpp_v1.3.0.zip) · [版本包 SHA-256](../releases/SHA256SUMS)

## 板上验收

| 步骤 | 检查 |
| --- | --- |
| 从板端菜单启动 | 画面与按钮正常，启动日志显示原生宽高和 Y/VU 行距。留意 `mmf add vi channel failed` 等底层报错。 |
| 用相同相机、场景和存储介质录约 60 秒，准备提示结束后让 MARK 以 0.5 m/s 匀速通过 | 对照 1.2.0 的 25.03 fps、P95 47.475 ms 和最长 81.127 ms，记录本次帧率、P95 和最长间隔。核对启动排空数、首两个 PTS 和软件间隔、队列高水位及 TimeRef 跳变。 |
| 录像中拍一张 JPG，随后停止并退出 | 视频、CSV、计时 TSV 和 JPG 都写入 `/root/capture_cpp/`。重新打开可以再次录制。 |
| 核对导出的录像和 CSV | H.265 可以整段解码，可见尺寸 1440×1080，帧数等于 CSV 数据行数。观察 MARK 边缘、颜色以及相邻帧时间。 |

在仓库根目录检查导出的每段录像。FFprobe 的解码帧数应等于脚本报告的 CSV 帧数，有配套 TSV 时脚本同时核对其帧号和采集时间。 [日志读法](timing.md)

```bash
for video in "$HOME"/Downloads/maix-capture-cpp/video_*.h265; do
    ffprobe -v error -count_frames -select_streams v:0 \
        -show_entries stream=codec_name,width,height,coded_width,nb_read_frames \
        -of default=nw=1 "$video"
    ffmpeg -v error -xerror -i "$video" -f null -
    python3 vision/tools/capture_cpp/scripts/profile_capture.py "${video%.h265}.csv"
done
```
