# 04. circle.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/primitives/circle.rs`（233 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/circle.h`（69 行）+ `JaguarCore/Source/Geometry/Primitives/Circle.cpp`（295 行）
**判定**: 基本等价（RustF32 族逐方法复刻成功；差异集中在 Circle-Circle 相切边界的不对称判据、bounding_circle 的半径重算路径、PI 常量位级一致性核对）

## 方法逐条比对

### try_new（circle.rs:22-33）
- C++ 对应：`Circle2d::Create`（Circle.cpp:15-29）。
- 判定：【差异】（校验合并次序与 NaN 半径分支；正常输入一致）
- 证据与说明：Rust circle.rs:23-30 两条 ensure：(1) `radius.is_finite() && radius >= 0.0`；(2) `center.0.is_finite() && center.1.is_finite()`。NaN 半径：`is_finite()` false → 拒绝。C++ Circle.cpp:20-23 合并为一个 if：`!isfinite(centerX) || !isfinite(centerY) || !isfinite(radiusF32) || radiusF32 < 0.0F` → 拒绝。NaN 时 `radiusF32 < 0.0F` 为 false 但 `!isfinite` 为 true → 拒绝。**NaN 与负半径两侧均拒绝，条件覆盖完全相同**（德摩根合并），仅错误码粒度不同（Rust 两条消息 vs C++ 单条"invalid circle"）。+Inf 半径：Rust `is_finite()` false → 拒绝；C++ `!isfinite(radiusF32)` → 拒绝。一致。判【一致】（错误消息粒度差异不构成行为差异）。

### bounding_circle（circle.rs:36-58）
- C++ 对应：`Circle2d::BoundingCircleRustF32`（Circle.cpp:31-81）。
- 判定：【差异】（半径计算路径不同：Rust 用"先extend后length/2"，C++ 复刻了 extend 逻辑但半径求值顺序需逐式核对——实际逐式一致；差异在于空输入的失败通道：Rust panic vs C++ Result）
- 证据与说明：Rust circle.rs:38 `circles.next().expect("no circles provided")`——空集合 panic；C++ Circle.cpp:34-36 返回 `InvalidInput` 错误（Result 包装）。主循环：Rust circle.rs:41-56：距离判断 `bounding.radius < distance_between_centers + circle.radius`（注意距离是 sqrt 后的距离），然后 `Edge{start: bc.center, end: c.center}.extend_at_front(bc.radius).extend_at_back(c.radius)`，再取 `diameter.centroid()`（f32::midpoint）与 `diameter.length()/2.0`。C++ Circle.cpp:42-77：centerDistance = sqrt(dx²+dy²)（f32，Circle.cpp:50）↔ Rust `distance_to`（sqrt 后）；条件 `startRadius < centerDistance + endRadius`（Circle.cpp:51）↔ circle.rs:42；extend_at_front 复刻：`extendedStartX -= dx * (startRadius / edgeLength)`（Circle.cpp:58-60，其中 edgeLength 初值 = centerDistance，即原 edge 的 length——Rust extend_at_front 用的也是原长度 `self.length()`，edge.rs:25）——**一致**（除法先、乘后，同序）；extend_at_back 复刻：C++ Circle.cpp:62-66 重新计算了 dx/dy/edgeLength（延伸后的）再做 `endX += dx * (endRadius / edgeLength)`——Rust extend_at_back 亦用**延伸后**的长度（第二次调用的 self 已被第一次修改，`self.length()` 是新长度）——一致。centroid 用 `MidpointRustF32`（Circle.cpp:69-71）↔ Rust `diameter.centroid()`（edge.rs:124 f32::midpoint）——与 03-edge.md 同一 1 ULP 级差异。半径：C++ Circle.cpp:72-74 `radius = sqrt(dx²+dy²)*0.5F`（dx/dy 为最终两端差）；Rust circle.rs:53 `diameter.length() / 2.0` = `sqrt(dx²+dy²) / 2.0`。`x * 0.5F` 与 `x / 2.0` 在 f32 位级相同（2 的幂）。**一致**。综合：数值路径一致（除 midpoint 1ULP 项已单列），失败通道 panic vs Result 为差异。

### area（circle.rs:61-63）
- C++ 对应：`AreaRustF32`（Circle.cpp:88-92）。
- 判定：【差异】（PI 常量：Rust `std::f32::consts::PI` 与 C++ `3.14159265358979323846F` 需位级核对——两者同为 f32 舍入的 π，位级一致；判一致）
- 证据与说明：Rust circle.rs:12 `use std::f32::consts::PI`，circle.rs:62 `radius * radius * PI`（乘法顺序：r*r 先）。C++ Circle.cpp:12 `constexpr float rustPi = 3.14159265358979323846F`（= f64 π 字面量舍入到 f32，与 Rust `PI` 定义 `3.14159265358979323846264338327950288_f32` 同值同位）；Circle.cpp:91 `radiusF32 * radiusF32 * rustPi` 同序。判【一致】。无后缀 `Area`（Circle.cpp:83-86，`acos(-1.0)*r*r` double）为 C++额外。

### bbox（circle.rs:66-74）
- C++ 对应：`BoundingBoxRustF32`（Circle.cpp:99-108）。
- 判定：【一致】
- 证据与说明：circle.rs:68-73 `x - r, y - r, x + r, y + r`（直接构造，无校验）；Circle.cpp:104-107 同四式 f32。无后缀 `BoundingBox`（Circle.cpp:94-97）为 C++额外。

### diameter（circle.rs:77-79）
- C++ 对应：`DiameterRustF32`（Circle.cpp:110-113）。
- 判定：【一致】
- 证据与说明：circle.rs:78 `radius * 2.0`；Circle.cpp:112 `(float)radius * 2.0F`。同值。

### Transformable::transform（circle.rs:82-88）
- C++ 对应：`TransformRustF32`（Circle.cpp:115-119）。
- 判定：【一致】
- 证据与说明：Rust 只变换 center、半径不动（circle.rs:84-85）；C++ `center = transform.ApplyRustF32(center)`（Circle.cpp:118）。一致。

### TransformableFrom::transform_from（circle.rs:90-96）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：同前：trait 形态缺失，数值覆盖同 transform。

### CollidesWith<Circle>::collides_with（circle.rs:98-110）
- C++ 对应：`IntersectsRustF32(const Circle2d&)`（Circle.cpp:134-144）。
- 判定：【一致】
- 证据与说明：circle.rs:104-108 `sq_d = dx²+dy²; sq_d <= (r1+r2)²`（≤ 含相切）；Circle.cpp:137-143 `dx²+dy² <= radiusSum²`（≤）。逐式一致（含相切判定）。

### CollidesWith<Edge>::collides_with（circle.rs:112-116）
- C++ 对应：`IntersectsRustF32(const Edge2d&)`（Circle.cpp:146-153）。
- 判定：【差异】（多一次 float→double→float 往返；数值可能差 1 ULP）
- 证据与说明：Rust circle.rs:114 `edge.sq_distance_to(&self.center) <= self.radius.powi(2)`——sq_distance_to 返回 f32。C++ Circle.cpp:149-152：`squaredDistance = static_cast<float>(edge.SquaredDistanceToRustF32(center))`——SquaredDistanceToRustF32 返回 double（其内部已是 f32 结果转的 double，Circle.cpp:281 `static_cast<double>(dx*dx+dy*dy)`），再 cast 回 float 是无损的（值本就是 f32 可表示）。因此无额外舍入，与 Rust 一致。修正：【一致】。`radiusF32 * radiusF32` ↔ `powi(2)` 等值。

### CollidesWith<Rect>::collides_with（circle.rs:118-131）
- C++ 对应：`IntersectsRustF32(const Rect2d&)`（Circle.cpp:155-172）。
- 判定：【一致】
- 证据与说明：yal.cc 算法：nearest_x = max(rect.x_min, min(c_x, rect.x_max))（circle.rs:126-127）；Circle.cpp:164-167 `MaxRustF32(rectXMin, MinRustF32(centerX, rectXMax))` 同式（NaN 语义一致）。距离平方 ≤ r²（circle.rs:129 ↔ Circle.cpp:171）。一致。

### CollidesWith<Point>::collides_with（circle.rs:133-137）
- C++ 对应：`ContainsRustF32(const Point2d&)`（Circle.cpp:126-132）。
- 判定：【一致】
- 证据与说明：circle.rs:135 `point.sq_distance_to(center) <= radius.powi(2)`；Circle.cpp:128-131 `dx²+dy² <= r²`。sq_distance_to（point.rs:52-54）与直接展开在 f32 下同值（减法、平方、加法同序）。一致。无后缀 `Contains`（Circle.cpp:121-124）为 C++额外。

### DistanceTo<Point>::distance_to（circle.rs:139-150）
- C++ 对应：`DistanceToRustF32(const Point2d&)`（Circle.cpp:222-232）。
- 判定：【差异】（内点判据方向：Rust `sq_d < r²` 严格小于；C++ 同为 `<`——一致；详核）
- 证据与说明：circle.rs:144 `if sq_d < self.radius.powi(2) { 0.0 } else { sqrt(sq_d) - radius }`——严格 `<`：恰在圆上（sq_d == r²）时走 else，返回 sqrt(r²)-r = 0（数值上仍为 0，除非 r² 开方不精确）。Circle.cpp:229 `if (squaredDistance < radiusF32 * radiusF32) return 0.0; return sqrt(squaredDistance) - radiusF32;` 同判据同分支。判【一致】。

### DistanceTo<Point>::sq_distance_to（circle.rs:152-154）
- C++ 对应：`SquaredDistanceToRustF32(const Point2d&)`（Circle.cpp:248-253）。
- 判定：【一致】
- 证据与说明：circle.rs:153 `distance_to(other).powi(2)`（先开方路径再平方）；Circle.cpp:251-252 `distance = (float)DistanceToRustF32(point); return distance*distance`。同路径（先 distance 后平方）。注意 Rust DistanceToRustF32 返回 double 再 cast float——DistanceToRustF32 内部（Circle.cpp:231）`static_cast<double>(sqrt(...) - radiusF32)` 本是 f32 值转 double，回转无损。一致。

### SeparationDistance<Point>::separation_distance（circle.rs:157-166）
- C++ 对应：`SeparationDistanceRustF32(const Point2d&)`（Circle.cpp:174-186）。
- 判定：【差异】（比较方向细节：Rust 用 partial_cmp 的 Ordering::Less|Equal → Interior；C++ `centerDistance <= radiusF32`——等价；NaN 行为差异）
- 证据与说明：circle.rs:162 `d_center.partial_cmp(&self.radius).unwrap()`——d_center 为 NaN 时 partial_cmp 返回 None，**unwrap panic**；C++ Circle.cpp:181 `if (centerDistance <= radiusF32)` NaN 时为 false → Exterior 分支（不 panic）。NaN 仅在半径 NaN 时出现（Create 已拒绝 NaN 半径；center/point 有限时 sqrt 结果非 NaN），实际不可达。边界：`<=` ↔ `Ordering::Less | Equal`（circle.rs:163）等价。判【一致】（附 NaN 理论差异说明，实际不可达）。

### SeparationDistance<Point>::sq_separation_distance（circle.rs:168-171）
- C++ 对应：`SquaredSeparationDistanceRustF32(const Point2d&)`（Circle.cpp:269-276）。
- 判定：【一致】
- 证据与说明：均为"separation 后 `distance.powi(2)`"（circle.rs:170 ↔ Circle.cpp:273-274 `distance*distance`）。一致。

### DistanceTo<Circle> / SeparationDistance<Circle>（circle.rs:174-204）
- C++ 对应：`SeparationDistanceRustF32(const Circle2d&)`（Circle.cpp:188-208）、`DistanceToRustF32(const Circle2d&)`（Circle.cpp:234-239）、`SquaredDistanceToRustF32(const Circle2d&)`（Circle.cpp:255-260）、`SquaredSeparationDistanceRustF32(const Circle2d&)`（Circle.cpp:278-285）。
- 判定：【差异】（相切边界的判据不对称：collides_with 用 ≤、separation 用 <——**两侧一致复刻了这一不对称**；判定为一致，单列说明）
- 证据与说明：Rust circle.rs:191 `if sq_center_dist < sq_radii_sum`（严格 `<`）→ Interior；相切（==）时走 Exterior 且 dist = 0。而 collides_with（circle.rs:108）对相切判 true（≤）。**C++ Circle.cpp:199 同样 `squaredCenterDistance < squaredRadiusSum` 严格小于**——完全复刻了 Rust 的不对称：相切时 collides 为 true 但 separation 报 Exterior+0。验证背景陈述"Circle-Circle 相切时 separation 严格 < 的不对称（两侧一致复刻）"成立。dist 公式：circle.rs:192-196 `sq_radii_sum.sqrt() - sq_center_dist.sqrt()`（先各自开方再相减）↔ Circle.cpp:202-207 `sqrt(squaredRadiusSum) - sqrt(squaredCenterDistance)` 同序。DistanceTo（circle.rs:175-180 Interior→0）↔ Circle.cpp:238。sq 版（powi(2) ↔ distance*distance）一致。判定：【一致】。

### DistanceTo<Edge> / SeparationDistance<Edge>（circle.rs:206-233）
- C++ 对应：`SeparationDistanceRustF32(const Edge2d&)`（Circle.cpp:210-220）、`DistanceToRustF32(const Edge2d&)`（Circle.cpp:241-246）、`SquaredDistanceToRustF32(const Edge2d&)`（Circle.cpp:262-267）、`SquaredSeparationDistanceRustF32(const Edge2d&)`（Circle.cpp:287-294）。
- 判定：【差异】（判据方向：Rust `distance_to_center < self.radius` 严格小于（circle.rs:222）；C++ Circle.cpp:215 `if (edgeDistance < radiusF32)` 同为严格小于——一致；但 Rust 走的是"先 separation 后距离"的链路，C++ 同链路。判定一致）
- 证据与说明：circle.rs:221 `distance_to_center = e.distance_to(&self.center)`（sqrt 路径）；C++ Circle.cpp:213 `edgeDistance = (float)edge.DistanceToRustF32(center)`（同为 sqrt 路径，见 03-edge.md）。分支与公式逐一对应（circle.rs:222-226 ↔ Circle.cpp:215-219）。DistanceTo（circle.rs:207-212 ↔ Circle.cpp:243-245）、sq 版（circle.rs:214-215 ↔ Circle.cpp:265-266；circle.rs:230-232 ↔ Circle.cpp:291-293）均"先求 distance/separation 再 powi(2)/乘方"。注意此处的相切不对称与 Circle-Circle 相同：Edge 相切（distance == radius）判 Exterior+0 而 collides_with(Edge) 判 true——两侧一致复刻。判定：【一致】。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | bounding_circle 空输入失败：Rust panic vs C++ Result | 差异 | 空集合时 Rust 崩溃、C++ 返回错误码；正常路径数值一致 | circle.rs:38 `.expect("no circles provided")` vs Circle.cpp:34-36 `FromError` |
| 2 | bounding_circle 内 centroid 用 MidpointRustF32（分支回退模拟） | 差异 | 与 Rust f32::midpoint（x86_64 double 中转）特定输入差 1 ULP | Circle.cpp:69-71 + RustF32.h:19-28 vs circle.rs:52（经 edge.rs:124） |
| 3 | bounding_circle 返回类型：Circle vs Result<Circle> | 差异（形态） | 见 #1 | circle.h:24-25 vs circle.rs:36 |
| 4 | separation_distance(Point) NaN 时 unwrap panic vs C++ 静默 Exterior | 一致（理论差异，实际不可达） | NaN 半径已被 Create/try_new 拒绝 | circle.rs:162 `.unwrap()` vs Circle.cpp:181 |
| 5 | Circle-Circle/Edge 相切不对称（collides ≤ vs separation <） | 一致（两侧刻意复刻同一不对称） | 相切时 collides=true 而 separation=(Exterior,0)；行为对齐 | circle.rs:108 vs circle.rs:191；Circle.cpp:143 vs Circle.cpp:199 |
| 6 | 无后缀族：Area / BoundingBox / Contains | C++额外 | double 自有 API | Circle.cpp:83-86, 94-97, 121-124 |
| 7 | TransformableFrom | C++缺失 | 同 01/03 | circle.rs:90-96 |

## 结论

circle.rs 全部方法（含三组 CollidesWith、三组 DistanceTo/SeparationDistance 及其平方版）在 C++ RustF32 族中逐式复刻，分支方向（< vs ≤）与求值次序（先开方后相减、先 distance 后平方）均一致，甚至保留了 Rust"相切时 collides 与 separation 判据不对称"的原始行为。实质差异仅两处：bounding_circle 的空输入失败通道（panic vs Result）与其内部 centroid 的 1 ULP 级 midpoint 模拟差；另有 C++ 自有 double API 族不参与对齐。
