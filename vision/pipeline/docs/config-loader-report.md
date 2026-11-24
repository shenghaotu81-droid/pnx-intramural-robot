# pipeline 配置加载器

detector、pose、tracking 采用 offline 已有的读取规则。先默认构造配置，再用 YAML 中出现的字段逐项覆盖。这样可省略单个字段或整组，同时允许显式配置零值。启动时读取的配置文件或其中指定的外部文件打不开时仍抛出异常。

| 文件 | 改动 |
| --- | --- |
| `src/config/detector/detector_config.cpp` | 分别读取工作宽高、两处阈值及轮廓拟合误差。有角点映射时才读取四个索引。 |
| `src/config/pose/pose_config.cpp`、`include/vision/config/pose_config.hpp` | 有标定路径时才加载标定，分别覆盖图像宽、高。有模型点时才读取并换算单位。缺模型点时使用 80 mm 方形的四角，按 P0 到 P3 排列，以米为单位。 |
| `src/config/tracking/tracking_config.cpp`、`include/vision/config/tracking_config.hpp` | 缺来源时用 `MARK`。缺 profile 或路径为空时保留空 profile。有 profile 文件时分别读取量测矩阵、q 和初速方差。三者的回退值取自当前模板，分别为 `diag(2e-8, 2e-8, 2.5e-7)`、`0.5`、`0.25`。调参改 YAML。 |
| `tests/config/config_load_test.cpp`、`tests/config/partial/*.yaml` | 在既有配置测试中增加部分配置文件，同时检查缺省字段和相邻字段的显式值。 |
| `README.md`、`../README.md` | 说明加载规则，并更新视觉模块的独立构建和测试入口。 |

## 验证

pipeline 独立 Release 构建成功，CTest **13/13 通过**。扩展的 `config_load_test` 覆盖以下情形。

- detector 只配置阈值零时，工作尺寸为 960 × 720，几何参数和角点映射保留默认值。
- pose 缺标定和模型点时，相机矩阵为空、图像尺寸为零，四角构成 80 mm 方形。另一份配置只给标定宽度，读出尺寸 640 × 0。显式给定的 40 × 60 mm 模型按米读出，以便发现加载器误用默认方形。
- tracking 缺来源时读出 `MARK`，缺 profile 时返回空 profile。两份部分 profile 交叉核对三个回退值、非对角矩阵的复制、显式为零的 q 和自定义初速方差。

```sh
cmake -S vision/pipeline -B build/loaders-pipeline -DCMAKE_BUILD_TYPE=Release
cmake --build build/loaders-pipeline
ctest --test-dir build/loaders-pipeline --output-on-failure
```
