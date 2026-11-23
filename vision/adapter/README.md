# adapter

适配层库：把 pipeline 的相机系输出（G4 滤波后的位置/速度 + G2 的 rvec）转成车体系，按 G3 给的 mode 从配置里取期望值，拼成 G5 要的 CurrentState/DesiredState。只做几何变换 + 模式语义，不做判断。

## 接口

输入是 `AdapterInput`，输出是 `AdapterOutput`，配置是 `AdapterConfig`。命名空间 `adapter`。

### AdapterInput

```cpp
struct AdapterInput {
    Target target;                              // 这个位姿属于哪个目标
    std::optional<cv::Vec3d> position_camera;   // 目标位置，相机系
    std::optional<cv::Vec3d> velocity_camera;   // 目标表观速度，相机系
    std::optional<cv::Vec3d> rvec;              // 目标 rvec，相机系
    controller::Vec2 car_velocity;              // 车速，车体系
    double car_angular_velocity;                // 车角速度，车体系，rad/s
    Mode mode;                                  // G3 给的模式
};
```

- `target`：`Mark` 或 `Tag`。MARK 的位姿来自 G4（滤波过、有速度），TAG 的位姿来自 G2（原始、没速度）；FOLLOW 跟 MARK 和跟 TAG 的期望距离可能不一样，adapter 靠 target 取对的期望值。TAG 只指 exit 的 end TAG，全场只有一个 TAG，exchange 区只有 MARK。
- `position_camera` / `velocity_camera`：目标位置 / 表观速度，相机系。目标丢了就是 nullopt，原样透传，不编值。
- `rvec`：G2 最新 rvec，相机系，用来算 yaw。G2 丢了就是 nullopt。
- `car_velocity` / `car_angular_velocity`：车速和车角速度，车体系，来自 0x0015 或指令值，由 app 提供。
- `mode`：G3 决定的模式，adapter 只执行，不决策。

### AdapterOutput

```cpp
struct AdapterOutput {
    controller::CurrentState current;   // 车体系
    controller::DesiredState desired;   // 车体系
};
```

### AdapterConfig

```cpp
struct AdapterConfig {
    cv::Matx33d rotation;                       // 相机 → 车体旋转 R
    cv::Vec3d translation;                      // 相机光心在车体系的位置 t
    controller::Vec2 follow_target_position;    // FOLLOW 跟 MARK 的期望目标位置，车体系
    controller::Vec2 approach_target_position;  // FOLLOW 跟 TAG 的期望目标位置，车体系
    controller::Vec2 backup_target_position;    // BACKUP 的期望目标位置，车体系
    double scan_angular_velocity;               // SEARCH 的扫描角速度，rad/s
    double align_target_heading;                // ALIGN 的期望朝向，车体系，rad
};
```

- `rotation` / `translation`：外参。R 是相机系到车体系的旋转（含坐标约定差和安装角）；t 是相机光心在车体系的位置（x 前向、y 侧向、z 高度），实测填。
- 期望值命名跟 DesiredState 的字段对齐（`follow_target_position` 对应 `DesiredState.target_position`），都是车体系，都是实测值。

### Mode / Target

```cpp
enum class Mode { Follow, Search, Align, Backup };
enum class Target { Mark, Tag };
```

- `Mode`：FOLLOW（边走边对）、SEARCH（原地转着找 TAG）、ALIGN（站住只转不移）、BACKUP（后退扫 RFID）。退出流程 = SEARCH → FOLLOW → ALIGN → BACKUP。
- `Target`：位姿属于 MARK 还是 TAG。

### Adapter 类

```cpp
class Adapter {
public:
    explicit Adapter(const AdapterConfig& config);
    AdapterOutput adapt(const AdapterInput& input) const;

private:
    AdapterConfig config_;
};
```

构造时传 config，之后每次调 `adapt(input)` 算一次。类本身无状态，不记历史、不做滤波。

## 做法

- 位置：`p_body = R · p_camera + t`，取 x、y。速度：`v_body = R · v_camera`（只转不平移），取 x、y。yaw：rvec 经 Rodrigues 得 R_cam，再 `R_body = R · R_cam`，取 `yaw = atan2(R_body(1,0), R_body(0,0))`。
- 缺数据原样传 nullopt：位置/速度/rvec 丢了就是 nullopt，CurrentState 里对应字段也是 nullopt，不编值。G5 按 nullopt 处理，G3 按 P + 量测新鲜度判“真丢了”。
- DesiredState 按 mode + target 拼：
  - FOLLOW：target 是 Mark 取 `follow_target_position`，是 Tag 取 `approach_target_position`；期望朝向 0。
  - SEARCH：期望角速度取 `scan_angular_velocity`；期望位置、期望朝向都是 nullopt。
  - ALIGN：期望朝向取 `align_target_heading`；期望位置 nullopt（只转不移）。
  - BACKUP：期望位置取 `backup_target_position`；期望朝向 0。
- 几何变换只有一套，MARK 和 TAG 走同一套；分叉只在 FOLLOW 时取哪个期望位置。

## 依赖

- 依赖 controller（CurrentState、DesiredState、Vec2）和 OpenCV（Matx33d、Vec3d、Rodrigues）。不依赖 pipeline 的类型——输入是 Vec3d 数值，由 app 从 pipeline 类型里取出来再传。
- app 依赖 pipeline + adapter + controller。

## 状态

接口已定（见上）。实现待写。
