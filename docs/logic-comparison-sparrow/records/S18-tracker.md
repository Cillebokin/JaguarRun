# S18. quantify/tracker.rs（CollisionTracker） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/tracker.rs`（212 行）
**C++**: `SparrowCore/Include/SparrowCore/Quantify/CollisionTracker.h`（78 行）+ `Source/Quantify/CollisionTracker.cpp`（864 行，2026-10-07 基线）
**判定**: 正常有限状态下的 loss/权重主路径基本等价；GLS 权重上限和部分碰撞倍率的 f32 舍入已按 Rust 修复并用同一 fixture 逐位验证。moved-item 更新现在只刷新对应逻辑行；C++ 临时检测器生命周期/布局对账、非有限输入处理与扩展 API 仍有差异

---

## 函数逐条比对（以 Rust 为纲）

### `struct CollisionTracker` / `pub type CTSnapshot`（tracker.rs:13-21）
- Rust 字段：`size`、`pk_idx_map: SecondaryMap<PItemKey, usize>`、`pair_collisions: PairMatrix`、`container_collisions: Vec<CTEntry>`。快照 = 整结构 clone。
- C++ 对应（CollisionTracker.h:71-76）：`m_Handles: vector<PlacedItemHandle>`（对应 pk_idx_map 的键序列，索引即值）、`m_PlacedItems: vector<PlacedItem>`（**C++ 额外缓存**，Rust 不存 placed item 副本而直接读 `l.placed_items[pk]`）、`m_PairMatrix`、`m_ContainerEntries`、`m_ContainerBounds`（缓存容器 bbox，Rust 每次从 `l.container.outer_cd.bbox` 取）、`m_IsSeparated`（**C++ 额外**，对应 Rust 侧无直接等价物——Rust 用 `get_total_loss()==0.0` 判断，separator.rs:137,193）。
- 快照：C++ `CollisionTrackerSnapshot{handles, pairMatrix, containerEntries, isSeparated}`（h:18-24），不含 m_PlacedItems/m_ContainerBounds（Restore 时从 layout 重建）。Rust 快照是全量 clone（含 pk_idx_map）。【差异】（快照内容范围不同，恢复路径补齐后语义等价）
- 判定：【差异】（结构映射：pk_idx_map→m_Handles 定位表；额外缓存字段）

### `CollisionTracker::new`（tracker.rs:24-45）
- C++ 对应：`Create`（cpp:67-76）+ 私有 `BuildFromLayout`（cpp:770-863）。
- 索引建立：Rust `l.placed_items.keys().enumerate()` 建 pk→idx（tracker.rs:30-32）。C++ 按 `snapshot.placements` 顺序填 `m_Handles`（cpp:783-787）——顺序一致（placement 顺序）✓。
- loss 重算：Rust 对每个 pk 调 `recompute_loss_for_item`（tracker.rs:38-40），逐项重置该行后 CDE 收集。C++ `BuildFromLayout` 对每个 item 调 JaguarCore 碰撞引擎 `CollectPolygonCollisionsUntil`（cpp:833-837）并填 pair/container loss（cpp:839-858）。
- 非对称检测边界：Rust 对每个 item 重算时先清零该 item 的整条共享 pair 行（tracker.rs:47-56）；后续 item 重算会再次清零与先前 item 共用的三角矩阵 pair 槽。Rust 自身承认部分边界输入的碰撞检测可能不对称（`util/assertions.rs:42-45,104-106`）。若先处理方向命中、后处理反向未命中，Rust 最终 pair loss 会被后一次清零；C++ `BuildFromLayout` 只在命中时赋值，不会因反向未命中清除先前命中（CollisionTracker.cpp:823-859），因此保留任一方向命中的 pair。此为源码推导的窄边界差异，未找到专门 Rust/C++ fixture；不能将初建碰撞集合笼统判为对所有边界输入内容等价。
- hazard 收集对（核对任务指定项）：Rust 用 jagua-rs CDE：`BasicHazardCollector::with_capacity(len+1)` + `l.cde().collect_poly_collisions(shape, &mut collector)` + `collector.remove_by_entity(&HazardEntity::from((pk, pi)))` 移除自身（tracker.rs:59-62）。C++ 把 placements 注册为 hazards（带各自 handle，cpp:801-821），`HazardFilter::irrelevantHandle` 排除自身 handle（cpp:823-831），再调用 `CollectPolygonCollisionsUntil`（cpp:832-837）。两侧均收集该 item 与其它 item/Exterior 的当前碰撞集合；C++ 从容器 collision engine 获取 Exterior hazard（cpp:854-858）。【一致】（渠道不同、内容等价）
- 自身排除：Rust `remove_by_entity`（按实体）；C++ 按 handle 过滤（BuildFromLayout cpp:825-831；UpdateMovedItem cpp:200-206）✓。
- `debug_assert!(tracker_matches_layout(&ot, l))`（tracker.rs:42）→ C++ 无逐行一致性自检，改为 UpdateMovedItem/Restore 中的显式对账校验。【差异】（既定 debug_assert→无/显式校验）
- 判定：【正常对称检测路径一致；非对称边界存在差异且未测试】（移动更新的实现细节见 `register_item_move`）
- 证据：tracker.rs:24-45,59-62 vs CollisionTracker.cpp:67-76,770-863。

### `recompute_loss_for_item`（tracker.rs:47-83）— 私有核心
- C++ 对应：没有单独命名的 `recompute_loss_for_item` helper。初建由 `BuildFromLayout` 的 per-item 循环收集（cpp:770-861）；移动更新则在 `UpdateMovedItem` 内只收集移动项的碰撞并重写其 pair 行和 container loss（cpp:158-257），不触碰其他 loss/weight 行。
- 逐项核对：
  1. 重置该行 loss：Rust 显式清零 `self.pair_collisions[(idx, i)].loss` 与 container loss（tracker.rs:53-56）。C++ rebuild 场景由新矩阵默认 0 替代 ✓。
  2. PlacedItem hazard → `quantify_collision_poly_poly(shape, shape_other)`（tracker.rs:71）→ C++ `CalculatePolygonCollisionLoss(m_PlacedItems[first].CollisionShape(), m_PlacedItems[*second].CollisionShape())`（BuildFromLayout cpp:849-852）✓（公式比对见 S17）。
  3. Exterior hazard → `quantify_collision_poly_container(shape, l.container.outer_cd.bbox)`（tracker.rs:76）→ C++ `CalculateContainerPenalty(..., m_ContainerBounds)`，其中 `m_ContainerBounds = snapshot.container.CollisionShape().BoundingBox()`（BuildFromLayout cpp:778,856-858）✓。
  4. `assert!(loss > 0.0, ...)`（tracker.rs:72,77）→ C++ 无对应断言（CalculateXxx 的防御保证非负有限，0 也被接受写入）。【差异】（既定 panic→容错；语义：Rust 保证碰撞必产生正 loss，C++ 允许 0 写入）
  5. `_ => unimplemented!`（tracker.rs:80）→ C++ if/else-if 静默忽略其他 kind（BuildFromLayout cpp:839-859；UpdateMovedItem cpp:216-245）。【差异】（既定 panic→跳过）
- 判定：【基本一致】（逻辑更新范围一致；C++ 将逻辑内联在 UpdateMovedItem，而非独立 helper）
- 证据：tracker.rs:47-83 vs CollisionTracker.cpp:158-257,770-861。

### `register_item_move`（tracker.rs:101-109）— 核对重点
- Rust：O(1) 索引换键（`pk_idx_map.remove(old).insert(new)`，tracker.rs:103-104）+ 对 moved item 执行 `recompute_loss_for_item(new_pk, l)`（tracker.rs:106），该重算会更新与该 item 相关的三角矩阵唯一 pair 条目及其 container 项。PairMatrix 的对称访问共用一份存储，因此无需重算邻居行；未移动的其它 pair/container 项保持不变。
- C++ 对应：`UpdateMovedItem`（cpp:83-259）：
  1. 前置对账：容器 bbox 不变、placement 数与矩阵尺寸一致（cpp:90-100）、previousHandle 在表中（cpp:102-108）、newHandle 在 layout（cpp:109-120）、其余 item 的 itemIndex/rotation/translation 与缓存逐项相等（cpp:128-156）——任何不符返回 Result 错误（Rust 无此检查，直接信任调用方）。
  2. 为当前 layout 创建临时 collision engine，并注册所有 placements 作为 hazards（cpp:158-184）；这会遍历所有 placement，但不计算所有 item pairs。
  3. 只对 moved placement 调用 `CollectPolygonCollisionsUntil`，过滤自身 hazard；按移动项为第一参数计算其 container loss 和相关 pair losses（cpp:186-245）。
  4. 只覆写 moved tracker index 对应的 PairMatrix 逻辑行和 container entry，保留其余行及全部 GLS weights；更新 handle、placement 缓存，再按总 tracked loss 更新 `m_IsSeparated`（cpp:247-258）。
- 语义对比：两侧均刷新 moved item 的容器损失及所有与之关联的唯一 pair 项，未移动项的缓存 loss/weight 保持不变。C++ 额外验证未移动 placement 的身份/变换未变，并每次新建临时 engine、注册全部 hazards；所以碰撞查询范围与 Rust moved-row 重算一致，但还有检测器创建/注册开销和额外对账，不会重算所有 pair loss。
- 性能/复杂度：Rust O(1) 换键后用 layout 的既有 CDE 重算 moved row；C++ 的对账循环对 N 个 tracker 项逐个搜索 N 个 placements，最坏 O(N²) 比较，随后 O(N) 注册 placements，再查询 moved shape 的碰撞集合。这里 O(N²) 指对账查找，不是 O(N²) pair-loss 重算。实际耗时仍依赖 JaguarCore collision engine 的空间索引和候选数量；当前用例只验证行为，没有建立两侧耗时对比。
- 判定：【基本一致】（刷新范围与保留状态对齐；检测器生命周期和 C++ 对账校验不同）
- 证据：tracker.rs:101-109 vs CollisionTracker.cpp:83-259；`RustSppCompressWorkerMove.json` / `CompressionWorkerMoveRustReference.tsv` / Rust `compression_worker_move_reference.rs` / C++ `TestCompressionWorkerMoveRetryMatchesRustReference`。

### `restore_but_keep_weights`（tracker.rs:85-95）
- Rust：`pk_idx_map` 整体替换为快照；pair/container 的 **loss** 逐项从快照拷贝，**weight** 保留当前值；随后 debug_assert 一致性。
- C++ 对应：`RestoreButKeepWeights`（cpp:322-366）：先缓存当前所有 weight，调 `Restore`（恢复 handles/matrix/entries/IsSeparated，cpp:266-320），再回写缓存 weight；另做前置尺寸校验。
- 语义核对：loss 全取快照、weight 全保当前、键表取快照——与 Rust 逐字一致 ✓（Rust 直接 zip 拷 loss，C++ 借道 Restore 再覆写 weight，最终状态相同）。
- C++ 额外：Restore 中的对账校验（快照 handles 与 layout placements 一一对应、entry 有效性，以及 `isSeparated` 与快照 tracked loss 是否为零相符，cpp:266-320）——Rust 仅 debug_assert。【C++ 额外】
- 判定：【一致】
- 证据：tracker.rs:85-95 vs CollisionTracker.cpp:266-366。

### `save`（tracker.rs:97-99）
- C++ 对应：`Save`（cpp:261-264）——按值拷贝四个字段（不含 m_PlacedItems/m_ContainerBounds，Restore 时重建）。Rust 为整结构 clone。【一致】（快照恢复语义等价，见上）
- 证据：tracker.rs:97-99 vs CollisionTracker.cpp:261-320。

### `update_weights`（tracker.rs:113-135）— Algorithm 8
- 逐项核对：
  1. max_loss：Rust 对 pair+container loss 做 `fold(0.0, f32::max)`（tracker.rs:115-118）；Rust `f32::max` 忽略单个 NaN、返回另一个操作数，正无穷则保留为最大值（[标准库文档](https://doc.rust-lang.org/std/primitive.f32.html#method.max)）。C++ 只接受 `isfinite && > maximumLoss` 的值（cpp:700-720），所以 NaN 被两侧排除，但正无穷在 Rust/C++ 的异常输入下不同；正常有限损失一致。
  2. multiplier：Rust `loss==0.0 → GLS_WEIGHT_DECAY(0.95)`；否则 `MIN_INC(1.2) + (MAX_INC(2.0)-MIN_INC(1.2)) * (loss/max_loss)`（tracker.rs:123-132；consts.rs:4-6）。C++ 现用相同的运算表达式 `1.2F + (2.0F - 1.2F) * (loss/maximumLoss)`（cpp:721-725）。原先的 `0.8F` 与 f32 的 `2.0F-1.2F` 相差 1 ULP，连续迭代会放大；已由部分碰撞比例的 Rust 位模式 reference 修复并验证。【已修复：有限正 loss】
  3. 更新：Rust `(weight * multiplier).max(1.0)`（tracker.rs:133）。C++ 现在只保留同样的 1.0 下限，不再截到 `1,000,000.0F`；同一 tracker 连续 20 次最大倍率更新得到 `1,048,576`，Rust/C++ 参考测试均精确通过。C++ `std::max` 与 Rust `f32::max` 对 NaN 的规则不同，但正常 tracker 状态要求有限、非负 loss；非有限输入处置见第 1 项。【已修复：有限有效状态】
  4. 覆盖集合：pair（三角含对角）+ container 全部条目 ✓（cpp:707-739 vs tracker.rs:121-134）。注意对角线条目 (i,i) 在 Rust 中存在于 data 但从不被赋非零 loss（recompute 时 (idx,idx) 自碰撞被排除）——两侧同样处理 ✓。
- C++ 额外重载：`UpdateWeights(float collisionMultiplier, float clearDecay = 0.95F)`（h:65-66，cpp:742-768）——固定倍率版，**Rust 无对应**（生产代码未调用，仅公共 API 扩展）。【C++ 额外】
- 判定：【正常有限状态一致】（更新倍率表达式、f32 舍入、下限及无上限均对齐；`max_loss` 对正无穷的处理差异和参数化重载另计）
- 证据：tracker.rs:113-135、consts.rs:4-6 vs CollisionTracker.cpp:698-740,742-768。

### `get_pair_weight` / `get_container_weight`（tracker.rs:137-145）
- C++ 对应：`PairWeight(first, second)` / `ContainerWeight(index)`（cpp:402-411,422-430）。Rust 按键查 SecondaryMap（越界 panic）；C++ 按索引 + `Contains`/边界检查越界返回 0.0F。【差异】（既定 panic→0 返回；且 C++ 键→索引转换由调用方完成）
- 证据：tracker.rs:137-145 vs CollisionTracker.cpp:402-430。

### `get_pair_loss` / `get_container_loss`（tracker.rs:147-157）
- C++ 对应：`PairLoss` / `ContainerLoss`（cpp:392-400,413-420）+ `IndexForHandle`（cpp:379-390，对应 pk_idx_map 查询）。`SafeLoss` 过滤非正/非有限值（cpp:26-35）——Rust 直接返回存储值。【差异】（防御）
- 证据：tracker.rs:147-157 vs CollisionTracker.cpp:379-420。

### `get_loss`（tracker.rs:159-167）— Algorithm 1
- Rust：`container_loss + Σ_i pair_collisions[(idx, i)].loss`（i 从 0..size **含 idx 自身**——对角条目 loss 恒 0，无害）。
- C++ 对应：`ItemLoss`（cpp:432-447）：`ContainerLoss(index) + Σ_{other != index} PairLoss(index, other)`——**显式跳过自身**。数值一致 ✓（对角为 0）；f32 求和顺序：Rust 0..size 顺序（含 idx 处的 0），C++ 跳过 idx——加 0.0 不改变 f32 结果，顺序同为升序 ✓。
- 判定：【一致】
- 证据：tracker.rs:159-167 vs CollisionTracker.cpp:432-447。

### `get_weighted_loss`（tracker.rs:169-177）
- C++ 对应：`WeightedItemLoss`（cpp:490-505）：`ContainerLoss*ContainerWeight + Σ PairLoss*PairWeight`，显式跳过自身。Rust 用 `weighted_loss()`（= weight*loss，tracker.rs:209-211）；C++ 逐项显式相乘（未走 CollisionEntry::WeightedLoss 的防御过滤）。一致 ✓（求和顺序同上）。
- 证据：tracker.rs:169-177 vs CollisionTracker.cpp:490-505。

### `get_total_loss`（tracker.rs:179-187）
- C++ 对应：`TotalLoss`（cpp:507-515）按 `containerTotal + pairTotal` 汇总，外层加法次序与 Rust（tracker.rs:180-186）一致。C++ `SafeLoss` 过滤非有限/非正值；Rust 直接求和。PairMatrix 内部的 pair 遍历顺序仍受两侧物理布局影响（见 S17），指定 separator trace 对总 loss 用 `2e-2` 容差，不能据此外推任意输入的 ULP 上界。【差异】（防御过滤和 pair 内部遍历次序）
- 证据：tracker.rs:179-187 vs CollisionTracker.cpp:507-516、PairMatrix.cpp:80-91。

### `get_total_weighted_loss`（tracker.rs:189-199）
- C++ 对应：`TotalWeightedLoss`（cpp:518-526）按 `containerWeightedTotal + pairWeightedTotal` 汇总，外层顺序与 Rust（tracker.rs:189-199）一致；`CollisionEntry::WeightedLoss` 仍有有限值/正值防御，PairMatrix 内部物理遍历顺序也可能不同。正常有效状态的行为对应，但未证明任意输入逐位一致。【差异】（防御过滤与内部遍历次序）
- 证据：tracker.rs:189-199 vs CollisionTracker.cpp:518-526。

### `CTEntry` / `CTEntry::weighted_loss`（tracker.rs:202-211）
- C++ 对应：`CollisionEntry`（PairMatrix.h:11-17）+ `WeightedLoss`（PairMatrix.cpp:9-18，带防御）。字段默认值 {loss=0, weight=1} 与 Rust 构造一致 ✓。
- 判定：【一致】（防御另计）

### C++ 额外成员（Rust 无对应）
- `Rebuild(layout)`（h:31，cpp:78-81）：公共全量重建入口——Rust 对应操作是重新 `CollisionTracker::new`（separator.rs:261 rollback 无快照分支），C++ 提供就地版本。【C++ 额外】
- `IsSeparated()`（cpp:529-532）+ `m_IsSeparated`：Rust 无存储标志，以 `get_total_loss()==0.0` 动态判断（separator.rs:137,193；compress.rs:76）。C++ 在 `BuildFromLayout`、`UpdateMovedItem` 后均按 tracker `TotalLoss()==0` 更新；`Restore` 验证快照标志与快照 tracked losses 一致（cpp:257,301,861）。它不是从 `layout.IsFeasible()` 推导。C++ 缓存字段与 Rust 动态查询的存储形态不同；正常 API 更新后判定依据对应。`TestQuantify.cpp` 仍覆盖 C++ 的更新后状态。
- `EvaluateCandidateLoss` 两个重载（h:55-63，cpp:534-696）：Rust 无 tracker 内对应——候选损失评估在 Rust 位于 eval/collision_loss.rs（`CollisionLossEvaluator`，见 S12 记录）。第一重载（无 layout，遍历所有已放置项，cpp:571-576 用 `CalculateOverlapProxy` 裸代理值，**不含 shape penalty/epsilon 公式**）在 Rust 侧无对应物（Rust 只有带完整公式的 bounded 版本）；生产使用的第二重载（layout 版，经 hazard 收集 + `CalculatePolygonCollisionLoss`）与 Rust `calc_weighted_loss_bounded` 语义对应（S12 已详录）。【C++ 额外】（第一重载）/【差异】（位置归属，结果对应 S12）
- 证据：CollisionTracker.h:29-66 vs tracker.rs 全文、collision_loss.rs:62-96。

---

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `register_item_move`：Rust 使用既有 CDE 重算 moved row；C++ 新建临时 collision engine、注册所有 placement 后只收集 moved row | 【基本一致】 | loss 更新范围和其余行/weights 保留语义对应；C++ 额外有最坏 O(N²) 的 placement 对账及 O(N) hazard 注册，未测耗时 | tracker.rs:101-109 vs CollisionTracker.cpp:83-259; worker-move Rust fixture |
| 2 | hazard 收集渠道：Rust jagua-rs CDE（`l.cde().collect_poly_collisions` + `remove_by_entity`）vs C++ JaguarCore（新建 CollisionEngine + RegisterHazard + `CollectPolygonCollisionsUntil` + handle 过滤） | 【一致】 | 检测器实例不同、碰撞集合内容等价 | tracker.rs:59-62 vs CollisionTracker.cpp:801-837,200-212 |
| 3 | `assert!(loss > 0.0)` / `unimplemented!` / `debug_assert!(tracker_matches_layout)` → C++ 无断言、静默跳过未知 kind、显式对账校验 | 【差异】 | 既定 panic→Result/容错 | tracker.rs:42,72,77,80 vs CollisionTracker.cpp:83-156,216-245,839-859 |
| 4 | `update_weights`：曾使用 `0.8F` 替代 f32 运算 `2.0F-1.2F`，并额外截顶 1e6 | 【已修复：有限有效状态】 | 两侧对齐后，20 次最大倍率结果均为 1,048,576；部分碰撞比例的第 20 次权重位模式均为 `0x480192d6`。非有限 loss 的 max/floor 规则仍须单独界定 | tracker.rs:113-135、consts.rs:4-6 vs CollisionTracker.cpp:698-740; `CollisionTrackerWeightRust.json` |
| 5 | C++ 参数化重载 `UpdateWeights(multiplier, decay)` | 【C++ 额外】 | 生产未调用（公共 API 扩展） | CollisionTracker.h:65-66, cpp:742-768 |
| 6 | Total/Item loss 求和：Total 外层现在同为 container→pair；PairMatrix 内部物理迭代顺序与防御过滤仍不同 | 【差异】 | 指定 trace 对 loss/weight 用 2e-2/5e-2 容差；没有测得任意输入的最大误差 | tracker.rs:159-199 vs CollisionTracker.cpp:432-447,490-526、PairMatrix.cpp:80-101 |
| 7 | C++ `SafeLoss`/`IsValidEntry`/`WeightedLoss` 防御过滤 vs Rust 直接读值 | 【C++ 额外】 | 正常数据等价 | CollisionTracker.cpp:26-35 vs tracker.rs:137-157 |
| 8 | C++ `IsSeparated`/`m_PlacedItems`/`m_ContainerBounds` 缓存字段与 `Rebuild` 公共入口 | 【C++ 额外】 | `IsSeparated` 按 tracked `TotalLoss()==0` 派生/校验，与 Rust 判定依据对应；缓存存储形态不同 | CollisionTracker.h:31,54,71-76; cpp:257,301,861 vs separator.rs:137,193 |
| 9 | C++ `EvaluateCandidateLoss` 两重载（Rust 对应逻辑在 eval/collision_loss.rs，S12 已录）；无 layout 重载用裸 `CalculateOverlapProxy` 无 shape penalty | 【C++ 额外】/【差异】 | 生产走 layout 重载，与 Rust bounded 版对应 | CollisionTracker.cpp:534-696 vs collision_loss.rs:62-96 |
| 10 | `restore_but_keep_weights`：loss 取快照、weight 保留——两侧一致 | 【一致】 | — | tracker.rs:85-95 vs CollisionTracker.cpp:266-366 |
| 11 | 键→索引：Rust SecondaryMap(PItemKey→idx) 越界 panic vs C++ `IndexForHandle` 线性扫描返回 optional，调用方检查 | 【差异】 | 既定 panic→optional | tracker.rs:16,137-139 vs CollisionTracker.cpp:379-390 |
| 12 | 初建 tracker 时共享 pair 槽在非对称检测下的清零差异：Rust 后处理项会清除该 pair，C++ 只写命中且不清除 | 【差异：代码推导，未测试】 | Rust 最终保留后处理方向的结果（可能为 0），C++ 保留任一方向的命中；影响依赖 CDE 对该 pair 是否非对称 | tracker.rs:47-56; util/assertions.rs:42-45,104-106 vs CollisionTracker.cpp:823-859 |

Fixture 与断言：`TestQuantify.cpp:168-303` 覆盖 C++ tracker 的 Create/Save/Restore/UpdateWeights/UpdateMovedItem/IsSeparated（含 stale 快照与 NaN entry 防御）；Rust `SeparatorRoundRustTrace.tsv` 由 `TestSeparator.cpp` 消费，loss/weight 分别按 `2e-2`/`5e-2` 容差比较，计数/状态字段精确断言。`CollisionTrackerWeightRust.json` 由 Rust `tracker_weight_reference.rs` 与 C++ 测试共用：20 次更新后，完全重叠 pair 权重精确为 1,048,576，部分碰撞 pair 第 20 次权重位模式为 `0x480192d6`。新增 `RustSppCompressWorkerMove.json` 与 `CompressionWorkerMoveRustReference.tsv` 通过 Rust `compression_worker_move_reference.rs` 和 C++ `TestCompressionWorkerMoveRetryMatchesRustReference` 对照固定 seed=42、单 worker 两次失败压缩尝试；每轮分别为 438 evaluations/8 moves/5 iterations 和 362/6/4，事件序、报告位置/loss、最终 placements 及规定容差均检查。它证明该压缩重试路径能触发 tracker moved-row 更新并复现 Rust 决策，不是随机 move 下所有 tracker loss/weight 的逐字段 trace。

## 结论

tracker 的 GLS 权重更新已有指定 Rust/C++ 位模式证据，且已移除 1e6 权重上限；moved-item 更新现在只刷新对应逻辑行，Rust worker-move 重试 fixture 验证了该更新在一条实际压缩轨迹中的决策结果。初建 tracker 在检测器非对称边界下存在共享 pair 清零/保留差异，目前仅源码推导、没有专用 fixture；正常对称路径的固定用例不覆盖它。C++ 每次更新仍会新建 collision engine 并注册全部 placements，且保留额外布局对账；性能差异尚未测量。separator trace 的其他 loss/weight 字段仍按容差比较，不能据此宣称所有 tracker 状态逐位一致。非有限 loss 的 max/floor 规则、一般随机 move 下的逐字段 loss/weight/weighted-loss 轨迹仍待补齐。

### 补充审计（2026-09-29，实体句柄专项；以下行号为当时记录）
**【任务指定核对项】CollisionTracker 私有引擎的 hazard 实体句柄（2026-09-29 修复，commit 6910145）现场复验**：

1. **修复内容确认**：`BuildFromLayout`（cpp:673-682）与 `EvaluateCandidateLoss(layout,...)`（cpp:488-506）注册 hazard 时，实体字段已从 `{PlacedItem, itemIndex, 0, 0}`（仅 itemIndex，同 item 多放置会撞 CollisionEngine::RegisterHazard 的重复实体检查，CollisionEngine.cpp:101-112 `slot.hazard->entity == hazard.entity` → 报错 "hazard entity is already registered"）改为 `{PlacedItem, itemIndex, 0, 0, m_Handles[itemSequence], placedItem.Transform()}`——即补上 `placedItemHandle`（布局槽位+generation）与 `transform` 两个字段，附注释"用布局句柄+变换保证实体唯一性，同 Layout::MakeHazard"。
2. **与 Rust pk 语义的对应验证**：jagua-rs 的 hazard 实体 `HazardEntity::PlacedItem { pk, .. }` 以 slotmap PItemKey（每次放置唯一）标识；JaguarCore `HazardEntity::operator==`（hazard.h:57-62）对 PlacedItem 比较 `itemIndex && placedItemHandle && f32 精度的 rotation/translation`（hazard.h:44-46 两个字段）——C++ 的 `PlacedItemHandle{slotIndex, generation}`（Layout.cpp:90 AllocateHandle 的槽位复用+代数）在"每次放置唯一"意义上与 pk 对齐：remove_item 后 re-place 走新 slot/新 generation，即使同 itemIndex 也构成不同实体。同 item 两处放置（itemIndex 相同）现在因 handle 不同而实体不同 ✓。
3. **改造点覆盖完整**：经 grep 复验，SparrowCore 内私有引擎注册 hazard 的位置共 3 处——SeparationEvaluator.cpp:76-101、CollisionTracker.cpp:488-506（EvaluateCandidateLoss）、CollisionTracker.cpp:673-682（BuildFromLayout），6910145 全部改齐（diff 确认 3 处同模式）；Exploration.cpp 的 `CollectContainedHandles`（:364-377）复用布局自带引擎快照（`engine->Restore(cdeSnapshot)`），实体来自 Layout::MakeHazard（Layout.cpp:97-103，本就带 handle+transform），无需改。**结论：实体唯一性修复已全覆盖，与 Rust pk 语义对应正确，主文"2026-09-29 修复注记"（S06 头部第②条）的描述准确**。
4. **【浅覆盖补全】** 主文"register_item_move"条目的对账校验清单（cpp:77-138）经现场核对更新：容器 bbox 不变（:77-87）、previousHandle 在表（:89-94）、newHandle 在 layout（:96-108）、**逐项校验其余 item 的 transform 与缓存一致**（:110-138，通过 `candidate.handle == expectedHandle` 定位后比对旋转/平移）——主文已概括为"其余 item 的 itemIndex/rotation/translation 与缓存逐项相等"，判定维持。
5. **【历史行数记录】** 2026-09-29 补充审计时 CollisionTracker.cpp 为 730 行；2026-10-07 当前基线为 864 行，主文引用已按当前函数位置更新。
