# camera tests

`camera_config_load_test` 测试配置加载器 `load_camera_config()`。只编 `config_loader.cpp`（OpenCV），不编 `camera.cpp`（MaixCDK，编给板子用的部分），所以测试能在笔记本上跑，不需要板子。

| 测试组 | 覆盖范围 |
| --- | --- |
| 完整配置 | camera.yaml 六个字段都写时，全部正确读入 |
| 缺字段留默认 | 缺字段时保留结构体默认值（width 1440、height 1080、format "BGR888"、fps 30、buff_num 1、skip_frames 10） |
| 缺文件抛错 | 文件不存在时抛错，错误信息带路径 |
| 未知格式抛错 | format 写了未知字符串时抛错 |

## 运行

相机库本身（`camera` 目标）只编给板子用（交叉编译），笔记本上只编测试目标：

```sh
cmake -S vision/camera -B /tmp/camera-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/camera-test --target camera_config_load_test
ctest --test-dir /tmp/camera-test --output-on-failure
```

测试源文件 `config_load_test.cpp` 及夹具由 Codex 编写。
