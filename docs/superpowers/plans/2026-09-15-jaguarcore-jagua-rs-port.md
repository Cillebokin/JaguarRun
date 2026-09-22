# JaguarCore：jagua-rs 纯 C++ 动态库转换实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Each task uses checkbox tracking and ends with an independent verification checkpoint.

**Goal:** 将 `jagua-rs/jagua-rs` 核心库按行为和数据不变量完整重实现为无 Rust、无 Qt、无 NestLab 依赖的 C++17 动态库 `JaguarCore.dll`，并为后续 Sparrow/NestLab 集成提供稳定的几何、碰撞和问题模型基础。

**Architecture:** 采用“标准 C++ 核心 + 独立 I/O/适配层”的分层结构。`JaguarCore` 只提供几何、形状处理、碰撞检测、布局、快照以及 SPP/BPP/MSPP 问题模型；NestLab 的 `Exp_Entity`、Qt 信号槽和 UI 适配放在核心库之外。Rust 版本只作为行为参考和差分测试 oracle，不进入最终运行时依赖。

**Tech Stack:** C++17、STL、QMake、MSVC2019 x64；Windows 动态库；纯 C++ 核心不链接 Qt、Rust、CADLab、Nest.dll、TileNest.dll 或其他排料库。

## Global Constraints

- 只转换 `jagua-rs/jagua-rs` crate；本计划不转换 `jagua-rs/lbf`，也不转换 Sparrow 优化器。
- 目标库名固定为 `JaguarCore`，Windows 产物为 `JaguarCore.dll` 和 `JaguarCore.lib`，Debug 产物使用 `JaguarCored.dll`/`JaguarCored.lib`。
- 核心库使用 C++17 和 STL；不得在公共头文件中出现 `QObject`、`QString`、`QVector`、`Exp_Entity*` 或 NestLab 类型。
- 默认内部几何精度使用 `double`；所有容差必须集中配置，不得散落硬编码。
- JSON 兼容层可以静态嵌入一个明确版本的通用 C++ JSON parser，但不得引入任何排料、CAD 或几何优化库；该 parser 不得出现在 JaguarCore 的算法公共 API 中。
- 保留 jagua-rs 的语义：简单多边形、形状预处理、Surrogate、Hazard、四叉树 CDE、Layout 快照、SPP/BPP/MSPP 和旋转策略。
- 不逐行翻译 Rust 代码；重实现应保持行为、边界条件和数据不变量，同时使用符合 C++ 习惯的接口。
- `JaguarCore` 不直接承担 NestLab 的 DXF 解析、CAM 图元恢复、路径规划、G-Code 或 PLC 通信。
- 每完成一个底层层次，必须先通过该层的单元测试和差分测试，再进入上层。
- 不执行任何 Git 提交、合并、变基或其他修改 commit 的操作；每个任务使用测试和工作区差异作为检查点。
- jagua-rs 使用 MPL-2.0。若参考或复制原实现中的代码、注释、结构或第三方代码，必须保留对应许可和版权声明；发布前需要许可证审查。

## 1. 现状、来源和范围

### 1.1 参考源码

参考库位于：

```text
D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs
```

核心 crate 位于：

```text
jagua-rs/
├─ src/geometry
├─ src/collision_detection
├─ src/entities
├─ src/probs
├─ src/io
└─ src/util
```

本计划明确不包含：

```text
lbf/
sparrow/
```

`lbf` 是基于 jagua-rs 的参考排料算法，Sparrow 是更上层的优化器；它们依赖 JaguarCore，但不是 JaguarCore 的第一阶段内容。

### 1.2 源码模块映射

| Rust 来源 | JaguarCore 目标模块 | 主要职责 |
|---|---|---|
| `src/geometry/primitives` | `src/geometry/primitives` | Point、Rect、Edge、Circle、SPolygon |
| `src/geometry/transformation.rs` | `src/geometry/transformation` | 矩阵变换和分解 |
| `src/geometry/d_transformation.rs` | `src/geometry/transformation` | 旋转 + 平移表示 |
| `src/geometry/geo_traits.rs` | `include/JaguarCore/geometry_traits.h` | 碰撞、距离、变换能力的 C++ 接口 |
| `src/geometry/geo_enums.rs` | `include/JaguarCore/geometry_enums.h` | 位置关系和旋转范围 |
| `src/geometry/original_shape.rs` | `src/geometry/original_shape` | 原始形状和内部形状转换 |
| `src/geometry/shape_modification.rs` | `src/geometry/shape_modification` | 偏移、简化、窄凹口处理 |
| `src/geometry/convex_hull.rs` | `src/geometry/convex_hull` | 凸包 |
| `src/geometry/fail_fast` | `src/fail_fast` | Pole、Pier、SPSurrogate |
| `src/collision_detection/hazards` | `src/collision/hazards` | Hazard、过滤器、收集器 |
| `src/collision_detection/quadtree` | `src/collision/quadtree` | 四叉树索引和局部遍历 |
| `src/collision_detection/cd_engine.rs` | `src/collision/collision_engine` | 动态注册、查询、快照 |
| `src/entities` | `src/entities` | Item、Container、PlacedItem、Layout |
| `src/probs/spp` | `src/problems/spp` | 条带排料问题模型 |
| `src/probs/bpp` | `src/problems/bpp` | 多 Bin/板材问题模型 |
| `src/probs/mspp` | `src/problems/mspp` | 多条带/多床问题模型 |
| `src/io/ext_repr.rs` | `src/io/external_repr` | 外部数据结构 |
| `src/io/import.rs` | `src/io/import` | 外部结构到内部结构 |
| `src/io/export.rs` | `src/io/export` | 内部结构到外部结构 |
| `src/io/svg` | `src/io/svg` | 调试和结果可视化 |
| `src/util` | `src/support` | 错误、断言、数值辅助 |

### 1.3 已确认的核心行为

必须保留以下行为：

1. `SPolygon` 拒绝少于三个点、重复点、自交和零面积轮廓，并统一为正面积绕序。
2. Item 保存原始形状和碰撞形状两份表示。
3. Item 在内部通常先以重心为中心，输出时需要反向应用预变换。
4. 裁片和容器的偏移方向不同：裁片通常膨胀，容器通常收缩。
5. 容器外部区域、孔洞和低质量区域通过 Hazard 表示。
6. CDE 先通过四叉树和 Surrogate 筛选，再进行完整边相交和包含检查。
7. Layout 支持动态放置、移除、容器替换、快照保存和恢复。
8. SPP/BPP/MSPP 都必须维护 demand、stock、layout 和 solution 的一致性。
9. 旋转策略必须区分禁止旋转、离散旋转和连续旋转。
10. 几何错误必须在导入阶段报告，不能把非法多边形留到优化阶段才崩溃。

## 2. 目标目录和构建产物

### 2.1 目录结构

```text
JaguarCore/
├─ JaguarCore.pro
├─ include/
│  └─ JaguarCore/
│     ├─ export.h
│     ├─ version.h
│     ├─ result.h
│     ├─ geometry/
│     ├─ collision/
│     ├─ entities/
│     ├─ problems/
│     ├─ io/
│     └─ JaguarCore.h
├─ src/
│  ├─ support/
│  ├─ geometry/
│  │  ├─ primitives/
│  │  ├─ transformation/
│  │  ├─ shape_modification/
│  │  └─ convex_hull/
│  ├─ fail_fast/
│  ├─ collision/
│  │  ├─ hazards/
│  │  └─ quadtree/
│  ├─ entities/
│  ├─ problems/
│  │  ├─ spp/
│  │  ├─ bpp/
│  │  └─ mspp/
│  └─ io/
│     ├─ external_repr/
│     └─ svg/
├─ tests/
│  ├─ JaguarCoreTests.pro
│  ├─ test_support.cpp
│  ├─ test_geometry.cpp
│  ├─ test_shape_modification.cpp
│  ├─ test_collision.cpp
│  ├─ test_layout.cpp
│  ├─ test_spp.cpp
│  ├─ test_bpp.cpp
│  ├─ test_mspp.cpp
│  └─ fixtures/
└─ docs/
   ├─ api.md
   ├─ numerical-policy.md
   └─ compatibility.md
```

### 2.2 QMake 动态库

`JaguarCore.pro` 的初始目标结构：

```qmake
TEMPLATE = lib
TARGET = JaguarCore
CONFIG += dll c++17 warn_on

CONFIG(debug, debug|release) {
    TARGET = JaguarCored
}

DEFINES += JAGUARCORE_BUILD

INCLUDEPATH += $$PWD/include

HEADERS += \
    include/JaguarCore/export.h \
    include/JaguarCore/version.h \
    include/JaguarCore/result.h

SOURCES += \
    src/support/version.cpp

DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc
```

核心库不添加 `QT +=`。测试工程可以使用普通 C++ 可执行程序，不依赖 Qt Test。

### 2.3 DLL 导出边界

`include/JaguarCore/export.h` 只负责导出宏：

```cpp
#pragma once

#if defined(_WIN32)
#  if defined(JAGUARCORE_BUILD)
#    define JAGUARCORE_API __declspec(dllexport)
#  else
#    define JAGUARCORE_API __declspec(dllimport)
#  endif
#else
#  define JAGUARCORE_API
#endif
```

公共头文件只导出稳定的值类型、错误类型和高层对象。四叉树节点、SlotMap 替代物、缓存和 Surrogate 内部字段保持私有。

## 3. 公共 C++ API 设计

### 3.1 数值和标识类型

```cpp
namespace jaguar {

using ItemId = std::uint64_t;
using ContainerId = std::uint64_t;
using ItemIndex = std::size_t;

struct Point2d {
    double x = 0.0;
    double y = 0.0;
};

struct Transform2d {
    double rotationRad = 0.0;
    Point2d translation{};
};

}
```

所有公共几何输入必须检查有限性；NaN 和无穷值进入 API 时返回 `InvalidInput`，不使用 C++ 异常作为正常控制流。

### 3.2 错误模型

```cpp
enum class ErrorCode {
    None,
    InvalidInput,
    InvalidPolygon,
    SelfIntersection,
    ZeroArea,
    UnsupportedShape,
    OffsetFailed,
    CollisionEngineFailure,
    SnapshotMismatch,
    Infeasible,
    Cancelled,
    TimeLimit,
    SerializationFailure,
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
};
```

实现一个 C++17 可用的 `Result<T>` 和 `Result<void>`，不依赖 `std::expected`。

### 3.3 几何公共对象

`SimplePolygon` 必须暴露只读查询和显式变换接口：

```cpp
class JAGUARCORE_API SimplePolygon {
public:
    static Result<SimplePolygon> Create(std::vector<Point2d> points);

    const std::vector<Point2d>& vertices() const noexcept;
    Rect2d boundingBox() const noexcept;
    Point2d centroid() const noexcept;
    double area() const noexcept;
    double diameter() const noexcept;

    Result<void> generateSurrogate(const SurrogateConfig& config);
    SimplePolygon transformed(const Transform2d& transform) const;
};
```

## 4. Rust 依赖替换策略

| Rust 能力 | C++ 实现 |
|---|---|
| `anyhow::Result` | `Result<T>` + `ErrorCode` |
| `thiserror` | `ErrorCode` + `Error::message` |
| `Arc<T>` | `std::shared_ptr<const T>` |
| `SlotMap` | `HandlePool<T>`，句柄包含索引和 generation |
| `SecondaryMap` | 与句柄绑定的稀疏数组或 `unordered_map` |
| `NotNan<f32>` | `double` 有限性检查 |
| `rayon` | 首先使用 `std::thread`，稳定后再接线程池 |
| `serde` | C++ 外部表示结构 + 独立 codec |
| `geo-types` | JaguarCore 自有 `Polygon`/`Point` 类型 |
| `geo-buffer` | 私有 C++ straight-skeleton rounded offset 重实现；不进入运行时依赖 |
| `ndarray` | `std::vector` 和定长数组 |
| Rust iterator/closure | 明确的 C++ range/循环/回调接口 |

不得把 Rust 的所有权语义直接替换成无约束裸指针。动态库内部使用 RAII、`std::unique_ptr`、`std::shared_ptr` 和值语义；跨 DLL 边界由库创建的对象必须由同一个库提供释放接口或通过值对象返回。

## 5. 详细实施任务

### Task 1: 建立 JaguarCore DLL 骨架

**Files:**

- Create: `JaguarCore/JaguarCore.pro`
- Create: `JaguarCore/include/JaguarCore/export.h`
- Create: `JaguarCore/include/JaguarCore/version.h`
- Create: `JaguarCore/src/support/version.cpp`
- Create: `JaguarCore/tests/JaguarCoreTests.pro`
- Create: `JaguarCore/tests/test_support.cpp`

**Interfaces:**

- Produces: `JaguarCore.dll`、`JaguarCore.lib`、公共导出宏和版本接口。

- [x] 写 DLL 导出宏和 `JaguarCoreVersion()`。
- [x] 配置 Debug/Release 输出目录和对象目录。
- [x] 创建最小测试程序，检查版本字符串非空、库对象可加载。
- [x] 用 MSVC2019 x64 Debug/Release 分别构建。
- [x] 检查产物目录中同时存在 DLL 和 import library。

**Verification:**

```powershell
qmake JaguarCore.pro
nmake
qmake tests/JaguarCoreTests.pro
nmake
tests\JaguarCoreTests.exe
```

Expected: 两种配置都能生成 DLL，测试程序返回 0。

### Task 2: 建立基础类型、错误和数值策略

**Files:**

- Create: `include/JaguarCore/result.h`
- Create: `include/JaguarCore/numeric_policy.h`
- Create: `include/JaguarCore/geometry/point.h`
- Create: `include/JaguarCore/geometry/rect.h`
- Create: `src/support/result.cpp`
- Create: `tests/test_support.cpp`

**Interfaces:**

- Produces: `Point2d`、`Rect2d`、`Result<T>`、`ErrorCode`、`NumericPolicy`。

```cpp
struct NumericPolicy {
    double equalityTolerance = 1.0e-9;
    double geometryTolerance = 1.0e-7;
    double areaTolerance = 1.0e-9;
    double arcChordTolerance = 1.0e-3;
};
```

- [x] 实现有限值检查、近似相等、符号判断。
- [x] 实现 `Rect2d::relationTo()`、`intersection()`、`contains()`、`inflate()`、`area()`。
- [x] 为退化矩形、负宽高、NaN 和无穷值写失败测试。
- [x] 保证误差策略不依赖当前线程或全局可变状态。

### Task 3: 实现 Point、Edge、Circle 和基础变换

**Files:**

- Create: `include/JaguarCore/geometry/edge.h`
- Create: `include/JaguarCore/geometry/circle.h`
- Create: `include/JaguarCore/geometry/transformation.h`
- Create: `src/geometry/primitives/edge.cpp`
- Create: `src/geometry/primitives/circle.cpp`
- Create: `src/geometry/transformation/transformation.cpp`
- Test: `tests/test_geometry.cpp`

**Interfaces:**

- Produces: 点距离、边距离、线段包围盒、线段相交、刚体变换和逆变换。

- [x] 实现旋转后平移的统一约定。
- [x] 实现变换复合、逆变换和分解。
- [x] 对平行、重合、端点相交和近似相交分别测试。
- [x] 对 0°、90°、180°、270° 和负角度测试。
- [x] 验证 `inverse(transform(p))` 在容差内恢复原点。

### Task 4: 实现 SimplePolygon 和合法性校验

**Files:**

- Create: `include/JaguarCore/geometry/simple_polygon.h`
- Create: `src/geometry/primitives/simple_polygon.cpp`
- Create: `src/geometry/convex_hull/convex_hull.cpp`
- Create: `include/JaguarCore/geometry/convex_hull.h`
- Test: `tests/test_geometry.cpp`

**Interfaces:**

```cpp
class SimplePolygon {
public:
    static Result<SimplePolygon> Create(std::vector<Point2d> points);
    bool contains(Point2d point) const;
    bool intersects(const Edge2d& edge) const;
    double distanceTo(Point2d point) const;
    std::vector<Edge2d> edges() const;
};
```

- [x] 在 SimplePolygon 核心层严格拒绝重复点；允许的首尾/连续重复点由后续 Importer 预处理层去除。
- [x] 拒绝非相邻重复点。
- [x] 检查自交。
- [x] 使用鞋带公式计算有向面积。
- [x] 将顺时针输入统一为逆时针内部表示。
- [x] 计算 bbox、重心、直径和凸包面积。
- [x] 实现边界点的确定性包含规则，避免射线穿过顶点时结果不稳定。
- [x] 测试矩形、凸多边形、凹多边形、自交轮廓、零面积轮廓和重复顶点。

### Task 5: 实现 OriginalShape 和形状预处理

**Files:**

- Create: `include/JaguarCore/geometry/original_shape.h`
- Create: `include/JaguarCore/geometry/shape_modification.h`
- Create: `src/geometry/shape_modification/simplify.cpp`
- Create: `src/geometry/shape_modification/offset.cpp`
- Create: `src/geometry/shape_modification/narrow_concavity.cpp`
- Create: `src/geometry/original_shape/original_shape.cpp`
- Test: `tests/test_shape_modification.cpp`

**Interfaces:**

```cpp
enum class ShapeModifyMode { Inflate, Deflate };

struct ShapeModifyConfig {
    std::optional<double> simplifyTolerance;
    std::optional<double> offset;
    std::optional<std::pair<double, double>> narrowConcavityCutoff;
};

class OriginalShape {
public:
    Result<SimplePolygon> convertToInternal() const;
};
```

- [x] 先应用预变换，再应用 offset、简化和窄凹口处理。
- [x] 裁片使用 Inflate 语义，容器使用 Deflate 语义。
- [x] 简化算法必须保持指定的内含/外含关系。
- [x] Offset 失败时返回 `OffsetFailed`，不能静默取第一个断开多边形。
- [x] 对 narrow concavity 关闭前后面积和轮廓合法性做断言。
- [x] 用人工构造的凹多边形、窄槽和接近退化的轮廓测试。

### Task 6: 实现 Pole、Pier 和 Surrogate

**Files:**

- Create: `include/JaguarCore/fail_fast/surrogate.h`
- Create: `src/fail_fast/pole.cpp`
- Create: `src/fail_fast/piers.cpp`
- Create: `src/fail_fast/surrogate.cpp`
- Test: `tests/test_shape_modification.cpp`

**Interfaces:**

```cpp
struct SurrogateConfig {
    std::array<std::pair<std::size_t, double>, 3> poleLimits{};
    double failFastPoleAreaRatio = 0.0;
    std::size_t failFastPiers = 0;
};

class Surrogate {
public:
    const std::vector<Circle2d>& poles() const noexcept;
    const std::vector<Edge2d>& piers() const noexcept;
    std::size_t failFastPoleCount() const noexcept;
    double convexHullArea() const noexcept;
};
```

- [x] 实现 Pole of Inaccessibility 的四叉树细分搜索。
- [x] 保证生成的 Pole 不相互重叠并位于轮廓内部。
- [x] 实现 Pier 射线生成、裁剪和覆盖选择。
- [x] 缓存 Surrogate，避免每次变换重新生成。
- [x] 测试凸、凹、细长和近退化多边形。

### Task 7: 实现 Hazard、Filter 和 Collector

**Files:**

- Create: `include/JaguarCore/collision/hazard.h`
- Create: `include/JaguarCore/collision/hazard_filter.h`
- Create: `include/JaguarCore/collision/hazard_collector.h`
- Create: `src/collision/hazards/hazard.cpp`
- Create: `src/collision/hazards/collector.cpp`
- Test: `tests/test_collision.cpp`

**Interfaces:**

```cpp
enum class HazardKind {
    PlacedItem,
    Exterior,
    Hole,
    InferiorQualityZone,
};

struct HazardEntity {
    HazardKind kind{};
    std::size_t itemIndex = 0;
    std::size_t quality = 0;
    std::size_t zoneIndex = 0;
};

struct Hazard {
    HazardEntity entity;
    std::shared_ptr<const SimplePolygon> shape;
    bool dynamic = false;
};
```

- [x] 用稳定的 `HazardHandle` 替代 Rust `SlotMap` key。
- [x] 区分 Interior hazard 和 Exterior hazard 的包含语义。
- [x] 实现基本过滤器和收集器。
- [x] 处理删除后句柄失效，禁止访问悬空 Hazard。

### Task 8: 实现 Quadtree

**Files:**

- Create: `include/JaguarCore/collision/quadtree.h`
- Create: `src/collision/quadtree/node.cpp`
- Create: `src/collision/quadtree/hazard_vector.cpp`
- Create: `src/collision/quadtree/partial_hazard.cpp`
- Create: `src/collision/quadtree/query.cpp`
- Test: `tests/test_collision.cpp`

**Interfaces:**

```cpp
class QuadTree {
public:
    QuadTree(std::uint8_t depth, Rect2d bounds, std::uint8_t edgeThreshold);
    Result<void> registerHazard(HazardHandle, const Hazard&);
    Result<void> deregisterHazard(HazardHandle);
    std::vector<HazardHandle> query(const Edge2d&, const HazardFilter&) const;
    const QuadTreeNode& root() const noexcept;
};
```

- [x] 保持父节点和子节点 Hazard presence 状态一致。
- [x] 实现 `None`、`Partial`、`Entire` 三种存在状态。
- [x] 处理跨越多个象限的 Hazard。
- [x] 实现虚拟根查询，避免每次从根节点遍历。
- [x] 在 Debug 测试中重建全量索引并与增量索引比较。

### Task 9: 实现 CollisionEngine

**Files:**

- Create: `include/JaguarCore/collision/collision_engine.h`
- Create: `src/collision/collision_engine.cpp`
- Create: `src/collision/snapshot.cpp`
- Test: `tests/test_collision.cpp`

**Interfaces:**

```cpp
class CollisionEngine {
public:
    bool detectPolygonCollision(const SimplePolygon&, const HazardFilter&) const;
    bool detectSurrogateCollision(const Surrogate&, const Transform2d&, const HazardFilter&) const;
    std::vector<HazardEntity> collectPolygonCollisions(const SimplePolygon&) const;
    Result<void> registerHazard(const Hazard&);
    Result<Hazard> deregisterHazard(HazardHandle);
    CollisionSnapshot save() const;
    Result<void> restore(const CollisionSnapshot&);
};
```

- [x] 实现四叉树粗筛。
- [x] 对候选边进行精确相交检查。
- [x] 对 bbox 包含关系执行最小点包含检查。
- [x] 对容器外部区域报告 Exterior。
- [x] 对 Surrogate 只作 fail-fast；不能把 Surrogate 结果当最终精确碰撞结果。
- [x] 测试边相交、完全包含、完全被包含、外部越界、相切和小于容差的间隙。

### Task 10: 实现 Item、Container、PlacedItem 和 Layout

**Files:**

- Create: `include/JaguarCore/entities/item.h`
- Create: `include/JaguarCore/entities/container.h`
- Create: `include/JaguarCore/entities/placed_item.h`
- Create: `include/JaguarCore/entities/layout.h`
- Create: `src/entities/item.cpp`
- Create: `src/entities/container.cpp`
- Create: `src/entities/placed_item.cpp`
- Create: `src/entities/layout.cpp`
- Test: `tests/test_layout.cpp`

**Interfaces:**

```cpp
class Layout {
public:
    explicit Layout(Container container);
    Result<PlacedItemHandle> placeItem(const Item&, Transform2d);
    Result<PlacedItem> removeItem(PlacedItemHandle);
    Result<void> swapContainer(Container);
    bool isFeasible() const;
    double density() const;
    LayoutSnapshot save() const;
    Result<void> restore(const LayoutSnapshot&);
};
```

- [x] Item 同时保存原始轮廓和碰撞轮廓。
- [x] Container 创建 Exterior、Hole 和质量区域 Hazard。
- [x] Layout 放置时注册动态 Hazard。
- [x] Layout 移除时先从 CDE 注销，再释放 PlacedItem。
- [x] Snapshot 必须可恢复所有 placement、container、动态 hazard 和容器宽度。
- [x] 用随机放置/移除/恢复序列验证索引和 CDE 一致性。

### Task 11: 实现外部表示和 Importer

**Files:**

- Create: `include/JaguarCore/io/external_repr.h`
- Create: `include/JaguarCore/io/importer.h`
- Create: `src/io/external_repr.cpp`
- Create: `src/io/importer.cpp`
- Test: `tests/test_io.cpp`

**Interfaces:**

```cpp
struct ExternalItem {
    ItemId id = 0;
    RotationPolicy rotation;
    ExternalShape shape;
    std::optional<std::size_t> minQuality;
};

struct ExternalContainer {
    ContainerId id = 0;
    ExternalShape shape;
    std::vector<ExternalQualityZone> zones;
};

class Importer {
public:
    Result<Item> importItem(const ExternalItem&, ItemIndex);
    Result<Container> importContainer(const ExternalContainer&);
};
```

- [x] 支持 Rectangle、SimplePolygon 和带容器孔洞的 Polygon。
- [x] 按 jagua-rs 规则拒绝带孔洞的 Item 和 MultiPolygon Item。
- [x] 支持离散旋转、步进旋转和连续旋转。
- [x] 验证旋转角有限、非空、去重和 360° 步进合法性。
- [x] 支持最小裁片间距转为裁片膨胀/容器收缩。
- [x] 输入错误返回稳定的 ErrorCode 和可读消息。

### Task 12: 实现 SPP 问题模型

**Files:**

- Create: `include/JaguarCore/problems/spp.h`
- Create: `src/problems/spp/strip.cpp`
- Create: `src/problems/spp/instance.cpp`
- Create: `src/problems/spp/problem.cpp`
- Create: `src/problems/spp/solution.cpp`
- Test: `tests/test_spp.cpp`

**Interfaces:**

```cpp
struct Strip {
    double fixedHeight = 0.0;
    double width = 0.0;
    CDEConfig cdeConfig;
    ShapeModifyConfig shapeModifyConfig;
};

struct SPPlacement {
    ItemIndex itemIndex = 0;
    Transform2d transform;
};

class SPProblem {
public:
    Result<PlacedItemHandle> placeItem(const SPPlacement&);
    Result<SPPlacement> removeItem(PlacedItemHandle);
    Result<void> changeStripWidth(double width);
    Result<void> fitStrip();
    SPSolution save() const;
    Result<void> restore(const SPSolution&);
};
```

- [x] 保持 item demand 数量守恒。
- [x] 初始条带宽度使用总物品面积 / 固定高度。
- [x] `fitStrip()` 保留间距 offset 和容差。
- [x] 条带变宽/变窄时重建容器和 CDE。
- [x] 测试空需求、单裁片、多数量、不可放置和宽度边界。

### Task 13: 实现 BPP 问题模型

**Files:**

- Create: `include/JaguarCore/problems/bpp.h`
- Create: `src/problems/bpp/bin.cpp`
- Create: `src/problems/bpp/instance.cpp`
- Create: `src/problems/bpp/problem.cpp`
- Create: `src/problems/bpp/solution.cpp`
- Test: `tests/test_bpp.cpp`

**Interfaces:**

```cpp
struct Bin {
    std::size_t index = 0;
    ContainerId externalId = 0;
    Container container;
    std::size_t stock = 0;
    std::uint64_t cost = 0;
};

enum class LayoutTargetKind { Open, Closed };

struct BPPlacement {
    LayoutTargetKind targetKind = LayoutTargetKind::Closed;
    std::size_t layoutIndex = 0;
    std::size_t binIndex = 0;
    ItemIndex itemIndex = 0;
    Transform2d transform;
};
```

- [x] 实现打开/关闭 Bin。
- [x] 维护每类 Bin 的 stock。
- [x] 空 Layout 自动关闭并返还库存。
- [x] 计算总利用率和 Bin 成本。
- [x] 测试多种板材尺寸、库存不足、空 Bin、移除最后一个裁片。

### Task 14: 实现 MSPP 问题模型

**Files:**

- Create: `include/JaguarCore/problems/mspp.h`
- Create: `src/problems/mspp/strip.cpp`
- Create: `src/problems/mspp/instance.cpp`
- Create: `src/problems/mspp/problem.cpp`
- Create: `src/problems/mspp/solution.cpp`
- Test: `tests/test_mspp.cpp`

**Interfaces:**

```cpp
struct MultiStrip {
    double maxWidth = 0.0;
    double fixedHeight = 0.0;
    double width = 0.0;
};

class MSPProblem {
public:
    Result<std::size_t> addLayout(MultiStrip);
    Result<void> changeStripWidth(std::size_t layoutIndex, double width);
    Result<void> fitStrip(std::size_t layoutIndex);
    double totalStripWidth() const;
    MSPSolution save() const;
    Result<void> restore(const MSPSolution&);
};
```

- [x] 保证每个条带宽度不超过 `maxWidth`。
- [x] 支持多个 Layout 独立保存和恢复。
- [x] 维护全局 demand 数量。
- [x] 计算所有条带总宽度和总体利用率。
- [x] 测试新增、删除、扩展、收缩和跨条带恢复。

### Task 15: 实现快照和一致性断言

**Files:**

- Create: `include/JaguarCore/assertions.h`
- Create: `src/support/assertions.cpp`
- Modify: `src/entities/layout.cpp`
- Modify: `src/problems/spp/problem.cpp`
- Modify: `src/problems/bpp/problem.cpp`
- Modify: `src/problems/mspp/problem.cpp`
- Test: `tests/test_layout.cpp`, `tests/test_spp.cpp`, `tests/test_bpp.cpp`, `tests/test_mspp.cpp`

- [x] 实现 Layout 与 CDE 全量重建后的等价性检查。
- [x] 实现 Snapshot 与当前 layout 的 item 数量、container、Hazard 一致性检查。
- [x] Debug 构建启用断言，Release 构建不依赖断言保证正确性。
- [x] 对随机放置/移除/恢复序列运行至少 1000 次状态检查。
- [x] 任何恢复失败返回 `SnapshotMismatch`，不继续使用部分恢复状态。

### Task 16: 实现外部 JSON 兼容层

**Files:**

- Create: `include/JaguarCore/io/json_codec.h`
- Create: `src/io/json_codec.cpp`
- Create: `tests/test_json_codec.cpp`
- Create: `docs/compatibility.md`

**Design:**

核心几何和问题模型不依赖 JSON。JSON codec 只负责兼容 jagua-rs 的 external representation，输入结构和输出字段必须与以下 Rust 文件对应：

```text
jagua-rs/src/io/ext_repr.rs
jagua-rs/src/probs/spp/io/ext_repr.rs
jagua-rs/src/probs/bpp/io/ext_repr.rs
jagua-rs/src/probs/mspp/io/ext_repr.rs
```

- [x] 选择纯 C++ JSON 解析实现，并将其作为 JaguarCoreIO 的内部组件；不把 Qt JSON 类型暴露到 JaguarCore API。
- [x] 保持字段名称、默认值、旋转模式和错误语义。
- [x] 拒绝旧的 orientation 字段和未知的强约束字段。
- [x] 对 Rust 仓库中的 `assets/*.json` 做代表性导入回归（`swim.json`、`baldacci1.json`）。
- [x] 对生成 JSON 做重新导入，验证 item ID、需求、旋转和 Layout 变换一致。

### Task 17: 实现 SVG 调试导出

**Files:**

- Create: `include/JaguarCore/io/svg_export.h`
- Create: `src/io/svg_export.cpp`
- Test: `tests/test_svg_export.cpp`

- [x] 输出容器边界、原始轮廓、碰撞轮廓和 placement transformation。
- [x] 输出坐标轴方向、旋转角和 item ID。
- [x] 生成的 SVG 能在浏览器和 CairoSVG/Pillow 矢量渲染器中打开/渲染（浏览器 AX/截图、XML DOM、四套 `110×110` 非空 PNG 均已通过；本机未安装 Inkscape/rsvg，未宣称其专用工具验收）。
- [x] SVG 导出失败只影响诊断输出，不改变 Layout 状态。

### Task 18: 建立 Rust/C++ 差分 oracle

**Files:**

- Create: `tests/differential/README.md`
- Create: `tests/differential/compare_geometry.cpp`
- Create: `tests/differential/compare_problem.cpp`
- Create: `tests/fixtures/rust_reference/`
- Create: `docs/compatibility.md`

**Reference commands:**

```powershell
# 在安装 Rust 1.90 后，先运行原始参考库
cargo test --workspace --manifest-path D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\Cargo.toml

# 生成固定输入下的 Rust external JSON/SVG 结果
cargo run --manifest-path D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\lbf\Cargo.toml -- --input-file D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\assets\swim.json --solution-folder D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\tests\fixtures\rust_reference\lbf_spp --prob-var spp --log-level info
```

- [x] 先比较 Point、Rect、Edge、Polygon 的几何数值。
- [x] 再比较 shape modification 的顶点数、面积和边界关系（offset、simplify、narrow concavity 基线）。
- [x] 再比较 CDE 的边相交、包含、相切、Surrogate 和 Hazard 类型矩阵。
- [x] 再比较 Layout 放置/移除/恢复后的 placement。
- [x] 最后比较 SPP/BPP/MSPP 的 demand、stock、density 和基础宽度/布局不变量。
- [x] 不要求随机优化得到逐点相同的位置；必须比较可行性、数量守恒和指标范围。
- [x] 固定随机种子时，记录共享 xorshift64* 随机源、浮点类型、操作顺序和最终状态；不把 Rust `SmallRng` 内部序列假设为跨语言 API。

### Task 19: 性能和并发优化

**Files:**

- Create: `tests/bench/bench_geometry.cpp`
- Create: `tests/bench/bench_collision.cpp`
- Create: `tests/bench/bench_layout.cpp`
- Create: `docs/performance.md`

- [x] 先建立无优化基线：完整多边形碰撞、线性 hazard 遍历。
- [x] 加入 bbox 粗筛后重新测量。
- [x] 加入四叉树后重新测量。
- [x] 加入 Surrogate fail-fast 后重新测量。
- [x] 比较缓存变换和重复重建的成本。
- [x] 只在基线正确后考虑 SoA、SIMD 和线程池。
- [x] 记录 item 数、顶点数、查询次数、碰撞命中率和耗时。
- [x] 任何并发优化都必须通过快照一致性和重复运行测试。

### Task 20: JaguarCore 与 NestLab 的适配设计验证

**Files:**

- Create: `ProNestLab/Adapters/JaguarCorePolygonAdapter.h`
- Create: `ProNestLab/Adapters/JaguarCorePolygonAdapter.cpp`
- Create: `ProNestLab/Adapters/JaguarCorePlacementAdapter.h`
- Create: `ProNestLab/Adapters/JaguarCorePlacementAdapter.cpp`
- Create: `docs/nestlab-integration.md`

**Boundary:**

这一步只建立设计和验证程序，不替换生产排料调用。适配层负责：

```text
Exp_Polyline / Exp_Circle / Exp_Ellipse
        ↓
离散为 JaguarCore Polygon
        ↓
保存原始 Exp_Entity* 和 item ID
        ↓
JaguarCore placement
        ↓
Clone 原始 Exp_Entity
        ↓
应用旋转和平移
        ↓
返回 NestLab 结果对象
```

- [ ] 明确圆弧离散弦高误差。
- [ ] 验证闭合状态、绕序、重心和坐标轴方向。
- [ ] 验证 `Exp_Entity::Clone()` 的所有权和释放责任。
- [ ] 验证 output transformation 与原始 CAD 图元的旋转基点一致。
- [ ] 用 `排料文件/Dxf00` 的 `swim`/`dagli` 样例做转换回归。

### Task 21: 打包、部署和 DLL 兼容性

**Files:**

- Create: `JaguarCore/package/README.md`
- Create: `JaguarCore/package/JaguarCoreRuntimeCheck.cpp`
- Create: `docs/deployment.md`
- Modify: `JaguarCore/JaguarCore.pro`

- [x] 定义 Debug/Release 目录结构。
- [x] 定义头文件、DLL、LIB 和运行时依赖的发布目录。
- [x] 运行时检查 DLL 版本、架构和导出符号。
- [x] 验证 MSVC2019 x64 下的调用者与 DLL CRT 配置一致。
- [x] 不跨 DLL 释放由不同模块分配的内存。
- [x] 不把内部 STL 容器所有权暴露为需要调用方释放的裸指针。

**Task 21 checkpoint (2026-09-20):**

- Debug/Release × AVX2/scalar 四套 JaguarCore 和完整测试均构建、运行通过。
- 统一包包含 31 个公共头文件、四套 DLL/LIB、Debug/Release 检查器、部署文档和 `VerifyPackage.ps1`。
- 包级自动验收四套产物均输出 `runtime_check,result,pass`，最终输出 `package_check,result,pass`。
- 检查器直接解析目标 DLL 的 PE 导入表，强制核对 x64、`0.1.0.0` 文件版本、`0.1.0-dev` 公共版本、`JaguarCoreVersion` 精确导出和 Debug/Release 动态 CRT。
- qmake 实测拒绝非 x64 和非 MSVC2019 工具链标签；`dumpbin` 复核四套 DLL 无 Qt/Rust 依赖。
- `CollisionEnginePtr` 通过 DLL 导出的 `CollisionEngineDeleter` 在 JaguarCore 内释放 DLL 创建的引擎。

## 6. 重点测试矩阵

### 6.1 几何输入

| 场景 | 必须验证 |
|---|---|
| 矩形 | 面积、重心、四边、旋转 |
| 凸多边形 | 凸包、Pole、碰撞 |
| 凹多边形 | 点包含、边相交、窄凹口 |
| 重复首尾点 | 自动去除 |
| 连续重复点 | 自动去除 |
| 非相邻重复点 | 拒绝 |
| 自交 | 拒绝 |
| 零面积 | 拒绝 |
| 圆弧离散轮廓 | 弦高误差和闭合 |
| 非有限坐标 | 拒绝 |

### 6.2 CDE

- 空 Layout 无动态 Hazard；
- 放置一个 item 后恰有一个动态 Hazard；
- 移除 item 后 Hazard 不残留；
- 外部越界返回 Exterior；
- 完全包含和边相交都能报告；
- 相切是否视为碰撞由统一 NumericPolicy 决定；
- Surrogate false positive 必须由精确检查过滤；
- Snapshot 恢复后 Quadtree 与全量重建结果一致。

### 6.3 问题模型

- SPP：固定高度、宽度变化、需求守恒；
- BPP：多 Bin、stock、成本、空 Layout 关闭；
- MSPP：最大宽度、多个条带、总宽度、布局增删；
- 所有模型：旋转策略、最小间距、容器孔洞和质量区域。

## 7. 验收标准

### JaguarCore DLL 验收

- [x] MSVC2019 x64 Debug/Release 能生成 `JaguarCore.dll` 和 import library（本轮同时验证 AVX2/Scalar 四种配置）。
- [x] 核心公共头文件不依赖 Qt、Rust、NestLab 或 CADLab。
- [x] 所有几何输入错误返回可识别错误码。
- [x] CDE 的增量状态与全量重建状态一致。
- [x] Layout 快照保存/恢复通过随机序列测试。
- [x] SPP/BPP/MSPP 的 demand、stock、layout 和 solution 一致。
- [x] Rust fixtures 的几何和布局属性差分通过。
- [x] 运行时 DLL 加载检查通过。

### 生产前置验收

**JaguarCore-only 验收检查点（2026-09-21）：**

- JaguarCoreTests 已重新执行并通过 Debug/Release × AVX2/scalar 四种配置。
- 已安装交付包 `VerifyPackage.ps1` 已重新执行并通过四套 DLL 的架构、版本、导出符号和动态 CRT 检查。
- 已盘点 `Dxf00`/`Dxf01` 真实样例：`Dxf00` 为闭合点列形式的 `LWPOLYLINE`，`Dxf01` 还包含 ARC、CIRCLE、ELLIPSE、SPLINE、INSERT 等实体。
- 当前 JaguarCore 没有 DXF 读取器，也没有 `Exp_Entity` 适配层；以下涉及 DXF 转换、结果回写、NestLab、GUI 和机床的条目因此保持未完成，不能用核心 JSON/几何测试替代。

- [ ] `Dxf00` 样例转换和几何验证通过。
- [ ] `Dxf01` 多类型样例转换通过。
- [ ] 包含圆弧、凹边和细小特征的真实裁片通过。
- [ ] 结果转换回 `Exp_Entity` 后，原始圆弧和 CAD 属性未丢失。
- [ ] 后续 NestLab 路径规划和 G-Code 导出通过。
- [ ] GUI 结果页显示、结果树、报告和 PNG 导出通过。
- [ ] 现场切割前完成实际机床验证；静态测试通过不等于生产可用。

## 8. 不在本计划内的内容

以下内容必须作为独立后续计划处理：

- Sparrow 的 LBF、Exploration、Compression 和 Separator 优化器；
- NestLab 的普通自动排料替换；
- 平铺排料；
- 共边平铺；
- 切割路径规划；
- G-Code 后处理；
- PLC/Modbus 通信；
- UI 重构和排料参数界面改造。

## 9. 实施顺序总览

```text
Task 1  DLL 骨架
   ↓
Task 2-4  数值、基础几何、多边形
   ↓
Task 5-6  形状预处理和 Surrogate
   ↓
Task 7-9  Hazard、Quadtree、CDE
   ↓
Task 10  Layout 和快照
   ↓
Task 11  外部表示和 Importer
   ↓
Task 12-14  SPP / BPP / MSPP
   ↓
Task 15  一致性断言
   ↓
Task 16-17  JSON / SVG 兼容层
   ↓
Task 18  Rust/C++ 差分测试
   ↓
Task 19  性能和并发
   ↓
Task 20  NestLab 适配验证
   ↓
Task 21  部署和 DLL 验收
```

本计划完成后，`JaguarCore` 才具备作为 Sparrow 或 NestLab 新排料算法基础设施的条件；它本身仍不是排料优化器，Sparrow 的优化逻辑应在 JaguarCore 的可行几何和 Layout API 稳定后另行实现。

## 10. 执行进度记录（2026-09-15）

### 本轮已完成并验证

- 已安装用户级 Rust `1.90.0`，并运行原始 `jagua-rs` workspace：26 个实际测试通过，0 失败；同时确认 Rust 参考工程仍包含 `lbf` workspace 成员，但 JaguarCore 不链接它。
- `OriginalShape` 已按 Rust 语义执行：只有配置 `simplifyTolerance` 时才执行窄凹口关闭；简化结果增加 `shapeModificationValid` 关系校验。
- `offsetShape` 在结果不满足 Inflate/Deflate 包含关系或退化时返回 `ErrorCode::OffsetFailed`，不再静默接受反向/放大的 Deflate 结果。
- `offsetShape` 已对正向凸角加入约 `0.1` 弧度圆弧采样；Rust `geo-buffer` 方形参考值已写入 C++ 回归测试。Pier 裁剪结果按 `0.999` 比例内缩，端点不再落在轮廓边界上。
- `HazardCollector` 已支持稳定句柄去重、重复句柄实体更新和 `containsEntity`。
- 四叉树已从 bbox-only 索引升级为带 `None/Partial/Entire` presence 的重建索引；`cdThreshold` 会驱动节点细分，并提供 presence、节点数和 virtual-root 边界查询。
- CDE 已将完整 Hazard 注册到四叉树；Exterior Hazard 使用“候选轮廓必须完整位于容器内”的语义，支持过滤、精确相交、包含和快照基线。
- Pier 的 U 形覆盖回归、Surrogate 缓存命中/变换/配置失效回归均已通过；Debug/Release 两种 JaguarCore 测试程序在高分辨率 Pier 候选下均退出码 `0`。
- Task 10 基线已落地：Item 保存原始/碰撞轮廓并生成缓存 Surrogate；Container 构造 Exterior、Hole 和质量区 Hazard；PlacedItem 保留 Item、变换和碰撞轮廓；Layout 支持动态注册/注销、可行性、密度、容器替换和原子式快照重建。
- Layout 一致性增强已完成：Snapshot restore 逐项匹配 dynamic、HazardEntity 和多边形顶点；`minQuality` 过滤可接受质量区而始终保留 Hole；新增确定性 1000 步放置/移除/恢复并与全量重建 Layout 对照。
- Task 11 Importer 基线已落地：ExternalShape 支持 Rectangle/SimplePolygon/Polygon/MultiPolygon 表示；Item 支持重心归零、旋转策略归一化/去重/步进展开、最小间距；Container 支持外轮廓孔洞合并到 quality 0 和按 quality 合并质量区，并按 jagua-rs 规则拒绝 Item 孔洞与 MultiPolygon。
- Task 12 SPP 基线已落地：Strip 校验固定高度/宽度并生成条带 Container；SPInstance 按总需求面积/固定高度初始化宽度；SPProblem 维护 demand、放置/移除、宽度变更、fitStrip 和恢复；SPSolution 保存条带与 Layout 快照。
- Task 13 BPP 基线已落地：BPInstance 校验 Item/Bin identity；BPProblem 支持 Closed 打开 Bin、Open 复用 Layout、库存扣减/返还、空 Layout 自动关闭、成本/密度和 BPSolution 恢复。BPP 源文件已使用唯一 obj 名，避免与 SPP 同名源文件发生静默漏链接。
- Task 14 MSPP 基线已落地：MultiStrip 限制 `maxWidth`；MSPProblem 支持多个独立 Layout、跨条带 demand、宽度变更/fitStrip、删除和 MSPSolution 恢复，并计算总条带宽度/整体密度。MSPP 源文件同样使用唯一 obj 名。
- Task 15 验证层基线已落地：新增 `validateLayout`、`validateLayoutSnapshot` 及 SPP/BPP/MSPP solution validators；逐项比较 placement、变换、轮廓、dynamic Hazard、需求和库存。Debug 接入 `debugAssert...`，Release 不依赖断言执行正确性。
- Task 16 JSON 兼容层基线已落地：新增纯 C++ 内部 parser 和完整 base/Layout/SP/BP/MSP external representation 的读写 API；保持 Rust snake_case、`inner`、orientation/rotation 语义，拒绝旧 orientation 字段和未知强约束字段，并通过 `swim.json`、`baldacci1.json` 代表性资产回归。
- Task 17 SVG 调试导出基线已落地：新增只读 `exportLayoutSvg`，输出容器/Item 原始与碰撞轮廓、外部变换、ID、坐标轴和可选 Surrogate；生成结果已通过 XML DOM well-formed 验证，Layout Snapshot 前后保持一致。
- Task 18 差分 oracle 基线已落地：新增 Rust-compatible 固定 fixture、geometry/problem/matrix/trajectory/optimizer comparator，并纳入 JaguarCore Debug/Release 测试；覆盖基础几何、旋转/浮点边界、offset/simplify/narrow concavity、边相交/包含/相切、PlacedItem/Exterior/Hole/质量区/Surrogate、Layout 快照恢复、共享 seed 轨迹以及 SPP/BPP/MSPP 的 demand、stock、width/max-width/density/cost 指标。Rust workspace 使用 pinned `1.90.0` 离线全量回归通过，另有 LBF seed=42 指标 fixture。
- Task 19 已完成：在批量查询、固定内部线程池、同引擎并发提交串行化和 `PolygonSoA` 缓存基础上，新增 Scalar/Auto/Avx2 执行模式。AVX2 只用于 SoA 边 bbox 粗筛，候选命中后仍回到标量 `Edge2d::intersects()`；运行时检查 CPU/OS AVX2，未编译 AVX2 或硬件不支持时安全回退到 Scalar。qmake 使用独立 `AVX2_SOURCES`，只有 `soa_geometry_avx2.cpp` 使用 `/arch:AVX2`，不改变公共 `SimplePolygon` ABI；`CONFIG+=jaguarcore_no_avx2` 提供纯标量构建。Debug/Release、AVX2/Scalar 四种配置的 JaguarCoreTests 与 JaguarCoreBench `all` 均真实运行并退出码 `0`；brute-force/CDE/SoA 命中结果保持一致，测量已记录于 `docs/performance.md`。
- JaguarCore Debug/Release DLL 与测试均通过真实 MSVC2019 x64 重建；两种配置的测试程序退出码均为 `0`。

### 恢复执行记录（2026-09-17）

- 从异常中断点恢复后，重新运行四套 JaguarCoreTests（Debug/Release、AVX2/Scalar），四套最终退出码均为 `0`；Release 测试使用 `/UNDEBUG` 与强制包含 `test_release_asserts.h`，因此 Release 一致性断言仍实际执行。
- 重新运行四套 JaguarCoreBench `all`，四套退出码均为 `0`。最新一致性结果为 `brute_hits=1271`、`quadtree_hits=1271`、`surrogate_hits=792`、`soa_hits=32500`，Geometry/Layout checksum 分别保持 `911195373.202` 和 `576000.000`；最新耗时已写入 `docs/performance.md`。
- 补充 MSPP 快照负向验证：拒绝条带容器宽高不一致、`maxWidth/fixedHeight` 与实例基准不一致、CDE/Surrogate/ShapeModify 配置不一致；恢复失败保持原状态不变。SPP 同步拒绝 CDE/Surrogate/ShapeModify 配置篡改，并在统一 validator 中执行同样比较。
- 补充 MSPP JSON 兼容负向验证：拒绝外部容器高度不匹配、非零条带原点、重复容器 ID、多个 layout 重复引用同一容器 ID；错误均在绑定/快照层返回 `SnapshotMismatch`，不再因后续 demand 错误掩盖输入关系错误。
- 通过 Rust `1.90.0` 离线 oracle 复核复杂凹多边形：jagua-rs/geo-buffer 对内缩分裂场景会产生多个结果并取第一个外轮廓；该行为已作为 C++ straight-skeleton 差分目标锁定。
- 随后已补入 JaguarCore 私有 C++ straight-skeleton rounded offset 实现，按 Apache-2.0 geo-buffer 的事件模型重实现 shrink/split event、队列分裂和 rounded loop，并保持公共 API 不变；Rust 分裂样例已锁定为三角形、面积约 `1.419689416885` 及固定顶点回归。四套配置均通过该差分。
- straight-skeleton 接入后，四套 JaguarCoreTests 和四套 JaguarCoreBench 均重新构建并使用对应最新 DLL 执行；四套测试/基准退出码均为 `0`，最终 benchmark 命中仍为 `1271/1271/792/32500`，checksum 仍为 `911195373.202/576000.000`，数据已刷新到 `docs/performance.md`。
- 质量语义按 Rust `HazKeyFilter::from_irrelevant_qzones` 校正：`quality < min_quality` 的低等级区域被过滤，等于要求值的区域仍碰撞；新增 `None/0/1/2/3` × 质量 1/质量 2/Hole 矩阵和 Surrogate 过滤差分，四套配置继续通过。
- CDE Snapshot 新增动态实体类型约束：只有 `PlacedItem` 可动态注册/恢复；伪造动态 Exterior/Hole/质量区会返回 `SnapshotMismatch`，失败后原碰撞状态仍保持。
- SPP/BPP/MSPP 新增导出 JSON 后重新解析、再 Importer 恢复的 round-trip，验证布局数量、需求/库存、成本、条带宽度和密度。
- Surrogate 新增 L/C 两类凹轮廓 Rust Debug/Release fixture，验证 Pole 数量、fail-fast 前缀、Pier 数量、凸包面积和关键坐标。
- SVG 通过本机 `http://127.0.0.1` 只读预览完成浏览器实际渲染验收；DOM 标题/Item 节点和截图均可见。CairoSVG/Pillow 四套独立矢量渲染也已通过；Inkscape/rsvg 专用工具未安装。
- 新增 `offsetShapes()` 返回 straight-skeleton 的全部外部组件；凹形分裂样例验证返回 2 个组件，过度 Deflate 验证返回成功的空 vector；旧 `offsetShape()` 继续返回首组件。
- `offsetShapes()` 接入后四套 JaguarCoreTests/JaguarCoreBench 均重新构建并执行，四套退出码均为 `0`，最新 benchmark 命中和 checksum 保持一致。
- 2026-09-18 补充 `offsetShapes()` Rust oracle 的逐组件断言：两个三角形组件分别校验顶点数、面积和完整包围盒（`x_min/y_min/x_max/y_max`）；同时补充 Container 孔洞 Inflate 半间距与外轮廓 Deflate 半间距的 CDE 可观察行为回归。
- 质量区过滤已按 Rust 源码固定为 `quality < min_quality` 忽略、`quality >= min_quality` 碰撞；新增完整 `None/0/1/2/3` × 质量 1/质量 2/Hole 矩阵和 Surrogate 过滤差分。
- CDE restore/register 已拒绝非 `PlacedItem` 动态 Hazard，重复 `HazardEntity` 的多个动态槽位在 snapshot 保存/恢复后仍保持两个独立实例。
- `containment_matrix.tsv` 已接入 Rust `lbf` workspace 的真实 CDE 测试，9 个 Hole/Exterior case（含角点接触）与 C++ comparator 共用同一 fixture 并全部通过。
- Rust workspace 全量回归现为 27 个测试通过、0 失败；其中 `differential_matrix.rs` 的 Edge 与 Containment 两个测试均通过。
- `containment_matrix.tsv` 的 Rust 新增测试已使用固定 Rust 1.90.0 rustfmt 整理；全量 workspace 测试仍为 27/27 通过，仓库中未相关的既有 bench/trajectory 格式差异未被改动。
- SPP/BPP/MSPP 已补充导出 JSON -> 解析 -> Importer 恢复 round-trip；比较布局数量、需求/库存、Bin 成本、条带宽度和密度。
- Surrogate 已新增 L/C 两类凹轮廓 Rust Debug/Release fixtures，验证 Pole、fail-fast Pole、Pier、凸包面积和关键坐标。
- 2026-09-18 恢复执行完成：补充孔洞/外轮廓最小间距 offset 的 CDE 回归；Debug/Release × AVX2/Scalar 四套 JaguarCoreTests 均退出码 `0`，四套 JaguarCoreBench `all` 均退出码 `0`，命中保持 `1271/1271/792/32500`，checksum 保持 `911195373.202/576000.000`，Scalar 的 AVX2 支持位为 `0`。
- 2026-09-18 重新运行 Rust `1.90.0` workspace 离线测试，27 个测试通过、0 失败；重新运行 Rust offset/Surrogate oracle，分裂 offset 两个组件的面积和完整 bbox 与 C++ 逐项断言一致。
- 2026-09-18 四套测试生成的 SVG 均通过 XML DOM 校验：root/namespace/viewBox 正常，6 个 path、6 个 group、2 个 title；最新 Debug 输出又通过 localhost 浏览器实际渲染，AX/截图均可识别并显示容器、Item、Surrogate 和坐标轴；当前机器仍未安装 Inkscape/rsvg 等矢量工具。
- 2026-09-18 扩展共享 `edge_matrix.tsv` 到 9 个案例，加入 `5e-10` 小间隙与 `5e-7` 明显间隙；Rust differential test 与 C++ 四套测试均通过。
- 2026-09-18 新增 L 形 Deflate straight-skeleton oracle：Rust 返回 1 个 22 顶点组件，面积约 `14.263492548395`、bbox 为 `0.25,0.25,5.75,5.75`。发现并修复 `offsetShapes()` 在 skeleton 空结果时未复用已有 rounded fallback 的问题；过度 Deflate fallback 退化时仍保持成功空 vector。修正后的四套测试/基准重新通过。
- 2026-09-18 最终收紧 Inflate fallback 失败语义：不再把失败静默转换为空 vector，而是返回 `OffsetFailed`；Deflate 退化空结果语义保持不变。该版本四套 Debug/Release、AVX2/Scalar 测试和基准再次全部退出码 `0`。
- 2026-09-18 扩展 CDE 随机一致性验证：固定种子生成 64 个动态 Hazard 和 256 个查询，比较深四叉树与不细分索引，并验证初始快照恢复、删除 22 个 Hazard 后的缩减快照恢复；恢复后的新引擎使用新句柄，不错误复用旧句柄。四套 C++ 配置全部通过。
- 2026-09-18 将共享 `containment_matrix.tsv` 扩展到 9 个案例，新增 Hole 与查询轮廓的角点接触；Rust `differential_matrix` 和 C++ 四套配置均通过。
- 2026-09-18 新增固定种子几何/Surrogate 随机不变量：8 个 6--12 顶点极角排序轮廓，逐个验证 Inflate/Deflate 包含关系、Deflate 非空组件以及 Pole/Pier 端点内含；C++ 四套配置均通过。
- 2026-09-18 将 SPP/BPP/MSPP 随机状态 checkpoint 接入完整 JSON 闭环；每个模型定期执行 external serialize、JSON parse、Importer 恢复和 validator。发现并修复 MSPP Importer 错误拒绝合法空 Layout 的问题；BPP 空 Bin layout 仍按模型语义保持拒绝。
- 2026-09-18 扩展 Layout 随机场景：64 个内缩网格放置位置、512 步放置/移除/快照恢复，每 64 步与全量新 Layout 比较数量、密度、可行性和统一 validator；四套 C++ 配置均通过。
- 2026-09-18 增加 CDE/QuadTree generation 回归：槽位注销后复用产生新句柄，旧句柄无法注销新 Hazard，且 QuadTree 不返回旧句柄；四套 C++ 配置均通过。
- 2026-09-18 SVG 外部验收新增 CairoSVG/Pillow 渲染证据：四套最新 SVG 均生成 `110×110` 非空 PNG，bbox 一致；浏览器、XML DOM 和独立矢量渲染三条证据均通过，Inkscape/rsvg 专用工具仍未安装。

### 当前边界与后续项

- 偏移算法已加入私有 straight-skeleton rounded offset；新增 `offsetShapes()` 返回全部外部组件，`offsetShape()` 保持 jagua-rs 首组件兼容行为。凹形分裂、L 形 Deflate 和空结果已有逐组件回归；skeleton 空结果会复用已有 rounded fallback，Container 孔洞作为 quality=0 独立区域的半间距 Inflate 语义也已通过 CDE 验证。多环 `SimplePolygon` 输入不属于 jagua-rs 的 `SPolygon` 公共语义，保留为明确不支持边界；更多事件 fixture 可作为后续增强。
- Pole 数量阈值已修正为覆盖率驱动；Pole 已对齐 Rust FIFO BFS 和四象限顺序；Pier 已实现 Rust 变换顺序的平移射线、轮廓裁剪、`0.999` 内缩、Pole/已有 Pier 距离损失和迭代选择；Surrogate 已支持同配置缓存、配置变化重建和刚体变换继承。U 形 Debug/Release 固定 Rust fixture 已通过四配置差分。
- 四叉树当前采用完整重建保证注册/注销一致性；已补充 virtual-root 直接查询、查询矩形二次 presence 筛选、Polygon/Surrogate Collector 回调、稳定句柄级去重和 generation/槽位复用回归。局部 edge-presence 增量优化保留为可选性能后续项，不影响当前 correctness 验收。
- CDE Snapshot restore 已改为临时 Slot/QuadTree 全部构建成功后一次性提交，非法快照不会破坏现有状态；动态实体类型、质量区全矩阵、角点接触和固定种子的大规模树/平坦索引差分已覆盖。更多相切/小间隙和复杂多区域案例属于可选扩展。
- Layout 已完成确定性 1000 步随机放置/移除/恢复和独立重建校验，并新增 64 位置/512 步的大规模网格场景；更完整的对外迭代 API属于后续扩展。
- Importer 已提供 Item/Container 和 SPP/BPP/MSPP instance/solution 的 JSON 一站式入口；codec 负责 external structs 读写，几何合法性和问题模型不变量仍由 Importer/Problem 层负责。
- SPP/BPP/MSPP 已补充 JSON external instance/solution 与 C++ 问题模型的一站式导入/导出闭环；导入通过真实放置流程校验 item ID、需求、库存、Bin/Container ID 和变换。无间距 optional offset 语义、正间距 half-offset 分支、每模型 1000 步随机恢复矩阵和随机 JSON checkpoint 均已锁定。
- Task 15 已补充每模型 1000 步随机问题序列、CDE 边界矩阵和跨模块快照校验；更多错误注入场景属于可选扩展。
- 浏览器和 CairoSVG/Pillow 的 SVG 外部打开/渲染验收已完成；Inkscape/rsvg 专用工具未安装，部署验收属于 Task21；JSON/SVG 的 C++ Debug/Release 回归已执行。
- 差分 oracle 已新增 U 形 Pole/Pier Debug/Release fixture、凹多边形 straight-skeleton 分裂 fixture 和固定种子随机几何/Surrogate 不变量；当前不等同于 Sparrow 优化器，因为 Sparrow 优化器明确不在本计划内。
- 本轮新增的质量/CDE 动态实体矩阵、Surrogate L/C fixture、SPP/BPP/MSPP JSON round-trip、随机 JSON checkpoint 和 Layout 大规模场景均已完成；Task20/Task21 仍未执行。
- 本轮最终四套 JaguarCoreTests 和四套 JaguarCoreBench 均加载最新 DLL 重新执行并退出码 `0`；bench 命中仍为 `1271/1271/792/32500`，checksum 仍为 `911195373.202/576000.000`。SVG XML 解析显示根元素/命名空间/viewBox 正常，浏览器截图验收已记录。
