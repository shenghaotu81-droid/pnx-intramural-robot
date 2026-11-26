# Adapter 车体系 yaw 修正报告

adapter 现在输出板面法线在车体水平面上的方位角。车头正对板面时为 0，俯视时从车头往车左转为正，单位为 rad。固定目标世界朝向为 0 时，输出与控制器仿真的 `wrap(-car.yaw)` 一致。

## 坐标与符号核验

MARK 的模型点从左上开始，沿右、下方向张成模型平面。TAG 以中心为原点，正面朝向相机时，左下、右下、右上、左上的像点依次对应模型点 `(-h,h)`、`(h,h)`、`(h,-h)`、`(-h,-h)`。两者的模型 `+z = +x × +y` 都指向板背。[MARK 角点定义](../../pipeline/docs/flow-guide.md) · [TAG 模型](../../pipeline/src/pose/tag/tag_detector.cpp) · [TAG 角点顺序测试](../../pipeline/tests/pose/tag/tag_detector_test.cpp)

`R_cam = Rodrigues(rvec)` 将模型坐标转到相机系。`R_body = config_.rotation * R_cam` 将其转到车体系，第三列就是车体系法线。计算 `atan2(R_body(1,2), R_body(0,2))` 得到从车体前向 `+x` 朝左向 `+y` 量的角。默认安装时，模型绕相机向下的 `+y` 轴转 θ，法线在车体平面的投影为 `(cos θ, −sin θ)`，故 yaw 为包角后的 −θ。旧公式在这个例子中给出 +θ。[外参模板](../config/adapter.yaml) · [adapter 实现](../src/adapter.cpp)

相机换装时，同一块板在相机系中的姿态随之变化。用对应的相机到车体旋转左乘后，得到的仍是同一条车体系法线，因此安装旋转可以包含偏航、俯仰和横滚。前提是输入姿态和外参有效，且法线的水平投影非零。法线恰好竖直时，几何上没有水平航向，此时 `atan2` 的数值没有朝向意义。

## 与仿真及单测对照

世界系中板面法线固定朝 `+x`。车体逆时针转 ψ 后，该法线在车体系中为 `(cos ψ, −sin ψ, 0)`。仿真直接给出 `wrap(-car.yaw)`。新增的 adapter 用例从车体世界姿态、复合安装旋转和固定 TAG 姿态反算相机观测，再调用 `Adapter::adapt()` 核对下表。[仿真观测](../../controller/tests/simulation.cpp) · [adapter 单元测试](../tests/adapter_test.cpp)

| 车体世界 yaw（rad） | 仿真约定（rad） | adapter 实测（rad，容差 `1e-9`） |
| ---: | ---: | ---: |
| 0 | 0 | 0 |
| +0.30 | −0.30 | −0.30 |
| −0.15 | +0.15 | +0.15 |

默认安装下，纯 yaw 和 ±π 切点两侧的测试期望翻转符号。相机下倾 30°、`rvec = (0, 0.35, 0)` 时，车体系投影角为 `−0.39890119564063126` rad。默认安装下，绕 `(1,1,0)` 转 90° 的法线投影指向车右，结果为 `−π/2`。这两个期望值均按各自的水平投影重算。[逐项推导与结果](test-report.md)

## 构建与验证

在仓库根目录使用 GNU C++ 13.3.0、CMake 3.28.3 和 OpenCV 4.6.0 运行独立 Release 构建。

```sh
cmake -S vision/adapter -B /tmp/pnx-adapter-yaw-fix -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/pnx-adapter-yaw-fix --parallel 4
ctest --test-dir /tmp/pnx-adapter-yaw-fix --output-on-failure

cmake -S vision/controller -B /tmp/pnx-controller-yaw-fix -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/pnx-controller-yaw-fix --parallel 4
ctest --test-dir /tmp/pnx-controller-yaw-fix --output-on-failure
```

- 用新期望检查旧实现，纯 yaw、俯装、复合安装下固定目标、±π 边界及组合观测共 5 组失败。其中正对固定 TAG 的复合安装用例返回了 `+0.328778` rad，期望为 0。
- 修正后 adapter CTest **2/2 通过**。其中 `adapter_test` 的 11 组、38 个用例全部通过，另一项为配置加载测试。
- controller CTest **7/7 通过**，涵盖 6 项仿真和 1 项配置加载测试。以上结果来自合成姿态和软件仿真。
