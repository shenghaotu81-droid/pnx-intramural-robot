# MaixPy 板端采集证据

| 目录 | 来源和用途 |
| --- | --- |
| [v1.0.0](v1.0.0/) | 初代板端正常 JPG、绿屏 H.265，以及当时用于播放器比较的 MP4。两段视频均解出 116 帧。 |
| [v1.0.1](v1.0.1/) | 行距修复后的板端 JPG、H.265 和配套 MP4。两个视频各有 98 帧，可见 1440×1080、编码宽 1472。 |
| [validation](validation/) | 绿屏分析使用的抽帧及 Y 平面、分量对照图、MP4 逐帧哈希、行复制反汇编。另存 12 帧合成输入 `padded.h265`、应用与 FFmpeg 参考裁剪输出及解码哈希，以及旧样片上做头部验证的派生码流。 |

原始 JPG 和裸流与下载目录 `maix-capture/tests-evidence` 的副本逐字节核对。配套 MP4 来自旧资料「图像采集工具／初代」与「迭代」。[媒体解码校验](media-decode.tsv) 给出原始媒体的尺寸、包数和 FFmpeg 整段解码 SHA-256。[旧版绿屏报告](../../board_capture/docs/green-video-report.md)

在本目录执行 `sha256sum -c SHA256SUMS` 检查归档文件。要重新生成用于分量分析的全量 YUV420P 中间数据，可从原始绿屏裸流无损展开。

```bash
ffmpeg -v error -xerror -i v1.0.0/*.h265 -pix_fmt yuv420p -f rawvideo /tmp/board_capture_v1.0.0.yuv
```
