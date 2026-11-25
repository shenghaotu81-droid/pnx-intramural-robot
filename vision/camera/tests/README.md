# camera tests

`camera_config_load_test` 测试配置加载器 `load_camera_config()`。只编 `config_loader.cpp`（OpenCV），不编 `camera.cpp`（MaixCDK，编给板子用的部分），所以测试能在笔记本上跑，不需要板子。

| 测试组 | 覆盖范围 |
| --- | --- |
| 完整配置 | `config/full.yaml` 核对六个字段，五个数值字段均使用非默认值 |
| 缺字段留默认及显式零值 | `config/partial.yaml` 只设置宽度和小数帧率，`config/zero_skip_frames.yaml` 只设置 `skip_frames: 0`。两次加载都核对其余字段的默认值 |
| 缺文件抛错 | 路径 `config/missing.yaml` 不存在，核对 `std::runtime_error` 及含完整输入路径的错误信息 |
| 未知格式抛错 | `config/unknown_format.yaml` 和 `config/lowercase_format.yaml` 分别使用 `FOO` 和 `bgr888`，核对异常类型及含原始格式值的错误信息 |
| 合法 YAML 无配置字段 | `config/no_fields.yaml` 含 OpenCV YAML 文件头、文档标记和注释，六个字段均取默认值 |
| 空文件和纯注释文件 | `config/empty.yaml` 为零字节，`config/comment_only.yaml` 只有一行注释，两者均触发 OpenCV 解析异常 |

## 运行

相机库本身（`camera` 目标）只编给板子用（交叉编译），笔记本上只编测试目标：

```sh
cmake -S vision/camera -B /tmp/camera-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/camera-test --target camera_config_load_test
ctest --test-dir /tmp/camera-test --output-on-failure
```

测试源文件 `config_load_test.cpp` 及夹具由 Codex 编写。
