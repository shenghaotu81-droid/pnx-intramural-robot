# controller 配置加载器

位置增益、航向增益、yaw 门控阈值和平滑系数可由调用方在启动时从 `controller.yaml` 加载，再传给 `Controller`。调参只需编辑配置文件并重启。

| 文件 | 改动 |
| --- | --- |
| `include/controller/config_loader.hpp`、`src/config_loader.cpp` | 新增 `load_controller_config(path)`，逐项覆盖三组配置。缺整组或缺组内字段时使用相应结构体默认值。 |
| `config/controller.yaml` | 模板中的位置和航向增益均为 3.0，门控阈值为 0.17 rad，平滑系数为 0.5。 |
| `CMakeLists.txt`、`README.md` | 加载器使用 OpenCV core 的 `FileStorage`，库私有链接 `opencv_core`。公共头文件使用标准库类型。 |
| `tests/CMakeLists.txt`、`tests/config_load_test.cpp`、`tests/config/*.yaml` | 在模块独立构建时登记加载测试，覆盖完整模板、局部覆盖、缺键回退和缺文件异常。 |
| `tests/README.md`、`docs/sim-report.md` | 说明构建依赖，并将仿真中的 1 / 1 标明为对照基线。当前配置模板的增益为 3 / 3。 |

## 验证

独立 Release 构建成功，CTest **7/7 通过**，包括六项仿真和新增的 `controller_config_load_test`。该测试只链接 `controller`，编译命令中只有 controller 的项目头文件路径。

完整模板与默认构造结果逐项相等。只给 `yaw_filter.alpha` 写入零时，其余三项取默认。只省略 `alpha` 时，其值回退到 0.5，两个不同的增益和门控阈值均取文件中的值。这两组配置同时检查了缺省整组与缺省同组字段。

缺文件用例传入 `config/missing.yaml`，断言抛出的 `std::runtime_error` 消息恰为 `CONTROLLER_CONFIG_OPEN_FAILED:config/missing.yaml`。该格式与 adapter 一致，保留调用方传入的路径，便于定位打开失败的文件。

```sh
cmake -S vision/controller -B build/loaders-controller -DCMAKE_BUILD_TYPE=Release
cmake --build build/loaders-controller
ctest --test-dir build/loaders-controller --output-on-failure
```
