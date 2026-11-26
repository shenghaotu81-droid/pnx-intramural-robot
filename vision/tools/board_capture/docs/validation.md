# 验证记录与板端验收

## 本机验证结果

| 检查 | 结果 |
| --- | --- |
| Python 语法与本地文档链接 | 三个运行模块均通过语法解析，Markdown 的仓库内链接均可解析。 |
| Sipeed `maixtool 1.4.4` 打包 | 成功生成 `capture_v1.0.0.zip`，核对 ZIP 的 `capture/` 内含 `app.yaml` 和三个运行模块，内容与源码一致。 |
| 模拟触屏、相机和编码器 | 在 640×480 和 552×368 的模拟屏幕验证按钮命中、长按只触发一次、录像中拍照、重复开始与停止、退出时收尾。也验证了坏帧尺寸被拒绝、拍照或编码失败清理临时文件。 |
| 模拟采集文件 | 用 `ffprobe` 确认测试 JPG 和 H.265 文件均为 1440×1080，并用 `ffmpeg` 解码测试录像。JPEG 和编码字节由本机测试替身生成。 |

本次未连接 MaixCAM 真机。安装启动、触屏坐标、硬件 H.265 编码画质和采集帧率由下列板端步骤确认。

## 板端验收

| 操作 | 通过条件 |
| --- | --- |
| 按 README 安装并从菜单打开 | 启动后看到完整 4:3 取景和三个触屏按钮，状态显示 `1440 x 1080`。 |
| 点按及长按 **Photo** | 每次按下新增一张 JPG，长按只生成一张。 |
| 点 **Start REC**，移动相机，期间拍一张照片，再点 **Stop REC** | 录制时红色按钮和计时可见。停止后有一段可解码的录像和一张 JPG。画面颜色、行对齐正确，采集文件中只有相机画面。 |
| 再次开始录像，随后点 **Exit** | 录像完成写入，应用回到菜单。重新打开后可以再次预览、拍照和录像，前两段文件各自可播放。 |

按 README 导出文件后，在电脑上使用 FFmpeg 工具确认媒体实际编码与分辨率。预期 JPG 为 `mjpeg`，H.265 为 `hevc`，宽和高分别是 `1440`、`1080`。

```bash
cd "$HOME/Downloads/maix-capture"
for file in photo_*.jpg video_*.h265; do
    printf '%s\n' "$file"
    ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name,width,height -of default=nw=1 "$file"
done
for file in video_*.h265; do
    ffmpeg -v error -xerror -f hevc -i "$file" -f null -
done
```

第二组命令逐段尝试完整解码。结合画面检查，可定位尺寸正确但颜色、行对齐或视频帧有异常的情况。
