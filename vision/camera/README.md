# 相机库接口说明

## 定位

camera 库是一个转换器：把 MaixCDK 相机（`maix::camera::Camera` + `maix::image::Image`）转成管线能直接吃的帧（BGR Mat + frame_id + timestamp_us）。只做转换和生命周期管理，不做判断。

## 输出

`Frame` 的三个字段 1:1 对应 `PipelineInput` 的三个字段，app 直接传过去。

```cpp
namespace camera {

struct Frame {
    cv::Mat image;          // BGR, CV_8UC3, 1440x1080
    uint64_t frame_id;      // 从 0 开始，每帧 +1
    int64_t timestamp_us;   // 单调时钟，微秒
};

class Camera {
public:
    explicit Camera(const CameraConfig& config);
    Frame read();  // 阻塞读一帧；读不到抛 std::runtime_error
};

}  // namespace camera
```

## 行为

- **构造**：按 config 打开 MaixCDK 相机，打开失败抛异常；打开后调用 `skip_frames` 丢启动不稳定帧。
- **read()**：阻塞等下一帧，主循环的驱动。零拷贝：用 `read(buff)` 直接读进库里的 buffer，`Frame.image` 引用这块 buffer，下一次 `read()` 会覆盖。app 必须在一帧处理完之前用完它（主循环是顺序的，天然满足）。`read(buff)` 返回一个 `Image*`，库里用完必须 `delete`（只释放 Image 外壳，不释放 buffer——buffer 是库里 cv::Mat 的，下一帧复用）；不 delete 会每帧漏一个 Image 对象。
- **读不到帧**：底层返回 nullptr 时抛 `std::runtime_error`。
- **格式**：构造相机时指定 `FMT_BGR888`，直接出 BGR；驱动出不了 BGR888 时 MaixCDK 内部转（Camera 有 `_format_impl`），库里不用再 cvtColor。app 拿到的永远是 BGR。
- **时间戳**：相机 API 没有硬件时间戳，库里用 frame_id 计数器 + 单调时钟（steady_clock）打。

## 配置 camera.yaml

字段（缺字段留结构体默认值，缺文件抛错），加载器在库里：

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| width | 1440 | |
| height | 1080 | |
| format | "BGR888" | 字符串，库里映射到 MaixCDK 格式枚举 |
| fps | 30 | 1440x1080 下实际就是 30fps |
| buff_num | 1 | 延时最低，读最新帧 |
| skip_frames | 10 | 启动时丢 10 帧（约 1/3 秒），等自动曝光收敛 |

## 构建

只编给板子用：笔记本上交叉编译（`maixcdk build`），出板子用的 RISC-V 可执行文件，检查就是交叉编译能不能通过。不编 x86 给笔记本跑的那份。MaixCDK 的调用只出现在实现文件里，app 和其他库不 include MaixCDK 头文件。
