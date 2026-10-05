# 01. point.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/primitives/point.rs`（87 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/point.h`（50 行）
**判定**: 基本等价（RustF32 族行为一致；C++ 缺失 Hash/位级相等语义与 point-collides 委托，另有 double 无后缀族与 IsFinite 为 C++ 额外）

背景约定（适用于全部 8 份记录）：Rust 全部运算在 f32 上进行；C++ 用 `double` 存储，`*RustF32` 后缀方法把每个操作数先 `static_cast<float>`，在 f32 域完成运算后再转回 double 存储（见 `JaguarCore/Include/JaguarCore/Geometry/rect.h:4` 唯一显式包含 RustF32.h 及各 cpp 中 `rustF32()` 辅助函数，如 `SimplePolygon.cpp:17`）。因此本记录默认：`*RustF32` 方法中"每次算术一个 f32 舍入"的模式与 Rust 等价，只有出现求值顺序、NaN/±0 语义、API 形态差异时才单列。

## 方法逐条比对

### Point 结构定义与访问器（point.rs:7-8, 33-43）
- C++ 对应：`Point2d`（point.h:8-11），成员 `x`/`y` 公有可直接访问。
- 判定：【一致】（访问器形态差异不构成行为差异：Rust `x()`/`y()`（point.rs:35-42）返回 `f32`，C++ 直接读 `double` 成员，无 `X()`/`Y()` 方法但语义等同）
- 证据与说明：point.rs:8 `pub struct Point(pub f32, pub f32)`；point.h:10-11 `double x = 0.0; double y = 0.0;`。C++ 默认零初始化，Rust 无默认值，属构造语义差异，不影响比对方法。

### Transformable::transform（point.rs:10-16）
- C++ 对应：`Transform2d::ApplyRustF32(point)`（Transformation.cpp:23-36），由使用方（如 `Edge2d::TransformRustF32` Edge.cpp:225-230、`SimplePolygon::TransformedRustF32` SimplePolygon.cpp:348-349）对每个点调用。
- 判定：【一致】
- 证据与说明：point.rs:26-30 `TRANSFORM_FORMULA` = `m[0][0]*x + m[0][1]*y + m[0][2]*1.0` / `m[1][0]*x + m[1][1]*y + m[1][2]*1.0`。Transformation.cpp:31-35 `sinValue * x ... cosValue * x - sinValue * y + tx`。矩阵展开后 a=cos、b=-sin、c=tx（Rust 侧矩阵由 rot_transl_m 生成，transformation.rs:153 `[[cos,-sin,h],[sin,cos,k]]`），逐项对应且全部在 f32（C++ 先 `static_cast<float>` 三个输入，Transformation.cpp:26-30）。乘 `1.0` 在 f32 中是精确操作，无舍入差。注意 C++ 未提供原地 `transform(&mut self)` 形态，而是返回新点由调用方赋值——内存语义差异，数值一致。

### TransformableFrom::transform_from（point.rs:18-24）
- C++ 对应：无独立 API；`Transform2d::ApplyRustF32` 配合"先拷贝 reference 再赋值"达到同效（SimplePolygon.cpp:347 `transformedPolygon = *this` 后逐点 `transform.ApplyRustF32(point)`）。
- 判定：【C++缺失】（trait 形态 API；数值路径等价由 ApplyRustF32 承担）
- 证据与说明：point.rs:19-23 以 reference 的坐标为输入写入 self。C++ 无此区分（Rust 该 trait 的存在动机是避免重新分配内存，C++ 侧无此需求）。数值行为等价，缺失的是内存优化语义，不是几何语义。

### DistanceTo::distance_to / sq_distance_to（point.rs:45-55）
- C++ 对应：`Point2d::DistanceToRustF32` / `SquaredDistanceToRustF32`（point.h:25-36）。
- 判定：【一致】
- 证据与说明：point.rs:48 `((self.0-other.0).powi(2) + (self.1-other.1).powi(2)).sqrt()`；point.h:27-29 dx、dy 在 f32 相减，`dx*dx + dy*dy` 在 f32 求和。C++ `SquaredDistanceToRustF32` 返回 double 但值域是精确的 f32 结果（static_cast<double> 不引入舍入）。`DistanceToRustF32`（point.h:32-36）先在 f32 求平方和、再 `sqrt(float)` 后转 double，与 Rust `.sqrt()`（f32 sqrt）一致。

### Eq / Hash（point.rs:57-66）
- C++ 对应：`operator==`（point.h:40-43）。
- 判定：【差异】
- 证据与说明：Rust point.rs:61-64 Hash 用 `to_bits()`（位级：+0.0 ≠ -0.0，NaN 之间按位区分），且 point.rs:7 derive `PartialEq` 也是位级 `==`（Rust f32 的 `==`：+0.0 == -0.0 为 true，NaN != NaN）。`impl Eq`（point.rs:57）叠加 Hash 约束（要求 eq 是自反/传递的等价关系）在 f32 NaN 时技术上不被满足——这是 jagua-rs 接受的既有松动。C++ point.h:42 `lhs.x == rhs.x && lhs.y == rhs.y`：对 double 成员直接 `==`，+0.0 == -0.0 为 true（与 Rust PartialEq 一致），但存储值可能已是 f32 量化后的 double（无 bit 级 Hash 供哈希容器使用）。C++ 完全没有 `to_bits` 哈希等价物——任何依赖 Point 哈希的 Rust 数据结构（如 `UniqueVertices`）在 C++ 中必须另行处理（本层未提供）。NaN 输入下：Rust `==` false 而 Hash 有值；C++ `==` 也 false——判定大体一致，缺的是哈希功能本身。

### From<Point> for (f32,f32) / From<(f32,f32)> for Point（point.rs:68-78）
- C++ 对应：隐式构造/`Point2d{a, b}`。
- 判定：【一致】（语法糖层面等价）
- 证据与说明：Rust 的 From 转换无损；C++ `Point2d{double,double}` 聚合初始化。C++ 无显式"转 tuple" API，但成员公有可直接取。

### CollidesWith<T> for Point（泛型反转委托，point.rs:80-87）
- C++ 对应：无统一反转层；具体判交 API 分散在各形状上（如 `Circle2d::ContainsRustF32(Point2d)` circle.cpp:126、`Rect2d::ContainsRustF32` rect.h:326）。
- 判定：【C++缺失】（trait 层反转委托；具体几何行为已由各形状的对偶方法覆盖）
- 证据与说明：point.rs:84-86 `other.collides_with(self)` 把 Point 对任意形状的碰撞委托给该形状对 Point 的实现。C++ 各形状确实都实现了对 Point 的判断（circle.cpp:126-132、rect.h:326-334、SimplePolygon.cpp:430），语义覆盖等价，缺的是"从 Point 一侧发起"的调用形态。

### （C++ 额外）IsFinite（point.h:14-15）
- Rust 对应：无（Rust 侧对非有限值依赖各 try_new 的校验，point.rs 本身不校验）。
- 判定：【C++额外】
- 证据与说明：point.h:14-15 `std::isfinite(x) && std::isfinite(y)`。在 SimplePolygon::Create（SimplePolygon.cpp:205-208）等处用作前置校验，是 C++ 防御性编程层，Rust 把这类校验放在 `anyhow::ensure!`（如 circle.rs:23-30）。

### （C++ 额外）SquaredDistanceTo（无后缀 double 版，point.h:18-23）
- Rust 对应：无。
- 判定：【C++额外】
- 证据与说明：point.h:20-23 在 double 域计算 dx*dx+dy*dy，供 C++ 无后缀族（如 `Edge2d::ClosestPoint` Edge.cpp:232-244）使用。Rust 无 double 路径。注意 C++ 额外族被标注为 C++ 自有 API（如 Edge.cpp:309-311 注释明确说明 Intersects(double) 与 jagua-rs 语义刻意不同）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | Hash（to_bits 位级哈希） | C++缺失 | 依赖 Point 哈希的容器/去重结构无法在 C++ 层复刻 | point.rs:59-66 vs point.h 全文无 hash |
| 2 | Eq（derive Eq + Hash 的位级语义约定） | 差异 | C++ `==` 只覆盖 PartialEq 语义；±0.0 在 Rust PartialEq 也是相等，此处两点一致，但 Hash 侧 +0/-0 不同 | point.rs:57-66 vs point.h:40-43 |
| 3 | TransformableFrom（transform_from） | C++缺失 | 仅内存优化语义；数值等价已由 ApplyRustF32 覆盖 | point.rs:18-24 vs point.h 无对应 |
| 4 | CollidesWith 泛型反转（point.collides_with(other)） | C++缺失 | 调用形态缺失，几何行为由对偶方法等价覆盖 | point.rs:80-87 vs point.h 无对应 |
| 5 | IsFinite | C++额外 | 防御性校验，无 Rust 对应 | point.h:14-15 |
| 6 | SquaredDistanceTo（double 无后缀族） | C++额外 | C++ 自有 double API 族入口 | point.h:18-23 |

（`transform`/`transform_from` 的数值路径、`distance_to`/`sq_distance_to`、From 转换均为一致，未列入表。）

## 结论

Point 层的数值核心（距离计算、变换应用公式）在 `*RustF32` 族下与 jagua-rs 逐操作等价，包括 f32 域的减法、平方、求和与 sqrt 次序。剩余差异集中在 Rust trait 系统的形态性 API（Hash 位级语义、TransformableFrom、泛型反转委托），这些在 C++ 中没有直接对应物但几何数值行为已被覆盖；另有一组 C++ 自有的 double 无后缀 API 与防御性 IsFinite，属于设计上的双 API 族策略，不构成对 Rust 行为的偏离。

## 补充审计（2026-09-29）

逐行复核 point.rs 全部 87 行（结构、2 个 trait 实现、TRANSFORM_FORMULA、2 个访问器、DistanceTo、Eq/Hash、2 个 From、泛型 CollidesWith），确认主记录条目已覆盖全部函数。以下为补充核对项：

### 【浅覆盖→补齐】Point 的 Hash（to_bits）实际消费链核实
- 主记录判定 C++ 缺失 Hash，并推测"任何依赖 Point 哈希的 Rust 数据结构（如 UniqueVertices）在 C++ 中必须另行处理"。
- 补充核实（全库检索）：jagua-rs 核心中**没有任何结构以 Point 为键做哈希**——`util/assertions.rs` 的 HashSet 键是 `(HazardEntity, u8)`，而 `HazardEntity`（hazard.rs:38-40 derive Eq+Hash）的字段为 `usize / DTransformation / PItemKey`，均不含 Point。geometry 层唯一疑似点 `simple_polygon.rs:45 points.iter().unique()` 经查 itertools 0.13.0 `unique_impl.rs:100-117` 确认其约束是 `Item: Eq + Hash + Clone`，**用的是 HashMap（Hash 路径）而非主记录推测的"Vec+contains（PartialEq 路径）"**——即 Rust 的重复点查重实际走 `to_bits` 哈希。这对 05 记录的"重复点查重语义一致"结论有修正，见 05 补充审计。
- 判定：Hash 缺失本身成立，但**实际影响面比主记录表述更小**：jagua-rs 无任何以 Point 为哈希键的容器（除上述 unique 一处，其行为差异见 05）。
- 证据：point.rs:59-66；itertools-0.13.0/src/unique_impl.rs:107-116（`used.entry(v)` HashMap）；全库 grep 无 `HashSet<Point>`。

### 【浅覆盖→补齐】PartialEq/Eq 的 NaN 语义细节修正
- 主记录"Eq / Hash"条称 `impl Eq`（point.rs:57）"在 f32 NaN 时技术上不被满足——这是 jagua-rs 接受的既有松动"。补充：`Point` 的 PartialEq 是**逐字段 f32 `==`**（derive），NaN != NaN；Eq 标记只是编译期断言"eq 是等价关系"，运行时无额外行为——该松动不影响任何已比对路径的数值行为，两侧（C++ `==` 同为 NaN 不等）在 NaN 判定上一致。
- 判定：【一致】（NaN 语义两侧相同：`==` 均 false）。
- 证据：point.rs:7（derive PartialEq）、point.h:42。

### 【遗漏→补齐】TRANSFORM_FORMULA 常量条目
- point.rs:26-31 的模块级 `const TRANSFORM_FORMULA`（闭包常量）是 Point 两个变换方法的唯一数值来源，主记录只在 transform 条目里间接引用。此处单列确认：公式 `m[0][0]*x + m[0][1]*y + m[0][2]*1.0`（y 同理）与 C++ `Transform2d::ApplyRustF32`（Transformation.cpp:34-35 `cosValue*x - sinValue*y + tx`）在 rot_transl 矩阵展开下逐项对应（m[0][0]=cos, m[0][1]=-sin, m[0][2]=tx）；`* 1.0` 在 f32 中精确无舍入。C++ 侧无独立常量（内联于 ApplyRustF32）。
- 判定：【一致】（数值路径；C++ 缺常量形态但由 ApplyRustF32 内联覆盖，已在记录 06 覆盖）。

### 【遗漏→补齐】point.rs 无 cfg(test) 测试模块——行为契约核实
- 审计要求确认 cfg(test) 锁定的行为契约：point.rs 全文（87 行）**无任何 `#[cfg(test)]` 或 `#[test]`**，geometry 层除 quadtree/tests.rs 外同样无测试模块。因此 Point 层无测试锁定的行为需要 C++ 侧复刻。
- 判定：不适用（无测试契约；C++ tests/TestGeometry.cpp 等的锁定属 C++ 自有验收，不影响等价性判定）。

补充结论：01 记录覆盖完整，无实质遗漏；Hash 影响面表述已修正（更小），itertools unique 的哈希路径发现移交 05 记录处理。
