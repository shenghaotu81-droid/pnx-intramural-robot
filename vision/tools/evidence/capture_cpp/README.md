# C++ 板端采集证据

1.0.0 与 1.1.0 原件来自下载目录 `maix-capture-cpp/tests-evidence`，1.2.0 的五个原件来自 `~/Downloads/maix-capture-cpp/`。同一趟的原文件名、时间、进程尾号与配套封装一并保留。1970 年是文件名里的板端墙钟日期，帧间隔取 CSV 单调时钟。

| 目录 | 板端原件与验证材料 |
| --- | --- |
| [v1.0.0](v1.0.0/) | 2 张 JPG、原始 H.265、标定 CSV，以及配套的 `test1.mp4`。550 帧，38.328876 s。 |
| [v1.1.0](v1.1.0/) | 1 张 JPG、原始 H.265、标定 CSV、计时 TSV，以及配套的 `test2.mp4`。1,238 帧，62.502680 s。 |
| [v1.2.0](v1.2.0/) | 1 张 JPG、原始 H.265、三列标定 CSV、计时 TSV 和 `test3.mp4`。1,294 帧，51.650681 s，默认单缓冲实测 25.03 fps。 |
| [validation](validation/) | 主机使用的 1440 宽合成码流、随包 libsys 的缓存映射反汇编和分版回放记录。1472 宽的合成夹具来自较早的 [MaixPy 裁剪验证](../board_capture/validation/padded.h265)。 |

每个版本的 `profile.txt` 从该目录的原始日志生成。媒体清单记录 FFprobe 包数、尺寸和 FFmpeg 整段解码像素 SHA-256。同一趟的 MP4 与裸流解码哈希相同。 [统计脚本](../../capture_cpp/scripts/profile_capture.py) · [媒体解码校验](media-decode.tsv) · [收尾轮实测报告](../../capture_cpp/docs/final-round.md)

1.2.0 的 `analysis/` 保存从原始 H.265 解出的逐帧像素哈希，以及按从左到右、从上到下排列的第 0、323、647、952、953、1,293 帧联系图。图中最后一行前两张是最长间隔两端。 [抽帧图](v1.2.0/analysis/sample-frames.jpg) · [逐帧哈希](v1.2.0/analysis/decoded.framehash)

在本目录执行 `sha256sum -c SHA256SUMS` 检查归档文件。下列命令可重新核对新版实拍。

```bash
python3 ../../capture_cpp/scripts/profile_capture.py v1.2.0/*.csv
ffprobe -v error -count_frames -show_entries stream=width,height,coded_width,nb_read_frames v1.2.0/*.h265
ffmpeg -v error -xerror -i v1.2.0/*.h265 -f hash -hash sha256 -
ffmpeg -v error -xerror -i v1.2.0/*.h265 -map 0:v:0 -fps_mode passthrough -f framehash -hash sha256 -
```
