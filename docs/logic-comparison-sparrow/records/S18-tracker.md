# S18. quantify/tracker.rs（CollisionTracker） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/tracker.rs`（211 行）
**C++**: `SparrowCore/Include/SparrowCore/Quantify/CollisionTracker.h`（78 行）+ `Source/Quantify/CollisionTracker.cpp`（728 行）
**判定**: 基本等价（loss/权重语义与读接口逐项对应；rebuild/moved-item 更新路径实现不同但结果等价，另含 C++ 防御层与 Rust 无对应的参数化 UpdateWeights 重载）

---

## 函数逐条比对（以 Rust 为纲）

### `struct CollisionTracker` / `pub type CTSnapshot`（tracker.rs:13-21）
- Rust 字段：`size`、`pk_idx_map: SecondaryMap<PItemKey, usize>`、`pair_collisions: PairMatrix`、`container_collisions: Vec<CTEntry>`。快照 = 整结构 clone。
- C++ 对应（CollisionTracker.h:71-76）：`m_Handles: vector<PlacedItemHandle>`（对应 pk_idx_map 的键序列，索引即值）、`m_PlacedItems: vector<PlacedItem>`（**C++ 额外缓存**，Rust 不存 placed item 副本而直接读 `l.placed_items[pk]`）、`m_PairMatrix`、`m_ContainerEntries`、`m_ContainerBounds`（缓存容器 bbox，Rust 每次从 `l.container.outer_cd.bbox` 取）、`m_IsSeparated`（**C++ 额外**，对应 Rust 侧无直接等价物——Rust 用 `get_total_loss()==0.0` 判断，separator.rs:137,193）。
- 快照：C++ `CollisionTrackerSnapshot{handles, pairMatrix, containerEntries, isSeparated}`（h:18-24），不含 m_PlacedItems/m_ContainerBounds（Restore 时从 layout 重建）。Rust 快照是全量 clone（含 pk_idx_map）。【差异】（快照内容范围不同，恢复路径补齐后语义等价）
- 判定：【差异】（结构映射：pk_idx_map→m_Handles 定位表；额外缓存字段）

### `CollisionTracker::new`（tracker.rs:24-45）
- C++ 对应：`Create`（cpp:54-63）+ 私有 `BuildFromLayout`（cpp:639-727）。
- 索引建立：Rust `l.placed_items.keys().enumerate()` 建 pk→idx（tracker.rs:30-32）。C++ 按 `snapshot.placements` 顺序填 `m_Handles`（cpp:653-657）——顺序一致（placement 顺序）✓。
- loss 重算：Rust 对每个 pk 调 `recompute_loss_for_item`（tracker.rs:38-40），逐项重置该行后 CDE 收集。C++ `BuildFromLayout` 对每个 item 调 JaguarCore 碰撞引擎 `CollectPolygonCollisionsUntil`（cpp:688-702）并填 `m_PairMatrix.At(first, second).loss` / `m_ContainerEntries[first].loss`（cpp:704-724）。
- hazard 收集对（核对任务指定项）：Rust 用 jagua-rs CDE：`BasicHazardCollector::with_capacity(len+1)` + `l.cde().collect_poly_collisions(shape, &mut collector)` + `collector.remove_by_entity(&HazardEntity::from((pk, pi)))` 移除自身（tracker.rs:59-62）。C++：把所有 placed item 注册为 hazard（带各自 handle，cpp:673-686），`HazardFilter::irrelevantHandle` 排除自身 handle（cpp:690-696），同一容器引擎上 `CollectPolygonCollisionsUntil`（cpp:698-702）。语义对齐：均为"该 item 与其它 item/Exterior 的当前碰撞集合" ✓。差异点：Rust 的 CDE 是 layout 自带容器碰撞检测（外 CD + placed item CD）；C++ 新建 `CreateCollisionEngine` 并逐个 RegisterHazard——检测器实例不同但内容相同（container exterior 由 engine 自带：`HazardKind::Exterior` 条目 cpp:719-722 出现即证）。【一致】（渠道不同、内容等价）
- 自身排除：Rust `remove_by_entity`（按实体）；C++ 按 handle 过滤（cpp:505-509 亦同）✓。
- `debug_assert!(tracker_matches_layout(&ot, l))`（tracker.rs:42）→ C++ 无逐行一致性自检，改为 UpdateMovedItem/Restore 中的显式对账校验。【差异】（既定 debug_assert→无/显式校验）
- 判定：【一致】（实现不同：per-item 增量重算 vs 全量重建，但初建时两者均为"逐 item 收集碰撞"）
- 证据：tracker.rs:24-45,59-62 vs CollisionTracker.cpp:54-63,639-727。

### `recompute_loss_for_item`（tracker.rs:47-83）— 私有核心
- C++ 对应：**无逐 item 的独立重算函数**。其逻辑分布于：(a) `BuildFromLayout` 的 per-item 循环（cpp:688-725，重置由新建 PairMatrix/entries 天然完成）；(b) `UpdateMovedItem` 通过整体 rebuild + 权重回拷实现（见下条）。
- 逐项核对：
  1. 重置该行 loss：Rust 显式清零 `self.pair_collisions[(idx, i)].loss` 与 container loss（tracker.rs:53-56）。C++ rebuild 场景由新矩阵默认 0 替代 ✓。
  2. PlacedItem hazard → `quantify_collision_poly_poly(shape, shape_other)`（tracker.rs:71）→ C++ `CalculatePolygonCollisionLoss(m_PlacedItems[first].CollisionShape(), m_PlacedItems[*second].CollisionShape())`（cpp:714-717）✓（公式比对见 S17）。
  3. Exterior hazard → `quantify_collision_poly_container(shape, l.container.outer_cd.bbox)`（tracker.rs:76）→ C++ `CalculateContainerPenalty(..., m_ContainerBounds)`，其中 `m_ContainerBounds = snapshot.container.CollisionShape().BoundingBox()`（cpp:647）✓。
  4. `assert!(loss > 0.0, ...)`（tracker.rs:72,77）→ C++ 无对应断言（CalculateXxx 的防御保证非负有限，0 也被接受写入）。【差异】（既定 panic→容错；语义：Rust 保证碰撞必产生正 loss，C++ 允许 0 写入）
  5. `_ => unimplemented!`（tracker.rs:80）→ C++ if/else-if 静默忽略其他 kind（cpp:704-724）。【差异】（既定 panic→跳过）
- 判定：【差异】（增量重算路径在 C++ 中不存在独立形态；结果等价）
- 证据：tracker.rs:47-83 vs CollisionTracker.cpp:688-725,155-170。

### `register_item_move`（tracker.rs:101-109）— 核对重点
- Rust：O(1) 索引换键（`pk_idx_map.remove(old).insert(new)`，tracker.rs:103-104）+ 只对 moved item 调 `recompute_loss_for_item(new_pk, l)`（tracker.rs:106）——**其它 item 与旧 item 相关的 loss 行保持原值**（由于 moved item 变换后与邻居的碰撞变化，邻居行中与旧位置的 loss 在 Rust 中**不刷新**——这是 Rust 侧已知的近似：邻居行的 (neighbor, moved_idx) 条目仅在邻居自身 recompute 时更新；但碰撞检测是即时 CDE，且 moved item 行已重算，get_loss 汇总以行为准）。
- C++ 对应：`UpdateMovedItem`（cpp:70-172）：
  1. 前置对账：容器 bbox 不变、placement 数与矩阵尺寸一致（cpp:77-87）、previousHandle 在表中（cpp:89-94）、newHandle 在 layout（cpp:96-108）、其余 item 的 itemIndex/rotation/translation 与缓存逐项相等（cpp:110-138）——任何不符返回 Result 错误（Rust 无此检查，直接信任调用方）。
  2. **整体 rebuild** 新 tracker（cpp:140-141，BuildFromLayout 全量重算所有行），并校验 moved item 索引不变（cpp:146-150）。
  3. 权重回拷：新旧矩阵逐项 weight 覆盖（cpp:155-169）。
  4. `*this = std::move(rebuilt)`（cpp:170）。
- 语义对比：C++ 重建所有行的 loss（**比 Rust 更新**——Rust 邻居行残留旧 loss，C++ 邻居行是重算后的当前值）。关键观察：Rust 邻居行的 (neighbor_idx, moved_idx) 条目在移动前若为 0（无碰撞）移动后产生碰撞时，**Rust 该条目保持 0 直到邻居被 recompute**——但实际上 tracker.rs:53-56 的 recompute 只清零自己行；hazard 收集只发生在自己行。因此 Rust 的 get_loss(neighbor_pk) 可能漏掉新产生的 (neighbor, moved) 碰撞？——核对：`register_item_move` 只 recompute moved item；但 sparrow 的调用链中，移动后总 loss 以 `get_total_loss()` 汇总 data 全量（tracker.rs:179-187），邻居行未重算则 data 中旧值保留。**这是 Rust 实现的真实行为**：接受邻居行陈旧性（碰撞检测保证只有真实碰撞的条目非零；移动后邻居与 moved 的新碰撞会体现在 moved item 行 `(moved_idx, neighbor)`——**同一存储条目**（三角矩阵对称合一），故 data 中的该 pair 只有一份，moved 行重算即刷新该 pair 的唯一存储。因此三角矩阵合一存储使"只重算一行"即刷新所有相关 pair 条目——**无陈旧性问题**，Rust 与 C++ 的 rebuild 在 loss 数值上等价 ✓。唯一差别：container_collisions[idx] 与 pair 行均全部重算（C++）vs 仅 moved 相关条目重算（Rust）——由于其它 item 未移动，其 pair/container loss 不变，rebuild 结果与增量结果相同。**结果等价** ✓。
- 性能/复杂度：C++ O(N²+检测) 全量 rebuild vs Rust O(N) 单行；C++ 注释自述原因（cpp:153-155："Rebuilding the collision cache here keeps the same observable semantics and avoids stale pair entries"）。
- 判定：【差异】（实现路径不同——增量单行 vs 全量 rebuild+权重保留；结果等价）
- 证据：tracker.rs:101-109 vs CollisionTracker.cpp:70-172；三角矩阵对称合一存储（pair_matrix.rs:44-48）保证单行重算覆盖所有相关 pair。

### `restore_but_keep_weights`（tracker.rs:85-95）
- Rust：`pk_idx_map` 整体替换为快照；pair/container 的 **loss** 逐项从快照拷贝，**weight** 保留当前值；随后 debug_assert 一致性。
- C++ 对应：`RestoreButKeepWeights`（cpp:231-281）：先缓存当前所有 weight（cpp:242-258），调 `Restore`（全量恢复：handles/matrix/entries/IsSeparated，cpp:216-227），再回写缓存 weight（cpp:265-279）。前置尺寸校验（cpp:235-240）。
- 语义核对：loss 全取快照、weight 全保当前、键表取快照——与 Rust 逐字一致 ✓（Rust 直接 zip 拷 loss，C++ 借道 Restore 再覆写 weight，最终状态相同）。
- C++ 额外：Restore 中的对账校验（快照 handles 与 layout placements 一一对应、entry 有效性、isSeparated 与 layout.IsFeasible() 一致，cpp:184-214）——Rust 仅 debug_assert。【C++ 额外】
- 判定：【一致】
- 证据：tracker.rs:85-95 vs CollisionTracker.cpp:231-281。

### `save`（tracker.rs:97-99）
- C++ 对应：`Save`（cpp:174-177）——按值拷贝四个字段（不含 m_PlacedItems/m_ContainerBounds，Restore 时重建）。Rust 为整结构 clone。【一致】（快照恢复语义等价，见上）
- 证据：tracker.rs:97-99 vs CollisionTracker.cpp:174-177,179-229。

### `update_weights`（tracker.rs:113-135）— Algorithm 8
- 逐项核对：
  1. max_loss：Rust 遍历 pair+container 全部 loss 的 `fold(0.0, max)`（tracker.rs:115-118，f32::max，NaN 传播语义——但正常数据无 NaN）。C++ 遍历相同集合取 max，且要求 `isfinite && > maximumLoss`（cpp:561-580，跳过非有限值）。正常数据一致 ✓。
  2. multiplier：Rust `loss==0.0 → GLS_WEIGHT_DECAY(0.95)`；否则 `MIN_INC(1.2) + (MAX_INC(2.0)-MIN_INC(1.2)) * (loss/max_loss)`（tracker.rs:123-132；consts.rs:4-6）。C++：`loss > 0.0F && maximumLoss > 0.0F ? 1.2F + 0.8F * (loss/maximumLoss) : 0.95F`（cpp:583-585）——硬编码 1.2/0.8/0.95，与常量值一致（0.8 = 2.0-1.2）✓。
  3. 更新：Rust `(weight * multiplier).max(1.0)`（tracker.rs:133）。C++ `max(weight*multiplier, 1.0F)` 再 `min(updated, 1000000.0F)`（cpp:586-590）——**C++ 额外上限 1e6 clamp**。【差异】（Rust 无上限；上限极高，正常迭代内不可达，被 fixture 容差覆盖）
  4. 覆盖集合：pair（三角含对角）+ container 全部条目 ✓（cpp:592-604 vs tracker.rs:121-134）。注意对角线条目 (i,i) 在 Rust 中存在于 data 但从不被赋非零 loss（recompute 时 (idx,idx) 自碰撞被排除）——两侧同样处理 ✓。
- C++ 额外重载：`UpdateWeights(float collisionMultiplier, float clearDecay = 0.95F)`（h:65-66，cpp:607-637）——固定倍率版，**Rust 无对应**（生产代码未调用，仅公共 API 扩展）。【C++ 额外】
- 判定：【一致】（GLS 公式逐字等价；1e6 clamp 与参数化重载另计）
- 证据：tracker.rs:113-135、consts.rs:4-6 vs CollisionTracker.cpp:559-605,607-637。

### `get_pair_weight` / `get_container_weight`（tracker.rs:137-145）
- C++ 对应：`PairWeight(first, second)` / `ContainerWeight(index)`（cpp:311-320,331-339）。Rust 按键查 SecondaryMap（越界 panic）；C++ 按索引 + `Contains`/边界检查越界返回 0.0F。【差异】（既定 panic→0 返回；且 C++ 键→索引转换由调用方完成）
- 证据：tracker.rs:137-145 vs CollisionTracker.cpp:311-339。

### `get_pair_loss` / `get_container_loss`（tracker.rs:147-157）
- C++ 对应：`PairLoss` / `ContainerLoss`（cpp:301-309,322-329）+ `IndexForHandle`（cpp:288-299，对应 pk_idx_map 查询）。`SafeLoss` 过滤非正/非有限值（cpp:32-35）——Rust 直接返回存储值。【差异】（防御）
- 证据：tracker.rs:147-157 vs CollisionTracker.cpp:288-329。

### `get_loss`（tracker.rs:159-167）— Algorithm 1
- Rust：`container_loss + Σ_i pair_collisions[(idx, i)].loss`（i 从 0..size **含 idx 自身**——对角条目 loss 恒 0，无害）。
- C++ 对应：`ItemLoss`（cpp:341-356）：`ContainerLoss(index) + Σ_{other != index} PairLoss(index, other)`——**显式跳过自身**。数值一致 ✓（对角为 0）；f32 求和顺序：Rust 0..size 顺序（含 idx 处的 0），C++ 跳过 idx——加 0.0 不改变 f32 结果，顺序同为升序 ✓。
- 判定：【一致】
- 证据：tracker.rs:159-167 vs CollisionTracker.cpp:341-356。

### `get_weighted_loss`（tracker.rs:169-177）
- C++ 对应：`WeightedItemLoss`（cpp:358-373）：`ContainerLoss*ContainerWeight + Σ PairLoss*PairWeight`，显式跳过自身。Rust 用 `weighted_loss()`（= weight*loss，tracker.rs:209-211）；C++ 逐项显式相乘（未走 CollisionEntry::WeightedLoss 的防御过滤——cpp:364-371 直接乘）。一致 ✓（求和顺序同上）。
- 证据：tracker.rs:169-177 vs CollisionTracker.cpp:358-373。

### `get_total_loss`（tracker.rs:179-187）
- C++ 对应：`TotalLoss`（cpp:375-383）= `m_PairMatrix.TotalLoss() + Σ SafeLoss(container)`。注意求和顺序与过滤：Rust `cont_o + pair_o`（先 container 和再 pair 和再相加，tracker.rs:180-186）；C++ `pairTotal + containerTotal`（先 pair）——f32 加法交换此处两侧各自内部顺序不同（S17 差异#1 的布局顺序），ulp 级差异可能，被 fixture 容差（2e-2）覆盖。【差异】（求和顺序；结果等价）
- 证据：tracker.rs:179-187 vs CollisionTracker.cpp:375-383、PairMatrix.cpp:80-91。

### `get_total_weighted_loss`（tracker.rs:189-199）
- C++ 对应：`TotalWeightedLoss`（cpp:385-393）= `m_PairMatrix.TotalWeightedLoss() + Σ container.WeightedLoss()`。Rust 侧 `weighted_loss()` 无防御，C++ `CollisionEntry::WeightedLoss` 有 isfinite/>0 过滤（PairMatrix.cpp:9-18）——正常数据等价。求和顺序同上差异。【差异】（防御+顺序；结果等价）
- 证据：tracker.rs:189-199 vs CollisionTracker.cpp:385-393。

### `CTEntry` / `CTEntry::weighted_loss`（tracker.rs:202-211）
- C++ 对应：`CollisionEntry`（PairMatrix.h:11-17）+ `WeightedLoss`（PairMatrix.cpp:9-18，带防御）。字段默认值 {loss=0, weight=1} 与 Rust 构造一致 ✓。
- 判定：【一致】（防御另计）

### C++ 额外成员（Rust 无对应）
- `Rebuild(layout)`（h:31，cpp:65-68）：公共全量重建入口——Rust 对应操作是重新 `CollisionTracker::new`（separator.rs:261 rollback 无快照分支），C++ 提供就地版本。【C++ 额外】
- `IsSeparated()`（cpp:395-398）+ m_IsSeparated：Rust 无存储标志（以 `get_total_loss()==0.0` 动态判断，separator.rs:137,193；compress.rs:76）。C++ 在 Build/Restore 时从 `layout.IsFeasible()` 取值。**语义差异注意**：Rust "separated" ⇔ total_loss==0；C++ "separated" ⇔ layout.IsFeasible()（几何可行），两者在正常流程一致（无碰撞 ⇔ 可行），但 update_weights 后/移动后 C++ 的标志只在下次 Build/Restore 刷新（UpdateMovedItem 走 rebuild 会刷新 cpp:648；Restore 校验与 layout 一致 cpp:188）——TestQuantify.cpp:298-302 验证 UpdateMovedItem 后 `IsSeparated()` 为真。生产路径（Separator）无中途读取窗口，等价。【C++ 额外】
- `EvaluateCandidateLoss` 两个重载（h:55-63，cpp:400-457）：Rust 无 tracker 内对应——候选损失评估在 Rust 位于 eval/collision_loss.rs（`CollisionLossEvaluator`，见 S12 记录）。第一重载（无 layout，遍历所有已放置项，cpp:425-442 用 `CalculateOverlapProxy` 裸代理值，**不含 shape penalty/epsilon 公式**）在 Rust 侧无对应物（Rust 只有带完整公式的 bounded 版本）；生产使用的第二重载（layout 版，经 hazard 收集 + `CalculatePolygonCollisionLoss`）与 Rust `calc_weighted_loss_bounded` 语义对应（S12 已详录）。【C++ 额外】（第一重载）/【差异】（位置归属，结果对应 S12）
- 证据：CollisionTracker.h:29-66 vs tracker.rs 全文、collision_loss.rs:62-96。

---

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `register_item_move`：Rust O(1) 换键 + 单行重算 vs C++ 全量 rebuild + 逐项权重回拷（含前置对账校验） | 【差异】 | 结果等价（三角矩阵对称合一使单行重算即刷新该 pair 的唯一存储；未移动项 loss 不变，rebuild 值相同）；性能 O(N) vs O(N²) | tracker.rs:101-109 vs CollisionTracker.cpp:70-172 |
| 2 | hazard 收集渠道：Rust jagua-rs CDE（`l.cde().collect_poly_collisions` + `remove_by_entity`）vs C++ JaguarCore（新建 CollisionEngine + RegisterHazard + `CollectPolygonCollisionsUntil` + handle 过滤） | 【一致】 | 检测器实例不同、碰撞集合内容等价 | tracker.rs:59-62 vs CollisionTracker.cpp:666-702 |
| 3 | `assert!(loss > 0.0)` / `unimplemented!` / `debug_assert!(tracker_matches_layout)` → C++ 无断言、静默跳过未知 kind、显式对账校验 | 【差异】 | 既定 panic→Result/容错 | tracker.rs:42,72,77,80 vs CollisionTracker.cpp:77-138,704-724 |
| 4 | `update_weights` 公式：C++ 硬编码 1.2/0.8/0.95 与 Rust 常量一致；C++ 额外 weight 上限 clamp 1e6 | 【差异】 | 正常迭代不可达上限，被 fixture 容差覆盖 | tracker.rs:123-133、consts.rs:4-6 vs CollisionTracker.cpp:583-590 |
| 5 | C++ 参数化重载 `UpdateWeights(multiplier, decay)` | 【C++ 额外】 | 生产未调用（公共 API 扩展） | CollisionTracker.h:65-66, cpp:607-637 |
| 6 | Total/Item loss 求和：f32 加法结合顺序不同（pair/container 先后、矩阵物理布局顺序） | 【差异】 | ulp 级；fixture 容差 2e-2/5e-2 覆盖 | tracker.rs:159-199 vs CollisionTracker.cpp:341-393、PairMatrix.cpp:80-101 |
| 7 | C++ `SafeLoss`/`IsValidEntry`/`WeightedLoss` 防御过滤 vs Rust 直接读值 | 【C++ 额外】 | 正常数据等价 | CollisionTracker.cpp:26-35 vs tracker.rs:137-157 |
| 8 | C++ `IsSeparated`/`m_PlacedItems`/`m_ContainerBounds` 缓存字段与 `Rebuild` 公共入口 | 【C++ 额外】 | `IsSeparated`⇔`layout.IsFeasible()` 与 Rust `total_loss==0` 在正常流程一致 | CollisionTracker.h:31,54,71-76 vs separator.rs:137,193 |
| 9 | C++ `EvaluateCandidateLoss` 两重载（Rust 对应逻辑在 eval/collision_loss.rs，S12 已录）；无 layout 重载用裸 `CalculateOverlapProxy` 无 shape penalty | 【C++ 额外】/【差异】 | 生产走 layout 重载，与 Rust bounded 版对应 | CollisionTracker.cpp:400-457 vs collision_loss.rs:62-96 |
| 10 | `restore_but_keep_weights`：loss 取快照、weight 保留——两侧一致 | 【一致】 | — | tracker.rs:85-95 vs CollisionTracker.cpp:231-281 |
| 11 | 键→索引：Rust SecondaryMap(PItemKey→idx) 越界 panic vs C++ `IndexForHandle` 线性扫描返回 optional，调用方检查 | 【差异】 | 既定 panic→optional | tracker.rs:16,137-139 vs CollisionTracker.cpp:288-299 |

Fixture 锁定情况：`TestQuantify.cpp:168-303`（TestCollisionTrackerAndSeparationEvaluator）锁 Create/Save/Restore/UpdateWeights/UpdateMovedItem/IsSeparated 行为（含拒绝 stale 快照与 NaN entry）；`SeparatorRoundRustTrace.tsv`（state/placement/pair 行：total_loss=5.382490158、pair loss/weight 序列）+ `TestSeparator.cpp:694-776` 以容差 2e-2（loss）/5e-2（weight）锁端到端 GLS 权重演化与 tracker 终态；`SeparatorMultiWorkerRoundRustTrace.tsv`/`SeparatorThreeWorkerSeed43RoundRustTrace.tsv` 锁多 worker 的 RestoreButKeepWeights/权重合并路径。

## 结论

tracker 的 GLS 权重演化（Algorithm 8）、loss 读写接口、快照恢复语义与 Rust 逐项等价，且端到端被 SeparatorRoundRustTrace 系列 fixture 锁定。最重要的实现差异是 `register_item_move`：Rust 做单行增量重算（借助三角矩阵对称合一存储保证无陈旧条目），C++ 以全量 rebuild + 权重回拷替代并辅以前置对账校验——计算量 O(N²) vs O(N) 但观测语义等价。C++ 侧集中体现了既定转换策略（panic→Result/容错 0）与面向公共 API 的防御层（快照校验、entry 有效性、IsSeparated 缓存）。

### 补充审计（2026-09-29，实体句柄专项）
**【任务指定核对项】CollisionTracker 私有引擎的 hazard 实体句柄（2026-09-29 修复，commit 6910145）现场复验**：

1. **修复内容确认**：`BuildFromLayout`（cpp:675-695）与 `EvaluateCandidateLoss(layout,...)`（cpp:488-506）注册 hazard 时，实体字段已从 `{PlacedItem, itemIndex, 0, 0}`（仅 itemIndex，同 item 多放置会撞 CollisionEngine::RegisterHazard 的重复实体检查，CollisionEngine.cpp:101-112 `slot.hazard->entity == hazard.entity` → 报错 "hazard entity is already registered"）改为 `{PlacedItem, itemIndex, 0, 0, m_Handles[itemSequence], placedItem.Transform()}`——即补上 `placedItemHandle`（布局槽位+generation）与 `transform` 两个字段，附注释"用布局句柄+变换保证实体唯一性，同 Layout::MakeHazard"。
2. **与 Rust pk 语义的对应验证**：jagua-rs 的 hazard 实体 `HazardEntity::PlacedItem { pk, .. }` 以 slotmap PItemKey（每次放置唯一）标识；JaguarCore `HazardEntity::operator==`（hazard.h:57-62）对 PlacedItem 比较 `itemIndex && placedItemHandle && f32 精度的 rotation/translation`（hazard.h:44-46 两个字段）——C++ 的 `PlacedItemHandle{slotIndex, generation}`（Layout.cpp:90 AllocateHandle 的槽位复用+代数）在"每次放置唯一"意义上与 pk 对齐：remove_item 后 re-place 走新 slot/新 generation，即使同 itemIndex 也构成不同实体。同 item 两处放置（itemIndex 相同）现在因 handle 不同而实体不同 ✓。
3. **改造点覆盖完整**：经 grep 复验，SparrowCore 内私有引擎注册 hazard 的位置共 3 处——SeparationEvaluator.cpp:76-101、CollisionTracker.cpp:488-506（EvaluateCandidateLoss）、CollisionTracker.cpp:675-695（BuildFromLayout），6910145 全部改齐（diff 确认 3 处同模式）；Exploration.cpp 的 `CollectContainedHandles`（:364-377）复用布局自带引擎快照（`engine->Restore(cdeSnapshot)`），实体来自 Layout::MakeHazard（Layout.cpp:97-103，本就带 handle+transform），无需改。**结论：实体唯一性修复已全覆盖，与 Rust pk 语义对应正确，主文"2026-09-29 修复注记"（S06 头部第②条）的描述准确**。
4. **【浅覆盖补全】** 主文"register_item_move"条目的对账校验清单（cpp:77-138）经现场核对更新：容器 bbox 不变（:77-87）、previousHandle 在表（:89-94）、newHandle 在 layout（:96-108）、**逐项校验其余 item 的 transform 与缓存一致**（:110-138，通过 `candidate.handle == expectedHandle` 定位后比对旋转/平移）——主文已概括为"其余 item 的 itemIndex/rotation/translation 与缓存逐项相等"，判定维持。
5. **【核对确认：行号漂移】** 6910145 向 CollisionTracker.cpp 增 4 行注释+字段，当前 738 行（主文头部声称 728 行）；抽验 `UpdateWeights` 硬编码（cpp:583-590 附近，现约 +4 偏移）、`EvaluateCandidateLoss` 重载（cpp:453 起）等引用仍在有效范围内，不影响判定。
