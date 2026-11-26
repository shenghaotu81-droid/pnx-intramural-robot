# 相机库接口说明

## 定位

camera 库是一个转换器：把 MaixCDK 相机（`maix::camera::Camera` + `maix::image::Image`）转成管线能直接吃的帧（BGR Mat + frame_id + timestamp_us）。只做转换和生命周期管理，不做判断。

## 输出

`Frame` 的三个字段 1:1 对应 `PipelineInput` 的三个字段，app 直接传过去。

```cpp
namespace camera {

struct Frame {
    cv::Mat image;          // 借用当前 Image 的 BGR 像素，CV_8UC3，尺寸同配置
    uint64_t frame_id;      // 每个相机实例从 0 开始，每次成功读帧加 1
    int64_t timestamp_us;   // 单调时钟，微秒
};

class Camera {
public:
    explicit Camera(const CameraConfig& config);
    Frame read();  // 阻塞读取一次，失败抛 std::runtime_error
};

}  // namespace camera
```

## 行为

构造时依次创建 SDK Camera、显式 `open()`、调用 `skip_frames()` 预热。任一阶段失败均抛 `std::runtime_error`，消息包含阶段（`construct`、`open` 或 `skip_frames`）及 SDK 原因。预热帧不计入 `frame_id`。

每次 `read()` 调用一次 SDK 的无参阻塞读取。SDK 分配 Image，将驱动的 RGB 像素换序写入紧密排列的 BGR888 数据。封装持有该 Image，用 `cv::Mat` 直接引用其像素，交给管线时没有额外的整帧拷贝。`timestamp_us` 取自读取完成时的单调时钟。

**借用期限。** `Frame.image` 及其所有 Mat 浅拷贝在下次 `read()` **开始时**或所属 Camera 析构时失效，以先发生者为准，即便这次读取随后抛出异常。顺序主循环应先处理完当前帧再读。跨帧保存、异步处理或在销毁 Camera 后继续使用像素，须在借用期内执行 `frame.image.clone()`。

**读取失败由 app 处理。** SDK 抛出异常、返回空图像、像素地址为空，或图像格式及尺寸与配置不符时，`read()` 抛出带原因的 `std::runtime_error`。失败不消耗帧号。库内每次只读一次，app 可决定再次读取，或连续失败超 N 次后结束采集。新实例首个成功帧的帧号为 0。

当前 MaixCAM SDK 在取出驱动帧后才分配 Image 像素。若分配失败，驱动帧可能未归还，单缓冲下后续读取可能继续超时。源码确认 `vpss_destroy_grp` 不回收未归还的 VB 块，重建大概率不能恢复采集，故 app 不做重建，连续失败超 N 次即退出。[实施报告](docs/read-refactor-report.md)

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
