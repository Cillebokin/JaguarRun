# JaguarCore API v2 迁移说明

本文面向从 API v1 或早期 jagua-rs 风格 C++ 封装迁移到当前 JaguarCore 的调用方。
API v2 的目标是统一公共接口命名、明确错误返回和固定发布边界；它不是 ABI
兼容更新，旧接口名不会继续作为别名导出。

## 版本边界

当前发布契约为：

- C++ API version：`2`，由 `JaguarCoreApiVersion()` 返回；
- public product version：`0.1.0-dev`，由 `JaguarCoreVersion()` 和
  `jaguar::Version()` 返回；
- PE file version：`0.1.0.0`；
- binary target：Windows x64、MSVC2019、C++17。

API version、产品版本和 PE 文件版本相互独立。升级其中一个值时，必须同步更新
发布检查和下游兼容策略，不能把产品版本字符串当作 API 版本。

## 头文件和目录名

当前公共头文件统一使用首字母大写的目录和文件名：

```cpp
#include <JaguarCore/Geometry/SimplePolygon.h>
#include <JaguarCore/IO/JsonCodec.h>
#include <JaguarCore/Problems/Spp.h>
```

不要继续使用旧的 `geometry/simple_polygon.h`、`io/json_codec.h` 或
`problems/spp.h` 路径。Windows 通常不暴露大小写错误，但 Linux 或大小写敏感的
工具链会直接找不到文件。

## 函数命名迁移

公共 C++ 函数和方法统一使用 UpperCamelCase。常见迁移如下：

| API v1 / 旧调用 | API v2 当前调用 |
|---|---|
| `SimplePolygon::create` | `SimplePolygon::Create` |
| `SimplePolygon::transformed` | `SimplePolygon::Transformed` |
| `Rect2d::create` | `Rect2d::Create` |
| `Circle2d::create` | `Circle2d::Create` |
| `Edge2d::create` / `intersects` | `Edge2d::Create` / `Intersects` |
| `Surrogate::create` / `transformed` | `Surrogate::Create` / `Transformed` |
| `Importer::importItemJson` | `Importer::ImportItemJson` |
| `Importer::importContainerJson` | `Importer::ImportContainerJson` |
| `parseExternalItemJson` | `ParseExternalItemJson` |
| `serializeExternalItemJson` | `SerializeExternalItemJson` |
| `externalLayoutFromLayout` | `ExternalLayoutFromLayout` |
| `externalSPSolutionFromSolution` | `ExternalSPSolutionFromSolution` |
| `offsetShape` / `offsetShapes` | `OffsetShape` / `OffsetShapes` |
| `exportLayoutSvg` | `ExportLayoutSvg` |
| `registerHazard` / `deregisterHazard` | `RegisterHazard` / `DeregisterHazard` |
| `CollisionEngine::restore` | `CollisionEngine::Restore` |
| `containsEntity` | `ContainsEntity` |
| `collectPolygonCollisionsUntil` | `CollectPolygonCollisionsUntil` |

Rust 源码或 JSON 字段中的 snake_case 名称仍然保持原样；本表只约束 JaguarCore
公共 C++ 接口。

## `Result<T>` 调用方式

所有可能因输入或内部不变量失败的公共操作都返回 `Result<T>` 或 `Result<void>`。
调用方必须先判断 `HasValue()`，成功后读取 `Value()`，失败时读取或转发
`GetError()`：

```cpp
const auto polygon = jaguar::SimplePolygon::Create(points);
if (!polygon.HasValue())
    return HandleError(polygon.GetError());

const auto transformed = polygon.Value().Transformed(transform);
if (!transformed.HasValue())
    return HandleError(transformed.GetError());
```

当前特别需要迁移的可失败接口包括：

```cpp
Result<Surrogate> Surrogate::Transformed(const Transform2d&) const;
Result<bool> CollisionEngine::DetectSurrogateCollision(
    const Surrogate&, const Transform2d&, const HazardFilter&) const;
Result<std::vector<std::uint8_t>>
CollisionEngine::DetectPolygonCollisionsParallel(
    const std::vector<SimplePolygon>&,
    const HazardFilter&,
    const BatchCollisionOptions&) const;
```

低层 `Transform2d` 的值语义运算（例如 `Apply()`、`Inverse()`、`FollowedBy()`）仍然
直接返回值；只有跨越几何或碰撞公共边界、可能拒绝非法输入的操作才使用 `Result`。

## 线程和所有权迁移

- `CollisionEngine` 的 const 查询可以并发执行；并行查询使用的
  `HazardFilter` 可能在多个线程中同时被调用，捕获状态必须可并发访问。
- `RegisterHazard`、`DeregisterHazard` 和 `Restore` 会修改引擎状态，必须由调用方
  与查询和其他修改操作进行外部串行化。
- `SimplePolygon::GetSurrogate()` 返回借用指针，不得释放，也不能超过所属多边形或
  surrogate 替换操作的生命周期。
- `CollisionEnginePtr` 必须使用导出的 `CollisionEngineDeleter` 释放；不要跨模块
  使用调用方的 `delete`。

## 迁移检查清单

1. 替换 include 路径和所有公共函数名；
2. 将可失败调用改为 `HasValue()` / `Value()` / `GetError()` 三段式处理；
3. 确认 Debug/Release 与 AVX2/Scalar 选择和 DLL 搜索目录一致；
4. 使用包内的 `VerifyPackage.ps1` 验证 API version、PE 版本、CRT 和导出符号；
5. 在与 JaguarCore 相同的 MSVC2019 x64 工具链和 `_ITERATOR_DEBUG_LEVEL` 下重新编译。
