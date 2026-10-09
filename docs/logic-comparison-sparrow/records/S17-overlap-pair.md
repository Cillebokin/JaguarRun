# S17. quantify 量化公式（overlap_proxy + pair_matrix + mod） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/mod.rs`（53 行）+ `quantify/overlap_proxy.rs`（27 行）+ `quantify/pair_matrix.rs`（49 行）
**C++**: `SparrowCore/Include/SparrowCore/Quantify/OverlapProxy.h`（29 行）+ `Source/Quantify/OverlapProxy.cpp`（245 行）+ `Include/SparrowCore/Quantify/PairMatrix.h`（41 行）+ `Source/Quantify/PairMatrix.cpp`（120 行）
**判定**: 已闭合 PairMatrix 的索引映射、遍历/累加次序、profile-specific 越界与长度溢出语义；有效几何输入下的量化公式仍有独立的 midpoint/防御策略差异，不能据此宣称整体逐位等价

---

## 函数逐条比对（以 Rust 为纲）

### `mod.rs` 模块声明（mod.rs:1-10）
- C++ 对应：`OverlapProxy.h:12-28` 暴露三个自由函数 `CalculateOverlapProxy` / `CalculatePolygonCollisionLoss` / `CalculateContainerPenalty`；`pair_matrix` 模块（Rust `mod pair_matrix` 私有，mod.rs:7）在 C++ 升格为公共类 `sparrow::PairMatrix`（PairMatrix.h:19），因 C++ 侧 `CollisionTracker`/`Separator` 需按索引直接访问矩阵。`pub mod tracker`（mod.rs:8）见 S18。`simd` 子模块（mod.rs:9-10，feature 门控）见 S19。
- 判定：【差异】（可见性布局：Rust pair_matrix 为模块私有，C++ 公开；结果语义不受影响）

### `quantify_collision_poly_poly`（mod.rs:14-25）
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:137-183）。
- 逐项核对：
  1. epsilon：Rust `f32::max(s1.diameter, s2.diameter) * OVERLAP_PROXY_EPSILON_DIAM_RATIO`（mod.rs:16，常量=0.01，consts.rs:7）。C++ `diameter = max(firstDiameter, secondDiameter)`，两直径 double→float cast（cpp:147-149）后 `epsilon = diameter * epsilonDiameterRatio`，`epsilonDiameterRatio = 0.01F` 硬编码（cpp:155-157）。数值一致 ✓（常量未共享但值相同）。
  2. overlap_proxy：Rust `overlap_area_proxy(sur1, sur2, eps) + epsilon.powi(2)`（mod.rs:18）。C++ `rawProxy + epsilon * epsilon`（cpp:169，调用传两个 **Identity** 变换，cpp:163-168）。两者在源代码层面都对 f32 epsilon 求平方后相加；本记录没有独立测试证明不同编译器/优化选项下的运算逐位一致。
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
- API 边界：C++ 公共声明现在要求调用方显式传入 `epsilon`（OverlapProxy.h:12-17），与 Rust `overlap_area_proxy` 的调用契约一致。C++ tracker-only 扩展仍用裸代理值，但其 `0.01F` 已在调用点显式指定，不再靠 API 默认值。
- 逐项核对：
  1. 双循环：Rust `for p1 in &sp1.poles { for p2 in &sp2.poles }`（overlap_proxy.rs:10-11）。C++ `for (firstPole : first.Poles()) { for (secondPole : second.Poles()) }`（cpp:115-117）✓（同为行主序 p1 外层）。
  2. pd：Rust `(p1.radius + p2.radius) - p1.center.distance_to(&p2.center)`（f32 全程）。C++ `penetration = firstRadius + secondRadius - distance`；`dx/dy = float(firstCenter.x) - float(secondCenter.x)`（cpp:42-49），center 先经 `firstTransform.ApplyRustF32` / `secondTransform.ApplyRustF32`（cpp:35-36，见差异 a）。
  3. decay：Rust `pd >= epsilon ? pd : epsilon.powi(2) / (-pd + 2.0*epsilon)`（overlap_proxy.rs:15-18）。C++ `penetration >= epsilon ? penetration : (epsilon*epsilon / (-penetration + 2.0F*epsilon))`（cpp:57-59）✓ 逐字对应。
  4. min 半径：Rust `f32::min(p1.radius, p2.radius)`。C++ `std::min(firstRadius, secondRadius)`（f32 cast 后，cpp:50）✓。
  5. 求和后乘 PI：Rust `total_overlap *= PI`（std f32::PI，overlap_proxy.rs:23）。C++ `total *= rustPi`，`rustPi = 3.14159265358979323846F`（cpp:131,18）；该 f32 常量与 Rust 的 f32 PI 数值相同，乘法结果仍取决于此前累加值与编译器浮点选项。
  6. Rust `debug_assert!(total_overlap.is_normal())`（overlap_proxy.rs:24）→ C++ 返回处 `max(total, 0.0F)`（cpp:134）。【差异】（debug 断言 vs clamp 0；语义：Rust release 模式无检查，C++ 始终 clamp——对正常输入结果一致）
- 差异 a（变换路径）：Rust 的 `overlap_area_proxy` 收到的 surrogate 属于**已变换到世界系**的 SPolygon（tracker.rs:71 传入 `l.placed_items[pk].shape`），无需再变换；C++ 的 `Surrogate` 存**局部系** pole（Circle2d，double 存储），故 `CalculateOverlapProxy` 带 `firstTransform/secondTransform` 参数经 `ApplyRustF32`（f32 精度旋转平移，Transformation.cpp:23-36）变换。**当前碰撞量化调用点传 Identity**（CalculatePolygonCollisionLoss；`EvaluateCandidateLoss` tracker-only overload 传真实 transform，CollisionTracker.cpp:534-585，用于未变换 surrogate 对）——Identity 时 ApplyRustF32 仅做 `cos(0)*x - sin(0)*y + 0` 的 f32 恒等运算，数值不变。结果等价 ✓。
- C++ 额外：`CirclePairContribution` 对 radius 非有限/负、center/transform 非有限、epsilon 非有限/<=0、distance/penetration/minRadius 非有限、minRadius<=0、结果非有限/<=0 全部返回 0.0F（cpp:26-64）——Rust 无任何检查。【C++ 额外】（防 NaN 传播；正常输入下不触发）
- 判定：【一致】（公式逐字对应；变换入口差异与防御项单独列出）
- 证据：overlap_proxy.rs:7-27 vs OverlapProxy.cpp:20-66,108-135；Transformation.cpp:23-36。

### `PairMatrix::new`（pair_matrix.rs:12-20）
- C++ 对应：`PairMatrix::Create`（PairMatrix.cpp:26-57）+ 私有构造（cpp:20-24）。
- 长度：Rust `len = size * (size + 1) / 2`（pair_matrix.rs:14，usize 直接乘，Debug 溢出 panic / Release 回绕）。C++ 现在按同一乘法顺序计算：Debug 对溢出抛 `std::overflow_error`，Release 使用 `size_t` 回绕后分配相应长度；`SIZE_MAX` 在 Release 得到 `size=SIZE_MAX`、空 entries。可观察失败/结果与 Rust 对齐，异常类型按语言映射。
- 初始值：Rust `vec![CTEntry { weight: 1.0, loss: 0.0 }; len]`。C++ `m_Entries(entryCount)` 用 `CollisionEntry` 默认成员 `{loss=0.0F, weight=1.0F}`（PairMatrix.h:11-14）✓。
- 差异：Rust `new(0)` 得到空矩阵（len=0）合法；C++ `size==0` 走早退分支返回默认 PairMatrix（cpp:28-31），行为一致 ✓。
- 判定：【profile 行为一致】（Debug 是 panic→`std::overflow_error` 映射；Release 的 `SIZE_MAX` 回绕结果相同）
- 证据：pair_matrix.rs:12-20 vs PairMatrix.cpp:20-57、PairMatrix.h:11-14。

### `Index<(usize,usize)>` / `IndexMut`（pair_matrix.rs:22-34）
- C++ 对应：`At(first, second)` / const `At`。C++ 用引用返回替代 Rust 的 `Index/IndexMut` 运算符重载；`At` 现在可抛出 bounds exception，不再声明 `noexcept`，并通过 `vector::at` 消除越界 UB。
- 判定：【一致映射】（语法形态不同，越界 profile 语义按下一节实现）

### `calc_idx`（pair_matrix.rs:36-49）
- Rust：对 `row <= col` 使用 `row*size + col - row*(row+1)/2`，反向时交换 row/col（pair_matrix.rs:44-48）。C++ 对合法索引使用等价的 row-start 计算，避免中间乘法不必要的溢出。size=4 的物理顺序为 `(0,0),(0,1),(0,2),(0,3),(1,1)...`，与 Rust 完全一致。
- 边界：Rust Debug 先由 `debug_assert!` 检查逻辑坐标；Release 去掉断言后仍按原公式索引 `Vec`，所以某些逻辑越界坐标可能别名到现有元素（size=4 的 `(4,0)` 读到 `(1,1)`），再由 Vec 长度检查剩余越界。C++ Debug 在逻辑越界时抛 `std::out_of_range`；Release 对逻辑越界坐标执行 Rust 的无符号展平公式并用 `vector::at` 检查 backing length，保留同一别名与最终越界边界。对 `(4,0)` 和 `(10,0)` 的 Debug/Release Rust/C++ 参考已覆盖。
- 总损失：C++ `TotalLoss`/`TotalWeightedLoss` 现在按 Rust data 顺序直接累加 loss 与 `weight * loss`，不再过滤 NaN/Inf/负值，也不再将非有限总和折成 0；float cancellation case 固定验证总和位于 Rust 行顺序。
- 判定：【已闭合】
- 证据：pair_matrix.rs:36-49 vs PairMatrix.cpp；Rust `pair_matrix_reference.rs` 与 C++ `TestQuantify.cpp` 的 N=4 累加、profile bounds 与 max-size case。

### `PairMatrix` 的 Rust 私有辅助 vs C++ 公共 API（pair_matrix.rs 全文件）
- C++ 额外公共方法：`Size()`、`Contains()`、`TotalLoss()`、`TotalWeightedLoss()`。Rust 在 tracker 内联求和；C++ 下沉到 PairMatrix，但求和次序和 IEEE 非有限值传播现与 Rust 对齐。
- `CollisionEntry::WeightedLoss` 现在直接返回 `weight * loss`，与 Rust `CTEntry::weighted_loss` 一致。
- 判定：【接口额外、有效运算语义一致】
- 证据：PairMatrix.cpp 与 tracker.rs:179-199,209-211。

---

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | pair_matrix 物理布局与总和次序：C++ 已改为 Rust 上三角行顺序；N=4 cancellation case 精确得到 Rust `16,777,218` | 【已闭合】 | tracker 全量求和不再因布局改变舍入 | pair_matrix.rs:36-49; pair_matrix_reference.rs; TestQuantify.cpp |
| 2 | overlap_area_proxy 的变换入口：Rust 收已变换 surrogate 无变换；C++ 带 Transform 参数经 ApplyRustF32（当前调用全 Identity 或已变换对） | 【差异】 | 结果等价（Identity 恒等、f32 路径） | overlap_proxy.rs:8 vs OverlapProxy.cpp:12-17,35-36 |
| 3 | C++ 全面的退化防御（isfinite/<=0 返回 0）vs Rust debug_assert/panic | 【C++ 额外】 | 正常输入无影响；异常输入 C++ 容错返回 0，Rust panic | OverlapProxy.cpp:26-64,125-134,150-181 vs mod.rs:20,48、overlap_proxy.rs:24 |
| 4 | PairMatrix 长度溢出：Debug 失败边界与 Release 回绕结果均按 Rust 算术复现，语言异常类型映射 | 【已闭合】 | SIZE_MAX Debug 抛出边界异常；Release 产生空 backing Vec，后续访问再 bounds-fail | PairMatrix.cpp; pair_matrix_reference.rs; TestQuantify.cpp |
| 5 | TotalLoss/TotalWeightedLoss 与 `WeightedLoss` 的非有限值过滤 | 【已闭合】 | NaN、±Inf 按 Rust 的原始浮点加法/乘法传播 | PairMatrix.cpp; tracker_weight_reference.rs; TestQuantify.cpp |
| 6 | container centroid：Rust `f32::midpoint` vs C++ `(min+max)*0.5F` | 【差异】 | 亚 ulp 级，仅极值可见 | jagua-rs rect.rs:262-267 vs OverlapProxy.cpp:92-99 |
| 7 | 0.01/PI 等常量：Rust consts.rs 集中定义 vs C++ 逐处硬编码（PI 的 f32 数值相同） | 【一致】 | 常量值对应；没有由此推导整段计算位级相同 | consts.rs:7 vs OverlapProxy.cpp:18,155 |
| 8 | epsilon 使用 `powi(2)`（Rust）vs `epsilon*epsilon`（C++） | 【源码对应】 | 都表示 f32 平方；未做跨工具链逐位断言 | mod.rs:18 vs OverlapProxy.cpp:169 |
| 9 | 精度路径：C++ 存储 double、部分运算前转 f32；Rust 量化使用 f32 | 【源码对应】 | trace 中虽存有 `loss_f32_bits` 字段，消费测试按容差比较，不能据此称为位级锁定 | OverlapProxy.cpp:42-50,77-78,147-149,196-200 vs overlap_proxy.rs/mod.rs |
| 10 | `CalculateOverlapProxy` epsilon 调用契约 | 【已闭合】 | C++ 现在要求显式 epsilon；扩展调用点显式传 `0.01F` | OverlapProxy.h:12-17 vs overlap_proxy.rs:7-8 |

Fixture 与断言：Rust `pair_matrix_reference.rs` 直接编译锁定的 Sparrow `pair_matrix.rs`，验证 N=4 物理顺序、loss fold、Debug/Release 越界和 `usize::MAX` 算术；C++ `TestQuantify.cpp` 验证 N=4 顺序敏感总和、profile-specific 越界、溢出和 NaN/Inf 传播。`TestOverlapProxy` 的几何损失仍是 C++ 侧容差断言，不是 Rust oracle；其他现有 separator trace 仍按原记录的容差比较。

## 结论

PairMatrix 的合法索引映射、物理遍历顺序、总损失 fold、profile-specific 越界和长度溢出行为已按当前锁定 Rust 基线对齐并有 Debug/Release 参考。量化几何仍不是逐位闭合：容器 centroid 的 `f32::midpoint` 与 C++ 算术路径不同，Rust debug assertion 与 C++ 退化防御也不同；这些剩余项应在 S12/数值边界工作中单独收敛。

### 补充审计（2026-09-29）
1. **【核对确认：53+27+49 行逐项覆盖】** quantify/mod.rs（3 pub fn + 2 模块声明 + calc_shape_penalty）、overlap_proxy.rs（overlap_area_proxy 单函数）、pair_matrix.rs（结构/new/Index/IndexMut/calc_idx）——主文 7 小节全覆盖；`#[inline(always)]` 属性能标注。
2. **【浅覆盖补全：mod.rs 的 debug_assert 位置与 release 行为】** `debug_assert!(overlap_proxy.is_normal())`（mod.rs:20/:48）与 `debug_assert!(total_overlap.is_normal())`（overlap_proxy.rs:24）——**is_normal 含下溢/零/inf/NaN 检查**，release 构建静默跳过；C++ 以 isfinite/<=0 检查返回 0 替代（主文差异 #3 已记方向，此处补 release 差异：release 下 Rust 对异常值继续参与运算（可能产 NaN loss → 上层 FPA panic 链），C++ 恒 0——即 C++ 的防御在 release 也生效，比 Rust debug-only 更强。判定维持【C++ 额外】，影响面精确化）。
3. **【2026-10-06 更正：越界访问语义】** 先前把 Rust release 越界索引写成未定义读，这是错误的：`Vec` 索引在 release 仍做边界检查并 panic，即使 `debug_assert!` 被移除。C++ `std::vector::operator[]` 越界则未定义行为；主文已区分两者。
4. **【核对确认：布局映射手算复核】** size=4 的 `(0,3)` 在 Rust 映射到 3、C++ 映射到 6；两种索引均由各自矩阵的本地访问函数闭合使用。区别是物理排列及遍历顺序，不共享内存布局。

### P0 复核更正（2026-10-09）

本节 2026-09-29 的第 3、4 条记录的是当时实现，不是当前状态：PairMatrix 现在采用 Rust 的上三角行顺序，size=4 的 `(0,3)` 两侧均落在物理槽位 3；Release 的非法逻辑坐标按 Rust unsigned flatten 算式访问并保留 alias，backing 越界由 checked access 失败；Debug 非法逻辑坐标由 Rust `debug_assert!` 与 C++ `std::out_of_range` 退出。`usize::MAX` 长度运算也有 Debug/Release Rust/C++ reference。当前结论以主文 PairMatrix 小节和 [共用错误分类表](../error-semantics.md) 为准，不能继续引用旧审计行作为现状差异。
