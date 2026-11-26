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
| 录像错误收尾 | 首包缺少 SPS 时拒绝发布文件，并清理视频和 CSV 暂存文件。 |

本机结果验证了构建与数据处理。NV21 单缓冲实际开通道、原生 VENC 吞吐和持续帧率仍需用目标板与相机组合测量。

## 板上验收

| 步骤 | 检查 |
| --- | --- |
| 从板端菜单启动 | 画面与按钮正常，启动日志显示原生宽高和 Y/VU 行距。留意 `mmf add vi channel failed` 等底层报错。 |
| 让 MARK 以 0.5 m/s 匀速通过，录一段约 20 秒的视频 | 录像计时与实际采集相近，FPS 接近 30。预览更新节流，跟踪取录像中的每帧。 |
| 录像中拍一张 JPG，随后停止并退出 | 视频、CSV 和 JPG 都写入 `/root/capture_cpp/`。重新打开可以再次录制。 |
| 核对导出的录像和 CSV | H.265 可以整段解码，可见尺寸 1440×1080，帧数等于 CSV 数据行数。观察 MARK 边缘、颜色以及相邻帧时间。 |

电脑上在导出目录执行以下检查。同名 CSV 的 `t_us` 用于计算测得帧率和帧间隔，视频的播放时间仍按 30 fps 解释。

```bash
cd "$HOME/Downloads/maix-capture-cpp"
for video in video_*.h265; do
    ffprobe -v error -count_frames -select_streams v:0 \
        -show_entries stream=codec_name,width,height,coded_width,nb_read_frames \
        -of default=nw=1 "$video"
    ffmpeg -v error -xerror -i "$video" -f null -
    python3 - "${video%.h265}.csv" <<'PY'
import csv
import statistics
import sys

with open(sys.argv[1], newline="") as source:
    frames = list(csv.DictReader(source))
times = [int(frame["t_us"]) for frame in frames]
gaps = [end - start for start, end in zip(times, times[1:])]
print(f"{sys.argv[1]}: {len(times)} frames")
if gaps and times[-1] > times[0]:
    print(f"{(len(times) - 1) * 1e6 / (times[-1] - times[0]):.2f} fps, "
          f"median gap {statistics.median(gaps) / 1000:.1f} ms, "
          f"max gap {max(gaps) / 1000:.1f} ms")
PY
done
```
