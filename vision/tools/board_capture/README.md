# 板端图像采集

`capture` 是面向 MaixCAM／MaixCAM-Pro 触屏设备的 MaixPy v4 应用。相机、JPG 照片和 H.265 录像均固定为 **1440×1080**。预览按屏幕大小缩放，按钮和状态只显示在屏幕上。

## 使用

在板端应用菜单打开 **图像采集（Capture）**。

| 按钮 | 操作 |
| --- | --- |
| **Photo** | 将当前原帧保存为 JPG，录像期间也可拍照。 |
| **Start REC / Stop REC** | 开始或结束一段录像。录制时按钮变红，屏幕显示 `REC` 和已录时间。 |
| **Exit** | 结束正在进行的录像，关闭应用并返回板端菜单。 |

应用会创建 `/root/capture/`。照片使用 `photo_*.jpg`，录像使用 `video_*.h265`。文件名包含板端时间和唯一尾缀，保存成功后会在屏幕提示并将完整路径打印到日志。每次开始录像都会生成新文件，结束时写入最终文件名。

录像为无音频的 H.265 裸码流，可用支持 HEVC 的播放器打开。相机和编码器均请求 30 fps，实际采集帧率取决于板端处理和写盘速度。

## 部署到板上

板端使用已安装 MaixPy v4 的 MaixCAM 系统，接好支持该分辨率的相机模组和触屏，并退出正在使用相机的应用。电脑使用 Python 3 和 Sipeed 的 `maixtool`。在仓库根目录执行下列命令。[MaixPy 应用开发与安装](https://wiki.sipeed.com/maixpy/doc/en/basic/app.html)

```bash
cd vision/tools/board_capture
python3 -m pip install maixtool
maixtool release
```

生成的安装包为 `dist/capture_v1.0.0.zip`。

### 同一局域网扫码安装

保持电脑与设备在同一局域网，在 `board_capture/` 下运行以下命令。打开板端 **应用商店（App Store）**，扫描终端中的二维码，安装后回应用菜单启动 **图像采集**。[官方局域网安装说明](https://wiki.sipeed.com/maixpy/doc/en/basic/app.html#local-qr-code-installation-for-local-package)

```bash
maixtool deploy --pkg dist/capture_v1.0.0.zip
```

### SSH 安装和导出

也可在 `board_capture/` 下把 ZIP 传到板上安装。将示例地址 `192.168.1.50` 改为实际板端地址。安装完成后从板端应用菜单启动。[官方本地安装说明](https://wiki.sipeed.com/maixpy/doc/en/basic/app.html#local-installation)

```bash
BOARD=192.168.1.50
scp dist/capture_v1.0.0.zip "root@$BOARD:/root/"
ssh "root@$BOARD" 'app_store_cli install /root/capture_v1.0.0.zip'
```

结束采集后，将文件下载到电脑的 `~/Downloads/maix-capture/`。

```bash
mkdir -p "$HOME/Downloads/maix-capture"
scp -r "root@$BOARD:/root/capture/." "$HOME/Downloads/maix-capture/"
```

## 代码与文档

| 文件 | 用途 |
| --- | --- |
| `app.yaml` | 安装包元数据和运行文件清单 |
| `main.py` | 应用入口、设备生命周期和事件循环 |
| `capture_media.py` | JPG 保存、H.265 编码与文件收尾 |
| `capture_ui.py` | 预览布局、按钮命中和状态显示 |
| [MaixPy API 与实现说明](docs/maixpy-api.md) | API 用法、源码依据和帧处理路径 |
| [验证记录与板端验收](docs/validation.md) | 已完成的检查和实机验收步骤 |
