# S17. quantify 量化公式（overlap_proxy + pair_matrix + mod） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/mod.rs`（52 行）+ `quantify/overlap_proxy.rs`（26 行）+ `quantify/pair_matrix.rs`（49 行）
**C++**: `SparrowCore/Include/SparrowCore/Quantify/OverlapProxy.h`（29 行）+ `Source/Quantify/OverlapProxy.cpp`（245 行）+ `Include/SparrowCore/Quantify/PairMatrix.h`（41 行）+ `Source/Quantify/PairMatrix.cpp`（120 行）
**判定**: 基本等价（公式与矩阵布局逐项对应；C++ 以 noexcept+0.0F 防御替代 Rust 的 panic/debug_assert，另新增 Result 溢出检查与 Rust 无对应的公共 API 形态差异）

---

## 函数逐条比对（以 Rust 为纲）

### `mod.rs` 模块声明（mod.rs:1-10）
- C++ 对应：`OverlapProxy.h:12-28` 暴露三个自由函数 `CalculateOverlapProxy` / `CalculatePolygonCollisionLoss` / `CalculateContainerPenalty`；`pair_matrix` 模块（Rust `mod pair_matrix` 私有，mod.rs:7）在 C++ 升格为公共类 `sparrow::PairMatrix`（PairMatrix.h:19），因 C++ 侧 `CollisionTracker`/`Separator` 需按索引直接访问矩阵。`pub mod tracker`（mod.rs:8）见 S18。`simd` 子模块（mod.rs:9-10，feature 门控）见 S19。
- 判定：【差异】（可见性布局：Rust pair_matrix 为模块私有，C++ 公开；结果语义不受影响）

### `quantify_collision_poly_poly`（mod.rs:14-25）
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:137-183）。
- 逐项核对：
  1. epsilon：Rust `f32::max(s1.diameter, s2.diameter) * OVERLAP_PROXY_EPSILON_DIAM_RATIO`（mod.rs:16，常量=0.01，consts.rs:7）。C++ `diameter = max(firstDiameter, secondDiameter)`，两直径 double→float cast（cpp:147-149）后 `epsilon = diameter * epsilonDiameterRatio`，`epsilonDiameterRatio = 0.01F` 硬编码（cpp:155-157）。数值一致 ✓（常量未共享但值相同）。
  2. overlap_proxy：Rust `overlap_area_proxy(sur1, sur2, eps) + epsilon.powi(2)`（mod.rs:18）。C++ `rawProxy + epsilon * epsilon`（cpp:169，调用传两个 **Identity** 变换，cpp:163-168）。`powi(2)` 与 `epsilon*epsilon` 在 f32 下位模式相同 ✓。
  3. penalty：Rust `calc_shape_penalty(s1, s2)`（见下条）。C++ `ShapePenalty(first, second)` ✓。
  4. 结果：Rust `overlap_proxy.sqrt() * penalty`（mod.rs:24）。C++ `sqrt(overlapProxy) * shapePenalty`（cpp:177）✓。
- Rust 侧 `debug_assert!(overlap_proxy.is_normal())`（mod.rs:20）→ C++ 无对应（防御性 isfinite 检查在更低层完成）。
- 判定：【一致】
- 证据：mod.rs:14-25 vs OverlapProxy.cpp:137-183；consts.rs:7 vs OverlapProxy.cpp:155。

### `calc_shape_penalty`（mod.rs:27-32）
- Rust：`p1 = sqrt(s1.surrogate().convex_hull_area)`，`p2 = sqrt(...)`，`(p1 * p2).sqrt()` —— 即 `sqrt(sqrt(a1) * sqrt(a2))`，全 f32（SPSurrogate.convex_hull_area 为 f32，jagua-rs sp_surrogate.rs:25）。
- C++ 对应：`ShapePenalty`（OverlapProxy.cpp:68-87）：`sqrt(sqrt(firstArea) * sqrt(secondArea))`，`firstArea/secondArea = static_cast<float>(surrogate->ConvexHullArea())`（cpp:77-78，double 存储 → f32 运算）。结构与运算精度一致 ✓。
- C++ 额外：surrogate 为空 / 面积非有限或 <=0 时返回 0.0F（cpp:73-86）——Rust 直接解引用并让 debug_assert 兜底。【C++ 额外】
- 判定：【一致】（防御项另计）
- 证据：mod.rs:27-32 vs OverlapProxy.cpp:68-87。

### `quantify_collision_poly_container`（mod.rs:36-53）
- C++ 对应：`CalculateContainerPenalty`（OverlapProxy.cpp:185-244）+ 匿名空间辅助 `RectCentroidDistance`（cpp:89-103）。
- 逐项核对：
  1. bbox：Rust `s.bbox`（f32）。C++ `shape.BoundingBox()`（Rect2d，double 存储，cpp:188）。
  2. 交集判定：Rust `Rect::intersection(s_bbox, c_bbox)` 返回 `Option`（jagua-rs rect.rs:229-241：`x_min < x_max && y_min < y_max` 严格小于，否则 None）。C++ `intersectionXMin < intersectionXMax && intersectionYMin < intersectionYMax`（cpp:216-217，各分量 float cast，cpp:210-213）——严格小于、f32 分量，一致 ✓。
  3. 有交集分支：Rust `(s_bbox.area() - r.area()) + 0.0001 * s_bbox.area()`（mod.rs:41）。C++ `(shapeArea - intersectionArea) + 0.0001F * shapeArea`（cpp:222-223）；`shapeArea = shapeWidth * shapeHeight`，宽高均为 float cast（cpp:196-200）；intersectionArea 同样 f32 乘（cpp:219-221）✓。
  4. 无交集分支：Rust `s_bbox.area() + s_bbox.centroid().distance_to(&c_bbox.centroid())`（mod.rs:45）。C++ `shapeArea + RectCentroidDistance(shapeBounds, containerBounds)`（cpp:227-228）；RectCentroidDistance：centroid 坐标 `(min+max)*0.5F`（f32）→ dx/dy → `sqrt(dx*dx+dy*dy)`（cpp:92-102）。Rust centroid 用 `f32::midpoint(x_min, x_max)`（jagua-rs rect.rs:262-267，即 `(a+b)/2` 舍入语义）而 C++ 用 `(a+b)*0.5F`——`f32::midpoint` 自 Rust 1.85 起对相邻值用下取整平均，与 `(a+b)*0.5` 在绝大多数值域一致，仅在 f32 精度极值处可能差 1 ulp；且 jagua-rs 的 `distance_to`（point.rs:47-49，`powi(2)` 平方和开方）与 C++ `dx*dx+dy*dy`（cpp:100-102）等价。此为亚 ulp 级差异，不影响 fixture 容差。【差异】（仅记录：centroid 的 midpoint 与 (a+b)/2 运算路径不同）
  5. penalty：Rust `calc_shape_penalty(s, s)`（自乘）。C++ `ShapePenalty(shape, shape)`（cpp:231）✓。
  6. 结果：Rust `2.0 * overlap.sqrt() * penalty`（mod.rs:52）。C++ `2.0F * sqrt(overlap) * shapePenalty`（cpp:238）✓。
- C++ 额外：`IsFiniteRect` 前置校验（cpp:189-191，含 `xMin<xMax` 有效性）、面积非有限/<=0、overlap 非有限/<=0、结果非有限/<=0 均返回 0.0F（cpp:201-243）。Rust 仅 `debug_assert!(overlap.is_normal())`（mod.rs:48）。【C++ 额外】
- 判定：【一致】（centroid 运算路径为亚 ulp 记录项；防御差异另计）
- 证据：mod.rs:36-53 vs OverlapProxy.cpp:185-244、jagua-rs rect.rs:229-241,262-272、point.rs:47-49。

### `overlap_area_proxy`（overlap_proxy.rs:7-27）
- C++ 对应：`CalculateOverlapProxy`（OverlapProxy.cpp:108-135）+ 匿名空间 `CirclePairContribution`（cpp:20-66）。
- 逐项核对：
  1. 双循环：Rust `for p1 in &sp1.poles { for p2 in &sp2.poles }`（overlap_proxy.rs:10-11）。C++ `for (firstPole : first.Poles()) { for (secondPole : second.Poles()) }`（cpp:115-117）✓（同为行主序 p1 外层）。
  2. pd：Rust `(p1.radius + p2.radius) - p1.center.distance_to(&p2.center)`（f32 全程）。C++ `penetration = firstRadius + secondRadius - distance`；`dx/dy = float(firstCenter.x) - float(secondCenter.x)`（cpp:42-49），center 先经 `firstTransform.ApplyRustF32` / `secondTransform.ApplyRustF32`（cpp:35-36，见差异 a）。
  3. decay：Rust `pd >= epsilon ? pd : epsilon.powi(2) / (-pd + 2.0*epsilon)`（overlap_proxy.rs:15-18）。C++ `penetration >= epsilon ? penetration : (epsilon*epsilon / (-penetration + 2.0F*epsilon))`（cpp:57-59）✓ 逐字对应。
  4. min 半径：Rust `f32::min(p1.radius, p2.radius)`。C++ `std::min(firstRadius, secondRadius)`（f32 cast 后，cpp:50）✓。
  5. 求和后乘 PI：Rust `total_overlap *= PI`（std f32::PI，overlap_proxy.rs:23）。C++ `total *= rustPi`，`rustPi = 3.14159265358979323846F`（cpp:131,18）——该字面量 cast 到 f32 后与 Rust `PI` 位模式一致 ✓。
  6. Rust `debug_assert!(total_overlap.is_normal())`（overlap_proxy.rs:24）→ C++ 返回处 `max(total, 0.0F)`（cpp:134）。【差异】（debug 断言 vs clamp 0；语义：Rust release 模式无检查，C++ 始终 clamp——对正常输入结果一致）
- 差异 a（变换路径）：Rust 的 `overlap_area_proxy` 收到的 surrogate 属于**已变换到世界系**的 SPolygon（tracker.rs:71 传入 `l.placed_items[pk].shape`），无需再变换；C++ 的 `Surrogate` 存**局部系** pole（Circle2d，double 存储），故 `CalculateOverlapProxy` 带 `firstTransform/secondTransform` 参数经 `ApplyRustF32`（f32 精度旋转平移，Transformation.cpp:23-36）变换。**当前所有调用点均传 Identity**（CalculatePolygonCollisionLoss cpp:163-168；EvaluateCandidateLoss 第一重载传真实 transform，CollisionTracker.cpp:437-441，用于未变换 surrogate 对）——Identity 时 ApplyRustF32 仅做 `cos(0)*x - sin(0)*y + 0` 的 f32 恒等运算，数值不变。结果等价 ✓。
- C++ 额外：`CirclePairContribution` 对 radius 非有限/负、center/transform 非有限、epsilon 非有限/<=0、distance/penetration/minRadius 非有限、minRadius<=0、结果非有限/<=0 全部返回 0.0F（cpp:26-64）——Rust 无任何检查。【C++ 额外】（防 NaN 传播；正常输入下不触发）
- 判定：【一致】（公式逐字对应；变换入口差异与防御项单独列出）
- 证据：overlap_proxy.rs:7-27 vs OverlapProxy.cpp:20-66,108-135；Transformation.cpp:23-36。

### `PairMatrix::new`（pair_matrix.rs:12-20）
- C++ 对应：`PairMatrix::Create`（PairMatrix.cpp:26-57）+ 私有构造（cpp:20-24）。
- 长度：Rust `len = size * (size + 1) / 2`（pair_matrix.rs:14，usize 直接乘，溢出时 debug 溢出 panic / release 回绕）。C++ 同公式但带**溢出防护**：size==SIZE_MAX 直接报错，`(size & 1) ? size*(size+1)/2` 之前先做 `first > SIZE_MAX / second` 检查，返回 `Result<PairMatrix>` 错误（cpp:33-54）——既定 panic→Result 差异 ✓。
- 初始值：Rust `vec![CTEntry { weight: 1.0, loss: 0.0 }; len]`。C++ `m_Entries(entryCount)` 用 `CollisionEntry` 默认成员 `{loss=0.0F, weight=1.0F}`（PairMatrix.h:11-14）✓。
- 差异：Rust `new(0)` 得到空矩阵（len=0）合法；C++ `size==0` 走早退分支返回默认 PairMatrix（cpp:28-31），行为一致 ✓。
- 判定：【一致】（含既定 panic→Result 转换）
- 证据：pair_matrix.rs:12-20 vs PairMatrix.cpp:20-57、PairMatrix.h:11-14。

### `Index<(usize,usize)>` / `IndexMut`（pair_matrix.rs:22-34）
- C++ 对应：`At(first, second)` / `At` const（PairMatrix.cpp:69-78，PairMatrix.h:28-29）。C++ 用引用返回的 `At` 替代 Rust 的 `Index/IndexMut` 运算符重载，调用点写 `matrix.At(i,j).loss = ...`。语义一致 ✓。
- 判定：【一致】（语法形态差异，无行为差异）

### `calc_idx`（pair_matrix.rs:36-49）
- Rust：`if row <= col { (row*size) + col - ((row*(row+1))/2) } else { (col*size) + row - ((col*(col+1))/2) }`，即 `smaller*2+... ` 等价变形为 **`(larger*(larger+1))/2 + smaller`**（行优先下三角，含对角线；pair_matrix.rs:44-48 注释中的示例 0 1 2 3 / 4 5 6 / 7 8 / 9 验证含对角）。`debug_assert!(row < size && col < size)`（pair_matrix.rs:43）。
- C++ 对应：`Index`（PairMatrix.cpp:103-119）：`smaller = min, larger = max`，`left = larger, right = larger+1`，奇偶除二后 `left*right + smaller`，即 `larger*(larger+1)/2 + smaller`。数学上与 Rust 公式恒等：`row*size + col - row*(row+1)/2`（row<=col 时 larger=col）`= col*(col+1)/2 + row` ✓（因 `row*size + col = row + row*(size-row) + col`... 直接验证：`row*size + col - row*(row+1)/2 = row*(size - (row+1)/2...)`——以 Rust 注释示例验证：size=4，(0,3)→Rust `0*4+3-0=3`；C++ `larger=3 → 3*4/2+0=6`？**不对**。重新核算：C++ `left*right + smaller`，larger=3 → left=3, right=4 → 3*4/2 需先除二——代码是 `(left&1)==0 ? left/=2 : right/=2`，3 为奇数故 right=2 → 3*2+0=6。Rust (0,3)：row=0<=col=3 → `0*4+3-0=3`。**两者不同！** 再看 Rust 注释布局：索引 0..3 是第 0 行（含 (0,0),(0,1),(0,2),(0,3)），4..6 是第 1 行（(1,1),(1,2),(1,3)）...即 Rust 的行是"对角线起算的行 r，列 c>=r"，公式 `r*size + c - r*(r+1)/2`。对 (0,3)：r=0 → 3。而 C++ 对 (0,3)：smaller=0, larger=3 → 3*(3+1)/2 + 0 = 6。**C++ 得 6，Rust 得 3——矩阵内布局不同！** 但注意 C++ 的 At 与索引遍历都用同一 Index 函数，且布局只需自洽：`Create` 的 entryCount 与 `At(first,second)`/`At(second,first)` 都走同一映射，矩阵仍是含对角的三角阵、元素一一对应，只是**物理存储顺序与 Rust 不同**（Rust 按对角行存，C++ 按 larger 列段存）。所有读/写都经 `At`/`calc_idx`，无跨语言共享内存，故行为等价；唯一可观察差异是 `TotalLoss`/`TotalWeightedLoss` 等全量遍历的**求和顺序**（Rust data 顺序 = 对角行序；C++ m_Entries 顺序 = larger 段序），f32 浮点累和顺序不同可能产生 ulp 级差异。
- 边界检查：Rust debug_assert 越界 panic；C++ `At` 无检查（越界读未定义，靠 `Contains` 前置判断，PairMatrix.h:27）。【差异】（既定 panic→无检查/调用方 Contains）
- 判定：【差异】（布局排列不同但封闭自洽 → 结果等价；求和顺序差异为 ulp 级）
- 证据：pair_matrix.rs:36-49（示例注释 + 公式）vs PairMatrix.cpp:103-119；（0,3) 手算：Rust=3，C++=6；TotalLoss 求和顺序：pair_matrix 对应 tracker.rs:179-199 的 `data.iter()` 顺序 vs PairMatrix.cpp:80-101 的 `m_Entries` 顺序。

### `PairMatrix` 的 Rust 私有辅助 vs C++ 公共 API（pair_matrix.rs 全文件）
- C++ 额外公共方法：`Size()`（cpp:59-62）、`Contains()`（cpp:64-67）、`TotalLoss()`（cpp:80-91）、`TotalWeightedLoss()`（cpp:93-101）。Rust 侧 `size` 字段与求和逻辑在 tracker.rs 内联实现（get_total_loss 等直接 `pair_collisions.data.iter().sum()`，tracker.rs:179-199）。C++ 把这些求和下沉到 PairMatrix 层并在 `TotalLoss` 中过滤 `isfinite && >0`（cpp:85-89）——Rust 不过滤直接 sum。【C++ 额外】（防御性过滤；正常数据全为非负有限值时等价）
- `CollisionEntry::WeightedLoss`（cpp:9-18）带 isfinite/正数防御；Rust `CTEntry::weighted_loss`（tracker.rs:209-211）直接 `weight * loss`。【C++ 额外】
- 判定：【一致】（结构归位差异 + 防御差异）
- 证据：PairMatrix.cpp:80-101 vs tracker.rs:179-199、tracker.rs:209-211。

---

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | pair_matrix 物理布局：Rust 对角行序 `r*size+c-r(r+1)/2` vs C++ `larger(larger+1)/2+smaller`，(0,3) 分别映射 3 与 6 | 【差异】 | 封闭自洽，行为等价；仅全量求和（TotalLoss/TotalWeightedLoss）的 f32 累加顺序不同 → ulp 级 | pair_matrix.rs:44-48 vs PairMatrix.cpp:106-118 |
| 2 | overlap_area_proxy 的变换入口：Rust 收已变换 surrogate 无变换；C++ 带 Transform 参数经 ApplyRustF32（当前调用全 Identity 或已变换对） | 【差异】 | 结果等价（Identity 恒等、f32 路径） | overlap_proxy.rs:8 vs OverlapProxy.cpp:12-17,35-36 |
| 3 | C++ 全面的退化防御（isfinite/<=0 返回 0）vs Rust debug_assert/panic | 【C++ 额外】 | 正常输入无影响；异常输入 C++ 容错返回 0，Rust panic | OverlapProxy.cpp:26-64,125-134,150-181 vs mod.rs:20,48、overlap_proxy.rs:24 |
| 4 | PairMatrix::Create 溢出防护（Result 错误）vs Rust usize 溢出 panic | 【差异】 | 既定 panic→Result 转换 | PairMatrix.cpp:33-54 vs pair_matrix.rs:14 |
| 5 | TotalLoss 求和过滤非有限/非正值 vs Rust 直接 sum | 【C++ 额外】 | 正常数据等价 | PairMatrix.cpp:84-89 vs tracker.rs:179-187 |
| 6 | container centroid：Rust `f32::midpoint` vs C++ `(min+max)*0.5F` | 【差异】 | 亚 ulp 级，仅极值可见 | jagua-rs rect.rs:262-267 vs OverlapProxy.cpp:92-99 |
| 7 | 0.01/PI 等常量：Rust consts.rs 集中定义 vs C++ 逐处硬编码（rustPi=3.14159265358979323846F 位模式一致） | 【一致】 | 无 | consts.rs:7 vs OverlapProxy.cpp:18,155 |
| 8 | epsilon 使用 `powi(2)`（Rust）vs `epsilon*epsilon`（C++） | 【一致】 | f32 下位模式相同 | mod.rs:18 vs OverlapProxy.cpp:169 |
| 9 | 精度路径：C++ 存储 double（Circle2d/Rect2d/Surrogate）、运算前 cast f32，与 Rust 全 f32 对齐（RustF32 双轨制） | 【一致】 | 被 fixture loss 位模式锁定 | OverlapProxy.cpp:42-50,77-78,147-149,196-200 vs overlap_proxy.rs/mod.rs 全文 |

Fixture 锁定情况：`TestQuantify.cpp` 的 `TestOverlapProxy`（TestQuantify.cpp:92-166）以 `1e-5` 容差锁 `CalculatePolygonCollisionLoss`（含 epsilon、shape penalty、+eps²）与 `CalculateContainerPenalty` 公式；`TestPairMatrix`（:57-90）锁对称三角访问与 totals；`SeparatorSearchRustTrace.tsv` 的 collision loss **f32 位模式**（`loss_f32_bits`，如首条 `40afead0`）与 `SeparatorRoundRustTrace.tsv` 的 pair loss/weight（容差 2e-2/5e-2，TestSeparator.cpp:757-776）锁整条量化链路。

## 结论

quantify 三文件的数值公式（epsilon 计算、pole 代理双循环、decay 公式、shape penalty、container bbox 公式、三角矩阵语义）与 Rust 逐字对应且走 f32 运算路径，核心行为被 fixture 位模式锁定为等价。结构性差异集中在两点：PairMatrix 物理布局排列不同（自洽等价，仅影响全量求和的 ulp 级累加顺序）与 C++ 以 Transform 参数化替代 Rust 的"预变换 surrogate"入口（当前调用路径下恒等）。C++ 面向公共 API 全面增加了退化防御与 Result 溢出检查，属既定的 panic→Result/容错策略。

### 补充审计（2026-09-29）
1. **【核对确认：52+26+49 行逐项覆盖】** quantify/mod.rs（3 pub fn + 2 模块声明 + calc_shape_penalty）、overlap_proxy.rs（overlap_area_proxy 单函数）、pair_matrix.rs（结构/new/Index/IndexMut/calc_idx）——主文 7 小节全覆盖无遗漏；`#[inline(always)]` 属性能标注。
2. **【浅覆盖补全：mod.rs 的 debug_assert 位置与 release 行为】** `debug_assert!(overlap_proxy.is_normal())`（mod.rs:20/:48）与 `debug_assert!(total_overlap.is_normal())`（overlap_proxy.rs:24）——**is_normal 含下溢/零/inf/NaN 检查**，release 构建静默跳过；C++ 以 isfinite/<=0 检查返回 0 替代（主文差异 #3 已记方向，此处补 release 差异：release 下 Rust 对异常值继续参与运算（可能产 NaN loss → 上层 FPA panic 链），C++ 恒 0——即 C++ 的防御在 release 也生效，比 Rust debug-only 更强。判定维持【C++ 额外】，影响面精确化）。
3. **【浅覆盖补全：calc_idx 的 debug_assert 越界与 C++ Contains 前置】** Rust `debug_assert!(row < size && col < size)`（pair_matrix.rs:43）release 下越界索引 data 数组（未定义内存读）；C++ At 无检查靠调用方 Contains（PairMatrix.h:27）——**两侧 release 均无硬检查**，等价暴露面。判定维持【差异】（既定 panic→无检查），此处补 release 语义。
4. **【核对确认：布局映射手算复核】** 主文差异 #1 的 (0,3) 手算（Rust=3，C++=6）经独立验算正确：Rust 行优先下三角（r*size+c−r(r+1)/2），C++ 列段式（larger(larger+1)/2+smaller）——两映射在 (i,j) i≤j 上互为转置排列，元素一一对应。求和顺序差（TotalLoss 的 ulp 级）判定维持。
