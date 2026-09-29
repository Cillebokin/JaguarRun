# S12. collision_loss（碰撞损失流式评估）+ Quantify 权重接线 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/collision_loss.rs`（97 行）+ 权重入口 `quantify/mod.rs`（quantify_collision_poly_poly / quantify_collision_poly_container）+ `quantify/tracker.rs` 的 get_pair_weight/get_container_weight
**C++**: `SparrowCore/Include/SparrowCore/optimizer/LbfLoss.h`（21 行，仅 LBF 损失，不涉碰撞）+ 实际对应物 `Source/Optimizer/SampleEval.cpp`（1 行，无碰撞逻辑）+ **碰撞损失与权重接线实际落在** `Source/Quantify/CollisionTracker.cpp`（EvaluateCandidateLoss，两重载）+ `Source/Quantify/OverlapProxy.cpp`（CalculatePolygonCollisionLoss / CalculateContainerPenalty / CalculateOverlapProxy）
**判定**: 基本等价（公式与权重语义等价；流式短路与 SIMD 有界变体为 C++ 缺失/替换项）

归属说明：任务指定"optimizer/LbfLoss.h 的碰撞损失部分"——经核验 `LbfLoss.h/.cpp`（21+57 行）只含 LBF 的 `xMax*10+yMax` 损失与 bbox 收紧，**没有**碰撞损失代码（Rust collision_loss 的对应物不在该文件）。C++ 的碰撞损失接线是 `SeparationEvaluator` 调 `CollisionTracker::EvaluateCandidateLoss`（layout 重载，CollisionTracker.cpp:453-553），权重经 `PairWeight`/`ContainerWeight`（cpp:311-343），量化公式在 `OverlapProxy.cpp`。以下按 Rust 函数为纲比对这条链。

## 函数逐条比对

### `CollisionLossEvaluator::new`（collision_loss.rs:25-35）
- C++ 对应：无独立结构。状态（layout 引用、ct 引用、current_pk、loss、loss_bound）被摊进 `SeparationEvaluator`（m_Layout/m_Tracker/m_MovingIndex，SeparationEvaluator.h:33-36）与 `EvaluateCandidateLoss` 的局部变量。
- 判定：【一致】（内联化；Rust 该结构本身是 pub(super) 私有辅助）
- 证据：collision_loss.rs:14-22 vs SeparationEvaluator.h:32-42、CollisionTracker.cpp:453-462。

### `reload`（collision_loss.rs:37-42）
- Rust：每次 evaluate 前重置 `loss=0.0`、设 `loss_bound`、（simd 时）装载 poles SoA。C++ 对应：`EvaluateCandidateLoss` 每次调用从 `result = 0.0F` 重新累加（cpp:526 起），上界不在 tracker 层（在 SeparationEvaluator::Evaluate cpp:188-193 比较）。
- 判定：【一致】（无状态化差异；上界检查位置不同已在 S11 差异#1 记录）
- 证据：collision_loss.rs:37-42 vs CollisionTracker.cpp:526。

### `add`（collision_loss.rs:44-56）— 逐 hazard 累加 + 短路
- C++ 对应：**没有逐 hazard 的 add 回调**。C++ 在 `EvaluateCandidateLoss(layout, ...)` 中先全量收集碰撞 hazard（cpp:486-514，两段 Collect*Until 且 should_stop 恒 false），再循环累加（cpp:527-548）。
- 语义核对：
  1. **每项贡献**：Rust PlacedItem hazard → `quantify_collision_poly_poly(other_shape, shape) * pair_weight`（:86-88）；Exterior hazard → `quantify_collision_poly_container(shape, container_bbox) * container_weight`（:90-93）。C++：PlacedItem → `CalculatePolygonCollisionLoss(transformed, other) * PairWeight(movingIndex, otherIndex)`（cpp:539-542）；Exterior → `CalculateContainerPenalty(transformed, m_ContainerBounds) * ContainerWeight(movingIndex)`（cpp:543-547）。逐项对应 ✓。
  2. **短路条件**：Rust 每 add 后 `self.loss > self.loss_bound` 即返回 true（提前终止遍历，:55）；C++ 无短路，累加完在 SeparationEvaluator 侧比较（S11 差异#1）。接受性结果一致。【差异】
  3. **`unimplemented!` 分支**（Rust :94，非 PlacedItem/Exterior 的 hazard）：C++ 循环只处理这两种 kind，其他静默跳过（cpp:528-548 的 if/else-if）——Rust panic → C++ 忽略。【差异】（既定 panic→跳过）
- 判定：【差异】（流式短路缺失；结果等价）
- 证据：collision_loss.rs:44-56 vs CollisionTracker.cpp:527-548。

### `calc_weighted_loss_bounded`（collision_loss.rs:62-96）— 量化公式
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:137-187）与 `CalculateContainerPenalty`（cpp:185-246）。逐公式核对：
  1. **poly-poly**（Rust quantify_collision_poly_poly，mod.rs:15-24）：`epsilon = max(d1,d2) * 0.01`（OVERLAP_PROXY_EPSILON_DIAM_RATIO，consts.rs:7）；`overlap_proxy = overlap_area_proxy(sur1, sur2, eps) + eps²`；`penalty = sqrt(sqrt(hull1) * sqrt(hull2))`（calc_shape_penalty，mod.rs:26-30：`p1=sqrt(ch_area1), p2=sqrt(ch_area2), (p1*p2).sqrt()`）；`loss = sqrt(overlap_proxy) * penalty`。C++ CalculatePolygonCollisionLoss：epsilonDiameterRatio=0.01（cpp:159）✓；`rawProxy = CalculateOverlapProxy(...Identity,Identity,eps)`、`overlapProxy = rawProxy + eps*eps`（cpp:170-171）✓；ShapePenalty = `sqrt(sqrt(a1)*sqrt(a2))`（cpp:98-100）✓；`sqrt(overlapProxy) * shapePenalty`（cpp:180）✓。**注意**：C++ 传给 CalculateOverlapProxy 的是两个 **Identity 变换**（cpp:163-169）——因为两个形状都已变换到世界系（transformed 与 placed 的 CollisionShape），Rust 的 overlap_area_proxy 收到的同样是变换后的 SPolygon 的 surrogate（:86 `quantify_collision_poly_poly(other_shape, shape)`，两形状均为世界系）。等价 ✓。
  2. **overlap_area_proxy**（overlap_proxy.rs:8-27）：双循环所有 pole 对，`pd = r1+r2-dist`，`pd_decay = pd>=eps ? pd : eps²/(2eps-pd)`，`total += pd_decay * min(r1,r2)`，最后 `* PI`。C++ CirclePairContribution（OverlapProxy.cpp:26-70）：penetration = r1+r2-dist ✓（dx/dy 在 float 上算，center 经 ApplyRustF32 变换——**差异**：Rust 传入的 shape 已是世界系、pole.center 已变换，直接 distance_to；C++ pole 是局部系，靠 firstTransform/secondTransform 变换——但两处调用均传 Identity，故中心不做任何变换，直接差 ✓）；decayedPenalty 三元与 `eps²/(2eps-pd)` 逐字对应（cpp:57-59）✓；`* min(r1,r2)`（cpp:60）✓；total `*= rustPi`（cpp:128，rustPi=3.14159265358979F，f32 位模式与 Rust `PI` 一致）✓。
  3. **poly-container**（Rust quantify_collision_poly_container，mod.rs:36-55）：有交集 → `(shape_area - inter_area) + 0.0001*shape_area`；无交集 → `shape_area + centroid 距离`；`loss = 2.0 * sqrt(overlap) * penalty(s,s)`。C++ CalculateContainerPenalty（cpp:185-245）：交集条件 `intersectionXMin < intersectionXMax && ...`（严格小于，对应 Rust `Rect::intersection` 的 Some/None——jagua-rs intersection 在退化零面积时返回 None？经核验 Rect::intersection 对零宽/高返回 None，即严格正交集，与 C++ 严格 < 一致）✓；`(shapeArea - intersectionArea) + 0.0001F * shapeArea`（cpp:221-223）✓；无交集 `shapeArea + RectCentroidDistance`（cpp:225-227，中心距离在 f32 上算 ✓）；`2.0F * sqrt(overlap) * shapePenalty`（cpp:239）✓。
  4. **精度路径**：Rust 全 f32（pole 半径/中心、面积、距离均 f32）；C++ pole 的 center/radius 存 double（Circle2d），差/距离在 cast float 后算（cpp:41-51）、ConvexHullArea/直径 double→float cast（cpp:88-90,141-144）、面积在 float 上算（cpp:196-199）。即 C++ 存储 double、运算 f32——与 RustF32 双轨一致，被 fixture 的 loss 位模式锁定。【一致】
  5. **C++ 额外的退化防御**：处处 isfinite/<=0 检查返回 0（Rust debug_assert is_normal + 直接算）。【C++ 额外】
- 判定：【一致】（公式逐字对应；防御差异另计）
- 证据：collision_loss.rs:62-96 + quantify/mod.rs:15-55 + overlap_proxy.rs:8-27 vs OverlapProxy.cpp:26-70,98-100,108-131,137-187,185-246；`SeparatorSearchRustTrace.tsv` 的 collision loss 位模式（如 reference 首条 `40afead0`≈5.498）逐位匹配锁定此链路。

### 权重接线 `get_pair_weight` / `get_container_weight`（tracker.rs，经 collision_loss.rs:71,92 调用）
- C++ 对应：`CollisionTracker::PairWeight(first, second)`（CollisionTracker.cpp:311-329）与 `ContainerWeight(index)`（cpp:331-343），接线点：EvaluateCandidateLoss(layout) 的 cpp:541-542（pair）与 cpp:545-547（container）。权重数值本身（GLS 递增/衰减，GLS_WEIGHT_* 常量，consts.rs:4-6）属 S18（tracker）范围，本记录确认接线方向：Rust poly-poly 项乘 `ct.get_pair_weight(current_pk, other_pk)`、container 项乘 `ct.get_container_weight(current_pk)`；C++ 同乘对应权重 ✓。
- 判定：【一致】
- 证据：collision_loss.rs:70-71,90-92 vs CollisionTracker.cpp:437-442（候选重载）/539-547（layout 重载）。

### `EvaluateCandidateLoss` 的两个重载（CollisionTracker.cpp:400-452 与 453-553）
- Rust 对应：无（Rust 是流式：CDE 遍历 + add 回调）。C++ 提供：(a) tracker-only 重载（cpp:400-452）：**遍历所有其它件**（不经 CDE 收集）直接 `CalculateOverlapProxy`（surrogate 级，含变换）——这是"无布局引擎"的近似路径；(b) layout 重载（cpp:453-553）：**经 CDE 精确收集**碰撞对再累加——这是 SeparationEvaluator 实际使用的路径（SeparationEvaluator.cpp:182-183 传 layout）。(b) 与 Rust 语义对应（只有真实碰撞的件才贡献损失）；(a) 是 C++ 额外的公共 API（供无引擎场景/测试），且它不套用 `+eps²`、sqrt、ShapePenalty 的完整 poly-poly 公式而是裸 proxy。【C++ 额外】（路径 (a)；注意 SeparationEvaluator 用的是 (b)，语义与 Rust 对齐）
- 证据：CollisionTracker.cpp:400-452 vs 453-553；SeparationEvaluator.cpp:182-183。

### SIMD 有界变体（collision_loss.rs:73-82，`quantify_collision_poly_poly_simd_bounded`）
- C++ 对应：`CalculatePolygonCollisionLoss` 无 SIMD、无 `max_loss / weight` 的预除界（SIMD 通道见 S19——C++ 的 SoA/AVX2 是双精度独立通道，不是本函数的位等价替换）。即 Rust simd feature 下"先除权重再界后乘回"的有界短路路径在 C++ 侧无对应（C++ 默认对应 Rust 的 `#[cfg(not(simd))]` 标量路径，:84-88）。
- 判定：【C++ 缺失】（simd 特性路径；标量路径等价，Rust 默认构建也是标量）
- 证据：collision_loss.rs:73-88 vs OverlapProxy.cpp:137-187。

### `LbfLoss.h/.cpp`（任务指定核对项）
- 内容核验：`LbfLoss` = {xMax, yMax}，`FromBoundingBox`（取 bbox.xMax/yMax）、`Cost() = xMax*10 + yMax`（LbfLoss.cpp:25，X_MULTIPLIER=10 同 lbf_evaluator.rs:9）、`TightenSampleBBox`（Cost/10 收紧采样盒 xMax）、`operator<`（NaN 处理后比 Cost）。**不含任何碰撞损失**。它与 Rust `X_MULTIPLIER/Y_MULTIPLIER` 的对应已在 S09 记录（lbf_evaluator.rs:9-10）。TightenSampleBBox 的 Rust 对应在 S03（lbf.rs 的 bbox 收紧）。
- 判定：【一致】（归属确认：LBF 损失公式对应 lbf_evaluator.rs:48 的 10x+y；碰撞损失不在该文件）
- 证据：LbfLoss.cpp:14-56 vs lbf_evaluator.rs:9-10,46-49。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 流式短路：Rust 每 hazard 累加后 `loss > bound` 即停；C++ 全量收集后一次性计算与比较 | 差异 | 接受/拒绝结果等价；C++ 无逐项提前终止（性能），碰撞集合收集完整 | collision_loss.rs:44-56 vs CollisionTracker.cpp:527-548 + SeparationEvaluator.cpp:188-193 |
| 2 | SIMD 有界变体（`max_loss/weight` 预除 + bounded SIMD） | C++ 缺失 | Rust 默认构建同用标量路径，等价；simd feature 不对齐 | collision_loss.rs:73-82 vs OverlapProxy.cpp:137-187 |
| 3 | `unimplemented!`（未知 hazard）→ C++ 静默跳过 | 差异 | 既定 panic→跳过；CDE 只产 PlacedItem/Exterior/Hole，Hole 已被 filter 排除 | collision_loss.rs:94 vs CollisionTracker.cpp:527-548 |
| 4 | `EvaluateCandidateLoss` tracker-only 重载（不经 CDE，直接全件对 proxy） | C++ 额外 | 公共 API；SeparationEvaluator 未使用（用 layout 重载，语义对齐 Rust） | CollisionTracker.cpp:400-452 vs collision_loss.rs 全文 |
| 5 | 量化公式退化防御（isnormal 断言 → isfinite/0 检查返回 0） | C++ 额外 | panic→0 化；正常数据不触发 | mod.rs:24,53 + overlap_proxy.rs:24 vs OverlapProxy.cpp 各处 return 0.0F |
| 6 | pole 中心变换路径：Rust 已是世界系坐标直算；C++ 局部系 + Transform 参数（实际调用传 Identity，等价） | 差异 | 形式差异，实际等价（两调用点均 Identity） | overlap_proxy.rs:13 vs OverlapProxy.cpp:38-45,163-169 |
| 7 | 候选 loss 下限：SeparationEvaluator 侧 `std::max(loss, 1e-6)`（见 S11）与 tracker 侧 `std::max(result, 0.0F)`（cpp:552） | C++ 额外 | 防 0/负 loss；正常量化值远大于阈值 | CollisionTracker.cpp:552 vs collision_loss.rs:58-60 |

## 结论

碰撞损失的量化公式链——`ε=max(d)·0.01`、pole 对累加 `pd_decay·min(r)` 乘 π、`+ε²`、几何均值 ShapePenalty、`sqrt(proxy)·penalty`（件-件）与 bbox 交集/质心距离、`2·sqrt(overlap)·penalty`（件-容器）——在 C++ OverlapProxy/CalculatePolygonCollisionLoss/CalculateContainerPenalty 中逐字复刻（含 f32 运算路径），权重接线（PairWeight/ContainerWeight）与 Rust 的 get_pair_weight/get_container_weight 一一对应，并被 SeparatorSearchRustTrace.tsv 的 loss 位模式锁定。主要结构差异是 Rust 的"流式逐 hazard 累加+上界短路"被替换为"全量收集+整体计算"，接受性结果等价但失去短路；SIMD 有界变体为 C++ 缺失（默认标量路径两侧一致）。任务指定的 LbfLoss.h 经核验仅含 LBF 损失（10x+y），不含碰撞部分。
