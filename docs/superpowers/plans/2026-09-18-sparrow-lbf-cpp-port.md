# Sparrow LBF 纯 C++ 移植实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不修改 NestLab 生产排料调用、且不执行 Task20/Task21 的前提下，参考 `jagua-rs/lbf` 与 Sparrow 的 LBF 相关逻辑，建立一个依赖 JaguarCore、无 Rust/Qt 运行时依赖、可重复验证的纯 C++ `SparrowCore` LBF 优化模块。

**Architecture:** `JaguarCore` 继续作为几何、碰撞、Layout 和 SPP/BPP/MSPP 问题模型内核；`SparrowCore` 只负责采样、损失函数、局部搜索、LBF 初始排料和 SPP/BPP 优化流程。Sparrow 的探索、压缩、Separator、碰撞量化和 TUI/UI 不在第一版 LBF 计划内，后续通过稳定的 `SparrowCore` API 再独立规划。SPP/BPP 优化器直接持有 JaguarCore 的值类型和 `Layout`，不新增 JaguarCore 对优化器内部状态的反向依赖。

**Tech Stack:** C++17、STL、QMake、MSVC2019 x64、JaguarCore public API、无 Qt 类型、无 Rust 运行时、无 libnest2d/Sparrow/LBF 第三方运行时依赖。

## Global Constraints

- 只在本计划内实现 Sparrow 的 LBF 相关能力；不实现 Sparrow 的 Exploration、Compression、Separator、Overlap Proxy、TUI、浏览器 viewer 或完整 Sparrow CLI。
- 不执行 Task20（NestLab 适配设计验证）和 Task21（DLL 打包、部署及 ABI 验收）。
- 不修改 NestLab 生产排料调用，不接入 `ProNestLab`，不改变现有 JaguarCore 生产行为。
- `SparrowCore` 公共头文件不得出现 `QObject`、`QString`、`QVector`、`Exp_Entity*`、Rust 类型或 NestLab 类型。
- `SparrowCore` 第一版使用 static library + 独立测试程序，避免把 DLL 部署问题带入本计划；动态库形式留到 Task21 之后处理。
- 所有生产行为修改遵循 TDD：先写失败测试、运行确认失败、实现最小修正、运行全量回归。
- 不使用 `std::uniform_real_distribution` 或 `std::normal_distribution` 作为跨语言 oracle 的唯一实现；随机浮点转换必须由 `SparrowRng` 明确定义。
- 优化器决策精度以 jagua-rs/Sparrow 的 `f32` 语义为参考；进入 JaguarCore 的 `Transform2d` 时进行明确的 `float -> double` 转换，不把双精度误差当成随机差异。
- JaguarCore 的 `Result<T>`、`ErrorCode`、`Item`、`Container`、`Layout`、`SPInstance`、`BPInstance`、`SPSolution`、`BPSolution` 和 JSON/SVG API 是唯一内核依赖边界。
- 不跨模块暴露由 `SparrowCore` 分配、需要调用方释放的裸指针；优化器结果使用值类型或明确所有权的智能指针。
- jagua-rs 使用 MPL-2.0，Sparrow 使用 MIT；只参考算法和数据不变量，不逐行复制源码。源码映射、许可证和差异记录必须写入 `SparrowCore/docs/source-map.md`。
- 不执行 Git commit、merge、rebase、push 或其他修改 commit 的操作；用测试、基准、fixture 和文档作为检查点。

## 1. 参考源码与当前边界

### 1.1 参考目录

```text
D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\lbf
D:\MyFiles\Gits_Xj\NestLab\NestLab\sparrow
D:\MyFiles\Gits_Xj\NestLab\NestLab\JaguarCore
```

### 1.2 本计划直接移植的 jagua-rs/lbf 模块

| Rust 文件 | C++ 目标 | 责任 |
|---|---|---|
| `lbf/src/config.rs` | `SparrowCore/include/SparrowCore/lbf_config.h` | LBF 参数、采样预算、seed、数值选项 |
| `lbf/src/samplers/rotation_distr.rs` | `src/sampling/rotation_distribution.cpp` | None/Discrete/Continuous 旋转采样 |
| `lbf/src/samplers/uniform_rect_sampler.rs` | `src/sampling/uniform_rect_sampler.cpp` | 条带/容器 bbox 均匀采样 |
| `lbf/src/samplers/ls_sampler.rs` | `src/sampling/local_search_sampler.cpp` | 平移/旋转正态局部采样与衰减 |
| `lbf/src/opt/loss.rs` | `src/optimizer/lbf_loss.cpp` | `10*x_max+y_max` LBF loss |
| `lbf/src/opt/search.rs` | `src/optimizer/lbf_search.cpp` | Surrogate 粗筛、精确碰撞、Uniform/LS 搜索 |
| `lbf/src/opt/lbf_spp.rs` | `src/optimizer/spp_lbf_optimizer.cpp` | SPP 需求守恒、扩宽、fitStrip |
| `lbf/src/opt/lbf_bpp.rs` | `src/optimizer/bpp_lbf_optimizer.cpp` | Open/Closed Bin 搜索与库存 |
| `lbf/src/util/assertions.rs` | `src/support/lbf_assertions.cpp` | 宽度上限和状态不变量 |
| `lbf/src/io/output.rs` | `src/io/lbf_output.cpp` | 结果指标和 external JSON/SVG 输出适配 |

### 1.3 Sparrow 中本计划直接移植的 LBF 相关模块

| Sparrow 文件 | 第一版 C++ 责任 |
|---|---|
| `src/optimizer/lbf.rs` | Sparrow 风格初始构造、按凸包面积×直径排序、扩宽上限 |
| `src/sample/search.rs` | 容器采样、聚焦采样、BestSamples、两阶段坐标下降 |
| `src/sample/coord_descent.rs` | Horizontal/Vertical/Diagonal/Wiggle 候选生成与步长衰减 |
| `src/sample/uniform_sampler.rs` | Item bbox 约束下的采样矩形和旋转合法化 |
| `src/eval/sample_eval.rs` | Invalid/Collision/Clear 三态排序 |
| `src/eval/lbf_evaluator.rs` | `10*(POI.x+bbox.x)+POI.y+bbox.y` Sparrow LBF loss |
| `src/consts.rs` | 坐标下降步长、采样阈值、默认采样预算 |

Sparrow 的 `explore.rs`、`compress.rs`、`separator.rs`、`quantify/*`、`worker.rs` 和 TUI 不在本计划内。它们依赖更复杂的 CollisionTracker、并行 worker 和论文算法阶段，必须在 LBF 初始构造稳定后另开计划。

## 2. 目标目录结构

```text
SparrowCore/
├─ SparrowCore.pro
├─ include/
│  └─ SparrowCore/
│     ├─ export.h
│     ├─ version.h
│     ├─ result_alias.h
│     ├─ lbf_config.h
│     ├─ lbf_metrics.h
│     ├─ random/sparrow_rng.h
│     ├─ sampling/rotation_distribution.h
│     ├─ sampling/uniform_rect_sampler.h
│     ├─ sampling/local_search_sampler.h
│     ├─ sampling/best_samples.h
│     ├─ sampling/coordinate_descent.h
│     ├─ optimizer/sample_eval.h
│     ├─ optimizer/lbf_loss.h
│     ├─ optimizer/lbf_search.h
│     ├─ optimizer/spp_lbf_optimizer.h
│     ├─ optimizer/bpp_lbf_optimizer.h
│     └─ io/lbf_output.h
├─ src/
│  ├─ support/version.cpp
│  ├─ random/sparrow_rng.cpp
│  ├─ sampling/rotation_distribution.cpp
│  ├─ sampling/uniform_rect_sampler.cpp
│  ├─ sampling/local_search_sampler.cpp
│  ├─ sampling/best_samples.cpp
│  ├─ sampling/coordinate_descent.cpp
│  ├─ optimizer/sample_eval.cpp
│  ├─ optimizer/lbf_loss.cpp
│  ├─ optimizer/lbf_search.cpp
│  ├─ optimizer/spp_lbf_optimizer.cpp
│  ├─ optimizer/bpp_lbf_optimizer.cpp
│  └─ io/lbf_output.cpp
├─ tests/
│  ├─ SparrowCoreTests.pro
│  ├─ test_rng.cpp
│  ├─ test_sampling.cpp
│  ├─ test_lbf_loss.cpp
│  ├─ test_spp_lbf.cpp
│  ├─ test_bpp_lbf.cpp
│  ├─ test_lbf_json.cpp
│  ├─ differential/compare_lbf.cpp
│  └─ fixtures/
│     ├─ lbf_spp_small.json
│     ├─ lbf_bpp_small.json
│     ├─ lbf_seed42_reference.tsv
│     └─ lbf_metrics_reference.tsv
├─ bench/
│  ├─ SparrowCoreBench.pro
│  └─ bench_lbf.cpp
└─ docs/
   ├─ source-map.md
   ├─ compatibility.md
   └─ performance.md
```

`SparrowCore` 不得把 JaguarCore 的 private 头文件加入 `INCLUDEPATH`；只允许链接 JaguarCore public headers 和 import library。

## 3. 公共接口与数据流

### 3.1 配置

```cpp
namespace sparrow
{
struct LbfConfig
{
    std::uint64_t seed = 0;
    std::size_t samplesPerItem = 5000;
    double localSearchFraction = 0.2;
    double minTranslationStddevRatio = 0.0005;
    double maxTranslationStddevRatio = 0.01;
    double minRotationStddevRad = 0.5 * 3.141592653589793 / 180.0;
    double maxRotationStddevRad = 2.0 * 3.141592653589793 / 180.0;
    std::size_t maxWidthGrowthAttempts = 32;
    double widthGrowthRatio = 1.1;
    bool useSparrowConstructorOrder = true;
};
}
```

校验规则：

- `samplesPerItem > 0`；
- `0 <= localSearchFraction <= 1`；
- 所有比例、步长和增长系数有限且非负；
- `widthGrowthRatio > 1`；
- seed 必须显式记录到 `LbfMetrics`；
- 违反规则返回 `jaguar::ErrorCode::InvalidInput`，不使用异常作为正常流程。

### 3.2 优化结果与指标

```cpp
namespace sparrow
{
struct LbfMetrics
{
    std::uint64_t seed = 0;
    std::size_t sampleCount = 0;
    std::size_t placedItemCount = 0;
    std::size_t remainingDemand = 0;
    double stripWidth = 0.0;
    double density = 0.0;
    bool feasible = false;
};

struct SppLbfResult
{
    jaguar::SPSolution solution;
    LbfMetrics metrics;
};

struct BppLbfResult
{
    jaguar::BPSolution solution;
    LbfMetrics metrics;
};
}
```

优化器入口：

```cpp
SPARROWCORE_API jaguar::Result<SppLbfResult> solveSppLbf(
    const jaguar::SPInstance& instance,
    const LbfConfig& config);

SPARROWCORE_API jaguar::Result<BppLbfResult> solveBppLbf(
    const jaguar::BPInstance& instance,
    const LbfConfig& config);
```

优化器内部不直接依赖 `SPProblem`/`BPProblem` 的 private 状态。SPP 状态由 `Strip + Layout + remainingDemand` 管理，BPP 状态由 `BinState{binIndex, optional<Layout>, remainingStock}` 管理；最终通过公开的 `SPSolution`/`BPSolution` 值字段构造结果。

### 3.3 采样与评估接口

```cpp
namespace sparrow
{
enum class SampleKind
{
    Invalid,
    Collision,
    Clear,
};

struct SampleEval
{
    SampleKind kind = SampleKind::Invalid;
    float loss = 0.0F;
};

bool betterSample(const SampleEval& left, const SampleEval& right) noexcept;

class SampleEvaluator
{
public:
    virtual ~SampleEvaluator() = default;
    virtual SampleEval evaluate(const jaguar::Transform2d& transform,
                                const std::optional<SampleEval>& upperBound) = 0;
    virtual std::size_t evaluationCount() const noexcept = 0;
};
}
```

评估顺序固定为：

```text
合法化旋转/变换
  ↓
Surrogate fail-fast
  ↓
完整 Polygon 碰撞
  ↓
Clear loss
```

`SampleEval` 排序规则保持 Sparrow：`Clear < Collision < Invalid`；同类按 loss 升序比较。

## 4. 数值、随机和兼容策略

### 4.1 随机源

实现明确的 `SparrowRng`：

```cpp
class SparrowRng
{
public:
    explicit SparrowRng(std::uint64_t seed);
    std::uint64_t nextU64() noexcept;
    std::uint32_t nextU32() noexcept;
    float uniform01F32() noexcept;
    float normal01F32();
    std::array<std::uint64_t, 4> state() const noexcept;
};
```

第一版使用显式 `xoshiro256++` 状态和固定 seed 扩展；不得调用实现依赖平台的 `std::random_device`。`normal01F32()` 使用固定 Box-Muller 算法和明确缓存策略，避免 MSVC/libstdc++ 分布实现差异。

验证分两层：

1. Rust/C++ 对同 seed 的原始 `u64` 序列、`uniform01F32()` 序列逐项比较；
2. 优化器只要求样本计数、可行性、需求守恒、宽度和密度在定义容差内一致，不假设 Rust `rand_distr` 与 C++ Box-Muller 的每个正态样本完全相同。

### 4.2 旋转

| JaguarCore RotationRange | SparrowCore 行为 |
|---|---|
| `None` | 始终 `0` |
| `Discrete` | 使用 `SparrowRng` 从角度数组均匀选择 |
| `Continuous` | 在 `[0, 2π)` 采样；局部搜索允许 rotation wiggle |

输入角度统一使用弧度；JSON/外部表示仍使用 JaguarCore 的 degree/radian 转换边界。

### 4.3 参考精度

- 采样、loss、步长、比较使用 `float` 语义；
- JaguarCore 查询接收 `Transform2d(double)`，候选先按 `float` 舍入再转换为 double；
- 输出指标使用 double 汇总，但 Rust oracle 比较使用绝对/相对容差；
- 禁止把 `NaN`、Infinity 或空采样矩形送入 JaguarCore。

## 5. 任务分解

### Task 1: 建立源码映射、许可证边界和 fixture 资产

**Files:**

- Create: `SparrowCore/docs/source-map.md`
- Create: `SparrowCore/tests/fixtures/lbf_spp_small.json`
- Create: `SparrowCore/tests/fixtures/lbf_bpp_small.json`
- Create: `SparrowCore/tests/fixtures/lbf_config_seed42.json`
- Create: `SparrowCore/tests/fixtures/lbf_seed42_reference.tsv`
- Create: `SparrowCore/tests/fixtures/lbf_metrics_reference.tsv`
- Modify: none in JaguarCore production code

**Interfaces:**

- Consumes: `jagua-rs/lbf`、`sparrow` 源码与 JaguarCore compatibility docs。
- Produces: 每个 Rust/Sparrow 模块对应的 C++ 文件、许可证来源、已知语义差异和 fixture 字段定义。

- [x] 列出 `jagua-rs/lbf` 的 SPP/BPP 入口、采样、loss 和 assertions 对应文件。
- [x] 列出 Sparrow LBFBuilder、LBFEvaluator、sample search、coordinate descent 对应文件。
- [x] 记录 MPL-2.0/MIT 许可边界，不复制 Rust 代码文本。
- [x] 固定一个小型 SPP 和 BPP 输入，字段使用现有 JaguarCore JSON snake_case 格式。
- [x] 生成 seed=42 的原始 RNG、sample count、placement count、width、density 和 demand reference 表。
- [x] 运行只读 JSON 解析检查，确认 fixture 不包含 Item holes、未知 orientation 或非有限数值。

**Task1 checkpoint (2026-09-18):**

- 已创建 `SparrowCore/docs/source-map.md`，覆盖 jagua-rs/lbf、Sparrow、JaguarCore 的模块映射、许可证边界和明确排除项；
- 已创建 `lbf_spp_small.json`、`lbf_bpp_small.json` 和 `lbf_config_seed42.json`；
- 已用 Rust `1.90.0` LBF CLI 实际运行两个 fixture；seed=42 配置下 SPP 得到 `placed=2/2`、`remaining=0`、`width=4.276253`、`density=0.46769914`，BPP 得到 `placed=1/1`、`remaining=0`、`cost=10`、`density=0.11111111`；
- 已填写 `lbf_seed42_reference.tsv` 的 raw RNG/float schema 和 `lbf_metrics_reference.tsv` 的真实 Rust LBF metrics；
- Python 只读检查确认三个 JSON 无 NaN/Infinity、Item 均使用新 `orientation.rotation`、无 holes/legacy orientation；JaguarCore 生产源码目录在 Task1 中未修改。

**Verification:**

```powershell
rg -n "jagua-rs|sparrow|MPL-2.0|MIT|seed=42" SparrowCore/docs/source-map.md
```

Expected: 每个后续实现文件都有来源映射，fixture 字段和许可边界完整。

### Task 2: 建立 SparrowCore static library 骨架

**Files:**

- Create: `SparrowCore/SparrowCore.pro`
- Create: `SparrowCore/include/SparrowCore/export.h`
- Create: `SparrowCore/include/SparrowCore/result_alias.h`
- Create: `SparrowCore/include/SparrowCore/version.h`
- Create: `SparrowCore/src/support/version.cpp`
- Create: `SparrowCore/tests/SparrowCoreTests.pro`
- Create: `SparrowCore/tests/test_support.cpp`
- Create: `SparrowCore/bench/SparrowCoreBench.pro`

**Interfaces:**

- Consumes: JaguarCore public headers和 import library。
- Produces: `SparrowCore.lib`、版本接口和独立测试/基准 target。

`SparrowCore.pro` 的核心配置必须是：

```qmake
TEMPLATE = lib
TARGET = SparrowCore
CONFIG += staticlib c++17 warn_on
QT -= core gui
INCLUDEPATH += $$PWD/../JaguarCore/include
LIBS += -L$$JAGUARCORE_BUILD_DIR/bin -lJaguarCore
```

`include/SparrowCore/result_alias.h` 必须只定义一个明确的结果别名：

```cpp
#pragma once

#include "JaguarCore/result.h"

namespace sparrow
{
template <typename T>
using Result = jaguar::Result<T>;
}
```

`include/SparrowCore/export.h` 在 static library 阶段不承担 DLL ABI：

```cpp
#pragma once

#define SPARROWCORE_API
```

以后若进入 Task21 的动态库阶段，再单独增加 `SPARROWCORE_BUILD` import/export 宏；本计划不提前引入 DLL 所有权规则。

- [x] 创建不含 Qt/Rust/NestLab 类型的导出宏和版本接口。
- [x] 配置 Debug/Release 的对象目录、输出目录和 `JAGUARCORE_BUILD_DIR`。
- [x] 建立测试程序，检查版本字符串和 JaguarCore 依赖可用。
- [x] 建立空基准程序，确认 `SparrowCoreBench` 不进入 JaguarCore DLL。
- [x] 用 MSVC2019 x64 Debug/Release 构建 static library。

**Verification:**

```powershell
qmake SparrowCore.pro CONFIG+=debug
nmake /NOLOGO
qmake SparrowCore.pro CONFIG+=release
nmake /NOLOGO
```

Expected: Debug/Release 均生成 SparrowCore static library，核心 target 不出现 Qt moc/uic/rcc 输入。

**Task2 checkpoint (2026-09-18):**

- 已建立 `SparrowCore` static library 骨架、公共导出宏、版本接口和 `jaguar::Result<T>` 别名；当前阶段不引入 DLL ABI。
- 已建立独立 `SparrowCoreTests` 与 `SparrowCoreBench` target；测试和 Bench 只通过 JaguarCore public headers/import library 使用内核。
- SparrowCore public headers 扫描未发现 Qt、Rust、NestLab、CADLab、`QObject`、`QString`、`QVector` 或 `Exp_Entity` 依赖。
- MSVC2019 x64 Debug/Release 的 SparrowCore、测试程序和 Bench 均已由 `nmake /NOLOGO` 构建成功；对应测试程序均以退出码 `0` 运行，Bench 均以退出码 `0` 输出 `sparrowcore,0.1.0-dev,empty,...`。
- 构建产物位于 `build-SparrowCore-MSVC2019x64-Debug/bin/SparrowCore.lib` 和 `build-SparrowCore-MSVC2019x64-Release/bin/SparrowCore.lib`；测试运行时使用匹配配置的 JaguarCore DLL。
- Task2 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 3: 实现 SparrowRng 和配置校验

**Files:**

- Create: `SparrowCore/include/SparrowCore/random/sparrow_rng.h`
- Create: `SparrowCore/src/random/sparrow_rng.cpp`
- Create: `SparrowCore/include/SparrowCore/lbf_config.h`
- Create: `SparrowCore/include/SparrowCore/lbf_metrics.h`
- Create: `SparrowCore/tests/test_rng.cpp`

**Interfaces:**

- Consumes: Task1 seed/reference fixture。
- Produces: `SparrowRng`、`LbfConfig`、`LbfMetrics`，供所有采样器和优化器使用。

- [x] 先写测试：同 seed 的 `nextU64()`、`uniform01F32()`、state snapshot 必须稳定。
- [x] 运行测试，确认缺少 SparrowRng 实现时失败。
- [x] 实现固定 xoshiro256++ 状态、seed 初始化、uniform 24-bit float 转换和 Box-Muller normal。
- [x] 实现配置有限性、范围、样本数和增长倍率校验。
- [x] 为 seed=0、最大 seed、重复 state、非法 `ls_frac`、零样本和非有限配置写测试。
- [x] 运行 Debug/Release RNG tests，与 reference TSV 逐项比较。

**Verification:**

```text
same seed -> same raw state and sample sequence
different seed -> different first 16 raw values
invalid config -> ErrorCode::InvalidInput
```

**Task3 checkpoint (2026-09-18):**

- 已新增 `SparrowRng`、`LbfConfig` 和 `LbfMetrics` 公共类型，并将 RNG 源文件加入 SparrowCore static library target。
- `SparrowRng` 使用 SplitMix64 seed expansion、xoshiro256++（64-bit 输出旋转 23）、高 32-bit `nextU32()`、top-24-bit `uniform01F32()` 和带 spare 缓存的 Box-Muller `normal01F32()`。
- `test_rng.cpp` 会从 `lbf_seed42_reference.tsv` 读取 seed=42 的 raw `u64`/uniform `f32` 行并逐项比较；同时覆盖同 seed 状态、异 seed、seed=0、最大 seed、normal 缓存和状态稳定性。
- `LbfConfig::validate()` 对样本数、local-search fraction、平移/旋转标准差范围、有限性和 width growth ratio 返回 `ErrorCode::InvalidInput`，默认配置通过校验。
- SparrowCoreTests Debug/Release 均已重新构建并运行成功，退出码均为 `0`；RED 阶段曾因缺少目标头文件失败，随后以最小实现转为 GREEN。
- Task3 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 4: 实现旋转分布和 UniformRectSampler

**Files:**

- Create: `SparrowCore/include/SparrowCore/sampling/rotation_distribution.h`
- Create: `SparrowCore/src/sampling/rotation_distribution.cpp`
- Create: `SparrowCore/include/SparrowCore/sampling/uniform_rect_sampler.h`
- Create: `SparrowCore/src/sampling/uniform_rect_sampler.cpp`
- Create: `SparrowCore/tests/test_sampling.cpp`

**Interfaces:**

```cpp
class RotationDistribution
{
public:
    static Result<RotationDistribution> fromItem(const jaguar::Item& item);
    float sample(SparrowRng& rng) const;
};

class UniformRectSampler
{
public:
    static Result<UniformRectSampler> create(
        jaguar::Rect2d bbox, const jaguar::Item& item);
    jaguar::Transform2d sample(SparrowRng& rng) const;
    jaguar::Rect2d bbox() const noexcept;
};
```

- [x] 测试 None/Discrete/Continuous 三种旋转模式。
- [x] 测试连续旋转始终为有限弧度，离散旋转只返回允许角度。
- [x] 测试空/退化 bbox 返回 `InvalidInput`，不构造非法 Uniform。
- [x] 测试固定 seed 的平移/旋转候选序列。
- [x] 实现最小代码，并将所有候选转为 JaguarCore `Transform2d`。
- [x] 验证候选 shape bounding box 与 sampler bbox 的关系。

**Task4 checkpoint (2026-09-18):**

- 已新增 `RotationDistribution` 和 `UniformRectSampler`，并加入 SparrowCore static library 的 QMake headers/sources。
- `RotationDistribution` 支持 JaguarCore `None`、`Discrete`、`Continuous` 三种模式；离散模式拒绝空/非有限角度，使用拒绝采样避免索引模偏差；连续模式采样 `[0, 2π)`。
- `UniformRectSampler` 拒绝非有限、退化和无法用 float 表示的 bbox；按 rotation → x → y 顺序采样，并显式将 float 决策候选转换为 JaguarCore `Transform2d(double)`。
- `test_sampling.cpp` 覆盖三种旋转、seed 确定性、离散合法角度、退化/非有限 bbox、候选边界和变换后 shape bounding box。
- Debug/Release SparrowCore 与 SparrowCoreTests 均重新构建成功，测试程序均以退出码 `0` 运行；Task2 的空 Bench 回归保持可运行。
- Task4 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 5: 实现 LocalSearchSampler 和坐标下降

**Files:**

- Create: `SparrowCore/include/SparrowCore/sampling/local_search_sampler.h`
- Create: `SparrowCore/src/sampling/local_search_sampler.cpp`
- Create: `SparrowCore/include/SparrowCore/sampling/coordinate_descent.h`
- Create: `SparrowCore/src/sampling/coordinate_descent.cpp`
- Modify: `SparrowCore/tests/test_sampling.cpp`

**Interfaces:**

```cpp
struct CoordinateDescentConfig
{
    float translationStepInit = 0.0F;
    float translationStepLimit = 0.0F;
    float rotationStepInit = 0.0F;
    float rotationStepLimit = 0.0F;
    bool rotationWiggle = false;
};

class LocalSearchSampler
{
public:
    static Result<LocalSearchSampler> create(
        const jaguar::Item& item,
        const jaguar::Transform2d& reference,
        const LbfConfig& config,
        const jaguar::Rect2d& containerBounds);
    jaguar::Transform2d sample(SparrowRng& rng);
    void shiftMean(const jaguar::Transform2d& transform);
    void decayStddev(float progress);
};

Result<std::pair<jaguar::Transform2d, SampleEval>> refineCoordinateDescent(
    std::pair<jaguar::Transform2d, SampleEval> initial,
    SampleEvaluator& evaluator,
    CoordinateDescentConfig config,
    SparrowRng& rng);
```

- [x] 测试四个平移轴和连续旋转 wiggle 轴。
- [x] 测试 better/equal/worse 三种 SampleEval 对步长和均值的影响。
- [x] 测试步长衰减在 progress=0/1 和边界值处有限。
- [x] 测试离散旋转不被 rotation wiggle 改变。
- [x] 为坐标下降设置最大评估保护，超过上限返回 `TimeLimit`，不死循环。
- [x] 用人工 evaluator fixture 验证最终 loss 不高于初始 loss。

**Task5 checkpoint (2026-09-18):**

- 已新增共享的 `SampleEval`/`SampleEvaluator` 最小契约、`LocalSearchSampler` 和 `CoordinateDescentConfig`/`refineCoordinateDescent`。
- `LocalSearchSampler` 使用显式 float 均值/标准差，按 jagua-rs 的 rotation → x → y 顺序采样；支持均值移动、指数标准差衰减、None/Discrete/Continuous 旋转，并拒绝非法配置、参考变换和容器边界。
- 坐标下降覆盖 Horizontal、Vertical、ForwardDiagonal、BackwardDiagonal 和 Continuous Rotation Wiggle；better/equal/worse 状态分别按 `1.1`/`0.5` 步长倍率处理，非改进时重新选择轴。
- `maxEvaluations` 保护在预算耗尽时返回 `ErrorCode::TimeLimit`，避免无界循环；离散/None 旋转不会被 rotation wiggle 改变。
- `test_sampling.cpp` 已覆盖四个平移轴、rotation wiggle、均值移动、progress=0/1 衰减、非法输入、确定性和最终 loss 不增。
- SparrowCore、Tests、Bench 的 Debug/Release 均已重新构建并运行成功，测试和 Bench 退出码均为 `0`。
- Task5 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 6: 实现 SampleEval、LBF Loss 和 BestSamples

**Files:**

- Modify: `SparrowCore/include/SparrowCore/optimizer/sample_eval.h`
- Create: `SparrowCore/src/optimizer/sample_eval.cpp`
- Create: `SparrowCore/include/SparrowCore/optimizer/lbf_loss.h`
- Create: `SparrowCore/src/optimizer/lbf_loss.cpp`
- Create: `SparrowCore/include/SparrowCore/sampling/best_samples.h`
- Create: `SparrowCore/src/sampling/best_samples.cpp`
- Create: `SparrowCore/tests/test_lbf_loss.cpp`

**Interfaces:**

```cpp
struct LbfLoss
{
    float xMax = 0.0F;
    float yMax = 0.0F;

    static LbfLoss fromBoundingBox(const jaguar::Rect2d& bbox);
    float cost() const noexcept;
    jaguar::Rect2d tightenSampleBBox(const jaguar::Rect2d& sample) const;
};

bool operator<(const LbfLoss& left, const LbfLoss& right) noexcept;
```

LBF reference loss 必须精确使用：

```text
cost = 10 * x_max + y_max
```

Sparrow constructor 的 evaluator loss 单独使用：

```text
loss = 10 * (POI.x + bboxCorner.x) + (POI.y + bboxCorner.y)
```

不得把两种 loss 合并成一个含义不清的类。

- [x] 测试 `x_max/y_max`、tighten bbox、相同 loss、NaN 拒绝和严格排序。
- [x] 测试 `Invalid < Collision < Clear` 和同类 loss 升序。
- [x] 测试 BestSamples 的去重阈值、容量限制和 upper bound。
- [x] 验证 loss 计算使用 float decision precision。

**Task6 checkpoint (2026-09-18):**

- 已补全 `SampleEval` 的 `Clear < Collision < Invalid` 状态排序、同类 loss 升序比较、NaN 稳定处理及相等判断；保留 Task5 所需的 `SampleEvaluator` 契约。
- 已新增 `LbfLoss`，严格使用 `10*x_max+y_max`，以 float 作为决策精度，并支持按 loss 收紧采样 bbox 的 `x_max`。
- 已新增 `BestSamples`，支持容量限制、upper bound、Sparrow 风格的平移/角度相似去重、较优相似样本替换和按评估排序；Invalid/NaN 样本不会进入候选集。
- 已新增 `test_lbf_loss.cpp`，覆盖 LBF 数值、bbox tightening、float 精度、NaN、三态排序、容量淘汰、upper bound、相似样本替换和 Invalid 拒绝。
- SparrowCore、Tests、Bench 的 Debug/Release 均已重新构建并运行成功，测试和 Bench 退出码均为 `0`。
- Task6 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 7: 实现 SampleEvaluator 与完整碰撞评估

**Files:**

- Create: `SparrowCore/include/SparrowCore/eval/lbf_evaluator.h`
- Create: `SparrowCore/src/eval/lbf_evaluator.cpp`
- Modify: `SparrowCore/include/SparrowCore/optimizer/sample_eval.h`
- Create: `SparrowCore/tests/test_sampling.cpp`

**Interfaces:**

```cpp
class LbfEvaluator final : public SampleEvaluator
{
public:
    LbfEvaluator(const jaguar::Layout& layout,
                 const jaguar::Item& item,
                 const jaguar::HazardFilter& filter);
    SampleEval evaluate(const jaguar::Transform2d& transform,
                        const std::optional<SampleEval>& upperBound) override;
    std::size_t evaluationCount() const noexcept override;
};
```

- [x] 每次 evaluate 先用 Item cached Surrogate 做 fail-fast。
- [x] Surrogate 未碰撞后，将 item collision shape 变换到 scratch polygon。
- [x] 通过 JaguarCore exact CDE 检查完整碰撞。
- [x] 质量过滤遵循 JaguarCore 当前语义：`quality < minQuality` 忽略、等于要求值仍碰撞、Hole 始终碰撞。
- [x] 上界为有限值且当前候选不可能改善时，跳过完整碰撞但保持评估计数规则明确。
- [x] 写 Surrogate false positive、Exterior、Hole、quality-zone、完全可行样例。

**Task7 checkpoint (2026-09-18):**

- 已新增 `LbfEvaluator`，通过 `Layout::save()`、`Container::createCollisionEngine()` 和 CDE snapshot restore 建立只读评估上下文，不访问 JaguarCore private 状态。
- evaluator 按 `legalize rotation/transform → cached Surrogate → exact polygon → Clear loss` 执行；LBF 语义下 Surrogate 命中、exact 碰撞、Exterior、Hole、质量不满足和 upper-bound 早停均返回 `SampleKind::Invalid`，仅完全可行样本返回 `Clear`。
- 质量 filter 保留调用方 HazardFilter，强制 Hole 不可被过滤；质量低于要求值的 InferiorQualityZone 可被调用方 filter 忽略，等于要求值仍作为碰撞。
- upper bound 早停仍递增 evaluation count，但不进行 exact CDE，并返回 Invalid，避免碰撞候选进入 BestSamples。
- `test_sampling.cpp` 已覆盖完全可行样本、Exterior、动态 placed-item、exact-only CDE、Hole、quality filter、Surrogate path、旋转合法化、upper bound 和 evaluation count。
- SparrowCore、Tests、Bench 的 Debug/Release 均已重新构建并运行成功，测试和 Bench 退出码均为 `0`。
- Task7 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 8: 实现通用 LBF Search

**Files:**

- Create: `SparrowCore/include/SparrowCore/optimizer/lbf_search.h`
- Create: `SparrowCore/src/optimizer/lbf_search.cpp`
- Modify: `SparrowCore/tests/test_sampling.cpp`
- Create: `SparrowCore/tests/fixtures/lbf_search_reference.tsv`

**Interfaces:**

```cpp
struct SearchResult
{
    jaguar::Transform2d transform = jaguar::Transform2d::identity();
    LbfLoss loss{};
    std::size_t samples = 0;
};

Result<std::optional<SearchResult>> searchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter);
```

算法顺序必须保持：

1. 计算 local-search/uniform sample budget；
2. 在 CDE bbox 内均匀采样；
3. Surrogate 粗筛；
4. 计算候选 loss，只对可能改善的样本做 exact collision；
5. 用新 best tighten uniform sampling bbox；
6. 有 best 时进入 local search；
7. 局部搜索按 Sparrow 的均值漂移和指数步长衰减；
8. 无可行样本返回空 optional，不伪造 placement。

- [x] 用人工矩形 Layout 测试搜索能找到可行位置。
- [x] 测试完全不可行 Layout 返回空 optional。
- [x] 测试 upper bound 不会丢失更优候选。
- [x] 固定 seed 比较样本数、首个 best loss、最终 loss 和 bbox tightening 序列。
- [x] 测试 filter 对 quality/hole 的行为。

**Task8 checkpoint (2026-09-18):**

- 已新增 `SearchResult` 和 `searchLbf`，串联 UniformRectSampler、`LbfLoss` reference objective、`LbfEvaluator`、LocalSearchSampler 和 bbox tightening。
- uniform/local budget 使用 `samplesPerItem` 与 `localSearchFraction` 的显式 floor 语义；无 uniform 可行样本时返回成功的空 `optional`，不伪造 placement；invalid config/item/container bounds 返回 `InvalidInput`。
- reference `LbfLoss` 与 Sparrow evaluator loss 保持独立：候选先以 `10*x_max+y_max` 预筛，之后再进行 Surrogate/exact 评估；不会把 constructor loss 当作 reference upper bound。
- 成功搜索会记录实际生成样本数以及每次 uniform best 改善的 `tighteningHistory`；有 best 才启动局部正态搜索，并在改进后移动均值、按进度衰减标准差。
- 已创建 `SparrowCore/tests/fixtures/lbf_search_reference.tsv`，固定 seed=42 fixture 逐项校验最终样本数、首个 best、最终 loss/transform 和完整 bbox tightening history。
- `test_sampling.cpp` 已覆盖空 Layout 可行搜索、完全阻塞 Layout、固定 seed 重复一致、16/64 uniform budget 不退化、非法配置以及 quality/hole filter 相关 evaluator 行为。
- SparrowCore、Tests、Bench 的 Debug/Release 均已重新构建并运行成功，测试和 Bench 退出码均为 `0`。
- Task8 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 9: 实现 SPP LBF 构造器

**Files:**

- Create: `SparrowCore/include/SparrowCore/optimizer/spp_lbf_optimizer.h`
- Create: `SparrowCore/src/optimizer/spp_lbf_optimizer.cpp`
- Modify: `SparrowCore/tests/test_spp_lbf.cpp`
- Modify: `SparrowCore/tests/fixtures/lbf_spp_small.json`

**Interfaces:**

```cpp
Result<SppLbfResult> solveSppLbf(
    const jaguar::SPInstance& instance,
    const LbfConfig& config);
```

SPP 状态规则：

- 按 descending diameter 的 item type 排序，逐项消耗 demand；
- `searchLbf` 无结果时按 `width *= widthGrowthRatio` 扩宽；
- 扩宽次数超过 `maxWidthGrowthAttempts` 返回 `Infeasible`；
- 每次成功放置后减少 demand，不能超过初始 demand；
- 最终执行 `fitStrip`，输出 strip width/density/placed count；
- `minQuality` 通过 JaguarCore filter 传入，不在 SparrowCore 重新解释。

Sparrow constructor order 的增强规则：

```text
sort key = convexHullArea(item) * diameter(item), descending
then repeat each item by demand
```

LBF reference mode 的 `diameter` order 和 Sparrow constructor order 必须通过配置显式区分，不能隐式改变 oracle。

- [x] 测试空需求、单 item、多 demand、无法放置、扩宽和最终 fitStrip。
- [x] 测试 demand 守恒、所有 placement item index 合法、solution feasible。
- [x] 测试固定 seed 的 metrics fixture。
- [x] 测试输出转 external SPP solution、JSON、再 Importer 恢复。

**Task9 checkpoint (2026-09-18):**

- 已新增 `SppLbfResult` 和 `solveSppLbf`，通过 JaguarCore public `SPProblem` 管理需求、Layout、条带宽度、`fitStrip` 和 solution snapshot。
- item order 支持 `useSparrowConstructorOrder=true` 时的 `convexHullArea*diameter` 降序，以及 reference mode 下的 diameter 降序；相同 key 按 item index 稳定排序。
- 每个 item type 按 demand 逐个搜索；无位置时按 `widthGrowthRatio` 扩宽并重新搜索，超过 `maxWidthGrowthAttempts` 返回 `Infeasible`；成功放置不会超过初始 demand。
- metrics 记录 seed、实际搜索样本数、placed/remaining、fit 后 strip width、density 和 validator feasibility；宽度增长失败搜索的 uniform budget 也计入 sampleCount。
- 已新增 `test_spp_lbf.cpp`，覆盖单 item 多 demand、需求守恒、宽度增长、宽度增长上限、不可行、固定 seed metrics、solution validator 和 external SPP JSON round-trip。
- 已新增 `tests/fixtures/spp_lbf_cpp_reference.tsv`，单独记录 C++ SparrowCore metrics；Task1 的 Rust `lbf_metrics_reference.tsv` 保持不变，当前 C++/Rust width、density 和 sample count 差异未被伪装成逐点一致。
- SparrowCore、Tests、Bench 的 Debug/Release 均已重新构建并运行成功，测试和 Bench 退出码均为 `0`。
- Task9 未修改 JaguarCore 生产源码，未接入 NestLab，未执行 Task20/Task21，未创建 Git commit。

### Task 10: 实现 BPP LBF 构造器

**Files:**

- Create: `SparrowCore/include/SparrowCore/optimizer/bpp_lbf_optimizer.h`
- Create: `SparrowCore/src/optimizer/bpp_lbf_optimizer.cpp`
- Modify: `SparrowCore/tests/test_bpp_lbf.cpp`
- Modify: `SparrowCore/tests/fixtures/lbf_bpp_small.json`

**Interfaces:**

```cpp
Result<BppLbfResult> solveBppLbf(
    const jaguar::BPInstance& instance,
    const LbfConfig& config);
```

BPP 搜索顺序：

1. 先遍历当前 open Layout；
2. 再按 bin index 遍历 stock>0 的 Closed Bin；
3. 首个有可行 placement 的 Layout 接受该 item；
4. 不适合的 item type 停止该 type，不伪造库存消耗；
5. 空 Layout 按 JaguarCore BPProblem 语义关闭并返还库存；
6. 最终输出 layouts、cost、density、remaining demand 和 remaining stock。

- [x] 测试 Open Layout 复用优先于新 Bin。
- [x] 测试库存耗尽、成本、空 Bin、多个 item type。
- [x] 测试 Hole/quality filter 和旋转策略。
- [x] 测试固定 seed metrics 和 JSON round-trip。
- [x] 测试 BPP 输出不包含空 Bin Layout。

### Task 11: 实现 Sparrow 风格 LBFBuilder 兼容层

**Files:**

- Create: `SparrowCore/include/SparrowCore/optimizer/sparrow_lbf_builder.h`
- Create: `SparrowCore/src/optimizer/sparrow_lbf_builder.cpp`
- Create: `SparrowCore/include/SparrowCore/eval/sparrow_lbf_evaluator.h`
- Create: `SparrowCore/src/eval/sparrow_lbf_evaluator.cpp`
- Modify: `SparrowCore/include/SparrowCore/lbf_config.h`
- Modify: `SparrowCore/tests/test_spp_lbf.cpp`

**Interfaces:**

```cpp
struct SparrowConstructorConfig
{
    std::uint64_t seed = 0;
    std::size_t nContainerSamples = 1000;
    std::size_t nFocussedSamples = 0;
    std::size_t nCoordinateDescents = 3;
    float uniqueSampleThreshold = 0.05F;
    float preTranslationStepRatio = 0.25F;
    float preTranslationLimitRatio = 0.02F;
    float finalTranslationStepRatio = 0.01F;
    float finalTranslationLimitRatio = 0.001F;
    float preRotationStepRad = 5.0F * 3.141592653589793F / 180.0F;
    float preRotationLimitRad = 1.0F * 3.141592653589793F / 180.0F;
    float finalRotationStepRad = 0.5F * 3.141592653589793F / 180.0F;
    float finalRotationLimitRad = 0.05F * 3.141592653589793F / 180.0F;
};

Result<SppLbfResult> constructSparrowInitialSolution(
    const jaguar::SPInstance& instance,
    const SparrowConstructorConfig& config);
```

Sparrow 风格 evaluator 使用：

```text
Invalid: surrogate collision or exact collision
Clear: 10*(POI.x + bboxCorner.x) + POI.y + bboxCorner.y
```

Builder 必须支持：

- 按 convexHullArea×diameter 排序；
- `n_container_samples`、`n_focussed_samples`、`n_coord_descents`；
- BestSamples 去重；
- 两阶段 coordinate descent；
- 无初始解时逐 item 构造；
- 宽度上限和 `ConstructionError` 到 JaguarCore `Result` 的稳定映射。

本 Task 只实现 Sparrow 的 initial LBF construction，不实现 Exploration/Compression/Separator。

### Task 12: 实现输出、JSON、SVG 和指标差分

**Files:**

- Create: `SparrowCore/include/SparrowCore/io/lbf_output.h`
- Create: `SparrowCore/src/io/lbf_output.cpp`
- Create: `SparrowCore/tests/test_lbf_json.cpp`
- Modify: `SparrowCore/tests/differential/compare_lbf.cpp`
- Modify: `SparrowCore/tests/fixtures/lbf_metrics_reference.tsv`

**Interfaces:**

```cpp
Result<std::string> serializeSppResultJson(
    const jaguar::SPInstance& instance,
    const SppLbfResult& result);

Result<std::string> serializeBppResultJson(
    const jaguar::BPInstance& instance,
    const BppLbfResult& result);
```

- [ ] 调用 JaguarCore external solution conversion，不复制 JSON parser。
- [ ] 验证输出 JSON 能由 JaguarCore Importer 恢复。
- [ ] 输出 metrics：seed、samples、placed count、remaining demand、width、density、feasible。
- [ ] 输出 SVG 只作为诊断，不改变 Layout/solution。
- [ ] 对 Rust lbf 和 C++ 结果比较可行性、数量守恒、宽度、密度、cost 和 sample count 范围。
- [ ] 明确不要求随机优化位置逐点相同；固定 RNG primitive 和固定小样例可要求 placement/loss 在容差内一致。

### Task 13: 建立 Rust/C++ LBF differential oracle

**Files:**

- Create: `SparrowCore/tools/rust_lbf_oracle/`
- Create: `SparrowCore/tests/fixtures/lbf_seed42_reference.tsv`
- Modify: `jagua-rs/lbf/tests/optimizer_fixture.rs` only when fixture needs an explicit field
- Create: `SparrowCore/docs/differential.md`

**Reference commands:**

```powershell
& 'C:\Users\36176\.cargo\bin\cargo.exe' +1.90.0 test -p lbf --offline --manifest-path D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\Cargo.toml
& 'C:\Users\36176\.cargo\bin\cargo.exe' +1.90.0 run --offline --manifest-path D:\MyFiles\Gits_Xj\NestLab\NestLab\SparrowCore\tools\rust_lbf_oracle\Cargo.toml
```

- [ ] Rust oracle 输出 raw RNG state、sample count、best loss、placement count、strip width、density 和 demand。
- [ ] C++ comparator 读取同一 TSV，不把 Rust `SmallRng` 未定义内部序列当作公共契约。
- [ ] seed=42、samples=小预算和 samples=默认预算分别比较。
- [ ] SPP/BPP 不可行样例比较 `Infeasible`/未放置数量语义。
- [ ] 所有浮点比较定义绝对和相对容差，并记录 `f32`/`double` 转换点。

### Task 14: 建立完整测试矩阵和 Bench

**Files:**

- Modify: `SparrowCore/tests/SparrowCoreTests.pro`
- Create: `SparrowCore/bench/bench_lbf.cpp`
- Modify: `SparrowCore/bench/SparrowCoreBench.pro`
- Create: `SparrowCore/docs/performance.md`
- Modify: `SparrowCore/docs/compatibility.md`

测试矩阵：

| 维度 | 必须覆盖 |
|---|---|
| 几何 | 矩形、凹形、窄凹口、Hole、质量区、连续/离散/禁止旋转 |
| SPP | 空需求、单 item、多 demand、扩宽、fitStrip、不可行 |
| BPP | open/closed bin、stock、cost、空 bin、不可行 |
| 随机 | seed=0、seed=42、重复运行一致、不同 seed 不相同 |
| 差分 | RNG、sampler、loss、sample count、metrics、JSON、SVG |
| 数值 | `f32` oracle、double JaguarCore、NaN/Infinity、极小 bbox |
| 构建 | MSVC2019 x64 Debug/Release，分别链接 JaguarCore Debug/Release |

Bench 必须报告：

```text
problem,items,demand,samples,elapsed_ms,placed,remaining,width,density,checksum
```

- [ ] 对每个 fixture 运行标量基线。
- [ ] 记录采样次数、有效样本率、Surrogate reject 数、exact collision 次数。
- [ ] 记录 SPP/BPP 最终 metrics 和 checksum。
- [ ] 同一配置重复 3 次，报告中位数，不使用单次异常值作为性能结论。
- [ ] Bench 只测 SparrowCore optimizer，不把 NestLab GUI、DXF、PLC 或部署开销混入。

### Task 15: 四套真实配置、文档和验收

**Files:**

- Modify: `SparrowCore/docs/compatibility.md`
- Modify: `SparrowCore/docs/performance.md`
- Modify: `docs/superpowers/plans/2026-09-18-sparrow-lbf-cpp-port.md`

**Verification commands:**

```powershell
# JaguarCore DLL 必须已由现有 JaguarCore 流程构建
qmake SparrowCore.pro CONFIG+=debug "JAGUARCORE_BUILD_DIR=D:\\MyFiles\\Gits_Xj\\NestLab\\NestLab\\build-JaguarCore-MSVC2019x64-Debug"
nmake /NOLOGO
qmake SparrowCore.pro CONFIG+=release "JAGUARCORE_BUILD_DIR=D:\\MyFiles\\Gits_Xj\\NestLab\\NestLab\\build-JaguarCore-MSVC2019x64-Release"
nmake /NOLOGO

qmake tests\SparrowCoreTests.pro CONFIG+=debug "JAGUARCORE_BUILD_DIR=D:\\MyFiles\\Gits_Xj\\NestLab\\NestLab\\build-JaguarCore-MSVC2019x64-Debug"
nmake /NOLOGO
tests\debug\SparrowCoreTests.exe

qmake tests\SparrowCoreTests.pro CONFIG+=release "JAGUARCORE_BUILD_DIR=D:\\MyFiles\\Gits_Xj\\NestLab\\NestLab\\build-JaguarCore-MSVC2019x64-Release"
nmake /NOLOGO
tests\release\SparrowCoreTests.exe

qmake bench\SparrowCoreBench.pro CONFIG+=debug "JAGUARCORE_BUILD_DIR=D:\\MyFiles\\Gits_Xj\\NestLab\\NestLab\\build-JaguarCore-MSVC2019x64-Debug"
nmake /NOLOGO
bench\debug\SparrowCoreBench.exe all

qmake bench\SparrowCoreBench.pro CONFIG+=release "JAGUARCORE_BUILD_DIR=D:\\MyFiles\\Gits_Xj\\NestLab\\NestLab\\build-JaguarCore-MSVC2019x64-Release"
nmake /NOLOGO
bench\release\SparrowCoreBench.exe all
```

- [ ] Debug/Release 测试程序均退出码 `0`。
- [ ] Rust workspace 和 LBF oracle 均通过，允许记录已有 filename collision warning，但不得有 test failure。
- [ ] 固定 seed 重复运行的 metrics、JSON 和 SVG checksum 一致。
- [ ] 结果 restore 后 JaguarCore validators 通过。
- [ ] public headers 扫描无 Qt/Rust/NestLab/CADLab 依赖。
- [ ] 文档明确哪些是 jagua-rs/lbf 兼容语义、哪些是 Sparrow constructor 语义。
- [ ] 不执行 Task20/Task21，不接入 NestLab 生产排料，不创建 Git commit。

## 6. 错误处理与边界语义

| 场景 | 结果 |
|---|---|
| `samplesPerItem == 0` | `InvalidInput` |
| 采样比例不在 `[0,1]` | `InvalidInput` |
| 非有限配置/seed state | `InvalidInput` |
| 采样矩形退化 | `InvalidInput` 或跳过该 sampler，不生成 NaN |
| Surrogate false positive | 进入 exact collision，不直接判定失败 |
| 无可行 placement | SPP 按增长上限扩宽；超过上限返回 `Infeasible` |
| BPP 当前布局和可用 Bin 都不适合 | 停止该 item type，保留 remaining demand |
| JaguarCore 过滤 Hole/quality | 直接使用 JaguarCore `HazardFilter` |
| 取消/时间限制 | 第一版不暴露异步取消；达到 sample budget 后正常返回 |
| JSON/solution mismatch | 复用 JaguarCore `SerializationFailure`/`SnapshotMismatch`/`Infeasible` |

不得把“随机搜索没有找到位置”报告为“数学不可行”；结果指标必须区分 `feasible=false` 和 `ErrorCode::Infeasible`。

## 7. 关键风险与处理

### 7.1 JaguarCore 没有暴露 SPProblem 当前 Layout

处理：SparrowCore 自己持有 `Strip + Layout + demand` 状态，使用 JaguarCore public `Layout` 和 `SPSolution`/`BPSolution` 值字段构造结果；不为优化器暴露新的 JaguarCore private 状态。

### 7.2 Rust/C++ 随机分布不完全一致

处理：固定 xoshiro raw sequence 和 f32 uniform sequence；normal distribution 只比较统计/metrics 容差，不把标准库分布内部实现当作契约。

### 7.3 `Transform2d` double 与 Rust f32 结果偏差

处理：候选决策先以 float 量化，进入 JaguarCore 前显式转换；差分 fixture 同时保存原始 float 输入和 C++ double 输出。

### 7.4 LBF widening 可能无限循环

处理：`maxWidthGrowthAttempts` 和基于所有 item diameter/area 的 width ceiling；超过上限返回 `Infeasible`，测试中必须覆盖。

### 7.5 旋转中心和 preTransform 语义错误

处理：所有候选只操作 JaguarCore `Item::collisionShape()` 与 `Transform2d`；输出统一调用 JaguarCore external conversion，禁止 SparrowCore 自己重写 preTransform 合成。

### 7.6 BPP Layout/stock 生命周期

处理：`BinState` 明确区分 Closed Bin、Open Layout、remaining stock；空 Layout 自动关闭只由 BPP state 执行一次，restore 后用 `validateBPSolution` 检查库存守恒。

## 8. 验收指标

Task15 完成时必须满足：

1. `SparrowCore` 不依赖 Qt、Rust、NestLab 或任何现有排料库运行时；
2. SPP LBF 能在小型 fixture 上完成需求，无法放置时受控扩宽并返回稳定结果；
3. BPP LBF 能在 open/closed Bin 之间搜索并保持 stock/cost 守恒；
4. 固定 seed 的 RNG、采样计数和 metrics 可重复；
5. Rust/C++ differential 通过，差异有明确的 f32/double 容差；
6. SPP/BPP 输出 JSON 可被 JaguarCore Importer 恢复，恢复后 validator 通过；
7. 输出 SVG 能通过 XML、浏览器和 CairoSVG/Pillow 渲染验收；
8. Debug/Release 测试和 Bench 均退出码 `0`；
9. `SparrowCore` 仍未接入 NestLab 生产流程，Task20/21 状态保持未执行；
10. 不把 Sparrow 的 Exploration/Compression/Separator/GUI 误标记为本计划已完成。

## 9. 实施顺序

```text
Task 1  源码映射与 fixture
  ↓
Task 2  SparrowCore static library 骨架
  ↓
Task 3  RNG 与配置
  ↓
Task 4-5  旋转、Uniform、Local Search、Coordinate Descent
  ↓
Task 6-7  SampleEval、Loss、BestSamples、Evaluator
  ↓
Task 8  通用 LBF Search
  ↓
Task 9  SPP LBF
  ↓
Task 10 BPP LBF
  ↓
Task 11 Sparrow LBFBuilder 兼容层
  ↓
Task 12 JSON/SVG/metrics 输出
  ↓
Task 13 Rust/C++ differential
  ↓
Task 14 Bench 与完整测试矩阵
  ↓
Task 15 Debug/Release 验收和文档
```

本计划完成后，`SparrowCore` 具备作为后续 Sparrow Exploration/Compression/Separator 或 NestLab 适配层基础的 LBF 初始排料能力；它仍不是 NestLab 集成，也不是完整 Sparrow 优化器交付。
