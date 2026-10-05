# 08. geo_enums.rs + geo_traits.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/geo_enums.rs`（29 行）+ `CodeResource/jagua-rs/jagua-rs/src/geometry/geo_traits.rs`（57 行）
**C++**: 分散对应——`JaguarCore/Include/JaguarCore/Geometry/rect.h`（RectRelation/GeoPosition 枚举）、`JaguarCore/Include/JaguarCore/entities/item.h`（RotationRange）、各形状头文件中的方法命名约定
**判定**: 基本等价（枚举值域与语义一致；差异在 RotationRange 的建模方式（enum+Vec vs mode+radians）、trait 家族的分散化、命名约定映射表）

## 方法逐条比对（geo_enums.rs）

### GeoPosition（geo_enums.rs:1-5）
- C++ 对应：`enum class GeoPosition { Interior, Exterior }`（rect.h:28-32）。
- 判定：【一致】
- 证据与说明：Rust geo_enums.rs:2-5 两值 Exterior/Interior；C++ rect.h:29-31 两值 Interior/Exterior（声明顺序不同，无语义影响）。C++ 另定义 `using SeparationDistance2d = std::pair<GeoPosition, double>`（rect.h:34）对应 Rust 的 `(GeoPosition, f32)` 元组返回。值域与语义一致。

### GeoRelation（geo_enums.rs:7-19）
- C++ 对应：`enum class RectRelation`（rect.h:20-26）。
- 判定：【差异】（命名与作用域：Rust 是所有几何体通用的关系枚举，C++ 命名为 RectRelation 且仅用于矩形对）
- 证据与说明：Rust geo_enums.rs:10-19 四值：Intersecting（A∩B≠∅ 且互不包含）、Enclosed（A⊆B）、Surrounding（B⊆A）、Disjoint（A∩B=∅）。C++ rect.h:21-25 四值 Disjoint/Intersecting/Enclosed/Surrounding（注释 rect.h:22-25：不相交/相交/被另一个矩形包含/包含另一个矩形）。**值域与方向语义逐一对应**（Enclosed = A 被 B 含，Surrounding = A 含 B——rect.h:374-379 的判断 `xMin <= other.xMin && ... xMax >= other.xMax → Surrounding` 与 rect.rs:51-57 同向，确认两侧"以 self 为 A"的语义一致）。差异仅在：C++ 名字带 Rect 前缀暗示只服务矩形（实际 Rust 侧该枚举也只被 Rect::relation_to/almost_relation_to 使用——作用域事实一致），**语义无差**。判定为【一致】（附命名说明）。

### RotationRange（geo_enums.rs:21-29）
- C++ 对应：`RotationMode` enum + `RotationRange` struct（item.h:21-33）。
- 判定：【差异】（建模方式：Rust 单枚举含载荷 `Discrete(Vec<f32>)`；C++ 平铺为 enum mode + vector<double> 成员）
- 证据与说明：Rust geo_enums.rs:22-29：`None | Continuous | Discrete(Vec<f32>)` 三态枚举（带类型安全载荷）。C++ item.h:21-26 `enum class RotationMode { None, Discrete, Continuous }` + item.h:29-33 `struct RotationRange { RotationMode mode; std::vector<double> radians; }`——mode 与 radians 可出现**非法组合**（mode=None 时 radians 非空、mode=Continuous 时 radians 非空、mode=Discrete 时 radians 空——Rust 类型系统排除这些状态，C++ 靠约定/校验）。 importer（io/importer.h:93 `Result<RotationRange> ImportRotation`）在构造侧把关。值域一致（None/Continuous/Discrete 角度列表）、载荷类型 f32 → double（全库统一策略）。判【差异】（类型安全性弱化，值域等价）。另注意：Rust 侧 RotationRange 定义在 geometry/geo_enums.rs 但消费在 entities 层；C++ 直接定义在 entities/item.h——**摆放位置不同，不影响行为**。

## 方法逐条比对（geo_traits.rs）

### trait CollidesWith<T>（geo_traits.rs:4-7）
- C++ 对应：各形状的 `Intersects*` / `Contains*` 方法族（circle.h:38-46、edge.h:48-58、SimplePolygon.h:63-75）。
- 判定：【差异】（trait 泛型 → 方法重载；命名映射：collides_with → Intersects/Contains 按语义拆分）
- 证据与说明：Rust 统一 `collides_with(&self, other: &T) -> bool`。C++ 按惯例拆分：对 Point 用 `Contains`（圆含点 circle.h:40、多边形含点 SimplePolygon.h:64-68、矩形含点 rect.h:321），对形状间用 `Intersects`（circle.h:42-46、edge.h:49-58）。命名约定核对（Rust → C++）：
  - `Rect.collides_with(Rect)` → `Rect2d::IntersectsRustF32`（rect.h:353）
  - `Rect.collides_with(Point)` → `Rect2d::ContainsRustF32`（rect.h:326）
  - `Rect.collides_with(Edge)` → `Edge2d::IntersectsRustF32(Rect)`（edge.h:58，**宿主反转**：Rust 在 Rect 上定义，C++ 挂在 Edge 上）
  - `Circle.collides_with(Circle/Edge/Rect/Point)` → `Circle2d::IntersectsRustF32(Circle2d/Edge2d/Rect2d)` + `ContainsRustF32(Point)`（circle.h:40-46）
  - `Edge.collides_with(Edge/Rect)` → `Edge2d::IntersectsRustF32(Edge2d/Rect2d)`（edge.h:53, 58）
  - `SPolygon.collides_with(Point)` → `SimplePolygon::ContainsRustF32`（SimplePolygon.h:68）
  泛型分发退化为重载+宿主选择，**逐对语义等价**（数值比对见 02-05 各记录）。判【差异】（形态；行为等价）。

### trait AlmostCollidesWith<T>（geo_traits.rs:8-16）
- C++ 对应：`detail::AlmostEqualRustF32` 系比较谓词（RustF32.h:49-79）+ 少量显式方法（`AlmostContainsRustF32` rect.h:336、`AlmostRelationToRustF32` rect.h:413）。
- 判定：【差异】（trait 未成族；Rust 的 almost 语义按用例内联）
- 证据与说明：Rust geo_traits.rs:14-16 定义"误差偏 false positive"的几乎碰撞；实现仅两处（rect.rs:290-296、rect.rs:306-315）。C++ 无 AlmostIntersects 家族，仅有 `AlmostContainsRustF32`（rect.h:336-344，对应 Rect-Point）与 `AlmostRelationToRustF32`（内联了 Rect-Rect 的 almost_collides）。FPA 语义本体（epsilon + 4 ULP 偏序）由 RustF32.h:49-79 的谓词族承担，与 float-cmp 的 F32Margin 默认（epsilon=f32::EPSILON, ulps=4，float-cmp-0.10.0/src/eq.rs:69-76）**一致**——位映射公式（RustF32.h:40-47 的 `~bits+1 / bits|0x80000000` ↔ float-cmp ulps.rs:8-16 的 `!bits / bits^SIGN_BIT`）核对：负数 `~bits`（按位取反）与 Rust 的 `!bits` 相同，再 +1（RustF32）对应 wrapping_sub 语义下的有序差；正数 `bits | 0x80000000` ↔ `bits ^ SIGN_BIT`（正数符号位为 0，两者等价）。**ULP 差计算等价**。判【差异】（覆盖面：仅 Rect 两对实现有对应，无统一 trait）。

### trait DistanceTo<T>（geo_traits.rs:18-25）
- C++ 对应：`DistanceToRustF32` / `SquaredDistanceToRustF32` 方法族（point.h:25-36、rect.h:207-243、circle.h:47-55、edge.h:45-47、SimplePolygon.h:78-82）。
- 判定：【一致】
- 证据与说明：Rust 成对方法 `distance_to`/`sq_distance_to`（geo_traits.rs:21-24）↔ C++ `DistanceToRustF32`/`SquaredDistanceToRustF32` 成对出现于全部五个形状头文件，签名与返回（double 承载 f32 值）统一。数值等价性见 01-05 各记录逐方法比对。

### trait SeparationDistance<T>（geo_traits.rs:27-35）
- C++ 对应：`SeparationDistanceRustF32` / `SquaredSeparationDistanceRustF32` 方法族（rect.h:245-269、circle.h:56-67、SimplePolygon.h:83-86）+ `SeparationDistance2d` 别名（rect.h:34）。
- 判定：【一致】
- 证据与说明：Rust `separation_distance`/`sq_separation_distance` 返回 `(GeoPosition, f32)`（geo_traits.rs:31-34，且要求 `DistanceTo<T>` 超约束）；C++ 返回 `std::pair<GeoPosition, double>`，三形状全覆盖。**注意超约束差异**：Rust 的 SeparationDistance: DistanceTo（geo_traits.rs:28）——拥有 separation 的类型必有 distance；C++ 无此静态约束但事实上三个实现方也都实现了 DistanceTo（行为等价，无类型层强制）。

### trait Transformable（geo_traits.rs:37-49）
- C++ 对应：各形状的 `TransformRustF32`（就地：edge.h:38、circle.h:36）/ `TransformedRustF32`（返回新对象：SimplePolygon.h:51）+ Transform2d::ApplyRustF32 组合（point 经 ApplyRustF32 使用）。
- 判定：【差异】（无统一接口；就地/复制语义按形状分裂）
- 证据与说明：Rust geo_traits.rs:38-41：`transform(&mut self, t)` 就地修改 + `transform_clone`（geo_traits.rs:43-48，默认实现 clone+transform）。C++：Edge/Circle 提供 `TransformRustF32(void)`（就地，Edge.cpp:225-230、Circle.cpp:115-119），SimplePolygon 只提供 `TransformedRustF32`（返回新对象，SimplePolygon.cpp:338-374），Point 无成员方法（经 `Transform2d::ApplyRustF32(point)` 自由函数形态）。三种形态并存，无统一抽象；`transform_clone` 的便捷无直接对应（调用方 clone 后 transform 即可）。判【差异】（形态分裂；数值路径等价，见 01/03/04/05 各记录）。

### trait TransformableFrom: Transformable（geo_traits.rs:51-57）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：`transform_from(&mut self, reference, t)`（geo_traits.rs:56）在 Point/Edge/Circle/SPolygon 四处实现（point.rs:18-24、edge.rs:150-158、circle.rs:90-96、simple_polygon.rs:250-277），Rust 侧是热路径内存优化（反复变换同一对象免重分配）。C++ 完全无此概念——由"值语义 + 编译器拷贝消除"承担同等性能目标。数值路径等价（reference 的坐标经同一 ApplyRustF32 公式）。验证背景陈述"缺 TransformableFrom"成立。

### 命名约定总表（Rust → C++ 映射）
- 判定：【一致】（系统性映射成立）
- 证据与说明：
  | Rust | C++ 约定 | 例证 |
  |---|---|---|
  | snake_case 方法 | PascalCase | collides_with → Intersects |
  | `collides_with(Point)` | `Contains(Point)` | circle.rs:133 → circle.h:40 |
  | `sq_distance_to` | `SquaredDistanceTo` | rect.rs:371 → rect.h:207 |
  | `sq_separation_distance` | `SquaredSeparationDistance` | rect.rs:396 → rect.h:245 |
  | `f32` 参数/返回 | `double` 存储 + `*RustF32` 后缀标记 f32 语义 | rect.rs:47 relation_to → rect.h:383 RelationToRustF32 |
  | Result/Option/panic | Result<T>/optional/错误码（panic 类已改为 Result：scale、bounding_circle；保留 expect 的：surrogate()） | rect.rs:120 vs rect.h:147-159 |
  | trait 泛型分发 | 方法重载 + 宿主选择 | geo_traits.rs:5 → circle.h:42-46 |
  | 无后缀（Rust 无此区分） | double 无后缀族 = C++ 自有 API | rect.h:347 Intersects vs rect.h:353 IntersectsRustF32 |

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | GeoRelation → RectRelation（命名与名义作用域） | 差异（命名） | 值域与方向语义一致；C++ 名称暗示仅矩形使用（与 Rust 实际使用范围一致） | geo_enums.rs:10-19 vs rect.h:20-26 |
| 2 | RotationRange 建模：带载荷枚举 vs mode+radians 平铺 | 差异 | C++ 可表达非法组合（None 带角度等），类型安全弱化；值域等价 | geo_enums.rs:22-29 vs item.h:21-33 |
| 3 | RotationRange 定义位置（geometry 层 vs entities 层） | 差异（摆放） | 无行为影响 | geo_enums.rs:21 vs item.h:29 |
| 4 | trait CollidesWith 泛型分发 → Intersects/Contains 重载族 | 差异（形态） | 逐对语义等价；Rect-Edge 的宿主从 Rect 反转为 Edge | geo_traits.rs:4-7 vs edge.h:58 |
| 5 | trait AlmostCollidesWith 未成族 | 差异 | 仅 Rect 两对（Rect-Rect/Rect-Point）有对应；谓词本体（AlmostEqual/LessEqual/Greater/LessRustF32）与 float-cmp 逐位等价 | geo_traits.rs:8-16 vs RustF32.h:49-79 |
| 6 | trait Transformable 无统一接口、就地/复制分裂 | 差异（形态） | Edge/Circle 就地、SimplePolygon 返回新值、Point 自由函数；transform_clone 无对应 | geo_traits.rs:37-49 vs edge.h:38 / SimplePolygon.h:51 / transformation.h:35 |
| 7 | trait TransformableFrom | C++缺失 | 四形状的 reference-变换优化路径无对应（数值等价已覆盖） | geo_traits.rs:51-57 |
| 8 | SeparationDistance: DistanceTo 超约束 | 一致（无类型强制，事实满足） | C++ 无静态约束但实现方均同时有 DistanceTo | geo_traits.rs:28 vs rect.h/circle.h/SimplePolygon.h |
| 9 | FPA 偏序 ↔ AlmostEqualRustF32 谓词族 | 一致 | epsilon + 4 ULP + 位映射公式逐位对应 | fpa.rs:18-32 + float-cmp eq.rs:69-76 vs RustF32.h:40-69 |

## 结论

枚举与 trait 这层"契约层"在 C++ 中没有直接的结构对应物，而是被翻译成了命名约定：GeoPosition/GeoRelation 值域一致（仅 Rect 前缀命名与 RotationRange 平铺建模弱化了类型安全），五个 trait 家族（碰撞/几乎碰撞/距离/分离距离/可变换）逐方法映射到各形状的 `*RustF32` 方法族，其中 FPA 偏序的谓词本体与 float-cmp 的默认 margin（epsilon + 4 ULP）做到位级一致。结构性缺口是 AlmostCollidesWith 未成族与 TransformableFrom 整体缺失，两者分别影响"容差碰撞"的可调用范围（仅 Rect 两对）与热路径内存优化形态，均不改变已实现路径的数值行为。

## 补充审计（2026-09-29）

逐行复核 geo_enums.rs 全部 29 行（3 个枚举 + 全部 derive）与 geo_traits.rs 全部 57 行（6 个 trait + transform_clone 默认实现）。确认主记录已覆盖全部类型。以下为补充核对项：

### 【遗漏→补齐】Transformable::transform_clone 默认实现（geo_traits.rs:43-48）
- 主记录 Transformable 条提到"transform_clone（默认实现 clone+transform）C++ 无直接对应"。补充确认其机制：`clone.transform(t)` 返回 clone——对 SPolygon 而言 = clone（含全部派生缓存）+ transform（重算 bbox、保留 area/diameter）。C++ 侧对应物为 `SimplePolygon::TransformedRustF32`（返回新对象、**不复制/不重算 POI 半径与 surrogate 转发**）——但注意一个真实差异：Rust transform_clone 对 **surrogate** 做逐元素 transform（simple_polygon.rs:239-241）；C++ TransformedRustF32 同（SimplePolygon.cpp:363-372）✓；**POI**：Rust `poi.transform(t)`（圆心变换）；C++ 同（cpp:359-362）✓。主记录 05 已覆盖，此处确认 transform_clone 作为 trait 默认方法在 C++ 无统一入口（各形状按需 Transformed/TransformedRustF32/Transform 自行承担）。
- 判定：【差异（形态）】（维持主记录判定；机制细节已确认无数值缝隙）。
- 证据：geo_traits.rs:43-48；simple_polygon.rs:232-244 vs SimplePolygon.cpp:338-374。

### 【遗漏→补齐】GeoPosition 的 derive 集合（Eq+Hash+Copy）与 C++ 枚举的使用面
- geo_enums.rs:1 `#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]`——GeoPosition 可哈希（作为 HashMap 键）。C++ `enum class GeoPosition`（rect.h:28-32）天然可哈希（枚举值）。全库检索：Rust 侧 GeoPosition 无哈希容器消费（仅 match/比较），两侧一致无需求缺口。
- GeoRelation：`#[derive(Debug, PartialEq)]`（无 Clone/Copy/Eq/Hash）；C++ RectRelation 为普通 enum class（隐式可拷贝）。Rust 无 Copy 导致值语义按引用传递——纯形态差异，无行为影响。
- 判定：【一致】。
- 证据：geo_enums.rs:1, 7 vs rect.h:20-32。

### 【浅覆盖→补齐】RotationRange 的 derive 集合（Clone, Debug, PartialEq——无 Eq/Hash/Serialize）
- geo_enums.rs:21 `#[derive(Clone, Debug, PartialEq)]`——注意 **无 Serialize/Deserialize**（serde 在 ShapeModifyConfig 上有，这里没有）；无 Eq/Hash。C++ item.h:21-33 平铺 struct + operator==（记录 23 范围）。PartialEq 含 `Discrete(Vec<f32>)` 的逐元素 f32 == ——C++ 需在 item.h/operator== 提供向量比较（见 23 号记录，不在本层）。f32 逐元素 == 语义两侧同（NaN 不等、±0.0 相等）。
- 判定：【一致】（契约层面；字段级比较的 C++ 落点在 23 号记录核实）。
- 证据：geo_enums.rs:21-29 vs item.h:21-33。

### 【遗漏→补齐】cfg(test)——两文件均无测试
- geo_enums.rs / geo_traits.rs 均无 `#[cfg(test)]`。判定：不适用。

补充结论：08 记录类型级覆盖完整。三处补充均为契约细节确认（transform_clone 机制、两个枚举的 derive 集合与 C++ 天然能力对齐），无新缝隙。
