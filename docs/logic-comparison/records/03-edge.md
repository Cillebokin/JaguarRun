# 03. edge.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/primitives/edge.rs`（248 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/edge.h`（60 行）+ `JaguarCore/Source/Geometry/Primitives/Edge.cpp`（385 行）
**判定**: 基本等价（RustF32 族与 Rust 逐操作一致，含交点公式与判交边界；主要差异是 C++ 额外的容差版 Intersects(double) 带有"共线重叠算相交"的额外语义）

## 方法逐条比对

### try_new（edge.rs:16-19）
- C++ 对应：`Edge2d::Create`（Edge.cpp:91-104）。
- 判定：【差异】（NaN 输入下的判定顺序；正常输入一致）
- 证据与说明：Rust edge.rs:17 `ensure!(start != end, ...)`——`Point` 的 `!=` 是 `PartialEq`（逐分量 f32 `!=`）：NaN != NaN 为 true，所以**两点同为 NaN 时 Rust 拒绝创建**；而 (+0.0,-0.0) 因 `==` 视为相等被拒绝。C++ Edge.cpp:93-100：先各自 cast f32，再 `if (startX == endX && startY == endY)` 返回错误——NaN == NaN 为 false，所以**同为 NaN 时 C++ 反而创建成功**；±0.0 与 Rust 一致（== 为 true → 拒绝）。两分支：正常有限值一致；NaN 输入相反。另注意 C++ 会把输入量化为 f32 再存回 double（Edge.cpp:101-103），Rust 天然 f32。NaN 分支判【差异】。

### extend_at_front（edge.rs:22-29）
- C++ 对应：`Edge2d::ExtendedAtFrontRustF32`（Edge.cpp:166-181）。
- 判定：【差异】（舍入次序：除法 vs 乘法结合次序不同，最多 1 ULP）
- 证据与说明：Rust edge.rs:26-27 `self.start.0 -= dx * (d / l)`——先 `d / l`（一次 f32 除法舍入），再乘 dx，再减。C++ Edge.cpp:176 `const float extension = static_cast<float>(distance) / edgeLength;` 然后 Edge.cpp:178 `startX - dx * extension`——同序：先除后乘再减。逐操作一致。修正：本条为【一致】。另注意 Rust 的 `l = self.length()`（sqrt(dx²+dy²)）↔ Edge.cpp:175 `std::sqrt(dx*dx+dy*dy)`，一致。

### extend_at_back（edge.rs:31-39）
- C++ 对应：`Edge2d::ExtendedAtBackRustF32`（Edge.cpp:183-198）。
- 判定：【一致】
- 证据与说明：edge.rs:36-37 `self.end.0 += dx * (d / l)`；Edge.cpp:193-195 同序（先除、乘、加）。一致。

### scale（edge.rs:41-49）
- C++ 对应：`Edge2d::ScaledRustF32`（Edge.cpp:200-214）。
- 判定：【差异】（运算次序：Rust 先 `(factor-1.0)/2.0` 再乘；C++ 先乘再除 2——中间舍入点不同，最多 1 ULP）
- 证据与说明：Rust edge.rs:44-47 `self.start.0 -= dx * (factor - 1.0) / 2.0`——按运算符优先级：`((dx * (factor-1.0)) / 2.0)`，即**先乘后除**。C++ Edge.cpp:208-213 `startX - dx * scaleFactor / 2.0F`——C++ 同样 `((dx * scaleFactor) / 2.0F)` 先乘后除。逐操作一致。修正：本条为【一致】。

### reverse（edge.rs:51-55）
- C++ 对应：`Edge2d::ReversedRustF32`（Edge.cpp:216-223）。
- 判定：【一致】
- 证据与说明：交换 start/end（edge.rs:53 `std::mem::swap`）；C++ 构造 {end, start}，且成员重新经 f32 量化（无舍入变化）。一致。

### collides_at（edge.rs:57-65）
- C++ 对应：`Edge2d::IntersectionPointRustF32`（Edge.cpp:336-340，实现在匿名空间 intersectionPointRustF32 Edge.cpp:32-76）。
- 判定：【一致】
- 证据与说明：Rust edge.rs:59-64 调 `edge_intersection(self, other, true)`，Yes(point) → Some(point)，No → None。C++ `intersectionPointRustF32` 返回 `std::optional<Point2d>`：bbox 预检（Edge.cpp:45-56，MinRustF32/MaxRustF32 组合 ↔ edge.rs:202-213 的 min/max 组合）、平行列 `t_denom == 0.0F || u_denom == 0.0F` 返回 nullopt（Edge.cpp:66-67 ↔ edge.rs:226-229）、t/u ∈ [0,1] 判定（Edge.cpp:71 `t >= 0.0F && t <= 1.0F && u >= 0.0F && u <= 1.0F` ↔ edge.rs:233 `(0.0..=1.0).contains(&t) && (0.0..=1.0).contains(&u)`，NaN 情形：Rust contains 为 false，C++ 连锁 && 也为 false，一致）、交点公式 `x2 + t*(x1-x2)`（Edge.cpp:74-75 ↔ edge.rs:236）。分子分母表达式逐项相同（Edge.cpp:58-65 ↔ edge.rs:221-224）。注意 Rust `Intersection::Yes(loc)` 中 loc 为 `Option<Point>`（calc_loc=false 时为 None），collides_at 用 expect 解包——C++ 直接合成为 optional 返回，语义等价。一致。

### closest_point_on_edge（edge.rs:70-94）
- C++ 对应：`Edge2d::ClosestPointRustF32`（Edge.cpp:246-271）。
- 判定：【一致】
- 证据与说明：stack overflow 算法逐步复刻：a/b/c/d 定义（Edge.cpp:255-258 ↔ edge.rs:76-79）、dot/len_sq（Edge.cpp:259-260 ↔ edge.rs:81-82）、`len_sq != 0.0` 时 param = dot/len_sq 否则 -1.0（Edge.cpp:261-263 ↔ edge.rs:83-86）、三分支 param<0 → start、param>1 → end、否则 x1+param*c（Edge.cpp:265-270 ↔ edge.rs:87-91）。`len_sq == 0.0` 含 NaN 情形：Rust `!= 0.0` 对 NaN 为 false → param=-1 → 返回 start；C++ `lengthSquared != 0.0F` 同为 false → param 保持 -1 → param<0 → start。一致（NaN 时同返 start）。无后缀 `ClosestPoint`（Edge.cpp:232-244，double + clamp）为 C++额外。

### x_min / y_min / x_max / y_max（edge.rs:96-114）
- C++ 对应：`XMinRustF32` / `YMinRustF32` / `XMaxRustF32` / `YMaxRustF32`（Edge.cpp:131-153）。
- 判定：【一致】
- 证据与说明：edge.rs:98-113 用 `f32::min`/`f32::max`；C++ 用 `MinRustF32`/`MaxRustF32`（fmin/fmax，NaN 语义与 Rust 一致）。逐项对应。

### length（edge.rs:116-119）
- C++ 对应：`Edge2d::LengthRustF32`（Edge.cpp:124-129）。
- 判定：【一致】
- 证据与说明：Rust `start.distance_to(&end)` = `sqrt(dx²+dy²)`（point.rs:48）；C++ Edge.cpp:126-128 `sqrt(dx*dx + dy*dy)` f32 域一致。无后缀 `Length`（Edge.cpp:119-122，double）为 C++额外。

### centroid（edge.rs:121-127）
- C++ 对应：`Edge2d::CentroidRustF32`（Edge.cpp:155-164）。
- 判定：【helper级逐位复核通过；生产调用点的历史向量待复验】
- 证据与说明：锁定的Rust 1.98.1（rustc commit `48a229ceaefd4985c50990b14116b6d856af0985`）在x86_64的`f32::midpoint`分支执行`((self as f64 + other as f64) * 0.5) as f32`（`library/core/src/num/f32.rs:1093-1109`）。C++ `detail::MidpointRustF32`现对`_M_X64`/`__x86_64__`使用相同提升、相加、乘0.5、回转路径；非x86_64构建仍走分支回退实现。2026-10-22以Clang 23.1.3 x86_64编译当前helper，并与锁定Rust 1.98.1对比12组固定边界值及10,000组确定性有限输入；10,012组输出位模式全部一致。当前Release JaguarCore全量runner退出0，包含活动Edge/Circle几何差分fixture，但该fixture经`AlmostEqualRustF32`最多允许4 ULP；因此它没有复现或否定旧记录中的特定1 ULP样例，生产调用点的历史向量仍待复验。

### bbox（edge.rs:129-137）
- C++ 对应：`Edge2d::BoundingBox()`（Edge.cpp:106-117）。
- 判定：【一致】
- 证据与说明：edge.rs:130-137 直接以 x_min/y_min/x_max/y_max 构造 Rect 字段（**不走 try_new 校验**）；C++ Edge.cpp:112-116 同样直接构造 Rect2d（无 Create 校验）。字段与 NaN 语义（fmin/fmax）一致。注意 C++ 方法名无 RustF32 后缀但实现是 f32 规则（内部全 MinRustF32/MaxRustF32）——命名不一致但行为对齐。

### Transformable::transform（edge.rs:140-148）
- C++ 对应：`Edge2d::TransformRustF32`（Edge.cpp:225-230，原地修改）。
- 判定：【一致】
- 证据与说明：Rust 对 start/end 各调 point transform（edge.rs:143-144）；C++ `start = transform.ApplyRustF32(start); end = transform.ApplyRustF32(end);`（Edge.cpp:228-229）。逐点等价（见 01-point.md transform 条）。

### TransformableFrom::transform_from（edge.rs:150-158）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：同 01-point.md：trait 形态 API（内存优化语义）缺失，数值路径由 ApplyRustF32 覆盖。

### DistanceTo<Point>::distance_to / sq_distance_to（edge.rs:160-174）
- C++ 对应：`DistanceToRustF32` / `SquaredDistanceToRustF32`（Edge.cpp:273-290）。
- 判定：【一致】
- 证据与说明：sq = (x-xx)² + (y-yy)²，其中 (xx,yy) = closest_point_on_edge（edge.rs:167-173）；C++ Edge.cpp:276-281 同样先 ClosestPointRustF32 再 f32 差平方和。distance = sqrt(sq)（edge.rs:163 ↔ Edge.cpp:287-289 sqrt((float)sq)）。注意 Rust edge.rs:163 用 `f32::sqrt(...)` 显式前缀，C++ 一致。

### CollidesWith<Edge>::collides_with（edge.rs:176-184）
- C++ 对应：`Edge2d::IntersectsRustF32(const Edge2d&)`（Edge.cpp:331-334）。
- 判定：【一致】
- 证据与说明：Rust 调 edge_intersection(…, false)，Yes→true / No→false（edge.rs:179-182）；C++ `intersectionPointRustF32(*this, other).has_value()`（Edge.cpp:333）——同一个函数，calc_loc 与否不影响 Yes/No 判定（交点计算在 Yes 分支内才执行）。边界：共线（denom==0）返回 No ↔ nullopt；端点相接（t 或 u 恰为 0/1）两侧均判 Yes。一致。

### CollidesWith<Rect>::collides_with（edge.rs:186-191）
- C++ 对应：`Edge2d::IntersectsRustF32(const Rect2d&)`（Edge.cpp:342-384）。
- 判定：【一致】
- 证据与说明：Rust 委托 `other.collides_with(self)`（rect.rs:317-362）；C++ 侧直接实现了同一算法（见 02-rect.md 对应条），且 Edge.cpp:342 版与 rect.rs:317 逐段一致（bbox 预检、端点包含、四角叉积同侧）。委托方向不同（Rust: Edge→Rect 委托 Rect 实现；C++: Edge 自己实现）但算法相同。一致。

### edge_intersection（私有函数，edge.rs:193-243）
- C++ 对应：匿名空间 `intersectionPointRustF32`（Edge.cpp:32-76）。
- 判定：【一致】
- 证据与说明：见 collides_at 条的逐式对照——bbox 预检（edge.rs:201-217 ↔ Edge.cpp:45-56）、t/u 分子分母四式（edge.rs:221-224 ↔ Edge.cpp:58-65）、平行早退（edge.rs:226-229 ↔ Edge.cpp:66-67）、范围判定与交点公式（edge.rs:231-241 ↔ Edge.cpp:69-75）。全部在 f32 域、同次序。Intersection 枚举（edge.rs:245-248）在 C++ 中坍缩为 optional 返回，语义覆盖。

### （C++ 额外）Intersects(other, tolerance)（Edge.cpp:292-329）
- Rust 对应：无。
- 判定：【C++额外】
- 证据与说明：这是 C++ 自有的容差版判交，采用完全不同的算法（叉积符号 + onSegment 端点在段上测试）。关键语义差异：Edge.cpp:312-314 对"四叉积全 ≤ tolerance"（共线/平行）显式返回 **false**（注释 Edge.cpp:309-311 说明刻意对齐 jagua-rs 的"共线不算相交"）；但 Edge.cpp:325-328 的 onSegment 回退分支会把**共线且端点落在对方段上**的情形判为 true（cross ≤ tol 且 within 区间）——即共线重叠时：若两段完全共线且四个端点都在彼此的 span 上，312 行先命中返回 false；若仅一端点落在对方段上（部分重叠），走到 325 行返回 true。而 Rust 的 edge_intersection 对一切共线（denom==0）一律 No。因此**部分共线重叠在 C++ 容差版算相交、Rust 算不相交**。验证背景陈述："Edge Intersects(tolerance) 共线重叠算相交（C++额外语义）"成立（312 行的全共线早退是 false，但端点相搭的部分共线走 onSegment 返回 true）。该 API 属 C++ 额外族，不影响 RustF32 族的等价性，但混用两族会得到不同结果。

### （C++ 额外）Length / ClosestPoint / 无后缀族（Edge.cpp:119-122, 232-244）
- 判定：【C++额外】
- 证据与说明：double 精度自有 API。ClosestPoint 用 clamp(projection) 而非三分支，数值结果与 Rust 三分支在 double 域同值（clamp(0,1,p) 等价于三分支选择），但精度域不同。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | try_new 对 NaN 输入的判定 | 差异 | 两端同为 NaN 时：Rust 拒绝（NaN != NaN → ensure 失败）、C++ 接受（NaN == NaN 为 false → 通过） | edge.rs:17 `start != end` vs Edge.cpp:97 `startX == endX && startY == endY` |
| 2 | centroid 的 midpoint 实现路径 | helper级位模式一致；历史调用点向量待复验 | x86_64 helper与pinned Rust 1.98.1在10,012组有限输入上逐位一致；Release全量runner通过，但调用点fixture使用4-ULP比较且未复现历史1 ULP输入 | edge.rs:124-125 vs Edge.cpp:162 + RustF32.h:19-31 + 2026-10-22 helper probe |
| 3 | Intersects(other, tolerance) 共线重叠语义 | C++额外 | 部分共线重叠（端点搭在对方段上）判 true，Rust 一律 No；属 C++ 自有 API 族 | Edge.cpp:292-329（尤其 312-314 与 325-328）vs edge.rs:226-229 |
| 4 | TransformableFrom::transform_from | C++缺失 | 内存优化 trait 形态缺失，数值由 ApplyRustF32 覆盖 | edge.rs:150-158 vs edge.h 无对应 |
| 5 | 无后缀族：Length / ClosestPoint | C++额外 | double 精度自有 API | Edge.cpp:119-122, 232-244 |
| 6 | collides_at 返回 Option<Point> vs optional<Point2d> | 一致（形态等价） | 语义相同 | edge.rs:57-65 vs Edge.cpp:336-340 |

## 结论

edge.rs 的核心几何（线段判交、交点计算、最近点、距离）在C++ `IntersectsRustF32`/`IntersectionPointRustF32`/`ClosestPointRustF32`中逐公式、逐舍入点复刻，包括平行列早退与[0,1]闭区间边界，行为等价。剩余差异为NaN端点创建判定相反，以及一套C++自有的容差判交API（共线重叠语义不同）；centroid helper已按pinned Rust x86_64路径调整，差分行为尚待复验。

## 补充审计（2026-09-29）

逐行复核 edge.rs 全部 248 行（14 个方法 + 4 个 trait 实现 + 私有 edge_intersection + Intersection 枚举），确认主记录已覆盖全部条目。以下为补充核对项：

### 【浅覆盖→补齐】bbox 判交公式在 NaN 语义下的等价性（edge_intersection bbox 预检）
- 主记录 collides_at 条称 bbox 预检"MinRustF32/MaxRustF32 组合 ↔ edge.rs:202-213 的 min/max 组合"一致。补充核对公式形态：Rust edge.rs:212 `x_min_e1.max(x_min_e2) > x_max_e1.min(x_max_e2)`（f32::max/min，NaN 时返回另一方）；C++ Edge.cpp:45-54 `MaxRustF32(MinRustF32(x1,x2), MinRustF32(x3,x4)) > MinRustF32(MaxRustF32(x1,x2), MaxRustF32(x3,x4))`——**嵌套组合次序逐字对应**（e1 内 min/max → 跨 e1/e2 max/min），fmin/fmax 的 NaN 传播语义与 Rust f32::min/max 相同。NaN 输入（Create 已拒）不可达。
- 判定：【一致】（公式形态与 NaN 语义双重确认）。
- 证据：edge.rs:202-213 vs Edge.cpp:45-54。

### 【浅覆盖→补齐】Edge 无 Hash/Eq（同 Rect）
- edge.rs:9 `#[derive(Clone, Debug, PartialEq, Copy)]`——Edge 无 Eq/Hash；C++ Edge2d 亦无 operator==（edge.h 全文无）。两侧一致地无哈希/相等容器依赖。
- 注意：C++ 侧 Edge2d 的相等比较**实际通过 Point2d::operator== 成员级发生**（Simplify.cpp:160 `edge.start == affected[i]`、Offset.cpp:131-136 samePolygonCycle）——但 Rust 对应代码（shape_modification.rs:194/209）用的是 `[Point; 3]/[Point; 4].contains(&l.start)`，同为 Point 的 PartialEq。逐字段等价，已在记录 10/11 覆盖，此处确认无形态缝隙。
- 判定：【一致】。
- 证据：edge.rs:9 vs edge.h；Simplify.cpp:160 vs shape_modification.rs:194。

### 【遗漏→补齐】collides_at 的 Intersection::Yes(None) 形态契约
- Rust `Intersection::Yes(Option<Point>)`（edge.rs:245-248）允许 Yes 分支携带 None（calc_loc=false 时）。collides_at 用 `.expect(...)`（edge.rs:62）解包——**仅当 collides_at 传 true 而 edge_intersection 返回 Yes(None) 才 panic**，检查 edge.rs:233-240 确认 calc_loc=true 时 loc 恒为 Some，**该 expect 不可达**（与主记录判断一致，此处补行号证据）。
- 判定：【一致】（不可达防御；C++ 直接返回 optional 无此形态）。
- 证据：edge.rs:61-64, 235-239。

### 【遗漏→补齐】cfg(test)——edge.rs 无测试模块
- edge.rs 248 行无 `#[cfg(test)]`，无测试锁定行为契约。判定：不适用。

补充结论：03 记录覆盖完整，无遗漏函数。两处补充确认（bbox 预检嵌套次序、Edge 相等的成员级消费链）均不改变既有判定。
