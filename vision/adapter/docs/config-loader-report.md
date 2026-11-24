# adapter 配置加载器

相机安装外参、各模式的期望位置和航向、扫描角速度都可在 `adapter.yaml` 中调整。调用方启动时加载一次并构造 `Adapter`，修改 YAML 后重启即可生效。

| 文件 | 改动 |
| --- | --- |
| `include/adapter/config_loader.hpp`、`src/config_loader.cpp` | 新增 `load_adapter_config(path)`。从默认构造的配置开始，按键覆盖。旋转矩阵通过 `cv::Mat` 复制到 `Matx33d`，平移和三个二维位置按分量读取。 |
| `config/adapter.yaml` | 模板平铺全部九个字段，初值与 `AdapterConfig` 一致。 |
| `CMakeLists.txt`、`README.md` | 将加载器编入 adapter，并记录调用方式和文件打开异常。 |
| `tests/CMakeLists.txt`、`tests/config_load_test.cpp`、`tests/config/*.yaml` | 在模块独立构建时登记加载测试，逐字段比较实际配置和预期配置。 |

## 验证

独立 Release 构建成功，CTest **2/2 通过**，包括既有适配测试和新增的 `adapter_config_load_test`。

- 完整模板加载后，九个字段均与默认构造结果相等。
- 只写扫描角速度 `0.0`，其余八项取默认。另一份配置省略扫描角速度并为其余八项写入不同于默认的值，验证扫描角速度回退到 `0.5`，同时检查矩阵方向、坐标分量顺序和三个航向的键名。
- 缺文件时得到 `std::runtime_error`，错误消息包含 `ADAPTER_CONFIG_OPEN_FAILED:` 和传入的路径。

```sh
cmake -S vision/adapter -B build/loaders-adapter -DCMAKE_BUILD_TYPE=Release
cmake --build build/loaders-adapter
ctest --test-dir build/loaders-adapter --output-on-failure
```
