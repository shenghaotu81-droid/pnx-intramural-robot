# Camera 配置加载器单元测试报告

相机库独立 Release 构建通过。CTest 注册的 `camera_config_load_test` 通过 1/1 项，测试程序的 6 组共 9 个用例全部通过。验证环境为 GNU C++ 13.3.0、CMake 3.28.3 和 OpenCV 4.6.0。

## 基础用例与结果

表中六个字段按 `width`、`height`、`format`、`fps`、`buff_num`、`skip_frames` 排列。夹具和缺文件用例的路径均相对于 `vision/camera/tests/`。

| 测试组 | 夹具与输入 | 期望结果 | 实际结果 |
| --- | --- | --- | --- |
| 完整配置 | `config/full.yaml`，写入全部六个字段 | `(1280, 720, "BGR888", 25, 3, 5)` | 通过 |
| 缺字段留默认及显式零值 | `config/partial.yaml`，只写 `width: 960`、`fps: 12.5` | `(960, 1080, "BGR888", 12.5, 1, 10)` | 通过 |
| 缺字段留默认及显式零值 | `config/zero_skip_frames.yaml`，只写 `skip_frames: 0` | `(1440, 1080, "BGR888", 30, 1, 0)` | 通过 |
| 缺文件抛错 | `config/missing.yaml`，路径不存在 | `std::runtime_error`，信息精确等于 `CAMERA_CONFIG_OPEN_FAILED:config/missing.yaml` | 通过 |
| 未知格式抛错 | `config/unknown_format.yaml`，写入 `format: "FOO"` | `std::runtime_error`，信息精确等于 `CAMERA_CONFIG_UNKNOWN_FORMAT:FOO` | 通过 |

`fps` 的实际类型为 `double`。部分配置使用可以精确表示的 `12.5`，同时检查小数帧率能否保留。

## 补充用例与结果

| 测试组 | 夹具与输入 | 期望结果 | 实际结果 |
| --- | --- | --- | --- |
| 未知格式抛错 | `config/lowercase_format.yaml`，写入 `format: "bgr888"` | `std::runtime_error`，信息精确等于 `CAMERA_CONFIG_UNKNOWN_FORMAT:bgr888` | 通过 |
| 合法 YAML 无配置字段 | `config/no_fields.yaml`，包含 `%YAML:1.0`、`---` 和一行注释 | `(1440, 1080, "BGR888", 30, 1, 10)` | 通过 |
| 空文件和纯注释文件 | `config/empty.yaml`，零字节 | 抛出 `cv::Exception` | 通过 |
| 空文件和纯注释文件 | `config/comment_only.yaml`，仅一行注释，无文件头 | 抛出 `cv::Exception` | 通过 |

小写格式用例验证格式名的大小写要求。后三个用例验证文件内容对默认值的影响。OpenCV 能解析带文档头的无字段夹具，加载器随即返回全部默认值。零字节和纯注释文件在 OpenCV 解析阶段就抛出异常。

## 观察

- **数值字段缺少类型校验。** 另用临时夹具写入 `width: "text"`，实测加载成功，返回的 `width` 为 `2147483647`。加载器直接用 OpenCV 的整数提取操作读取该节点。现有配置约定没有定义类型错误时的返回值，此观测单独记录，未将该值作为回归测试的期望宽度。
- 缺文件用例运行时 OpenCV 会先输出一条无法打开文件的日志。加载器随后抛出的异常类型和错误信息均符合约定。
- 在临时夹具副本中把 `config/full.yaml` 的宽度改为 `1279`，Release 程序打印了失败组、字段 `width`、期望值 `1280` 和实际值 `1279`。其余五组继续执行并通过，程序返回 `1`。

构建和 CTest 命令见[测试说明](../tests/README.md)。
