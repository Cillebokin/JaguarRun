# S18. quantify/tracker.rs（CollisionTracker） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/tracker.rs`（212 行）
**C++**: `SparrowCore/Include/SparrowCore/Quantify/CollisionTracker.h`（78 行）+ `Source/Quantify/CollisionTracker.cpp`（864 行，2026-10-07 基线）
**判定**: PairMatrix/Tracker raw 数值、GLS 更新、moved-row 和 getter 越界均按 Rust 基线复现；Tracker 初始化/移动遇到 Hole/QZone 现在进入 unsupported-hazard 失败通道，并核对移动失败后的部分状态。CDE 单向发现 live fixture 未找到；C++ Restore 与临时检测器仍保留实现差异

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
- loss 重算：Rust 对每个 pk 调 `recompute_loss_for_item`（tracker.rs:38-40），逐项重置该行后 CDE 收集。C++ `BuildFromLayout` 现也在每个 item 收集前清零整条共享 pair 行，再调用 JaguarCore 碰撞引擎并填 pair/container loss；与 Rust 扫描次序相同。
- 非对称检测边界：后处理 item 的清行会覆盖共享 pair 槽；C++ 现已执行相同转移。Rust 自身保留“非对称碰撞”告警分支（util/assertions.rs:42-45,104-106），但截至 2026-10-15 仍未找到可触发的实际几何。新增搜索覆盖锁定 Rust CDE 的直接双向收集、完整 Layout/Tracker、quadtree 分割线及多种边界几何；范围、数量和限制见下方“单向发现样本搜索”。因此目前只能确认 C++ 代码级清行状态转移与 Rust 源码一致，不能声称该运行时触发条件已验证。
- hazard 收集对（核对任务指定项）：Rust 用 jagua-rs CDE：`BasicHazardCollector::with_capacity(len+1)` + `l.cde().collect_poly_collisions(shape, &mut collector)` + `collector.remove_by_entity(&HazardEntity::from((pk, pi)))` 移除自身（tracker.rs:59-62）。C++ 把 placements 注册为 hazards（带各自 handle，cpp:801-821），`HazardFilter::irrelevantHandle` 排除自身 handle（cpp:823-831），再调用 `CollectPolygonCollisionsUntil`（cpp:832-837）。两侧均收集该 item 与其它 item/Exterior 的当前碰撞集合；C++ 从容器 collision engine 获取 Exterior hazard（cpp:854-858）。【一致】（渠道不同、内容等价）
- 自身排除：Rust `remove_by_entity`（按实体）；C++ 按 handle 过滤（BuildFromLayout cpp:825-831；UpdateMovedItem cpp:200-206）✓。
- `debug_assert!(tracker_matches_layout(&ot, l))`（tracker.rs:42）→ C++ 无逐行一致性自检，改为 UpdateMovedItem/Restore 中的显式对账校验。【差异】（既定 debug_assert→无/显式校验）
- 判定：【常规路径已对齐；单向发现触发样本仍未覆盖】（移动更新的实现细节见 register_item_move）
- 证据：tracker.rs:24-45,59-62 vs CollisionTracker.cpp:67-76,770-863。

### `recompute_loss_for_item`（tracker.rs:47-83）— 私有核心
- C++ 对应：没有单独命名的 `recompute_loss_for_item` helper。初建由 `BuildFromLayout` 的 per-item 循环收集（cpp:770-861）；移动更新则在 `UpdateMovedItem` 内只收集移动项的碰撞并重写其 pair 行和 container loss（cpp:158-257），不触碰其他 loss/weight 行。
- 逐项核对：
  1. 重置该行 loss：Rust 显式清零该项所有共享 pair loss 与 container loss（tracker.rs:53-56）。C++ BuildFromLayout 现显式逐项清零共享 pair 行；container entries 在重建时重置。
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
  1. max_loss：Rust 对 pair+container loss 做 `fold(0.0, f32::max)`（tracker.rs:125-132）；Rust `f32::max` 忽略单个 NaN、返回另一个操作数，正无穷则保留为最大值（[标准库文档](https://doc.rust-lang.org/std/primitive.f32.html#method.max)）。C++ `std::fmax`（cpp:677-692）对单个 NaN 也返回另一个值，并保留正无穷；此更新阶段的 maximum loss 规则相同。
  2. multiplier：Rust `loss==0.0 → GLS_WEIGHT_DECAY(0.95)`；否则 `MIN_INC(1.2) + (MAX_INC(2.0)-MIN_INC(1.2)) * (loss/max_loss)`（tracker.rs:123-132；consts.rs:4-6）。C++ 现用相同的运算表达式 `1.2F + (2.0F - 1.2F) * (loss/maximumLoss)`（cpp:721-725）。原先的 `0.8F` 与 f32 的 `2.0F-1.2F` 相差 1 ULP，连续迭代会放大；已由部分碰撞比例的 Rust 位模式 reference 修复并验证。【已修复：有限正 loss】
  3. 更新：Rust `(weight * multiplier).max(1.0)`（tracker.rs:153）。C++ 使用 `std::fmax(weight * multiplier, 1.0F)`（cpp:694-699），对单个 NaN 与 Rust `f32::max` 一样选择 1.0。C++ 不再截到 `1,000,000.0F`；同一 tracker 连续 20 次最大倍率更新得到 `1,048,576`，Rust/C++ 参考测试精确通过。NaN、+Inf、-Inf loss 的更新结果也有直接 Rust oracle 覆盖。
  4. 覆盖集合：pair（三角含对角）+ container 全部条目 ✓（cpp:707-739 vs tracker.rs:121-134）。注意对角线条目 (i,i) 在 Rust 中存在于 data 但从不被赋非零 loss（recompute 时 (idx,idx) 自碰撞被排除）——两侧同样处理 ✓。
- C++ 额外重载：`UpdateWeights(float collisionMultiplier, float clearDecay = 0.95F)`（h:65-66，cpp:742-768）——固定倍率版，**Rust 无对应**（生产代码未调用，仅公共 API 扩展）。【C++ 额外】
- 判定：【正常有限状态一致】（更新倍率表达式、f32 舍入、下限及无上限均对齐；`max_loss` 对正无穷的处理差异和参数化重载另计）
- 证据：tracker.rs:113-135、consts.rs:4-6 vs CollisionTracker.cpp:698-740,742-768。

### `get_pair_weight` / `get_container_weight`（tracker.rs:137-145）
- C++ 对应：`PairWeight(first, second)` / `ContainerWeight(index)`。Rust 用无效 `PItemKey` 查 `SecondaryMap` 或矩阵时 panic；C++ 当前对越界索引返回 `0.0F`（CollisionTracker.cpp:365-400），而 PairMatrix 自身已采用 checked access。此公共 Tracker getter 的无效索引仍是可观察差异，尚未有两侧错误边界 fixture。
- 证据：tracker.rs:137-145 vs CollisionTracker.cpp:402-430。

### `get_pair_loss` / `get_container_loss`（tracker.rs:147-157）
- C++ 对应：`PairLoss` / `ContainerLoss`（cpp:365-400）+ `IndexForHandle`（cpp:352-363，对应 pk_idx_map 查询）。C++ getter 现直接返回存储的 raw loss，包括 NaN、±Inf；无效索引仍返回 0，属于上一节所述边界差异。
- 证据：tracker.rs:147-157 vs CollisionTracker.cpp:379-420。

### `get_loss`（tracker.rs:159-167）— Algorithm 1
- Rust：`container_loss + Σ_i pair_collisions[(idx, i)].loss`（i 从 0..size **含 idx 自身**——对角条目 loss 恒 0，无害）。
- C++ 对应：`ItemLoss`（cpp:403-418）：先按 Rust 的 `0..size` 顺序折叠 pair loss（对角项也参与，正常为 0），再加 container loss；与 Rust 的 `pair_loss` 后加 container 次序一致。越界索引目前返回 0，而 Rust key lookup 会 panic。
- 判定：【一致】
- 证据：tracker.rs:159-167 vs CollisionTracker.cpp:432-447。

### `get_weighted_loss`（tracker.rs:169-177）
- C++ 对应：`WeightedItemLoss`（cpp:465-482）：按 Rust 的 `0..size` 顺序先折叠每个 pair 的 `weight * loss`，再加 container 的 `weight * loss`；NaN/±Inf 原样参与 IEEE 运算。越界索引目前返回 0，而 Rust key lookup 会 panic。
- 证据：tracker.rs:169-177 vs CollisionTracker.cpp:490-505。

### `get_total_loss`（tracker.rs:179-187）
- C++ 对应：`TotalLoss`（cpp:484-492）按 container 顺序和 Rust 上三角 PairMatrix 顺序分别折叠，再计算 `container + pair`；非有限值按 IEEE 算术传播。PairMatrix 的遍历顺序已由 S17 对齐。一般 separator trace 仍按其预设容差比较，不能据此宣称几何公式任意输入逐位一致。
- 证据：tracker.rs:179-187 vs CollisionTracker.cpp:507-516、PairMatrix.cpp:80-91。

### `get_total_weighted_loss`（tracker.rs:189-199）
- C++ 对应：`TotalWeightedLoss`（cpp:494-502）按 container 顺序与 Rust 上三角 PairMatrix 顺序折叠原始 `weight * loss`，最后相加；非有限值按 IEEE 算术传播。tracker 层求和顺序已对齐，几何 loss 产生端的数值保护差异见 S12/S17。
- 证据：tracker.rs:189-199 vs CollisionTracker.cpp:518-526。

### `CTEntry` / `CTEntry::weighted_loss`（tracker.rs:202-211）
- C++ 对应：`CollisionEntry`（PairMatrix.h:11-17）+ `WeightedLoss`（PairMatrix.cpp:9-12）。字段默认值 `{loss=0, weight=1}` 且 raw `weight * loss` 与 Rust 构造/方法一致。
- 判定：【一致】

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
| 3 | `assert!(loss > 0.0)` / `unimplemented!` / `debug_assert!(tracker_matches_layout)` → C++ 对非正 collision loss 抛 `logic_error`、未知 hazard 抛 `logic_error`、Tracker/Layout 对账由显式校验处理 | 【错误触发已覆盖；Debug 自检边界仍不同】 | Hole/QZone 在 BuildFromLayout 与 moved-row update 都有跨语言失败用例；moved-row panic/exception 后 key 已替换且已清零行保持一致；tracker_matches_layout 的 Debug-only 额外一致性断言没有逐项映射 | tracker.rs:42,72,77,80 vs CollisionTracker.cpp:83-156,200-257,796-852; collision_loss_unsupported_hazard_reference.rs; TestQuantify.cpp |
| 4 | update_weights：倍率表达式、无额外 1e6 上限，以及 NaN/±Inf 下 f32::max floor | 【已闭合】 | 有限项继续由权重 golden 锁定位模式；NaN、+Inf、-Inf 的总损失传播及权重回落到 1.0 在 Debug/Release 直接参考 | tracker.rs:113-135、consts.rs:4-6 vs CollisionTracker.cpp UpdateWeights; tracker_weight_reference.rs
| 5 | C++ 参数化重载 `UpdateWeights(multiplier, decay)` | 【C++ 额外】 | 生产未调用（公共 API 扩展） | CollisionTracker.h:65-66, cpp:742-768 |
| 6 | Total/Item loss 求和顺序与 PairMatrix 物理遍历 | 【已闭合】 | pair data 现用 Rust 上三角行顺序；ItemLoss/WeightedItemLoss 按 Rust 的 pair fold 后再加 container；NaN/±Inf 不再被压成 0 | tracker.rs:137-199 vs CollisionTracker.cpp ItemLoss/WeightedItemLoss/TotalLoss; PairMatrix.cpp
| 7 | C++ 防御过滤对非有限值的处理 | 【已闭合：Tracker 状态层】 | raw getter、直接乘法和累加传播 Rust IEEE 结果；结构正确且 feasibility 标志一致的快照可携带非有限 entry。S12 的几何公式/候选误差出口仍另有防御差异 | tracker.rs:137-157,179-199,209-211 vs CollisionTracker.cpp/PairMatrix.cpp
| 8 | C++ `IsSeparated`/`m_PlacedItems`/`m_ContainerBounds` 缓存字段与 `Rebuild` 公共入口 | 【C++ 额外】 | `IsSeparated` 按 tracked `TotalLoss()==0` 派生/校验，与 Rust 判定依据对应；缓存存储形态不同 | CollisionTracker.h:31,54,71-76; cpp:257,301,861 vs separator.rs:137,193 |
| 9 | C++ `EvaluateCandidateLoss` 两重载（Rust 对应逻辑在 eval/collision_loss.rs，S12 已录）；无 layout 重载用裸 `CalculateOverlapProxy` 无 shape penalty | 【C++ 额外】/【差异】 | 生产走 layout 重载，与 Rust bounded 版对应 | CollisionTracker.cpp:534-696 vs collision_loss.rs:62-96 |
| 10 | `restore_but_keep_weights`：loss 取快照、weight 保留——两侧一致 | 【一致】 | — | tracker.rs:85-95 vs CollisionTracker.cpp:266-366 |
| 11 | 无效 key/index：Rust 用 stale `PItemKey` 访问 `SecondaryMap` 时 panic；C++ 数值 getter 先前对越界返回 0，现改为 checked access 抛 `std::out_of_range` | 【getter 越界已闭合为语言异常映射】 | Rust stale-key Debug/Release panic 与 C++ 越界 exception 均不会伪装成 0 loss/weight；RustReference stale key 与 C++ 六类 getter 检查已加入。`IndexForHandle` 的 optional 仍是内部查找 API，没有 Rust 公共同形接口 | tracker.rs:157-186 vs CollisionTracker.cpp:352-418; tracker_weight_reference.rs; TestQuantify.cpp |
| 12 | 初建 tracker 时共享 pair 槽的逐行清零：Rust 后处理项会清除该 pair，C++ 现已清除 | 【源码语义已修复；触发 fixture 未得】 | 若 CDE 仅单向报告 pair，最终共享 loss 由后处理行决定；截至 2026-10-15 的定向与系统搜索均未触发，Tracker 端到端状态仍缺该触发条件的直接运行时验证 | tracker.rs:47-56; util/assertions.rs:42-45,104-106 vs CollisionTracker.cpp BuildFromLayout

### 单向发现样本搜索（2026-10-15）

使用 `SparrowCore/Tests/RustReference/Cargo.lock` 锁定的 jagua-rs 0.9.0（`f5e2b5ffd14931f17a20ce283a2122e877953ceb`）源码，以真实 `CDEngine::collect_poly_collisions`（`cd_engine.rs:273-319`）对同一对多边形分别执行 A→B 与 B→A。底层 CDE 探测每组注册 Exterior hazard 和两个 Interior-scope `Hole` hazard；`HazardEntity::scope` 将 `Hole` 与 Tracker 使用的 `PlacedItem` 同归为 Interior（`hazard.rs:40-64`）。随后另做了真实 Layout/Tracker 搜索，结果见下文。

已完成且未触发单向收集的确定性搜索：

- 20,000 组近接触旋转矩形，quadtree depth=6、`cd_threshold=0`。
- 3,000 组 65–192 顶点正多边形近接触样本，使用 Sparrow 默认 CDE depth=4、`cd_threshold=64`。
- 10,000 组 67–118 顶点不规则径向多边形，使用默认 depth/threshold（depth=4、`cd_threshold=64`）；此直接精确收集路径不读取 surrogate 配置。
- 100,000 组边界落在 0.5 网格上的轴对齐矩形对，容器为 dyadic 方形、depth=8、`cd_threshold=0`。
- 最多 35,000 组高坐标偏移旋转矩形，坐标尺度覆盖 `2^24` 至 `2^60`，形状从原点的有效模板经 Rust `transform_clone` 放置；depth=6、`cd_threshold=0`，并交替 hazard 注册顺序。超出容器 bbox 的样本跳过。
- 另对 5,000,000 组共享端点边、5,000,000 组构造交叉边及 5,000,000 组有限大坐标交叉边直接比较 `Edge::collides_with` 两个参数方向，均未得到方向性边相交样本。
- 另以当前 Sparrow `Importer` 构造真实 item 与容器，建立 12 个完整 `Layout`；每个布局放置 48 个 item，来自 4 种 94–124 条 collision edge 的不规则多边形，按固定 RNG 种子生成旋转和平移，共比较 13,536 个 pair。对每个 pair 检查真实 `PlacedItem` hazard 的双向 CDE collector，并核对 `CollisionTracker::new` 的共享 pair loss。
- 另做 24 个近接触完整 Layout，每个布局包含 9 对 importer 生成的复杂 item；按实际 collision contour 的支撑方向把每对放到 0.0001–0.1 的轻微重叠范围，并保留同布局其余 item hazards，共检查 216 对。两向 collector 与 Tracker loss 均逐对核对。

真实 Layout/Tracker 搜索在 Debug 与 Release 均完成，13,752 个 pair 没有单向发现，且 Tracker pair loss 与双向 collector 的结果一致。可用以下命令重跑这两组固定搜索：

```powershell
cargo test --release --locked --manifest-path SparrowCore\Tests\RustReference\Cargo.toml --test cde_layout_direction_search_reference -- --ignored --nocapture
cargo test --locked --manifest-path SparrowCore\Tests\RustReference\Cargo.toml --test cde_layout_direction_search_reference -- --ignored --nocapture
```

搜索 harness `SparrowCore/Tests/RustReference/tests/cde_layout_direction_search_reference.rs` 标记为 ignored，避免把约 36 秒的 Debug 探测加入每次常规全量测试。结论仍严格限定为以上确定性搜索域：它们不能证明所有合法多边形、所有容器树状态或所有浮点边界下该分支不可达。原始 `tracker_matches_layout` 告警是源码中的防御性处理，当前没有几何证据证明其触发。下一步若继续，应围绕 `cd_engine.rs` 的 `almost_relation_to` containment 路径、`qt_hazard.rs` 的 Partial/Entire 分区分类及 `qt_node.rs` 的 query quadrant 裁剪，构造源码分支级候选；命中后再固化 Rust/C++ 双向 collector 与最终 pair loss golden。在此之前保留为“未验证”，不增加虚构的 Rust/C++ golden。

Fixture 与断言：Rust `tracker_weight_reference.rs` 覆盖 20 次有限权重 golden、NaN/±Inf loss 与 GLS floor、stale key 的六种 getter panic，以及三件布局中每个 pair/container entry 在 moved-row 更新前后的 loss/weight；Rust `collision_loss_unsupported_hazard_reference.rs` 增加 Tracker 初始化 Hole/QZone panic 与 moved-row Hole panic 后的 key/清零状态断言。C++ `TestQuantify.cpp` 对应核对全矩阵、六种越界 getter、untouched pair 与 handle 替换，并核对 Tracker 初始化 Hole/QZone 与 moved-row Hole 失败。当前单向搜索 harness 在 Rust Debug/Release 手动运行，不是 Rust/C++ parity golden。SeparatorRoundRustTrace.tsv 的一般 optimizer loss/weight 仍按 2e-2/5e-2 容差比较；RustSppCompressWorkerMove.json 不外推至任意随机 move。

## 结论

Tracker 的 PairMatrix 次序、raw loss/weighted-loss 运算、GLS NaN/±Inf floor、moved-row 更新、getter 越界和 unsupported-hazard 失败状态已有直接 Rust/C++ Debug/Release 测试。BuildFromLayout 已复刻 Rust 每项重算前清空共享 pair 行；定向 CDE 搜索未找到单向发现样本，Tracker 端到端触发仍未验证。C++ Restore 仍有布局 handle/尺寸/feasibility 校验；每次 moved-row 更新新建并注册临时 collision engine，耗时未量化。一般 optimizer trace 仍按已登记容差比较。

### 补充审计（2026-09-29，实体句柄专项；以下行号为当时记录）
**【任务指定核对项】CollisionTracker 私有引擎的 hazard 实体句柄（2026-09-29 修复，commit 6910145）现场复验**：

1. **修复内容确认**：`BuildFromLayout`（cpp:673-682）与 `EvaluateCandidateLoss(layout,...)`（cpp:488-506）注册 hazard 时，实体字段已从 `{PlacedItem, itemIndex, 0, 0}`（仅 itemIndex，同 item 多放置会撞 CollisionEngine::RegisterHazard 的重复实体检查，CollisionEngine.cpp:101-112 `slot.hazard->entity == hazard.entity` → 报错 "hazard entity is already registered"）改为 `{PlacedItem, itemIndex, 0, 0, m_Handles[itemSequence], placedItem.Transform()}`——即补上 `placedItemHandle`（布局槽位+generation）与 `transform` 两个字段，附注释"用布局句柄+变换保证实体唯一性，同 Layout::MakeHazard"。
2. **与 Rust pk 语义的对应验证**：jagua-rs 的 hazard 实体 `HazardEntity::PlacedItem { pk, .. }` 以 slotmap PItemKey（每次放置唯一）标识；JaguarCore `HazardEntity::operator==`（hazard.h:57-62）对 PlacedItem 比较 `itemIndex && placedItemHandle && f32 精度的 rotation/translation`（hazard.h:44-46 两个字段）——C++ 的 `PlacedItemHandle{slotIndex, generation}`（Layout.cpp:90 AllocateHandle 的槽位复用+代数）在"每次放置唯一"意义上与 pk 对齐：remove_item 后 re-place 走新 slot/新 generation，即使同 itemIndex 也构成不同实体。同 item 两处放置（itemIndex 相同）现在因 handle 不同而实体不同 ✓。
3. **改造点覆盖完整**：经 grep 复验，SparrowCore 内私有引擎注册 hazard 的位置共 3 处——SeparationEvaluator.cpp:76-101、CollisionTracker.cpp:488-506（EvaluateCandidateLoss）、CollisionTracker.cpp:673-682（BuildFromLayout），6910145 全部改齐（diff 确认 3 处同模式）；Exploration.cpp 的 `CollectContainedHandles`（:364-377）复用布局自带引擎快照（`engine->Restore(cdeSnapshot)`），实体来自 Layout::MakeHazard（Layout.cpp:97-103，本就带 handle+transform），无需改。**结论：实体唯一性修复已全覆盖，与 Rust pk 语义对应正确，主文"2026-09-29 修复注记"（S06 头部第②条）的描述准确**。
4. **【浅覆盖补全】** 主文"register_item_move"条目的对账校验清单（cpp:77-138）经现场核对更新：容器 bbox 不变（:77-87）、previousHandle 在表（:89-94）、newHandle 在 layout（:96-108）、**逐项校验其余 item 的 transform 与缓存一致**（:110-138，通过 `candidate.handle == expectedHandle` 定位后比对旋转/平移）——主文已概括为"其余 item 的 itemIndex/rotation/translation 与缓存逐项相等"，判定维持。
5. **【历史行数记录】** 2026-09-29 补充审计时 CollisionTracker.cpp 为 730 行；2026-10-07 当前基线为 864 行，主文引用已按当前函数位置更新。
