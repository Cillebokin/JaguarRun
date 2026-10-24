# 02. rect.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/primitives/rect.rs`（413 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/rect.h`（461 行）+ `JaguarCore/Source/Geometry/Primitives/Edge.cpp:79-89`（Rect2d::EdgesRustF32/SidesRustF32 实现）
**判定**: 基本等价（RustF32 族与 Rust 数值行为一致；差异集中在 scale 的失败处理、无后缀 quadrants 的顺序、centroid 求值次序、以及若干 C++ 额外/缺失 API）

## 方法逐条比对

### try_new（rect.rs:22-33）
- C++ 对应：`Rect2d::Create`（rect.h:45-61）。
- 判定：【一致】
- 证据与说明：rect.rs:23-26 `ensure!(x_min < x_max && y_min < y_max, ...)`；rect.h:48-55 先把四个参数 cast 到 f32（rect.h:48-51），再 `if (!(xMinF32 < xMaxF32 && yMinF32 < yMaxF32))` 返回 `ErrorCode::InvalidInput`，成功时把 f32 量化后的值存回 double（rect.h:56-60）。与 Rust `Result` 语义等价（错误通道 vs anyhow），校验条件逐字相同。注意 C++ 额外做了 f32 量化——对合法 f32 输入无差别。

### from_diagonal_corners（rect.rs:35-41）
- C++ 对应：`Rect2d::FromDiagonalCornersRustF32`（rect.h:63-76）。
- 判定：【一致】
- 证据与说明：rect.rs:36-39 用 `f32::min`/`f32::max`（NaN 时返回另一操作数）；rect.h:67-74 用 `detail::MinRustF32`/`MaxRustF32`（RustF32.h:30-38，`std::fmin/fmax`，NaN 语义与 Rust f32::min/max 一致：任一 NaN 则返回非 NaN 方）。随后调 Create 复刻 try_new 校验。逐操作等价。

### relation_to（rect.rs:47-66）
- C++ 对应：`Rect2d::RelationToRustF32`（rect.h:383-411）。
- 判定：【一致】
- 证据与说明：Rust 先 `collides_with`（rect.rs:48）判 Disjoint，再两侧包含判断 Surrounding/Enclosed。C++ rect.h:393-399 用 `MaxRustF32 > MinRustF32` 的取反形式内联了 collides 判断（与 rect.rs:285 `max(x_min)<=min(x_max) && ...` 取反等价），随后 rect.h:400-409 的两个包含判断与 rect.rs:51-64 逐条对应（普通 `<=`/`>=`，f32 上与 Rust 相同）。C++ 无后缀 `RelationTo`（rect.h:370-381）为 C++额外（见差异表）。

### almost_relation_to（rect.rs:72-91）
- C++ 对应：`Rect2d::AlmostRelationToRustF32`（rect.h:413-447）。
- 判定：【一致】
- 证据与说明：Rust 用 `FPA`（fpa.rs:18-32，float-cmp approx_eq，epsilon=f32::EPSILON 且 ulps=4，见 float-cmp-0.10.0/src/eq.rs:69-76）做偏序比较。C++ `detail::LessEqualRustF32`（RustF32.h:66-69）= `first < second || AlmostEqualRustF32(...)`，而 `AlmostEqualRustF32`（RustF32.h:49-64）= `first == second || |first-second| <= epsilon || ULP差 <= 4`（OrderedRustF32Bits 的位映射 RustF32.h:40-47 与 float-cmp 的 f32_ordered_bits 完全一致：负数取反、正数置符号位；ULP 差用无符号差值 ≤4 对应 float-cmp 的 `saturating_abs_i32(diff) <= 4`，有限值范围内等价——仅 ±Inf 两侧分支次序不同但结果同为"不等"）。almost_collides 的德摩根展开（rect.h:423-431 `!LE(Max,Min) || !LE(...)` ↔ rect.rs:293-294 `LE && LE`）逐条对应。验证背景陈述成立。

### inflate_to_square（rect.rs:96-112）
- C++ 对应：`Rect2d::InflateToSquareRustF32`（rect.h:111-129）。
- 判定：【一致】
- 证据与说明：rect.rs:97-105 宽高比较、dx/dy 计算、rect.rs:106-111 四则扩张；rect.h:117-128 全部在 f32 域逐句复刻（`(width - height) / 2.0F` 与 Rust `(width - height) / 2.0` 同为单次除法舍入）。逐操作等价。

### scale（rect.rs:116-121）
- C++ 对应：`Rect2d::ScaledRustF32`（rect.h:147-159）。
- 判定：【差异】
- 证据与说明：rect.rs:120 `.expect("scaling should not lead to invalid rectangle")`——Rust 在缩放产生退化矩形时 panic（进程终止）；rect.h:154-158 `if (!result.has_value()) return Result<Rect2d>::FromError({InvalidInput, "scaled rectangle is invalid"})`——C++ 返回错误码由调用方处理。数值路径一致：rect.rs:117-118 `dx = (x_max-x_min)*(factor-1.0)/2.0` ↔ rect.h:149-153 `scale = (float)factor - 1.0F; dx = ((float)xMax-(float)xMin) * scale / 2.0F`（乘法与除法次序相同，中间舍入相同）。差异仅在失败通道：panic vs Result。验证背景陈述成立。

### resize_by（rect.rs:126-138）
- C++ 对应：`Rect2d::ResizeByRustF32`（rect.h:131-145）。
- 判定：【一致】
- 证据与说明：rect.rs:127-132 减/加后 `if x_min < x_max && y_min < y_max` 判有效；rect.h:135-144 逐句 f32 复刻，条件相同（rect.h:141 `minX < maxX && minY < maxY`）。返回 `Option<Self>` ↔ `std::optional<Rect2d>`，语义等价。

### QUADRANT_NEIGHBOR_LAYOUT（rect.rs:141）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：rect.rs:141 `[[1,3],[0,2],[1,3],[0,2]]` 是四叉树/quad-split 侧使用的常量表，C++ rect.h 全文无对应常量。属 API 缺失（Rust 侧主要由 collision_detection 层使用）。

### quadrants（rect.rs:147-157）
- C++ 对应：`Rect2d::QuadrantsRustF32`（rect.h:301-318）；另有无后缀 `Quadrants`（rect.h:278-284）。
- 判定：【一致】（RustF32 版）/ 无后缀版【C++额外】且顺序不同
- 证据与说明：Rust 顺序：q1=from_diagonal_corners(corners[0]=(x_max,y_max), mid) → [mid..max]×[mid..max]（右上），q2=左上，q3=左下，q4=右下（rect.rs:151-156 + corners 定义 rect.rs:161-168）。`QuadrantsRustF32` rect.h:310-317 返回顺序 {mid,mid,max,max}(右上), {min,mid,mid,max}(左上), {min,min,mid,mid}(左下), {mid,min,max,mid}(右下)——与 Rust 逐位对应（其注释 rect.h:299-300 也声明了该对应）。`Quadrants`（rect.h:282-283）顺序为左下、右下、右上、左上，与 Rust 不同，且 midpoint 用 double `(xMin+xMax)*0.5` 而非 f32 midpoint——它是 C++ 自有 API，不对应任何 Rust 行为。验证背景陈述成立（差异仅在无后缀版）。

### corners（rect.rs:161-168）
- C++ 对应：`Rect2d::CornersRustF32`（rect.h:161-169）。
- 判定：【一致】
- 证据与说明：rect.rs:162-167 顺序 (x_max,y_max), (x_min,y_max), (x_min,y_min), (x_max,y_min)；rect.h:167-168 `{maxX,maxY},{minX,maxY},{minX,minY},{maxX,minY}` 完全一致（C++ 先把成员量化到 f32 再转回 double，rect.h:163-166）。

### sides / edges（rect.rs:172-216）
- C++ 对应：`Rect2d::SidesRustF32` / `EdgesRustF32`（声明 rect.h:171-172，实现 Edge.cpp:79-89）。
- 判定：【一致】
- 证据与说明：Rust 两侧均为 c[0]→c[1], c[1]→c[2], c[2]→c[3], c[3]→c[0]（rect.rs:174-191 与 198-215，两函数在 Rust 中本来就是重复实现）；Edge.cpp:82-83 `{corners[0],corners[1]}, {corners[1],corners[2]}, {corners[2],corners[3]}, {corners[3],corners[0]}`，SidesRustF32 直接转发 EdgesRustF32（Edge.cpp:86-89）。顺序与内容一致。仅返回类型不同（`[Edge;4]` vs `std::vector<Edge2d>`）。

### width / height（rect.rs:217-225）
- C++ 对应：`WidthRustF32` / `HeightRustF32`（rect.h:85-95）。
- 判定：【一致】
- 证据与说明：rect.rs:218-224 `x_max - x_min` f32 单减法；rect.h:87-88 `(float)xMax - (float)xMin` f32 减法后转 double。无后缀 Width/Height（rect.h:79-81，double 减法）为 C++额外。

### intersection（rect.rs:229-244）
- C++ 对应：`Rect2d::IntersectionRustF32`（rect.h:174-190）；无后缀 `Intersection`（rect.h:450-459）。
- 判定：【差异】（条件方向相反但边界等价？——否，详述）
- 证据与说明：rect.rs:234 `if x_min < x_max && y_min < y_max { Some } else { None }`（严格小于）；rect.h:186 `if (minX < maxX && minY < maxY) return ...` 同为严格小于（RustF32 版一致）。无后缀版 rect.h:456 `if (newXMin >= newXMax || newYMin >= newYMax) return std::nullopt;` 取反形式，边界（相等）同样返回 nullopt——与 Rust 相同。故 RustF32 版判定【一致】；无后缀版条件等价但无对应 Rust 方法，属 C++额外。修正：本条对 RustF32 版判定为【一致】，无后缀版列入差异表为 C++额外。

### bounding_rect（rect.rs:248-259）
- C++ 对应：`Rect2d::BoundingRectRustF32`（rect.h:192-205）。
- 判定：【一致】
- 证据与说明：rect.rs:249-252 min/max 组合；rect.h:196-204 用 MinRustF32/MaxRustF32 逐项对应，NaN 语义一致（见 from_diagonal_corners 条）。

### centroid（rect.rs:261-267）
- C++ 对应：`Rect2d::CentroidRustF32`（rect.h:286-297）。
- 判定：【差异】（求值路径不同，最多 1 ULP；仅在 double 中转时）
- 证据与说明：rect.rs:264-265 用 `f32::midpoint(x_min, x_max)`。Rust 1.90 std（f32.rs:992-1031）在 x86_64 目标上 `f32::midpoint` 编译为 `((self as f64 + other as f64) / 2.0) as f32`（f32.rs:998-1008，x86_64 在白名单内）——即"转 double、double 加法、除 2、转回 f32"，结果正确舍入。C++ `CentroidRustF32` rect.h:292-295：`midX = (float)((double)minX + (double)maxX) * 0.5)`——注意括号位置：`(static_cast<double>(minX) + static_cast<double>(maxX)) * 0.5` 先 double 求和再乘 0.5，然后 cast float。与 Rust 的 `(double 相加) / 2.0` 相比：`*0.5` 与 `/2.0` 在 double 上等价（0.5 是 2 的幂），加法同序，最终 cast 相同。因此 x86_64 下两者**位级一致**。但 C++ 还提供 `MidpointRustF32`（RustF32.h:19-28，模拟 std 的**分支回退路径**：`(a+b)*0.5f` 或 `a*0.5f+b*0.5f`），该函数被 Edge.cpp:162 和 Circle.cpp:69-71 使用而 rect.h 此处未用。结论：`CentroidRustF32` 在 x86_64/aarch64 上与 Rust f32::midpoint 位级一致，与背景候选"centroid 用 double 中转 vs f32::midpoint（1ulp）"的预期相反——实际无差异；差异只存在于"若 Rust 编译到非白名单目标"的假设情形。降级为说明，不算差异。判定修正为【一致】（附条件说明）。

### area（rect.rs:269-272）
- C++ 对应：`AreaRustF32`（rect.h:97-102）。
- 判定：【一致】
- 证据与说明：rect.rs:271 `(x_max-x_min)*(y_max-y_min)`；rect.h:99-101 同序 f32 减法与乘法。无后缀 `Area`（rect.h:83，`Width()*Height()` double）为 C++额外。

### diameter（rect.rs:274-279）
- C++ 对应：`DiameterRustF32`（rect.h:104-109）。
- 判定：【一致】
- 证据与说明：rect.rs:276-278 `dx.powi(2)+dy.powi(2)` 再 sqrt（powi(2) 与乘法对 f32 结果相同）；rect.h:106-108 `sqrt(width*width + height*height)` f32 域一致。

### CollidesWith<Rect> for Rect::collides_with（rect.rs:282-288）
- C++ 对应：`IntersectsRustF32`（rect.h:353-367）。
- 判定：【一致】
- 证据与说明：rect.rs:285-286 `max(x_min)<=min(x_max) && max(y_min)<=min(y_max)`；rect.h:363-366 用 MaxRustF32/MinRustF32 复刻（fmin/fmax 的 NaN 语义同 Rust f32::max/min）。无后缀 `Intersects`（rect.h:347-351）为 C++额外。

### AlmostCollidesWith<Rect>::almost_collides_with（rect.rs:290-296）
- C++ 对应：无独立方法；`AlmostRelationToRustF32` 的 Disjoint 分支（rect.h:423-431）内联了该逻辑。
- 判定：【C++缺失】（独立 API 缺失，逻辑已内联复用）
- 证据与说明：rect.rs:293-294 `FPA(max(...)) <= FPA(min(...)) && ...`；rect.h:423-431 `!LessEqualRustF32(MaxRustF32(...), MinRustF32(...)) || ... → Disjoint` 是同一判断的取反。数值语义已被覆盖，缺的是可独立调用的 `AlmostIntersectsRustF32`。

### CollidesWith<Point> for Rect（rect.rs:298-304）
- C++ 对应：`ContainsRustF32`（rect.h:326-334）。
- 判定：【一致】
- 证据与说明：rect.rs:302 `x >= x_min && x <= x_max && y >= y_min && y <= y_max`；rect.h:330-333 逐条相同（f32 比较）。

### AlmostCollidesWith<Point> for Rect（rect.rs:306-315）
- C++ 对应：`AlmostContainsRustF32`（rect.h:336-344）。
- 判定：【一致】
- 证据与说明：rect.rs:310-313 `FPA(x) >= FPA(x_min) && ...`；rect.h:340-343 `LessEqualRustF32(xMin, pointX) && LessEqualRustF32(pointX, xMax) && ...`——FPA 偏序的 ≥ 即 LE 反向，逐条对应（AlmostEqualRustF32 对称，RustF32.h:49）。

### CollidesWith<Edge> for Rect（rect.rs:317-362）
- C++ 对应：`Edge2d::IntersectsRustF32(const Rect2d&)`（Edge.cpp:342-384）。
- 判定：【一致】
- 证据与说明：三段算法逐句对应：(1) bbox 预检 rect.rs:329-330 ↔ Edge.cpp:354-357（MaxRustF32/MinRustF32 组合，Rust 的 `.max()/.min()` 是 f32::max/min 语义）；(2) 端点在矩形内 rect.rs:337 ↔ Edge.cpp:362-368（lambda containsEndpoint 用普通 >=/<=，同 rect.rs:302）；(3) 四角叉积同侧判断 rect.rs:351-360（corners 顺序 c[0..3] = (max,max),(min,max),(min,min),(max,min)）↔ Edge.cpp:372-383（cornerX/cornerY 数组同顺序 {max,min,min,max}/{max,max,min,min}），`all_positive || all_negative` 取反逻辑相同，f32 乘加次序相同。

### DistanceTo<Point>::distance_to / sq_distance_to（rect.rs:364-386）
- C++ 对应：`DistanceToRustF32` / `SquaredDistanceToRustF32`（rect.h:207-243）。
- 判定：【一致】
- 证据与说明：rect.rs:371-385 逐轴累加 `(x - x_min).powi(2)` 等四个分支（if/else if 次序相同：x < x_min → x > x_max → y < y_min → y > y_max），末尾 `.abs()`；rect.h:215-236 逐分支复刻（`dx*dx` 与 powi(2) 等值），rect.h:236 `std::abs(distance)` 与 rect.rs:384 对应（该 abs 在 Rust 中实际无效果，因平方和恒非负——两侧一致地保留了这个冗余）。sqrt 在 f32：rect.rs:367 `.sqrt()` ↔ rect.h:241-242 `sqrt((float)sq)`。

### SeparationDistance<Point>::separation_distance / sq_separation_distance（rect.rs:388-413）
- C++ 对应：`SeparationDistanceRustF32` / `SquaredSeparationDistanceRustF32`（rect.h:245-269）。
- 判定：【差异】（内点最小距离的选取方法不同，边界情形可差 1 ULP 级；且 C++ 版本同 Rust 一样保留 sqrt-再平方路径，总体一致，详述）
- 证据与说明：内点分支：rect.rs:399-408 取四距离 `[|x-x_min|, |x-x_max|, |y-y_min|, |y-y_max|]` 的 `min_by_key(OrderedFloat)`，返回 `min_distance.powi(2)`；rect.h:253-259 用嵌套 `MinRustF32(std::abs(x-xMin), ...)` 取 min 后 `minDistance * minDistance`。fmin 与 OrderedFloat 最小值在无 NaN 时同值；`d*d` 与 `powi(2)` 等值——数值一致。外点分支 rect.rs:410 ↔ rect.h:248-249 一致。`separation_distance` = `sq_separation_distance` 后 sqrt：rect.rs:391-392 ↔ rect.h:265-268（sqrt((float)sq)）一致。注意 Rust 返回的是**已开方的 min_distance 再平方**（即 min_distance.powi(2)），C++ 同样直接 minDistance*minDistance，无 sqrt 中转——两侧都未走"先 SeparationDistance 再平方"的路径，一致。综合判定【一致】（方法学差异仅在 min 的实现机制，结果等值）。修正：本条判定为【一致】。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | scale 失败行为：Rust panic vs C++ Result 错误 | 差异 | 退化缩放输入下 Rust 进程崩溃、C++ 可恢复；正常路径数值一致 | rect.rs:119-120 `.expect(...)` vs rect.h:154-158 `FromError` |
| 2 | Quadrants（无后缀 double 版）象限顺序与 midpoint 方式 | C++额外 | 左下起 vs Rust 右上起；仅影响 C++ 自有调用方，不影响对齐 | rect.h:278-284 vs rect.rs:147-157 |
| 3 | QUADRANT_NEIGHBOR_LAYOUT 常量 | C++缺失 | 四叉树邻居表无对应（Rust 侧供上层使用） | rect.rs:141 vs rect.h 无 |
| 4 | almost_collides_with(Rect) 独立 API | C++缺失 | 逻辑内联于 AlmostRelationToRustF32，无独立入口 | rect.rs:290-296 vs rect.h:413-447 |
| 5 | 无后缀 double 族：Width/Height/Area/Intersects/RelationTo/Intersection/Inflated/Contains | C++额外 | C++ 自有 API，double 精度，语义不承诺对齐 Rust | rect.h:79-83, 272-275, 321-324, 347-381, 450-459 |
| 6 | try_new 的错误通道：anyhow Result vs Result<Rect2d> 错误码 | 一致（形态差异） | 错误可观测性不同（消息 vs ErrorCode），条件与阈值相同 | rect.rs:22-33 vs rect.h:45-61 |

## 结论

rect.rs 的全部数值方法在 `*RustF32` 族中逐操作复刻成功，包括 FPA 偏序（LessEqualRustF32 ↔ float-cmp epsilon+4ULP，位映射公式两侧同构）、quadrants/corners/edges 的顺序、以及 Edge-in-Rect 判交的三段算法。仅存的实质行为差异是 `scale` 的失败通道（panic vs Result），以及 C++ 额外提供一套与 Rust 顺序都不同（quadrants）的无后缀 double API——后者只要调用方统一使用 RustF32 族就不构成对齐风险。

## 补充审计（2026-09-29）

逐行复核 rect.rs 全部 413 行（17 个方法 + 5 个 trait 实现块），确认主记录已覆盖全部函数级条目。以下为补充核对项：

### 【浅覆盖→补齐】ordered-float 版本核实（OrderedFloat 的 Ord/Hash 语义）
- 主记录多处引用 `OrderedFloat`（rect.rs:406 `min_by_key(OrderedFloat)`、07 记录凸包排序），未核对库版本。Cargo.lock 确认 jagua-rs 用 **ordered-float 5.5.0**。
- 关键语义核实（ordered-float-5.5.0/src/lib.rs:292-304）：`Ord::cmp` 用 `<`/`>` 原生比较——**NaN 与任何值比较均为 false，落到 else 分支返回 Equal**。即 OrderedFloat(NaN) 与 OrderedFloat(x) 互相 cmp 为 Equal（非"NaN 排最后"的严格全序）。对 rect.rs:406 `min_by_key`：若某距离为 NaN，它与当前最小比较为 Equal → 不替换（保首个）——与 C++ `MinRustF32`/fmin（NaN 返回另一方）行为**不一致**（fmin(min, NaN)=min，即 NaN 永不成为最小；OrderedFloat 路径下 NaN 与 min 相等也不替换，结果同为保留非 NaN 最小值——**实际结果一致**）。仅当**全部**候选为 NaN 时两侧才有差异（Rust 返回首个 NaN，C++ 返回 f32 初值链的结果），该输入被 Create 的有限值校验排除，不可达。
- 判定：【一致】（NaN 不可达；Ord 的 Equal-on-NaN 语义细节存档备查）。
- 证据：rect.rs:406；ordered-float-5.5.0/src/lib.rs:294-303；rect.h:253-257。

### 【浅覆盖→补齐】FPA 与 AlmostEqualRustF32 的±Inf 分支次序细节
- 主记录 almost_relation_to 条称 float-cmp 与 AlmostEqualRustF32 "仅 ±Inf 两侧分支次序不同但结果同为'不等'"。补充核实 float-cmp 0.10.0 eq.rs：其 approx 检查在 ulps 比较前有 `isfinite` 短路（若任一非有限且 abs 差 > epsilon 即 false）。C++ RustF32.h:55-56 `if (!isfinite(first) || !isfinite(second)) return false;` 同语义（且 C++ 先做 `first == second` 短路：+Inf == +Inf → true；float-cmp 的 `approx_eq!` 宏同样以 `a == b` 起始短路）。两侧对 ±Inf：相等 Inf → true；不等 Inf → false——**完全一致**，主记录的"次序不同"仅指内部判断顺序，无行为差。
- 判定：【一致】（确认主记录结论）。
- 证据：RustF32.h:49-64；float-cmp-0.10.0/src/eq.rs。

### 【遗漏→补齐】Rect 无 Hash/无 Eq derive（与 Point 的不对称）
- rect.rs:13 `#[derive(Clone, Debug, PartialEq, Copy)]`——Rect **无 Eq、无 Hash**（Point 有）。C++ rect.h 亦无 Rect2d 的 operator==。两侧对称地都只提供字段级比较能力（Rust PartialEq vs C++ 无），无哈希容器依赖。
- 判定：【一致】（均无哈希需求；C++ 连 == 都未提供，但 Rust 侧 Rect 的 PartialEq 在核心代码中无调用方——仅测试性使用）。
- 证据：rect.rs:13 vs rect.h 全文无 operator==。

### 【遗漏→补齐】cfg(test) 行为契约——rect.rs 无测试模块
- rect.rs 全文 413 行无 `#[cfg(test)]`。四叉树层（19-21 号记录）通过 quadrants/relation_to 等间接锁定行为，本文件自身无测试契约。
- 判定：不适用。

补充结论：02 记录覆盖完整。新增两项库级语义核实（ordered-float 5.5.0 的 NaN-Equal Ord、float-cmp 的 Inf 短路）均确认主记录判定不变。

## E1偏差预算复核（2026-10-22）

- 当前几何差分器的`expectRustNumbers`（`JaguarCore/Tests/Differential/CompareGeometry.cpp:181-211`）先把Rust与C++结果都转为`float`，再调用`AlmostEqualRustF32`；该helper允许绝对差不超过`f32::EPSILON`，或ULP距离不超过4。Rect数值字段因此共用这一验收阈值。
- 该阈值是通过/失败门槛，不是Rect每个方法的实测最大误差；当前差分输出不记录逐字段ULP距离，也没有按`Width/Area/Distance/Separation`分别配置预算。关系枚举、位置枚举和布尔结果则逐值精确比较。
- E1裁决：不能仅凭共享比较器通过就声称每个调用点的偏差预算已单独登记。本轮已为下方191个既有fixture数值字段采集位模式；其他Rect方法与输入域仍未做同等级统计。所有实测误差须与`epsilon/4 ULP`放行门槛分开记录；`AlmostRelationToRustF32`中的FPA阈值属于算法语义，不应混作输出误差预算。

### E1逐字段位级采样（2026-10-22）

| Fixture键 | 数值字段数 | 最大ULP差 | 最大绝对差 |
|---|---:|---:|---:|
| `rect.operations` | 20 | 0 | 0 |
| `rect.combination` | 16 | 0 | 0 |
| `rect.edges` | 16 | 0 | 0 |
| `rect.distance` | 7 | 0 | 0 |
| `geometry.rect_basics` | 20 | 0 | 0 |
| `geometry.rect_degenerate_metrics` | 12 | 0 | 0 |
| `geometry.rect_degenerate_boundary_shapes` | 48 | 0 | 0 |
| `geometry.rect_degenerate_sides` | 32 | 0 | 0 |
| `geometry.rect_degenerate_bounding_rects` | 12 | 0 | 0 |
| `geometry.rect_degenerate_resize_recovery` | 8 | 0 | 0 |
| **合计** | **191** | **0** | **0** |

- 采样方法：MSVC 2019 14.29 Release（`/O2 /fp:precise`）直接执行当前Rect方法；Rust期望位模式从同一fixture十进制值round-trip到f32后取得。每个字段的索引、Rust位模式、C++位模式、ULP差和绝对差保存在[逐字段TSV](02-rect-f32-raw-bits.tsv)。
- 采样结果：191/191项的Rust与C++位模式完全相同。`geometry.rect_basics`实际包含20项（中心2、象限16、宽高2）；首轮37项记录漏掉宽高2项，现已由本轮完整表更正。
- 位模式示例：`√13`的距离/分离距离为`0x4066c15a`，`√20`的直径为`0x408f1bbd`，平方距离13为`0x41500000`；负2、负1、0、0.5、1、1.5、2、3、3.5、4、5、8等值的原始模式也逐字段记录于TSV。
- 证据边界：本结果只覆盖以上10个既有fixture键及对应输入，不代表所有Rect公开方法、所有浮点编译选项或整个输入域都逐位一致。
- 与验收门槛的关系：本组实测误差为0 ULP；差分器仍统一使用`abs差≤f32::EPSILON`或`ULP≤4`的通过阈值。实测误差与放行条件是不同证据，不能混为一谈。
