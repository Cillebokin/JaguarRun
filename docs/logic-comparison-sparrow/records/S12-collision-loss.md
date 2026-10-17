# S12. collision_loss（碰撞损失流式评估）+ Quantify 权重接线 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/collision_loss.rs`（97 行）+ 权重入口 `quantify/mod.rs`（quantify_collision_poly_poly / quantify_collision_poly_container）+ `quantify/tracker.rs` 的 get_pair_weight/get_container_weight
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/LbfLoss.h`（仅 LBF 损失）+ `Source/Quantify/CollisionTracker.cpp`（`EvaluateCandidateHazardLoss` 逐 hazard 损失 helper；另含两个整项 `EvaluateCandidateLoss` API）+ `Source/Quantify/OverlapProxy.cpp`（碰撞量化公式）+ `Source/Eval/SeparationEvaluator.cpp`（CDE 回调、上界累加与早停）
**判定**: 标量默认 feature 的已测路径按所列 fixtures 对齐（多 hazard 顺序、逐 hazard 早停、Exterior 损失、上界相邻浮点值、Hole/QZone 失败传播，以及候选平移/旋转分量 `±Inf` 的特定 profile 边界均有 Rust/C++ reference；有限候选导致的 Exterior 质心距离平方 f32 溢出，以及有限 raw loss 经 128 次 GLS 更新后与 `+Inf` 权重相乘的边界，均有 evaluator fixture 按 Debug/Release 对照；固定双 worker 的 Separator::Run 与 Optimize listener/phase 传播已核对；其他 worker 数不外推，opt-in SIMD 仍未认证）

归属说明：`LbfLoss.h/.cpp` 只含 LBF 的 `xMax*10+yMax` 损失与 bbox 收紧，不含碰撞损失。SparrowCore 分离评估的实际 Rust 对应路径是 `SeparationEvaluator::Evaluate` 逐个调用 `CollisionTracker::EvaluateCandidateHazardLoss`，并将每项与 pair/container weight 相乘；公式位于 `OverlapProxy.cpp`。公开 `EvaluateCandidateLoss` 重载仍供其他调用方使用，但它们不是分离评估器当前的逐 hazard 主路径。

## 函数逐条比对

### `CollisionLossEvaluator::new`（collision_loss.rs:25-35）
- C++ 对应：无独立结构。Rust 辅助状态内联于 `SeparationEvaluator::Evaluate` 的累计变量和 `EvaluateCandidateHazardLoss` 的逐项公式。
- 判定：【一致】（内联化；Rust 该结构本身是 pub(super) 私有辅助）
- 证据：collision_loss.rs:14-22 vs SeparationEvaluator.h:32-42、CollisionTracker.cpp:449-488。

### `reload`（collision_loss.rs:37-42）
- Rust：每次 evaluate 前重置 loss 和 bound；C++ 在 `SeparationEvaluator::Evaluate` 中为每个候选重新初始化累计值，并经 `EvaluateCandidateHazardLoss` 逐 hazard 加和。`simd` 时 Rust 还装载 poles SoA；C++ 没有该 Rust bounded-SIMD 路径。
- 判定：【一致】（逐候选状态重置一致；上界与 loss 在逐 hazard 路径中联动）
- 证据：`collision_loss.rs:37-42` vs `SeparationEvaluator.cpp` 的 `Evaluate` 与 `CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss`。

### `add`（collision_loss.rs:44-56）— 逐 hazard 累加 + 短路
- C++ 对应：`SeparationEvaluator::Evaluate` 在 CDE 遍历回调中逐项调用 `CollisionTracker::EvaluateCandidateHazardLoss`，按 Rust 的剩余预算和累计上界短路；不再全量收集后统一计算。
- 语义核对：
  1. **每项贡献**：Rust PlacedItem → `quantify_collision_poly_poly(other_shape, shape) * pair_weight`（:86-88），Exterior → `quantify_collision_poly_container(shape, container_bbox) * container_weight`（:90-93）。C++ helper 对应使用 `CalculatePolygonCollisionLoss(otherShape, transformedCandidate) * PairWeight(...)` 或 `CalculateContainerPenalty(...) * ContainerWeight(...)`。
  2. **逐 hazard 短路**：PlacedItem 单项 loss 大于剩余预算时，C++ 回调在累计前返回 stop；其他项加入累计后，以严格 `accumulatedLoss > lossBound` 决定是否停止。三项 hazard 的当前 fixture 揭示一个重要浮点边界：无界累计结果为 `0x4ab5c931`，上界恰为该值时 Rust 返回 `Invalid`，上界为下一相邻 `f32`（`0x4ab5c932`）时返回 `Collision(0x4ab5c931)`。原因是后续 hazard 使用 `lossBound - accumulatedLoss` 形成单项剩余预算，舍入后可能小于该项 loss；C++ 在 Debug/Release 对该夹具复现相同分类。Clear bound 下的零损失碰撞仍分类为 `Collision(0)`。
  3. **累加次序**：CDE 的 callback discovery order 决定 f32 累加次序。三项 PlacedItem fixture 的 Rust 顺序 `[0,1,2]` 得到 `0x4ab5c931`，逆序得到 `0x4ab5c932`；C++实际 evaluator raw bits 匹配 Rust 顺序。该证明只覆盖这组权重和几何。
  4. **`unimplemented!` 分支**（Rust :94，非 PlacedItem/Exterior hazard）：Rust 对 Hole 与 InferiorQualityZone 都 panic。质量区由 jagua-rs CDE 正常注册；导入时 quality=0 的 container holes 会合并进 zone 并变成 Hole（`jagua-rs/src/io/import.rs:175-194`; `entities/container.rs:114-123`），不是被 Rust 上游过滤。C++ 现不再过滤 Hole；对 Hole 和 InferiorQualityZone，`EvaluateCandidateHazardLoss` 均抛出 `std::logic_error("unsupported hazard entity")`，不再忽略或作为零损失碰撞分类。Rust `collision_loss_unsupported_hazard_reference.rs` 对真实导入布局分别断言两类 hazard 触发 panic；C++ `TestUnsupportedContainerHazardsMatchRustPanic` 对应断言异常类型和消息。新增 worker 搜索用例再以实际随机候选触发 InferiorQualityZone，逐项核对异常冒泡、RNG-after、布局/Tracker 不变和候选回调未触发。故已关闭静默忽略差异、单 worker 搜索调用链及固定双 worker Separator::Run/Optimize 传播边界；panic/exception 是语言层映射。该 fixture 不认证其他 worker 数、hazard 几何或错误源。
- 判定：【PlacedItem 主路径及 Hole/InferiorQualityZone 不支持输入均有定向 Rust/C++ reference；异常机制按各语言表达】。
- 证据：`collision_loss.rs:44-56,62-96`；`CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss`；`SeparationEvaluator.cpp` 的 `Evaluate`；`collision_loss_stream_reference.rs`、`collision_loss_unsupported_hazard_reference.rs`、`TestQuantify.cpp`。

### `calc_weighted_loss_bounded`（collision_loss.rs:62-96）— 量化公式
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:106-139）与 `CalculateContainerPenalty`（cpp:141-194）。逐公式核对：
  1. **poly-poly**（Rust quantify_collision_poly_poly，mod.rs:15-24）：`epsilon = max(d1,d2) * 0.01`（OVERLAP_PROXY_EPSILON_DIAM_RATIO，consts.rs:7）；`overlap_proxy = overlap_area_proxy(sur1, sur2, eps) + eps²`；`penalty = sqrt(sqrt(hull1) * sqrt(hull2))`（calc_shape_penalty，mod.rs:26-30：`p1=sqrt(ch_area1), p2=sqrt(ch_area2), (p1*p2).sqrt()`）；`loss = sqrt(overlap_proxy) * penalty`。C++ CalculatePolygonCollisionLoss：epsilonDiameterRatio=0.01（cpp:120）✓；`rawProxy = CalculateOverlapProxy(...Identity,Identity,eps)`、`overlapProxy = rawProxy + eps*eps`（cpp:124-130）✓；ShapePenalty = `sqrt(sqrt(a1)*sqrt(a2))`（cpp:46-58）✓；`sqrt(overlapProxy) * shapePenalty`（cpp:137）✓。**注意**：C++ 传给 CalculateOverlapProxy 的是两个 **Identity 变换**（cpp:124-129）——因为两个形状都已变换到世界系（transformed 与 placed 的 CollisionShape），Rust 的 overlap_area_proxy 收到的同样是变换后的 SPolygon 的 surrogate（:86 `quantify_collision_poly_poly(other_shape, shape)`，两形状均为世界系）。等价 ✓。
  2. **overlap_area_proxy**（overlap_proxy.rs:8-27）：遍历所有 pole 对并累加 `pd_decay * min(r1,r2)`，最后乘 PI。C++ `CirclePairContribution`（OverlapProxy.cpp:21-44）公式相同。Rust `overlap_area_proxy(other_shape, shape)` 以 `other_shape` 的 poles 为外层、候选 `shape` 为内层；对应的 C++ layout 路径由 `EvaluateCandidateHazardLoss` 调用 `CalculatePolygonCollisionLoss(otherShape, transformedCandidate)`（CollisionTracker.cpp:469-472），参数顺序与 Rust 一致。旧 C++ 调用顺序相反；非对称方形/矩形夹具在 0.137 rad 下曾得到 Rust `0x32560315`、旧 C++ `0x32560316`（差 1 ULP）。改为 Rust 参数顺序后，该 pair 的 11 个测试旋转角均逐位相同。此结论限于 layout poly-poly 路径及该单 pair 夹具；三项 hazard 的指定累加顺序另由 `collision_loss_stream_reference.rs` 覆盖，C++ tracker-only 重载仍是额外 API。
  3. **poly-container**（Rust quantify_collision_poly_container，mod.rs:36-55）：有交集 → `(shape_area - inter_area) + 0.0001*shape_area`；无交集 → `shape_area + centroid 距离`；`loss = 2.0 * sqrt(overlap) * penalty(s,s)`。C++ CalculateContainerPenalty（cpp:141-194）：交集条件 `intersectionXMin < intersectionXMax && ...`（严格小于；Rust `Rect::intersection` 在零宽或零高时返回 None，与 C++ 条件一致）✓；`(shapeArea - intersectionArea) + 0.0001F * shapeArea`（cpp:175-180）✓；无交集 `shapeArea + RectCentroidDistance`（cpp:181-185，中心距离在 f32 上算 ✓）；`2.0F * sqrt(overlap) * shapePenalty`（cpp:192-193）✓。
  4. **精度路径与累加序**：碰撞量化和逐 hazard loss 的计算使用 Rust 对应的 f32 表达式；JaguarCore 几何对象的坐标/变换存储仍含 double 字段，不能据计算中间量就概括整个 geometry 层为 f32。单 pair 非对称 fixture 的 11 个旋转候选逐位一致，三项 hazard 指定次序的加权总损失逐位一致；Exterior fixture 的完整容器惩罚也逐位为 `0x391152f2`。普通 trace 的 `loss_f32_bits` 会转为浮点后按容差比较，不能当作位断言；这些 raw-bit 结果仍不覆盖任意 geometry、所有 pole 集合或所有 CDE hazard 次序。
  5. **内部公式的退化/非有限值**：本轮已移除 `CirclePairContribution`、`CalculateOverlapProxy`、`ShapePenalty`、`CalculatePolygonCollisionLoss` 和 `CalculateContainerPenalty` 对公式中间值/最终损失的非有限或非正值归零；Debug 在 Rust 对应的 `total_overlap.is_normal()`、`overlap_proxy.is_normal()`、`overlap.is_normal()` 断言边界抛 `std::domain_error`，Release 继续 IEEE 运算。2026-10-15 的直接 helper 和实际 evaluator 结果见下节。C++ 仍保留无效矩形/包围盒、空 surrogate 等输入/表示防御；它们不等同于公式中间值检查。`shapeArea` 的前置守卫已由本轮可达性用例证明会改变合法有限输入的结果，现已删除。S11 所记的候选 `±Inf` 平移仍由 evaluator 在几何遍历前映射，不能作为公式内部已对齐的证据。
- 判定：【一致】（公式逐字对应；防御差异另计）
- 证据：collision_loss.rs:62-96 + quantify/mod.rs:15-55 + overlap_proxy.rs:8-27 vs OverlapProxy.cpp:21-58,79-139,141-194；`SeparatorSearchRustTrace.tsv` 的 `loss_f32_bits` 会被读取为浮点值并按测试容差比较，不是逐位比较。

### 有限输入导致的 Exterior 质心距离平方溢出（2026-10-12）

fixture CollisionLossFiniteDistanceOverflowRust.json 使用有限的 3e12 × 1e12 矩形，条带高度为 2e12、两侧均显式设置宽度 4e12；初始放置中心为 (2e12, 1e12)，再评估有限候选平移 (1.9e19, 1e12)。候选没有与条带相交，进入 Rust quantify_collision_poly_container 的无交集分支。输入和几何仍有限，但 dx*dx 的 f32 运算溢出为正无穷，使 shape_area + centroid_distance 成为非正规值。

Rust Debug 在 overlap.is_normal() 断言处 panic；Rust Release 保留 IEEE 结果并返回 Collision(+Inf)。旧 C++ CalculateContainerPenalty 会把这个中间正无穷折成 0。现 C++ helper 在正无穷 overlap 且 shape penalty 有限为正时保留正无穷，CollisionTracker::EvaluateCandidateHazardLoss 再按 profile 映射为 Debug std::domain_error（消息含 overlap.is_normal()）或 Release Collision(+Inf)。

Rust 与 C++ Debug/Release 均核对了评估计数为 1、失败候选不修改布局和 Tracker、随后以原有限变换重试得到 Clear(0)、计数变为 2 且 Tracker 总损失位模式不变。证据为 finite_exterior_centroid_distance_overflow_exposes_profile_boundary 与 TestFiniteExteriorCentroidDistanceOverflowMatchesRust。

该用例只证明列出的 Exterior 无交集路径、输入和两个 profile 的 evaluator 行为。它不证明其他几何/公式中间值溢出、NaN、poly-poly 路径或直接调用 Rust/C++ helper 的 Debug 行为等价。

### 有限 Exterior raw loss 与 GLS 权重相乘溢出（2026-10-12）

fixture `CollisionLossFiniteWeightOverflowRust.json` 是有限的 1×1 方形。测试将条带宽度设为 2，并将该件放在 `(1.75, 1.0)`，使 Exterior raw loss 保持有限且为正。随后对 Tracker 执行 128 次真实 `update_weights()`；该夹具的 container weight 到达 `+Inf`，Tracker 的 total weighted loss 也成为 `+Inf`。评估相同有限候选时，有限 raw loss 与 `+Inf` weight 的乘积为 `+Inf`。

Rust Debug/Release 与 C++ Debug/Release 均将该候选分类为 `Collision(+Inf)`；不会触发 raw Exterior 量化处的 `overlap.is_normal()` 失败。测试还核对评估计数为 1、布局和 Tracker raw total loss 不变，再以条带内部变换 `(1.0, 1.0)` 重试得到 `Clear(0)`，计数变为 2。证据为 `finite_exterior_weight_overflow_remains_collision` 与 `TestFiniteExteriorWeightOverflowMatchesRust`。

该用例只覆盖列明的有限 Exterior raw loss、128 次 GLS 更新和权重乘法路径；不证明其他权重增长轨迹、其他 hazard、内部 raw-loss 公式溢出或任意非有限输入等价。

### 容器损失最终乘法溢出的可达性（2026-10-15）

公式 `2.0 * sqrt(overlap) * penalty` 的最后一次 `f32` 乘法可在两个操作数均有限时溢出。直接低层夹具用 `side = sqrt(FLT_MAX * 0.49999)` 构造有限坐标方形，包围盒面积 raw bits 为 `0x7efffeaf`；容器取 `[-1,1]²` 后，overlap 为有限正规数 `0x7f00029e`，penalty 为 `0x5f35047c`，`2*sqrt(overlap)` 为有限值 `0x5fb506cd`，最终乘积为 `+Inf`。Rust Debug/Release 均通过该 helper 级用例。该方形的派生质心非有限，但 Rust `SPolygon::new` 与 C++ `SimplePolygon::Create` 都接受有限顶点及其他有限几何量；作为外部 Item 导入时，两侧仍会在建立中心变换时停止，不能用它进入 evaluator。

实际 evaluator 夹具使用有限 `min_item_separation=14288786000000000000` 和外部 1e12×1e12 矩形，Rust 使用默认 Sparrow 简化配置、`cde_quadtree_depth=0` 和固定 pole limits。Rust Importer 成功，偏置后的 `shape_cd` 面积和包围盒面积均有限；raw bits 为 bbox area `0x7f19999a`、shape area `0x7ef12454`、overlap `0x7f199d89`、penalty `0x5f2fb00c`、首乘积 `0x5fc64e82`。有限首乘积与 penalty 的最后乘法成为 `+Inf`。将 Item 放入手工 2×2 Container 后，Rust Layout/CDE/`SeparationEvaluator` 返回 `Collision(+Inf)`，Debug/Release 一致。深度固定为 0，因为移动 hazard 远大于小容器的 CDE 根范围，默认深度会在构树时触发断言。

初始 C++ importer 在 offset polygon 重导入时以 `OffsetFailed: polygon geometry contains a non-finite value` 拒绝了该 Item。首个分叉是 `SimplePolygon::Create` 额外要求派生质心有限；Rust `SPolygon::new` 不在构造期间计算或验证质心。C++ 现移除了这一项检查，仍校验顶点、面积、包围盒坐标和直径的有限性；同一 Item 现在可经 C++ `Importer::ImportItem`、Layout/CDE 到达 `CalculateContainerPenalty` 与真实 `SeparationEvaluator`，两者均返回 `Collision(+Inf)`，Debug/Release 通过。

**几何首次分叉与偏移路径对照（2026-10-15）**：逐阶段位哈希确认原始矩形与居中变换后的 4 个顶点 Rust/C++ 相同，首次分叉发生在 polygon offset/buffer 阶段，而不是坐标变换。Rust 当前锁定 `geo 0.33.1` 的 `geo::Buffer`，多边形路径进入 `i_overlay 4.5.2`；C++ 仍保留 `StraightSkeleton.cpp` 路径，并在 `Offset.cpp` 增加经定向用例证明的兼容逻辑。CCW Inflate 路径按 Rust 的 `FloatPointAdapter` 网格、`LineJoin::Round(0.1)` 和定点舍入生成轮廓；`log2(maxExtent)` 依 Rust `FloatNumber::to_i32()` 最近整数舍入（半值远离零），截断会令网格尺度在临界值处差一档。环起点按 Rust 轮廓顺序选择：唯一最左点取其 CCW 前一顶点，最左 x 并列时取并列点中 y 最大者。

极端尺度最终乘法 fixture 的偏置轮廓为 68 个顶点，Rust/C++ 有序 f32 顶点哈希均为 `0xd2af3ad80f009765`，面积 `0x7ef0e310`、包围盒面积 `0x7f19999a`；内部形状为 50 点，哈希 `0x43ff76136e8088d1`、面积 `0x7ef12454`、包围盒面积 `0x7f19999a`。公式入口 bbox area、shape area、overlap、penalty、首乘积分别为 `0x7f19999a`、`0x7ef12454`、`0x7f199d89`、`0x5f2fb00c`、`0x5fc64e82`，最后乘法与 evaluator 分类为 `Collision(+Inf)`。普通尺度 Inflate 的两种独立斜四边形、循环输入起点与 1e6 坐标 sub-ULP 方形仍保持原有位级结果。

新增 offset 金样覆盖：凹形 L `Inflate(0.25)` 的 86 点、面积 `0x41d175db`、包围盒面积 `0x42290000`、顶点哈希 `0xbf324096dceae641`；凸五边形 `Deflate(0.3)` 的 5 点、面积 `0x413a00b7`、包围盒面积 `0x418ba0c6`、哈希 `0x1710858bdb9fdb5f`。C++ Deflate 候选几何的顶点位模式原本与 Rust 一致，但环起点循环偏移；含共线输入时还多保留一个量化后共线点。C++ 现在仅对识别为 CCW 凸形的 Deflate 结果按 Rust 环起点规则旋转，并按偏移网格移除量化后重复/共线点。顺时针输入和带冗余共线顶点的 Rust 用例均产生同一 5 点金样。两个边长位模式 `0x4635008c`/`0x4635008e` 位于 adapter 指数舍入临界值两侧，分别验证 68 点 Inflate 哈希 `0x2b698cbf3a05d425`/`0x4248ff4142d7c2d5`。

真实 SPP 集成 fixture `ShapeOffsetEvaluatorSpp.json` 设置 `min_item_separation=0.5`，导入两个 L 形物品。Rust/C++ importer 生成的碰撞轮廓均为 10 点、面积 `0x41d1a88c`、包围盒面积 `0x42290000`、哈希 `0x1bed85a4ace8dc97`；将二者重叠放入条带后，Tracker pair loss 为 `0x4221ca2f`，真实 `SeparationEvaluator` 输出 `Collision` loss `0x4262ee64`。这证明该 SPP 输入经过 importer 的偏置、简化和 evaluator，而不只是在低层直接调用 `OffsetShape`。

这些结果仅证明列明的几何、尺度和 SPP→evaluator fixture，不等于完整移植 `i_overlay`。一般凹形 Inflate、其他 Deflate 轮廓、offset 后自交/塌缩/多分量、更多方向与退化组合，以及广泛的外部 SPP 输入仍需独立 Rust oracle；不能由这些样例推出任意几何位级一致。

证据：Rust `shape_offset_geometry_reference.rs` 五项测试、`collision_loss_final_multiply_reachability_reference.rs::strict_convex_inflate_contour_order_matches_reference` 及容器溢出测试；C++ `TestConcaveInflateContourBitsMatchRust`、`TestConvexDeflateContourBitsMatchRust`、`TestOffsetOrientationDegenerateScaleMatchesRust`、`TestSppImportedOffsetReachesSeparationEvaluator`、`TestStrictConvexInflateContourOrderMatchesRust`；fixture `ShapeOffsetEvaluatorSpp.json`；实现锚点 `Offset.cpp`、`SimplePolygon.cpp`、`Importer.cpp`、`OriginalShape.cpp`、`SeparationEvaluator.cpp`。

### `OverlapProxy` 非有限与退化中间值（2026-10-15）

新增 Rust/C++ reference 将“公式中间值”与“helper 输入契约”分开核对。低层公式使用有限边长 `1e-20` 的方形时，Rust Debug 在 `overlap_area_proxy` 的 `total_overlap.is_normal()` 处 panic，C++ Debug 映射为同边界的 `std::domain_error`；Release 的 raw proxy 和 poly-poly loss 位模式分别为 `0x0001c3bd`、`0x000162e2`。同尺寸两件方形可由 SPP importer 成功导入并进入真实 `SeparationEvaluator`：Rust/C++ Debug 在相同的 `total_overlap.is_normal()` 边界失败，Release evaluator pair loss 均为 `0x000162e2`。同一量级的导入方形以有限候选轻微越过条带边界时，Rust/C++ Debug 均在容器公式 `overlap.is_normal()` 边界失败；Release evaluator loss 均为 `0x00014e8e`。这两条证明所列合法导入输入能到达非正规中间值，不外推其他尺寸、极端变换或 hazard 顺序。

单独调用 `overlap_area_proxy` 时，以正常方形配 `epsilon=f32::MAX` 会让公式汇总成为 NaN：Debug 两侧在 `total_overlap.is_normal()` 失败，Release 两侧保留 NaN。这个 epsilon 不是 evaluator 从多边形直径推导出的值，因此它只证明公开低层 helper 的显式 epsilon 边界，不是合法排料输入路径。Rust fixture 为 `overlap_proxy_nonfinite_reference.rs` 的四项用例；C++ 对应 `TestExtremeEpsilonOverlapProxyBoundary`、`TestSubnormalPolygonLossBranch`、`TestSubnormalLossThroughSeparationEvaluator`、`TestSubnormalContainerLossThroughEvaluator`。

尚未闭合的边界包括：除列明 fixture 外的中间值溢出/下溢组合、形状罚项异常、malformed surrogate/矩形的表示差异（有效 polygon 尚未生成 surrogate 的调用出口已由 S17 覆盖），以及成功构造的合法几何经有限变换后 `shapeArea` 乘法下溢为零的全域可达性。新增固定候选与确定性有界搜索未发现任何成功构造并进入 helper 的零面积 bbox；候选拒绝边界与探测范围见下节。该证据不能证明任意合法几何都不可达。Rust 公共字段被直接改成 malformed 非正 bbox 的 profile 行为已有单侧参考，但不能替代合法几何证明或 C++ 等价输入。

### `shapeArea` 有限性与正值守卫可达性（2026-10-15）

Rust/C++ 配对低层用例以边长 `1.2e19f32` 的正方形作为有效初始几何。初始面积位模式为 `0x7ed8aac7`、直径位模式为 `0x5f6b838c`，均有限；再施加有限平移 `1.5 × 2^86`（f32 位模式 `0x6ac00000`）。变换后 bbox 的宽、高均为 `2^64`（`0x5f800000`），而 `shapeArea = width * height` 溢出为 `+Inf`。因此 `!isfinite(shapeArea)` 守卫并非只针对坏输入：它能拦截由有效有限形状和有限候选变换产生的 Rust 可达状态。

直接调用 `quantify_collision_poly_container` 时，Rust Debug 在 `overlap.is_normal()` 断言处 panic，Rust Release 返回 `+Inf`（`0x7f800000`）。旧 C++ 前置守卫会提前返回 `0`，与两个 Rust profile 都不一致；移除该守卫后，C++ Debug 在同一断言边界抛出对应 `std::domain_error`，Release 结果位模式为 `0x7f800000`。参考用例为 Rust `shape_area_guard_reachability_reference.rs` 和 C++ `TestFiniteTransformOverflowingContainerBboxAreaMatchesRust`。这是低层几何量化 API 的可达性证明，不是完整 SPP importer→CDE→evaluator 轨迹。

`shapeArea <= 0` 中的负值对有效有序 bbox 不可达：bbox 两端保持严格递增，转为 f32 是单调映射，宽高相减不会为负。成功构造/变换的 Rust polygon bbox 也要求两个宽高各自严格为正；乘积下溢为零仍没有全域可达/不可达证明。新增固定候选由 8 个精确 f32 位模式组成：Rust shoelace 面积为最小正 subnormal `0x00000001`，bbox 宽、高均为正，但 bbox 面积为 `+0`。`SPolygon::new` 未返回形状，而是在 POI 的 `Rect::quadrants → Rect::try_new(...).unwrap()` 处因退化子矩形 panic；配对 C++ `SimplePolygon::Create` 以 `InvalidPolygon` 拒绝它。两侧 Debug/Release 均未到达 loss helper；Rust panic 与 C++ 结构化错误是不同的构造失败出口，不能写成错误语义完全相同。固定用例为 Rust `positive_shoelace_area_with_zero_bbox_product_fails_before_loss_helper`、C++ `TestPositiveShoelaceZeroBboxCandidateIsRejectedBeforeLoss`。

本轮临时确定性探针另覆盖 `y=2^e`（`e=-4..12`，相邻基准偏移 0..3）、21 种 y 跨距（1 到 8191 ULP）、subnormal x 宽度（`x_max` 位模式 `1<<k`，`k=0..22`）和 16/32/64/128 点极薄轮廓，以 xorshift seed `0x6a09e667` 产生 41,761 个“shoelace 正值、bbox 面积为零”候选；0 个通过 `SPolygon::new`；36,911 个 panic（首个 panic 定位到 POI 四分矩形），4,850 个返回构造错误（首批抽样错误为 `no pole found`）。探针本身未保留，计数是有界搜索证据而非回归测试或穷尽证明；既有 361、268、1,224、192 个候选网格也保持相同证据等级。有限变换若把某个 bbox 宽/高完全舍入为零，则 Rust 在 `generate_bounding_box → Rect::try_new(...).unwrap()` 处 panic（`simple_polygon.rs:131-141,219-245`），C++ `TransformedRustF32` 在 `Rect2d::Create` 处返回错误（`SimplePolygon.cpp:351-357`）；两者均未向损失 helper 交付变换后形状，不能作为 `shapeArea` 守卫的成功路径证据。

非正面积在 Rust 低层 API 的 malformed 表示状态下已分别固定：`SPolygon.bbox` 和 `Rect` 坐标字段是公开可变字段。测试把有效方形 bbox 改成零宽高时，`quantify_collision_poly_container` 在 Debug 的 `overlap.is_normal()` 断言处 panic，Release 按公式返回 `+0`；改成反向 x 范围、面积为 `-1` 时，bbox 与单位容器不相交，`overlap=-1` 仍是 normal，两个 profile 都继续到 `sqrt(-1)` 并返回 NaN。reference 为 `nonpositive_bbox_area_is_reachable_only_through_malformed_public_bbox_state`。这些不是合法几何输入，也没有 C++ 等价对象；C++ `SimplePolygon` 不暴露可写 bbox。C++ 的 `shapeArea` 前置守卫已经删除，避免公式入口提前归零；对合法构造路径的零面积乘积仍无成功 fixture，任意几何的不可达性也未获证明。`IsFiniteRect` 对非有限 bbox 的入口拒绝仍保留，是独立的 C++ 输入防御。

### 非有限候选变换边界（与 S11 evaluator 入口配合）

Rust `DTransformation` 的 `NotNan<f32>` 允许 `±Inf` 平移和旋转。平移 fixture 的四种 X/Y 轴正负无穷候选均到达 Exterior 量化：Rust Debug 在 `overlap.is_normal()` 断言处 panic；Release 在无上界、Invalid、Collision `+Inf`/`NaN` 上界下返回 `Collision(+Inf)`，有限 Collision/Clear 上界下返回 Invalid。C++ `SeparationEvaluator::Evaluate` 在 `LegalizeTransform` 前对 f32 转换结果为无穷的平移执行 profile 映射：测试包括四种直接 `double ±Inf` 和四种有限 `double ±2*FLT_MAX`（转换为 f32 `±Inf`）；Debug 抛 `std::domain_error`，Release 返回相同分类。这不是 C++ 对非有限候选执行碰撞公式后得到的结果。Rust 核对 24 次、C++ 核对 48 次评估，布局和 Tracker 均不变；随后有限候选恢复到 raw loss `0x4ab5c931`（Rust/C++ 计数分别为 25/49）。

无限旋转在变换合成阶段失败，不会到达损失公式：Rust `compose()` 调用 jagua-rs `rot_transl_m`，`cos(±Inf)` 得到 NaN 后由 `NotNan::new(cos).expect("cos is NaN")` panic，Debug/Release 相同。Rust 两种符号 × 六类上界共 12 次；C++ 两种直接 double 无穷角和两种有限 `double ±2*FLT_MAX` 转 f32 无穷角 × 六类上界共 24 次，均核对含 `cos is NaN` 的 `std::domain_error`、评估计数、布局/Tracker 不变和有限候选恢复到 `0x4ab5c931`（Rust/C++ 计数分别为 13/25）。RustReference 与 SparrowCore Debug/Release 全量测试通过。

这些结论只覆盖该 fixture、每个变换分量单独为无穷、6 类上界和锁定工具链/profile；未覆盖 NaN 变换（Rust 在 evaluator 前构造即拒绝）、多个无穷分量组合、其他有限坐标/几何计算溢出（质心距离平方溢出仅有上一节所列 fixture）、除上一节所列轨迹外的权重乘法溢出、任意 malformed surrogate/pole 输入或其他 CDE hazard 顺序。JaguarCore 的公开不可变几何 API 不能直接构造 Rust 公共字段可表达的任意损坏内部 surrogate，因此这些内部公式输入仍未建立成对 reference。

### 权重接线 `get_pair_weight` / `get_container_weight`（tracker.rs，经 collision_loss.rs:71,92 调用）
- C++ 对应：`CollisionTracker::PairWeight(first, second)` 与 `ContainerWeight(index)`；当前主路径由 `EvaluateCandidateHazardLoss` 为每个 PlacedItem/Exterior 贡献乘相应权重，再交给 `SeparationEvaluator::Evaluate` callback 累加。tracker 和权重更新公式本身属 S18。
- 判定：【一致】
- 证据：`collision_loss.rs:70-71,90-92`、`CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss` 与 `SeparationEvaluator.cpp` 的 `Evaluate`。

### `EvaluateCandidateLoss` 的两个整项 API
- Rust 的 separation 主路径是 CDE 遍历 + `CollisionLossEvaluator::add` 的逐 hazard callback；C++ 对应 `SeparationEvaluator::Evaluate` + `EvaluateCandidateHazardLoss`。
- C++ 仍公开两个整项 API：(a) tracker-only overload 遍历其他件并算 overlap proxy；(b) layout overload 先收集再对已发现 hazard 求整项损失。两者是额外接口，不再是 `SeparationEvaluator` 当前使用的实现路径。
- 判定：【C++ 额外】（接口仍可用；Sparrow 分离主路径走逐 hazard Rust 对应）。
- 证据：`CollisionTracker.cpp` 的两个 `EvaluateCandidateLoss` overload 与 `EvaluateCandidateHazardLoss`；`SeparationEvaluator.cpp` 的 `Evaluate`。

### SIMD 有界变体（collision_loss.rs:73-82，`quantify_collision_poly_poly_simd_bounded`）
- C++ 对应：`CalculatePolygonCollisionLoss` 无 SIMD、无 `max_loss / weight` 的预除界（SIMD 通道见 S19——C++ 的 SoA/AVX2 是双精度独立通道，不是本函数的位等价替换）。即 Rust simd feature 下"先除权重再界后乘回"的有界短路路径在 C++ 侧无对应（C++ 默认对应 Rust 的 `#[cfg(not(simd))]` 标量路径，:84-88）。
- 判定：【C++ 缺失】（simd 特性路径；标量路径等价，Rust 默认构建也是标量）
- 证据：collision_loss.rs:73-88 vs OverlapProxy.cpp:106-139。

### `LbfLoss.h/.cpp`（任务指定核对项）
- 内容核验：`LbfLoss` = {xMax, yMax}，`FromBoundingBox`（取 bbox.xMax/yMax）、`Cost() = xMax*10 + yMax`（LbfLoss.cpp:25，X_MULTIPLIER=10 同 lbf_evaluator.rs:9）、`TightenSampleBBox`（Cost/10 收紧采样盒 xMax）、`operator<`（NaN 处理后比 Cost）。**不含任何碰撞损失**。它与 Rust `X_MULTIPLIER/Y_MULTIPLIER` 的对应已在 S09 记录（lbf_evaluator.rs:9-10）。TightenSampleBBox 的 Rust 对应在 S03（lbf.rs 的 bbox 收紧）。
- 判定：【一致】（归属确认：LBF 损失公式对应 lbf_evaluator.rs:48 的 10x+y；碰撞损失不在该文件）
- 证据：LbfLoss.cpp:14-56 vs lbf_evaluator.rs:9-10,46-49。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 流式短路与多 hazard 累加顺序：三项 PlacedItem fixture 的顺序 raw bits 为 `0x4ab5c931`，逆序 Rust 为 `0x4ab5c932`；bound=`0x4ab5c930/0x4ab5c931` 均返回 Invalid，bound=`0x4ab5c932` 返回 Collision，C++ 分类一致 | 已按固定 fixture 验证 | 不证明其他几何、所有 CDE 顺序及更宽的上界区间 | `collision_loss.rs:44-56`; `EvaluateCandidateHazardLoss`; `collision_loss_stream_reference.rs`; `TestQuantify.cpp` |
| 2 | SIMD 有界变体（`max_loss/weight` 预除 + bounded SIMD） | C++ 缺失 | Rust 默认构建同用标量路径，等价；simd feature 不对齐 | collision_loss.rs:73-82 vs OverlapProxy.cpp:106-139 |
| 3 | Hole：Rust 在损失分派中 panic；C++ 对应分派抛 `std::logic_error("unsupported hazard entity")`，Tracker 初始化与 moved-row 更新也进入同类失败通道 | 核心分派、Tracker 失败状态和 worker 搜索候选传播有 Rust/C++ reference；该新搜索夹具针对 QZone，Hole 的候选分派仍是直接 evaluator 路径 | worker 搜索调用链已证明异常冒泡、随机状态与布局/Tracker 不变、失败候选无 trace callback；双 worker QZone 的 Separator::Run 与 Optimize 有独立固定夹具；其他 worker 数及 listener callback 类型未穷尽 | `collision_loss.rs:68-95`; `container.rs:114-123`; `import.rs:175-194`; `SeparatorWorker.cpp`; `SeparatorWorkerTrace.h`; `collision_loss_unsupported_hazard_reference.rs`; `TestSeparator.cpp` |
| 4 | InferiorQualityZone：Rust 在损失分派中 panic；C++ 抛 `std::logic_error("unsupported hazard entity")`，Tracker 初始化同样失败 | 核心分派、Tracker 初始化和一条实际 worker 搜索调用链均按 Rust reference 验证 | Rust panic 与 C++ exception 在该调用链均中断搜索；双 worker QZone 的 Run 与顶层 Optimize 有固定传播夹具；其他 worker 数及错误源未认证 | `collision_loss.rs:68-95`; `SeparationEvaluator.cpp`; `SeparatorWorker.cpp`; unsupported-hazard references; `TestSeparator.cpp` |
| 5 | 两个 `EvaluateCandidateLoss` 整项重载（tracker-only 与 layout） | C++ 额外 | 作为独立 API 保留；`SeparationEvaluator` 当前使用逐 hazard helper，不使用这两个重载 | `CollisionTracker.cpp`; `SeparationEvaluator.cpp` |
| 6 | `OverlapProxy` 非有限/退化公式中间值：有限有效几何经有限平移可使 bbox `shapeArea=+Inf`，C++ 前置守卫已删除；malformed 零/负 bbox 及正 shoelace/零 bbox 乘积候选的构造边界已固定 | 溢出路径 Debug/Release 双侧；malformed bbox Rust 单侧；零乘积候选 Rust panic/C++ `InvalidPolygon` 双 profile | 溢出用例是低层 helper，不是完整 importer→evaluator 轨迹。固定零乘积候选两边均在 loss 前拒绝，41,761 个确定性候选亦无 Rust 构造成功；但有界搜索未证明任意成功构造几何都不可达。Rust malformed 零 bbox 为 Debug panic/Release `+0`；负 bbox 面积为 `-1` 时两 profile 返回 NaN。既有小矩形/极薄轮廓/矩形舍入/高纵横比搜索均未成功构造；完全塌缩的变换在 Rust/C++ bbox 重建阶段提前失败。Rust `SPolygon::new` 的 panic 与 C++ 结构化 `InvalidPolygon` 不是同一出口。无效/缺失 surrogate、矩形和其他公式组合仍开放 | `shape_area_guard_reachability_reference.rs`; `overlap_proxy_nonfinite_reference.rs`; `OverlapProxy.cpp`; `TestQuantify.cpp` |
| 7 | layout poly-poly pole 参数顺序、默认 importer 多 pole 几何、三项 hazard 固定次序及 Exterior 容器损失 | 已修复并由 Rust/C++ Debug/Release references 覆盖 | 只证明列出的几何、权重和发现顺序；其他 pole 集合与 hazard kind 仍未验证 | `overlap_proxy.rs:8-27`; `collision_loss_floor_reference.rs`; `collision_loss_stream_reference.rs`; `CollisionTracker.cpp`; `TestQuantify.cpp` |
| 8 | 候选 loss 下限：旧 `std::max(loss, 1e-6)` 已删除，当前返回逐 hazard 累计原值 | 已修复（Rust 原值语义） | 11 个 sub-micro 候选逐位匹配；多 hazard case 另验证累计 raw bits | `SeparationEvaluator.cpp`; `sep_evaluator.rs:92-93`; `collision_loss_floor_reference.rs`; `collision_loss_stream_reference.rs`; `TestQuantify.cpp` |
| 9 | 候选 X/Y 平移 `±Inf` 导致的 Exterior 非正规损失及 upper-bound profile 分类 | evaluator 入口按固定夹具映射 Rust Debug/Release 结果 | Rust 四种 f32 平移 × 六类上界；C++ 覆盖四种直接 double 无穷及四种 double→f32 无穷 × 六类上界；状态不变、计数和有限恢复已核对。C++ 直接公式 helper 的其他非有限输入仍未认证 | `quantify/mod.rs:49`; `SeparationEvaluator.cpp`; `collision_loss_stream_reference.rs`; `TestQuantify.cpp` |
| 10 | 候选旋转 `±Inf` 在 `compose()` 中使 `cos` 变 NaN 并失败 | Rust Debug/Release 均 panic，C++ Debug/Release 以 `domain_error` 映射；失败机制不同、状态对应 | Rust 2×6、C++ 4×6（包含有限 double 转 f32 无穷角）；失败前后计数、状态和有限恢复已核对 | jagua-rs `transformation.rs:146-150`; `infinite_candidate_rotation_panics_before_geometry_and_evaluator_recovers`; `TestQuantify.cpp`; `SeparationEvaluator.cpp` |
| 11 | 有限候选平移使 Exterior 无交集分支的 f32 质心距离平方溢出 | Rust/C++ Debug/Release evaluator 分类已对照 | 仅覆盖该 fixture 的 overlap.is_normal()/Collision(+Inf)、失败状态不变及有限恢复 | CollisionLossFiniteDistanceOverflowRust.json; collision_loss_stream_reference.rs; TestQuantify.cpp; OverlapProxy.cpp; CollisionTracker.cpp |
| 12 | 有限 Exterior raw loss 与 128 次 GLS 更新得到的 `+Inf` container weight 相乘 | Rust/C++ Debug/Release 均返回 Collision(+Inf) | 只覆盖列明的 weight 更新轨迹、评估计数、布局/Tracker raw loss 不变及有限恢复；不覆盖内部 raw-loss 公式溢出 | CollisionLossFiniteWeightOverflowRust.json; collision_loss_stream_reference.rs; TestQuantify.cpp; CollisionTracker.cpp |
| 13 | `CalculateContainerPenalty` 的最终 `f32` 乘法溢出：有限操作数下最后一步成为 `+Inf` | Rust 与 C++ 均可由成功导入的 Item 经 `Layout/CDE/SeparationEvaluator` 到达并返回 `Collision(+Inf)`；最终乘法 fixture 的输入、变换、偏置、内部转换、公式操作数均逐位相同 | RustReference 与 SparrowCore Debug/Release 均通过；另有凹形 Inflate、凸形 Deflate、顺时针/共线输入、adapter 指数临界两侧和一个真实 SPP importer→evaluator fixture 的 raw-bit 对照。一般几何及其他 SPP 输入仍未证明 | `shape_offset_geometry_reference.rs`; `TestQuantify.cpp`; `ShapeOffsetEvaluatorSpp.json`; `Offset.cpp`; `StraightSkeleton.cpp`; `SimplePolygon.cpp`; `Importer.cpp`; `OriginalShape.cpp`; `SeparationEvaluator.cpp` |

## 结论

碰撞损失公式与权重接线在源码层面对应，且 C++ 分离评估器已用 CDE callback 实现 Rust 的逐 hazard 加和和上界早停。非对称方形/矩形 fixture 的 11 个 sub-micro 候选位模式一致；三项 PlacedItem hazard fixture 的指定发现顺序 raw loss 为 `0x4ab5c931`，逆序 Rust 累加为 `0x4ab5c932`，C++ 按相同顺序匹配；Exterior 容器损失 raw bits 为 `0x391152f2`。上界邻域也已测明：bound 与无界累计结果相等仍返回 Invalid，增加 1 ULP 后恢复 Collision，Rust/C++ Debug/Release 一致。Clear 上界下零损失碰撞均为 `Collision(0)`。候选平移 `±Inf` 的指定 C++ evaluator profile 映射、旋转 `±Inf` 的变换合成失败均按 Rust Debug/Release 对照，核对评估次数、状态不变和有限恢复；有限输入导致的 Exterior 质心距离平方溢出、有限 raw loss 乘 GLS 权重溢出为 `+Inf`，以及本轮两条实际 evaluator subnormal 路径也按对应 profile 对照。直接 helper 的 `epsilon=f32::MAX` 证明 Debug NaN 断言与 Release IEEE 传播。容器损失最终乘法现由成功导入的 Rust/C++ Item 经手工 2×2 Container、CDE 和真实 evaluator 双侧触发为 `Collision(+Inf)`；该 fixture 的输入、变换、偏置/简化几何和公式操作数已逐位闭合。几何回归现覆盖严格凸与凹形 Inflate、凸形 Deflate、顺时针/共线输入及 adapter 指数边界两侧；一条完整 SPP importer→真实 evaluator fixture 也逐位匹配。其他 offset 外形/模式、更多 SPP 输入、一般几何位级一致、未测公式组合、malformed surrogate/矩形输入防御仍开放；缺失 surrogate 的高层调用边界已由 S17 配对闭合。C++ 对候选平移 `±Inf` 在几何遍历前映射、对旋转 `±Inf` 在变换合法化前抛异常，这些入口结果不证明一般非有限公式输入一致。Hole 与 InferiorQualityZone 的损失分派、Tracker 初始化/更新，以及单个 SeparatorWorker 搜索候选的异常传播进入 Rust panic、C++ `std::logic_error` 对应失败通道；worker fixture 还证明只消耗首个候选的 RNG、布局/Tracker 不变且不发出失败候选 callback。固定双 worker QZone failure 已覆盖 Separator::Run 的异常传播/重试状态，以及顶层 Optimize 的 callback cutoff；其他 worker 数、错误源和任意布局仍未认证。缺失 surrogate 的高层调用边界已由 S17 配对闭合；malformed surrogate/矩形及未测几何/pole 集合和 opt-in SIMD 仍未认证。`LbfLoss.h` 只包含 LBF 目标函数（10x+y），不包含碰撞损失。

### 实现与回归验证（2026-10-09）
1. **Exterior reference**：在 `CollisionLossFloorRust.json` 几何上按 Rust 顺序先设 base strip width=1.0，再创建问题；导入器的面积推导初始宽度无法构造该窄件夹具的 CDE POI，因此 C++ reference 显式重建同一 Items/Demands/Strip 后建问题。放置位置与候选 transform 对齐，Rust/C++ `SeparationEvaluator` 均分类为 Exterior collision，loss raw bits 均为 `0x391152f2`。
2. **累计上界相邻值**：三项 hazard unbounded loss 为 `0x4ab5c931`。Rust Debug/Release 与 C++ Debug/Release 对 `next_down(total)` 和 `total` 返回 Invalid；对 `next_up(total)` 返回 Collision，损失仍为 `0x4ab5c931`。这记录了逐项剩余预算扣减的浮点行为，不将其简化成“等于总损失即可接受”。
3. **回归范围**：RustReference 全量 Debug/Release 与 SparrowCore 全量 Debug/Release 在本轮均通过；对应 test names 为 `exterior_hazard_loss_matches_rust_container_quantification` 与 `collision_upper_bound_matches_adjacent_float_neighbors`。该验证不覆盖 listener/worker/phase 捕获边界。
4. **worker 搜索失败传播**：`unsupported_hazard_unwinds_from_worker_search_before_candidate_callback` 与 C++ `TestUnsupportedHazardPropagatesThroughWorkerSearch` 使用 seed=42，让首个容器样本命中由该样本 bbox 构造的 InferiorQualityZone；当前参考放置先评估为 Clear 并正常发出 Evaluation/Report，随后失败候选在 SeparationEvaluator 分派处 Rust panic/C++ `logic_error`。两侧 RNG-after 均等于只采样这一个候选后的状态；失败候选没有 Evaluation/Report/Result callback，布局和 Tracker 不变。RustReference 当前 Debug/Release、SparrowCore Debug/Release 全量 runner 均通过。此项只覆盖单 worker 搜索，不代表 `Separator::Run` 的并发任务、外层 listener 或阶段捕获已闭合。

### 补充审计（2026-09-29）
以下为 2026-09-29 时点的审计补记；其中关于“全量收集后事后比较”的表述已被 2026-10-06 的逐 hazard 实现取代，现行结论以本文主文、差异表和当日实现验证为准。

1. **【遗漏补记：`reload` 的 simd 分支内容】** 主文 reload 条目写"（simd 时）装载 poles SoA"但未展开。现场确认：collision_loss.rs:41 `poles_soa.load(&_shape.surrogate().poles)` 把**候选形状（被评估的移动件候选变换）**的 pole 装入 SoA（p2 方向），与标量路径的 `calc_weighted_loss_bounded`（:73-88 的 `quantify_collision_poly_poly_simd_bounded(other_shape, shape, &poles_soa, max_loss/weight)` 预除界）共同构成 simd feature 的有界短路链——C++ 无对应（归 S19 差异 #2，判定不变），此处补记函数体细节以闭合 reload 条目。
2. **【遗漏补记：`add` 的 remaining 计算】** Rust 对无界上界保持 `remaining=INFINITY`；当前 C++ 同样以正无穷作无界 sentinel，并在 callback 的 PlacedItem 单项比较/累计比较中避免将其误当有限预算。等号不触发 stop，超过才 stop。该控制流现位于 `SeparationEvaluator::Evaluate`，不再是事后全量比较。判定：【实现已按当前路径对齐】。
3. **【浅覆盖补全：calc_weighted_loss_bounded 的 Exterior 分支单项语义】** Rust Exterior 分支生成完整的容器惩罚乘 weight，不做 PlacedItem 专属的单项剩余预算比较，之后由 `add` 的总累计检查早停。当前 C++ `EvaluateCandidateHazardLoss` 也直接返回 Exterior 加权项，由 `SeparationEvaluator::Evaluate` 的累计上界检查决定是否停止；该 Exterior 专项 parity 后续已由 2026-10-09 的固定 fixture 闭合（见本文“实现与回归验证”第 1 条），仅认证列出的几何和参数。
4. **【核对确认：任务指定项 LbfLoss 归属】** 现场再验 LbfLoss.h（21 行）/LbfLoss.cpp（57 行）全文：`FromBoundingBox/TightenSampleBBox/Cost()/operator<`，无碰撞损失代码——主文归属判定维持（碰撞损失链在 OverlapProxy+CollisionTracker）。

### 实现与回归验证（2026-10-06）
1. **pole 参数顺序的 RED/GREEN 证据**：保留 Rust reference 的方形/矩形几何，在 0.137 rad 下，Rust 为 `0x32560315`，旧 C++ layout 调用顺序为 `0x32560316`；将 `CalculatePolygonCollisionLoss` 的实参改为 `(otherShape, transformedCandidate)` 后该断言通过。Rust 与 C++ 随后对 0、0.013、0.137、0.281、0.5235988、0.7853982、1.234、2.345、3.1415927、4.712389、5.999 rad 共 11 个候选逐位相同。
2. **历史 Rust/C++ 回归验证（2026-10-06）**：当日 RustReference 全量 `cargo test --locked` 的 12 个测试函数通过；Rust 两项 multi-hazard 测试核对发现次序、raw loss 和 early-stop；当时新增的 warm-start optimize→explore→compress 参考另见 README。2026-10-07 新增的 worker-move 压缩参考及当前套件 13 项结果记录在 README/S07/S18；本段只记录 10-06 的状态，不代表当前测试总数。以上 fixture 均不覆盖所有配置、几何、终止条件或 cold-start 默认整条 optimize 流程。

### 2026-10-12 多 worker 错误传播闭合

Rust Debug/Release 与 C++ Debug/Release 均执行了两 worker、固定 seed 与自定义 QZone 的失败轨迹。Separator 先发 iteration 0 progress；两侧 worker 都在首个候选的 unsupported-hazard 分派处 panic/抛异常，失败候选不产生报告或 SeparationResult，master layout 与 Tracker loss 保持原值。

C++ 原先在 future.get() 抛出时丢弃 worker lambda 的局部 RNG。现在 worker task 捕获 exception_ptr 并返回异常前的 RNG；父线程收集全部 future、提交每个 worker RNG 后再重抛首个 worker 异常。Rust 与 C++ 都在 catch 后复用同一 Separator 重试，最终完成分离；这验证失败前消耗的 worker 随机流状态没有回滚。

顶层 Optimize 夹具验证只发 Exploration phase、初始 ExploreFeasible report 和 iteration 0 progress，异常后不发 SeparationResult，也不进入 Compression/Final。warm-start 构造 child-stream 差异由同时布置两组候选 QZone 隔离，不把本用例扩展为 S08 RNG 派生顺序证明。

边界：只认证列出的 QZone 几何、seed、两 worker 和所测 callback prefix；不覆盖任意 worker 数、其他 hazard/错误源、所有 listener 类型、线程创建失败或 C++ 内部 Result-only 防御出口。

### S12 清单收口（2026-10-17）

工作项清单此前仍将“有限候选导致的 Exterior 质心距离平方溢出的其余路径”和“与 GLS 权重更新的组合用例”列为待补。现有定向 reference 已分别覆盖有限候选触发的质心距离平方溢出，以及 128 次真实 `update_weights()` 后有限 Exterior raw loss 与 `+Inf` 权重相乘；两者均比较 Rust/C++ Debug 与 Release 的分类、状态不变和有限候选恢复。容器损失最终乘法溢出、OverlapProxy 非有限/退化路径、Hole/QZone 分派与固定双 worker 传播也已有对应记录和 Rust/C++ 证据。

因此 S12 按默认标量 feature 和本文列出的固定 reference 范围标记完成。malformed surrogate/矩形表示、未列出的几何/pole/公式组合不从现有 fixture 外推；opt-in SIMD 的差异由 S19 单独跟踪。缺失 surrogate 的高层调用失败边界见 S17。
