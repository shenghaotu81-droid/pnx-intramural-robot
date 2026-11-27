# C++ 板端采集证据

来源为电脑下载目录 `maix-capture-cpp/tests-evidence`。按同一趟录制的原文件名、时间、进程尾号与配套封装归档。1970 年是这些文件名里的板端墙钟日期，帧间隔取 CSV 单调时钟。

| 目录 | 板端原件与验证材料 |
| --- | --- |
| [v1.0.0](v1.0.0/) | 2 张 JPG、原始 H.265、标定 CSV，以及配套的 `test1.mp4`。550 帧，38.328876 s。 |
| [v1.1.0](v1.1.0/) | 1 张 JPG、原始 H.265、标定 CSV、计时 TSV，以及配套的 `test2.mp4`。1,238 帧，62.502680 s。 |
| [validation](validation/) | 主机使用的 1440 宽合成码流、随包 libsys 的缓存映射反汇编。1472 宽的合成夹具来自较早的 [MaixPy 裁剪验证](../board_capture/validation/padded.h265)。 |

每个版本的 `profile.txt` 由 [profile_capture.py](../../capture_cpp/scripts/profile_capture.py) 从该目录原件生成。[媒体解码校验](media-decode.tsv) 记录 FFprobe 包数、尺寸和 FFmpeg 整段解码像素 SHA-256。同一趟的 MP4 与裸流解码哈希相同。各阶段耗时和因果边界见[性能报告](../../capture_cpp/docs/pop-performance.md)。

在本目录执行 `sha256sum -c SHA256SUMS` 检查归档文件。下列命令可重新核对新版实拍。

```bash
python3 ../../capture_cpp/scripts/profile_capture.py v1.1.0/*.csv
ffprobe -v error -count_frames -show_entries stream=width,height,coded_width,nb_read_frames v1.1.0/*.h265
ffmpeg -v error -xerror -i v1.1.0/*.h265 -f hash -hash sha256 -
```
