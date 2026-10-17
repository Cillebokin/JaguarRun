# S17. quantify 量化公式（overlap_proxy + pair_matrix + mod） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/mod.rs`（53 行）+ `quantify/overlap_proxy.rs`（27 行）+ `quantify/pair_matrix.rs`（49 行）
**C++**: `SparrowCore/Include/SparrowCore/Quantify/OverlapProxy.h`（32 行）+ `Source/Quantify/OverlapProxy.cpp`（192 行）+ `Include/SparrowCore/Quantify/PairMatrix.h`（41 行）+ `Source/Quantify/PairMatrix.cpp`（108 行）
**判定**: 已闭合 PairMatrix 的索引映射、遍历/累加次序、profile-specific 越界与长度溢出语义；`OverlapProxy` 对列明的非正规公式中间值、容器 centroid midpoint 溢出及缺失 surrogate 调用边界已有 Rust Debug/Release 对照。C++ 现复用 JaguarCore 的 `Rect2d::CentroidRustF32`；malformed 矩形防御和其他公式组合仍有差异，不能据此宣称整体逐位等价

---

## 函数逐条比对（以 Rust 为纲）

### `mod.rs` 模块声明（mod.rs:1-10）
- C++ 对应：`OverlapProxy.h:12-28` 暴露三个自由函数 `CalculateOverlapProxy` / `CalculatePolygonCollisionLoss` / `CalculateContainerPenalty`；`pair_matrix` 模块（Rust `mod pair_matrix` 私有，mod.rs:7）在 C++ 升格为公共类 `sparrow::PairMatrix`（PairMatrix.h:19），因 C++ 侧 `CollisionTracker`/`Separator` 需按索引直接访问矩阵。`pub mod tracker`（mod.rs:8）见 S18。`simd` 子模块（mod.rs:9-10，feature 门控）见 S19。
- 判定：【差异】（可见性布局：Rust pair_matrix 为模块私有，C++ 公开；结果语义不受影响）

### `quantify_collision_poly_poly`（mod.rs:14-25）
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:111-140）。
- 逐项核对：
  1. epsilon：Rust `f32::max(s1.diameter, s2.diameter) * OVERLAP_PROXY_EPSILON_DIAM_RATIO`（mod.rs:16，常量=0.01，consts.rs:7）。C++ `diameter = max(firstDiameter, secondDiameter)`，两直径 double→float cast（cpp:116-122）后乘 `0.01F`。数值公式对应 ✓（常量未共享但值相同）。
  2. overlap_proxy：Rust `overlap_area_proxy(sur1, sur2, eps) + epsilon.powi(2)`（mod.rs:18）。C++ `rawProxy + epsilon * epsilon`（cpp:124-130，调用传两个 **Identity** 变换）。两者在源代码层面都对 f32 epsilon 求平方后相加；本记录没有独立测试证明不同编译器/优化选项下的运算逐位一致。
  3. penalty：Rust `calc_shape_penalty(s1, s2)`（见下条）。C++ `ShapePenalty(first, second)` ✓。
  4. 结果：Rust `overlap_proxy.sqrt() * penalty`（mod.rs:24）。C++ `sqrt(overlapProxy) * shapePenalty`（cpp:137）✓。
- Rust 侧 `debug_assert!(overlap_proxy.is_normal())`（mod.rs:20）→ C++ Debug 在相同公式边界抛 `std::domain_error("assertion failed: overlap_proxy.is_normal()")`，Release 不加检查并继续 IEEE 运算。较早的 `CirclePairContribution`/结果归零防御已移除，按 S12 所列 reference 对照。
- 缺失 surrogate：Rust 先算 epsilon，再按 first/second 次序调用 `surrogate()`；C++ 在相同阶段依次要求两个 surrogate。Rust 的无条件 `expect` panic 对应 C++ 两个 profile 都抛 `std::logic_error("surrogate not generated")`。Rust/C++ 用例分别覆盖缺 first、缺 second；调用不修改 polygon 状态。
- 判定：【一致】
- 证据：mod.rs:14-25 vs OverlapProxy.cpp:106-139；consts.rs:7 vs OverlapProxy.cpp:120-123。

### `calc_shape_penalty`（mod.rs:27-32）
- Rust：`p1 = sqrt(s1.surrogate().convex_hull_area)`，`p2 = sqrt(...)`，`(p1 * p2).sqrt()` —— 即 `sqrt(sqrt(a1) * sqrt(a2))`，全 f32（SPSurrogate.convex_hull_area 为 f32，jagua-rs sp_surrogate.rs:25）。
- C++ 对应：`ShapePenalty`（OverlapProxy.cpp:57-65）：`sqrt(sqrt(firstArea) * sqrt(secondArea))`，`firstArea/secondArea` 转为 f32 后运算。结构与运算顺序对应 ✓。
- 缺失 surrogate：`SPolygon::new` 与 `SimplePolygon::Create` 都允许先创建有效 polygon、稍后单独生成 surrogate。Rust accessor 的 `expect("surrogate not generated")` 无条件 panic；C++ `RequireSurrogate` 在缺指针时抛 `std::logic_error`，不再以 `0.0F` 吞掉错误。polygon-polygon 的 first/second 缺失和 polygon-container 缺失均有 Debug/Release 配对用例；非有限或非正面积仍按 IEEE 公式计算。
- 判定：【一致】（防御项另计）
- 证据：mod.rs:27-32 vs OverlapProxy.cpp:46-58。

### `quantify_collision_poly_container`（mod.rs:36-53）
- C++ 对应：`CalculateContainerPenalty`（OverlapProxy.cpp:142-192）+ 匿名空间辅助 `RectCentroidDistance`（cpp:67-79）。
- 逐项核对：
  1. bbox：Rust `s.bbox`（f32）。C++ `shape.BoundingBox()`（Rect2d，double 存储，cpp:144）。
  2. 交集判定：Rust `Rect::intersection(s_bbox, c_bbox)` 返回 `Option`（jagua-rs rect.rs:229-241：`x_min < x_max && y_min < y_max` 严格小于，否则 None）。C++ `intersectionXMin < intersectionXMax && intersectionYMin < intersectionYMax`（cpp:172-173，各分量先转 f32）——严格小于、f32 分量，一致 ✓。
  3. 有交集分支：Rust `(s_bbox.area() - r.area()) + 0.0001 * s_bbox.area()`（mod.rs:41）。C++ `(shapeArea - intersectionArea) + 0.0001F * shapeArea`（cpp:178-179）；`shapeArea = shapeWidth * shapeHeight`（cpp:150-156）；intersectionArea 同样 f32 乘（cpp:175-177）✓。
  4. 无交集分支：Rust `s_bbox.area() + s_bbox.centroid().distance_to(&c_bbox.centroid())`（mod.rs:45）。C++ `shapeArea + RectCentroidDistance(shapeBounds, containerBounds)`（cpp:183-184）；centroid 现经 `Rect2d::CentroidRustF32` 将 f32 端点提升到 double 求平均，再转回 f32；大正端点和溢出的 fixture 中，两侧质心位模式匹配。dx/dy、平方和与开方结构对应，具体 profile 边界见 #6 fixture。【已修复并定向验证】
  5. penalty：Rust `calc_shape_penalty(s, s)`（自乘）。C++ `ShapePenalty(shape, shape)`（cpp:189）✓。
  6. 结果：Rust `2.0 * overlap.sqrt() * penalty`（mod.rs:52）。C++ `2.0F * sqrt(overlap) * shapePenalty`（cpp:190）✓。
- 缺失 surrogate：对有效 finite bbox 且 overlap 通过 Rust Debug `is_normal()` 守卫的 polygon，Rust 在 `calc_shape_penalty` 的 `surrogate().expect(...)` panic；C++ 在对应 `ShapePenalty` 阶段抛 `std::logic_error`。Debug/Release 均有配对用例且失败前后 shape 均未改变。C++ `IsFiniteRect` 对 malformed bbox 的更早防御出口仍是另一项表示边界，不由该用例覆盖。
- C++ 保留 `IsFiniteRect` 对非有限矩形/包围盒的入口防御（cpp:9-17），但已移除 `shapeArea` 非有限/非正前置守卫。Rust/C++ reference 证明：有效有限方形经有限平移后，bbox 宽高均为 `2^64`，f32 面积溢出为 `+Inf`；旧守卫返回 `0`，与 Rust Debug 的 `overlap.is_normal()` 失败及 Release `+Inf` 均不符。删除后，C++ Debug/Release 分别对应 Rust 断言边界和 `+Inf` 结果。负面积对有效有序 bbox 不可达。另有固定候选的 bbox 宽高均正、面积乘积为 `+0`，Rust 鞋带面积为最小正 subnormal；Rust `SPolygon::new` 在 POI 的退化 `Rect::quadrants` unwrap 处 panic，C++ `SimplePolygon::Create` 返回 `InvalidPolygon`，两边均未把该候选交给损失 helper。41,761 个有界候选也未找到 Rust 构造成功，但这不能证明所有成功构造/变换几何都不可达；Rust panic 与 C++ 错误返回也不是同一构造出口。完全塌缩某一 bbox 轴的变换会先在 Rust `Rect::try_new(...).unwrap()` 处 panic，C++ `TransformedRustF32` 则返回错误，不会把形状传入损失 helper。Rust 可通过公开字段突变构造 malformed 零/负 bbox，已有 Debug/Release Rust 单侧 reference：零面积为 Debug panic/Release `+0`，负面积状态两 profile 为 NaN；C++ `SimplePolygon` 不暴露可写 bbox，不能把这些状态当作成对 API 输入。无效矩形/包围盒防御也仍未与 Rust malformed 公共字段状态配成等价 API 输入。
- 判定：【公式分支按已测 profile 对齐】（centroid 运算路径与输入/表示防御仍单列）
- 证据：mod.rs:36-53 vs OverlapProxy.cpp:9-17,141-194、jagua-rs rect.rs:229-241,262-272、point.rs:47-49。

### `overlap_area_proxy`（overlap_proxy.rs:7-27）
- C++ 对应：`CalculateOverlapProxy`（OverlapProxy.cpp:84-109）+ 匿名空间 `CirclePairContribution`（cpp:21-44）。
- API 边界：C++ 公共声明现在要求调用方显式传入 `epsilon`（OverlapProxy.h:12-17），与 Rust `overlap_area_proxy` 的调用契约一致。C++ tracker-only 扩展仍用裸代理值，但其 `0.01F` 已在调用点显式指定，不再靠 API 默认值。
- 逐项核对：
  1. 双循环：Rust `for p1 in &sp1.poles { for p2 in &sp2.poles }`（overlap_proxy.rs:10-11）。C++ `for (firstPole : first.Poles()) { for (secondPole : second.Poles()) }`（cpp:86-96）✓（同为行主序 p1 外层）。
  2. pd：Rust `(p1.radius + p2.radius) - p1.center.distance_to(&p2.center)`（f32 全程）。C++ `penetration = firstRadius + secondRadius - distance`；`dx/dy = float(firstCenter.x) - float(secondCenter.x)`（cpp:30-38），center 先经 `firstTransform.ApplyRustF32` / `secondTransform.ApplyRustF32`（cpp:27-28，见差异 a）。
  3. decay：Rust `pd >= epsilon ? pd : epsilon.powi(2) / (-pd + 2.0*epsilon)`（overlap_proxy.rs:15-18）。C++ `penetration >= epsilon ? penetration : (epsilon*epsilon / (-penetration + 2.0F*epsilon))`（cpp:40-43）✓ 逐字对应。
  4. min 半径：Rust `f32::min(p1.radius, p2.radius)`。C++ `std::min(firstRadius, secondRadius)`（f32 cast 后，cpp:38）✓。
  5. 求和后乘 PI：Rust `total_overlap *= PI`（std f32::PI，overlap_proxy.rs:23）。C++ `total *= rustPi`，`rustPi = 3.14159265358979323846F`（cpp:98,19）；该 f32 常量与 Rust 的 f32 PI 数值相同，乘法结果仍取决于此前累加值与编译器浮点选项。
  6. Rust `debug_assert!(total_overlap.is_normal())`（overlap_proxy.rs:24）→ C++ Debug 在汇总后执行 `std::isnormal(total)` 并抛出对应 `std::domain_error`（cpp:105-106）；Release 不检查并返回原始 IEEE 结果。Rust Release 与 C++ Release 对列明 subnormal/NaN helper 用例都继续运算。
- 差异 a（变换路径）：Rust 的 `overlap_area_proxy` 收到的 surrogate 属于**已变换到世界系**的 SPolygon（tracker.rs:71 传入 `l.placed_items[pk].shape`），无需再变换；C++ 的 `Surrogate` 存**局部系** pole（Circle2d，double 存储），故 `CalculateOverlapProxy` 带 `firstTransform/secondTransform` 参数经 `ApplyRustF32`（f32 精度旋转平移，Transformation.cpp:23-36）变换。**当前碰撞量化调用点传 Identity**（CalculatePolygonCollisionLoss；`EvaluateCandidateLoss` tracker-only overload 传真实 transform，CollisionTracker.cpp:534-585，用于未变换 surrogate 对）——Identity 时 ApplyRustF32 仅做 `cos(0)*x - sin(0)*y + 0` 的 f32 恒等运算，数值不变。结果等价 ✓。
- 公式级防御差异已按 S12 的 profile fixtures 收敛；`CirclePairContribution` 当前直接执行 Rust 对应 f32 运算（cpp:21-44）。C++ 仍因 public API 使用 `Surrogate&` 而无法表达 Rust 可直接构造的任意坏 pole 状态；缺失 surrogate 的高层调用出口现按 Rust panic/C++ exception 语言映射闭合，任意坏 pole 和变换边界仍未证明。
- 判定：【公式对应；已测的断言/IEEE profile 行为闭合】（参数入口和任意 pole 输入未证明）
- 证据：overlap_proxy.rs:7-27 vs OverlapProxy.cpp:21-44,79-104；Transformation.cpp:23-36；S12 `overlap_proxy_nonfinite_reference.rs`、`TestQuantify.cpp`。

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
| 2 | overlap_area_proxy 的变换入口：Rust 收已变换 surrogate 无变换；C++ 带 Transform 参数经 ApplyRustF32（当前调用全 Identity 或已变换对） | 【差异】 | 结果等价（Identity 恒等、f32 路径） | overlap_proxy.rs:8 vs OverlapProxy.cpp:79-83,27-28 |
| 3 | OverlapProxy 公式中间值：公式级归零已移除；有限有效形状经有限变换可令 bbox `shapeArea=+Inf`；正 shoelace 面积、正 bbox 两轴、零 bbox 乘积候选两边均在构造阶段拒绝 | 【所测边界已闭合；全域零乘积可达性仍未证明】 | 两条实际导入 evaluator subnormal 路径、helper NaN 和有限变换导致 bbox 面积溢出均有 Debug/Release 对照；另有零乘积候选的 Rust Debug/Release POI panic 与 C++ Debug/Release `InvalidPolygon` 构造拒绝，以及 41,761 个有界 Rust 搜索候选无构造成功。以上未证明任意合法几何不可达；完全塌缩的变换不会进入 helper；公开字段突变的零/负 bbox 不属于可配对 C++ API 输入。Malformed 矩形防御仍未证明等价；缺失 surrogate 的高层调用边界见 #11 | `shape_area_guard_reachability_reference.rs`; `overlap_proxy_nonfinite_reference.rs`; `OverlapProxy.cpp`; `TestQuantify.cpp`; S12 #6 |
| 4 | PairMatrix 长度溢出：Debug 失败边界与 Release 回绕结果均按 Rust 算术复现，语言异常类型映射 | 【已闭合】 | SIZE_MAX Debug 抛出边界异常；Release 产生空 backing Vec，后续访问再 bounds-fail | PairMatrix.cpp; pair_matrix_reference.rs; TestQuantify.cpp |
| 5 | TotalLoss/TotalWeightedLoss 与 `WeightedLoss` 的非有限值过滤 | 【已闭合】 | NaN、±Inf 按 Rust 的原始浮点加法/乘法传播 | PairMatrix.cpp; tracker_weight_reference.rs; TestQuantify.cpp |
| 6 | container centroid：Rust `f32::midpoint` vs C++ 原 `(min+max)*0.5F` | 【已修复；极值 midpoint/loss 边界已按 Debug/Release 定向验证】 | 原 C++ 两个大正端点相加溢出，使两个质心都为 `+Inf`、距离为 NaN；Rust midpoint 得到有限质心，Release 损失为 `+Inf`。回归夹具通过低层直接改写 bbox 隔离公式，不代表合法构造几何路径 | jagua-rs `rect.rs:262-267`; `Rect2d::CentroidRustF32`; `container_centroid_midpoint_reference.rs`; `TestContainerCentroidMidpointOverflowMatchesRust` |
| 7 | 0.01/PI 等常量：Rust consts.rs 集中定义 vs C++ 逐处硬编码（PI 的 f32 数值相同） | 【一致】 | 常量值对应；没有由此推导整段计算位级相同 | consts.rs:7 vs OverlapProxy.cpp:19,120 |
| 8 | epsilon 使用 `powi(2)`（Rust）vs `epsilon*epsilon`（C++） | 【源码对应】 | 都表示 f32 平方；未做跨工具链逐位断言 | mod.rs:18 vs OverlapProxy.cpp:130 |
| 9 | 精度路径：C++ 存储 double、部分运算前转 f32；Rust 量化使用 f32 | 【源码对应】 | trace 中虽存有 `loss_f32_bits` 字段，消费测试按容差比较，不能据此称为位级锁定 | OverlapProxy.cpp:30-38,55-57,116-118,150-156 vs overlap_proxy.rs/mod.rs |
| 10 | `CalculateOverlapProxy` epsilon 调用契约 | 【已闭合】 | C++ 现在要求显式 epsilon；扩展调用点显式传 `0.01F` | OverlapProxy.h:12-17 vs overlap_proxy.rs:7-8 |
| 11 | 缺失 surrogate：pair loss 缺 first/second、container loss 缺 shape | 【已闭合：Rust panic → C++ `std::logic_error`】 | 两侧在对应 surrogate 访问阶段失败；Debug/Release 相同，失败不改 polygon；只覆盖有效构造 polygon 和正常 overlap 前置条件 | `missing_surrogate_loss_boundary_reference.rs`; `TestMissingSurrogateLossBoundaryMatchesRust`; `OverlapProxy.cpp`; `mod.rs:19-29,51-53` |

Fixture 与断言：Rust `pair_matrix_reference.rs` 直接编译锁定的 Sparrow `pair_matrix.rs`，验证 N=4 物理顺序、loss fold、Debug/Release 越界和 `usize::MAX` 算术；C++ `TestQuantify.cpp` 验证 N=4 顺序敏感总和、profile-specific 越界、溢出和 NaN/Inf 传播。`overlap_proxy_nonfinite_reference.rs` 与对应 C++ 定向断言核对非正规公式边界；`shape_area_guard_reachability_reference.rs` 的 `finite_candidate_translation_can_overflow_bbox_area_in_container_loss` 与 C++ `TestFiniteTransformOverflowingContainerBboxAreaMatchesRust` 配对核对有限输入导致 `shapeArea=+Inf`，`positive_shoelace_area_with_zero_bbox_product_fails_before_loss_helper` 与 `TestPositiveShoelaceZeroBboxCandidateIsRejectedBeforeLoss` 固定正鞋带面积/零 bbox 乘积候选在 POI/构造阶段的 Debug/Release 出口，另以 `nonpositive_bbox_area_is_reachable_only_through_malformed_public_bbox_state` 固定 Rust public-field malformed 零/负 bbox 行为。`missing_surrogate_loss_boundary_reference.rs` 与 `TestMissingSurrogateLossBoundaryMatchesRust` 以双方均可构造的有效无 surrogate polygon，分别核对 pair loss 缺 first/second 和 container loss 缺 shape 的 Rust panic/C++ exception；用例覆盖 Debug/Release 并确认失败不改 polygon。新增 `container_centroid_midpoint_reference.rs` 与 `TestContainerCentroidMidpointOverflowMatchesRust` 固定端点和溢出时的质心位模式、Debug 断言边界和 Release `+Inf`；两侧都通过临时 bbox 状态隔离公式，不将其表述为合法多边形可达路径。普通 `TestOverlapProxy` 的几何损失仍是 C++ 容差断言，不是完整 Rust raw-bit oracle，其他既有 separator trace 也保留原记录的容差范围。

## 结论

PairMatrix 的合法索引映射、物理遍历顺序、总损失 fold、profile-specific 越界和长度溢出行为已按当前锁定 Rust 基线对齐并有 Debug/Release 参考。量化公式对列明的 non-normal 分支也已通过 Debug/Release reference；新增有限变换溢出 fixture 证明旧 `shapeArea` 前置守卫可改变有效输入结果，现已删除并对齐 Rust profile 行为。Rust 公开字段突变可以构造零和负 bbox：零面积 Debug 断言失败、Release 返回 `+0`；负面积的 overlap 为 normal，两个 profile 后续返回 NaN。这些 malformed 状态无法由 C++ `SimplePolygon` 直接表达。另一个鞋带面积为最小正 subnormal、正 bbox 两轴而 bbox 乘积为零的固定候选，在 Rust POI unwrap panic、C++ `InvalidPolygon` 处被拒绝；41,761 个有界候选没有构造成功，但这不是任意几何不可达的证明，仍需保持该全域问题开放。把 bbox 轴完全变为零的变换则先在 Rust bbox 重建处 panic、在 C++ 返回变换错误。容器 centroid 现复用 Rust-f32 midpoint helper，并以端点加法溢出的低层 bbox 夹具锁定两 profile。有效无 surrogate polygon 的 pair/container loss 入口现按 Rust panic/C++ `std::logic_error` 在对应访问阶段失败，Debug/Release 均有配对用例；malformed 矩形防御、任意 pole 状态和其他几何仍有边界差异，整体几何尚未逐位闭合。

### 补充审计（2026-09-29）
1. **【核对确认：53+27+49 行逐项覆盖】** quantify/mod.rs（3 pub fn + 2 模块声明 + calc_shape_penalty）、overlap_proxy.rs（overlap_area_proxy 单函数）、pair_matrix.rs（结构/new/Index/IndexMut/calc_idx）——主文 7 小节全覆盖；`#[inline(always)]` 属性能标注。
2. **【历史浅覆盖补记，2026-10-15 已更正】** 当时记录的 Rust `is_normal` Debug 断言和 Release 继续 IEEE 运算是源码事实；“C++ 对所有异常值恒返回 0”描述的是旧实现，现已由 S12 定向工作修正。当前行为与边界见主文差异 #3 和 S12 的 `OverlapProxy` 非有限/退化中间值小节。
3. **【2026-10-06 更正：越界访问语义】** 先前把 Rust release 越界索引写成未定义读，这是错误的：`Vec` 索引在 release 仍做边界检查并 panic，即使 `debug_assert!` 被移除。C++ `std::vector::operator[]` 越界则未定义行为；主文已区分两者。
4. **【核对确认：布局映射手算复核】** size=4 的 `(0,3)` 在 Rust 映射到 3、C++ 映射到 6；两种索引均由各自矩阵的本地访问函数闭合使用。区别是物理排列及遍历顺序，不共享内存布局。
5. **【2026-10-15 profile 边界更正】** `CalculateOverlapProxy` 汇总、`CalculatePolygonCollisionLoss` 的 `overlap_proxy` 和 `CalculateContainerPenalty` 的 `overlap` 现分别在 Debug 对应 Rust `is_normal()` 断言，在 Release 保留公式 IEEE 结果。两条成功导入的微小件 evaluator fixture 与直接 `epsilon=f32::MAX` helper fixture 见 S12；midpoint 端点溢出另有低层 Rust/C++ reference，使用手工 bbox 状态隔离算术，不代表成功构造的几何；missing surrogate 的 pair/container loss 调用边界现有 Rust Debug/Release panic/C++ exception 配对用例；malformed bbox、防御性出口及任意 pole 输入仍未覆盖。

### P0 复核更正（2026-10-09）

本节 2026-09-29 的第 3、4 条记录的是当时实现，不是当前状态：PairMatrix 现在采用 Rust 的上三角行顺序，size=4 的 `(0,3)` 两侧均落在物理槽位 3；Release 的非法逻辑坐标按 Rust unsigned flatten 算式访问并保留 alias，backing 越界由 checked access 失败；Debug 非法逻辑坐标由 Rust `debug_assert!` 与 C++ `std::out_of_range` 退出。`usize::MAX` 长度运算也有 Debug/Release Rust/C++ reference。当前结论以主文 PairMatrix 小节和 [共用错误分类表](../error-semantics.md) 为准，不能继续引用旧审计行作为现状差异。

### PairMatrix 与 Tracker 组合核对（2026-10-17）

新增共享 fixture `SparrowCore/Tests/Fixtures/SparrowPairMatrixTrackerRustReference.tsv`，由 RustReference 从 `CollisionTrackerWeightRust.json` 导入三个相同的 1×1 方形，在 strip width=8 下生成初始状态、设置区分权重并执行 GLS 更新、将第三件从 `(1,1)` 移到 `(4,1)` 后的状态。Rust 集成测试与 C++ `TestPairMatrixTrackerRustReference` 均读取同一 TSV 和输入 JSON。

每个阶段逐位比较 6 个上三角 pair 项及 3 个 container 项的 loss/weight、总 loss、总加权 loss 和 `IsSeparated`；该组合夹具覆盖 PairMatrix 与 Tracker 交互时的初始化、权重传播及 moved-row 更新。RustReference Debug/Release 全量验证、SparrowCore Debug/Release 全量 runner 均通过；Rust fixture 生成器成功产出三阶段记录。

范围仍固定在该三件布局与一次移动，不证明任意数量、几何或搬迁轨迹的一般等价。S17 PairMatrix 与 Tracker 组合缺口已收口；OverlapProxy 的 malformed 矩形表示边界及 S18/S12 各自记录的非有限状态、CDE 与 pole 顺序问题仍按对应条目开放。
