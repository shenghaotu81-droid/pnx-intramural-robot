# controller

通用控制器库。给定期望状态算速度，与系和语义无关。

## 接口

坐标系：车体系（2D）。原点在车子中心，+x 为车头朝向（前进方向），+y 为车子左侧，+z 向上（右手系）。位置单位 m，速度单位 m/s，角度单位 rad（从 +x 轴起算，逆时针为正），角速度单位 rad/s。

输入是两个 struct，输出是一个速度指令。所有量都在同一车体系内。

### 当前状态

```cpp
struct CurrentState {
    Vec2 car_velocity;                    // 车子相对地面的速度，车体系，m/s
    double car_angular_velocity;          // 车子绕 z 轴的角速度 ω，rad/s
    std::optional<Vec2> target_position;  // 目标实测位置，车体系，m
    std::optional<Vec2> target_velocity;  // 目标表观速度，车体系，m/s
    std::optional<double> target_heading; // 目标实测朝向，rad
};
```

- `car_velocity` / `car_angular_velocity`：车子自己的运动。来源：C 板速度回馈 0x0015，或上一次发出的速度指令值。
- `target_position`：目标在车体系的实测位置（MARK/TAG 从 pipeline 来）。没有目标时（转着扫）留空。
- `target_velocity`：目标在车体系的表观速度，即目标位置在车体系里的变化率，G4 量出来的值。**注意：这是表观速度，不是目标相对地面的速度。** G5 内部用它 + ω × p + 车速算出目标相对地面的速度做前馈。MARK 有；TAG 没有（TAG 静止且无速度量测）；转着扫没有。
- `target_heading`：目标在车体系的实测朝向（yaw，G2 的名义值，未处理）。没有目标时留空。

### 期望状态

```cpp
struct DesiredState {
    std::optional<Vec2> target_position;    // 目标期望位置，车体系，m
    std::optional<double> target_heading;   // 目标期望朝向，rad
    std::optional<double> angular_velocity; // 期望角速度，rad/s
};
```

字段都是可选的，哪个字段在，就按哪个字段的意思动：

- `target_position`：目标应该在车体系的哪个位置（目标相对车子的期望位形，是固定偏移，不随车子移动而改变）。跟 MARK 时是兑换偏移；退 TAG 时是 TAG 应该在车前方的距离（x 正值）；平移到 TAG 时是 TAG 应该在的位置。只给朝向或只给角速度的动作留空。
- `target_heading`：目标应该在车体系的哪个朝向。对准 TAG / 跟 MARK 时给。只给位置或只给角速度的动作留空。
- `angular_velocity`：期望角速度。只在转着扫时直接给扫描转速 ω_scan；跟目标/对准/后退时留空，G5 从朝向误差算 ω。

### 输出

```cpp
struct Command {
    Vec2 velocity;            // 车子期望线速度 (vx, vy)，车体系，m/s
    double angular_velocity;  // 车子期望角速度 ω，rad/s
};
```

直接发串口 0x0001。

### 与规则文档（V1.0.4 §3.3）的对应

- 跟 MARK（100% 固定 / 125% 随机固定 / 175% 匀速往复 / 225% 随机往复）：当前状态给目标位置 + 表观速度 + 朝向，期望状态给期望位置 + 期望朝向。四个档位用的字段相同，只是表观速度的值不同。
- 平移到 TAG：当前状态给 TAG 位置 + 朝向（速度缺失），期望状态给 TAG 期望位置 + 期望朝向。
- 对准 TAG：当前状态给 TAG 朝向，期望状态给期望朝向。
- 退 TAG：当前状态给 TAG 位置 + 朝向（速度缺失），期望状态给 TAG 期望位置（车前方正 x 值）。
- 转着扫 TAG/MARK：当前状态目标字段全空，期望状态只给期望角速度 ω_scan。
- 扫 RFID、1 分钟时限、自动/手动模式、矿石种类、隧道/台阶/插杆区：不是运动量，不进 G5 接口（G3 或机构负责）。

## 加载配置

`controller/config_loader.hpp` 声明了 `controller::ControllerConfig controller::load_controller_config(const std::filesystem::path &path)`。启动时调用 `controller::load_controller_config(config_dir / "controller.yaml")`，将结果传给 `Controller` 构造函数。文件打不开时抛出 `std::runtime_error`，消息为 `CONTROLLER_CONFIG_OPEN_FAILED:<路径>`，其中路径为调用方传入的文件路径。缺少整组或组内某个字段时，对应参数保留结构体默认值。

加载器使用 OpenCV core 的 `FileStorage` 读取 YAML，库私有链接 `opencv_core`，公共头文件只用 C++17 标准库。部署时将模板复制到配置目录，修改增益或 yaw 滤波参数后重启生效。[配置模板](config/controller.yaml) · [加载器验证报告](docs/config-loader-report.md)

## 做法

- 内部做旋转补偿（ω × p），再 P + 前馈。
- 不做坐标变换、不解释语义。变换和期望状态由调用方（adapter/ 库）给。

## 覆盖的动作

跟 MARK、退 TAG、对准 TAG、转着扫 TAG，都是"把某个状态量调到期望值"，都走这个库。

## 状态

- 生产代码：已完成（前馈 + 位置 P + 航向 P；yaw 门控 + EMA）。
- 仿真：harness 已接入 CTest，6 项全过；推荐 Kp=3 /s、Kh=3 /s，见 [仿真报告](docs/sim-report.md)。
- 待建：串口 0x0001 输出、G3。增益为仿真推荐值，上车后复核。

独立构建控制器后可用 CTest 运行仿真 harness，构建命令与场景说明见 [tests/README.md](tests/README.md)。
