# 26. Layout 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/entities/layout.rs`（203 行）
**C++**: `JaguarCore/Include/JaguarCore/Entities/Layout.h`（93 行）+ `JaguarCore/Source/Entities/Layout.cpp`（474 行）
**判定**: 基本等价（place/remove/save/swap 语义一致；Restore 在变更前校验容器/槽元数据/null shape，随后按 Rust 顺序提交布局并恢复 CDE，Debug 一致性断言可能在变更后失败；句柄体系不同；cde()/is_empty() 未暴露；RightmostCollisionX/PlacedItemCount 为 C++ 额外）

## 函数逐条比对

### `struct Layout` + `Slot` 基础设施（Rust 15-23 行）
- C++ 对应：`Layout.h:37-92`（`m_Container`、`m_Cde: CollisionEnginePtr`、`m_Slots: vector<Slot>`、`m_FreeSlotIndices`）+ `Layout::Slot`（h:75-80：optional<PlacedItem> + cdeHandle + generation）。
- 判定：基本等价（数据结构对齐）。
- 说明：Rust 用 `SlotMap<PItemKey, PlacedItem>` 隐式管理槽位/代数/空闲栈；C++ 手工实现等价结构：`Get(handle)`（cpp:62-80，index 越界/generation 不匹配/无值 → nullptr）对应 slotmap 键查找；`AllocateHandle()`（cpp:82-95，空闲栈 LIFO 弹出 + `generation |= 1`，或追加新槽）对应 slotmap `insert` 的键分配；`m_FreeSlotIndices` 对应 slotmap 内部空闲链。C++ 槽位 0 为哨兵（构造 `m_Slots(1)` 且 `front().generation = 0`，cpp:54-60），保证生成键的 index 非 0——slotmap 无此显式哨兵（但其键 0 同样仅在特定分配序列出现）。generation 奇偶约定：占用态奇数（分配时 `|1`，cpp:89），释放时 `++generation` 变偶（RemoveItem cpp:153）。这些是对 slotmap 行为的显式重实现。

### `Layout::new`（Rust 26-34 行，`cde = container.base_cde.as_ref().clone()`）
- C++ 对应：`Layout::Layout(Container container)`（cpp:54-60），`m_Cde(m_Container.CreateCollisionEngine())`。
- 判定：一致（语义）。
- 说明：Rust 克隆 base_cde（含已注册静态 hazard 的引擎快照式克隆）；C++ 按需新建引擎并注册静态 hazard（见记录 24 差异 1）。两者得到"仅含静态 hazard 的空布局引擎"，等价。

### `Layout::from_snapshot`（Rust 37-43 行，restore 失败即 panic）
- C++ 对应：无直接等价物；等价用法内联于 `SPProblem::Restore`（SPP/Problem.cpp:242-249：先原地 restore，失败则 `m_Layout = std::move(restored)` 用快照容器新建再 restore）。
- 判定：差异（承载方式）。
- 说明：Rust 的 `expect("snapshot shares the container's static base")` 在容器不一致时 panic；C++ 调用方构造 `Layout restored(snapshot.container)` 保证容器必然一致，再 Restore，把 panic 变成不可达路径。功能等价。

### `swap_container`（Rust 46-55 行）
```rust
let cde_snapshot = self.cde.save();
self.container = container;
self.cde = self.container.base_cde.as_ref().clone();
for hazard in cde_snapshot.dynamic_hazards { self.cde.register_hazard(hazard); }
```
- C++ 对应：`Layout::SwapContainer`（cpp:159-200）。
- 判定：基本等价（多了失败通道与句柄回填）。
- 逐步对照：
  1. 新引擎构建（C++ cpp:161-167：`container.CreateCollisionEngine()`，空指针即错误）↔ Rust 取新 base_cde 克隆。判定：一致。
  2. 动态 hazard 重注册（C++ cpp:169-189：遍历旧引擎快照的 dynamicHazards，逐个 `newCde->RegisterHazard`）↔ Rust rs:51-54。判定：一致。C++ 额外校验：hazard 必须是 PlacedItem 且 `Get(handle)` 有效（cpp:173-179），否则 `SnapshotMismatch`；Rust 无此校验（信任内部不变量）。C++ 额外回填：`newHandles[handle.index]` 更新每个槽位的 cdeHandle（cpp:188, 193-197）——Rust 的 CDEngine 内部 hazard 键即 (pk) 对应，无需额外映射。
  3. 判定：失败语义差异——Rust 无失败路径（register_hazard 返回值被忽略，rs:53）；C++ 任一注册失败返回错误且**不改动原状态**（原子，cpp:191 之后才提交）。

### `save`（Rust 58-65 行）
- C++ 对应：`Layout::Save`（cpp:242-259）。
- 判定：基本等价（快照内容对应；C++ 额外保存槽位元数据）。
- 证据：`LayoutSnapshot{container, placements(vector<LayoutPlacementSnapshot>), cdeSnapshot, slotGenerations, freeSlotIndices}`（h:24-34）↔ Rust `LayoutSnapshot{container, placed_items: SlotMap, cde_snapshot}`（rs:179-186）。
- 说明：C++ 把 slotmap 的内部状态显式化：`slotGenerations`（每槽代数，cpp:246-248）、`freeSlotIndices`（空闲栈，按分配栈序，h:30）；Rust 靠 `placed_items: SlotMap` 整体克隆隐式承载。`LayoutPlacementSnapshot{handle, placedItem}`（h:17-21）= (PItemKey, PlacedItem)。集合语义一致：已放置项 + 各自句柄 + 引擎快照 + 容器。

### `restore`（Rust 74-89 行）
```rust
if !Arc::ptr_eq(&self.container.base_cde, &layout_snapshot.container.base_cde) { return Err(ContainerMismatch); }
self.container.clone_from(&layout_snapshot.container);
self.placed_items.clone_from(&layout_snapshot.placed_items);
self.cde.restore(&layout_snapshot.cde_snapshot);
debug_assert!(...);  // debug-only
```
- C++ 对应：`Layout::Restore`（cpp:261-424）。
- 判定：差异（C++ 有额外表示层预检，但布局/CDE 一致性失败并非普遍原子；Debug/Release 对损坏 CDE 快照的行为已由 S06 定向测量）。
- 逐步对照：
  1. 容器一致性（C++ cpp:263-269：`!m_Container.SameBase(snapshot.container)` → 错误）↔ Rust base_cde 指针等价（rs:75-80）。判定：一致（锚点等价，见记录 24）。
  2. C++ 在提交前重建 `newSlots` 并校验 slot sentinel、placement handle、generation、free-slot 完整性（cpp:277-376）；这些是 C++ 显式槽位表示的防御检查。Rust 通过 `SlotMap` 整体克隆恢复内部元数据（rs:83），安全 Rust API 无法构造同类非法 slot 元数据。C++ 对快照 dynamic-hazard 列表中的 null shape 也在变更前返回 `InvalidInput`（cpp:378-386）；Rust `Arc<SPolygon>` 无 null 对应值。
  3. 提交顺序：C++ 完成上述预检后，先写入容器、placements 和 free-slot 元数据，再调用 CDE Restore（cpp:388-397），与 Rust 先写 container/placed_items 再调 `cde.restore`（rs:81-84）对齐。因此 CDE Debug 断言失败时，Layout 已包含快照 placements，不能描述为全程原子。
  4. CDE Restore 按当前动态 hazard 的 entity 与快照列表做差分。匹配 entity 沿用当前 hazard；未匹配当前项删除；未匹配快照项按原字段新增。Rust `register_hazard` 对重复 entity 在 Debug 断言；数量等一致性检查也在更新后执行。C++ 对应地在 Debug 遇到待新增重复 entity 时提交已处理前缀后抛异常，且动态数量/布局匹配断言在 CDE 状态提交后发生（CollisionEngine.cpp:762-885；Layout.cpp:395-440）。Release 不执行这些断言，已测的非空损坏快照可被接受。
  5. C++ 在 CDE 恢复后回查 placement hazard 句柄并更新槽位句柄，再于 Debug 检查实际 dynamic hazards 是否与 placements 一致（cpp:399-440）。Rust 的 `layout_qt_matches_fresh_qt` / `snapshot_matches_layout` 也是 Debug-only（rs:86-87）；两侧都可能在恢复副作用发生后 panic/throw，具体残留状态取决于损坏字段与触发分支。
  6. 定向证据：S06 记录 15 个 Rust/C++ 配对 Restore 场景（包含六种 CDE hazard 字段变异），另有两个 C++ slot 元数据检查及一个 null-shape 检查。它们支持列明输入/profile 的结论，不证明任意损坏快照等价。
- 结论：Restore 只有容器、槽元数据和 null-shape 等前置拒绝路径具备变更前失败；CDE/布局一致性差异会在变更后暴露。不能将 C++ Restore 总结为比 Rust 更严格或普遍原子。合法快照路径及列出的损坏快照 profile 结果有定向证据，未列组合仍开放。

### `place_item`（Rust 93-105 行）
```rust
let pk = self.placed_items.insert(PlacedItem::new(item, d_transformation));
let pi = &self.placed_items[pk];
let hazard = Hazard::new((pk, pi).into(), pi.shape.clone(), true);
self.cde.register_hazard(hazard);
debug_assert!(assertions::layout_qt_matches_fresh_qt(self));
```
- C++ 对应：`Layout::PlaceItem`（cpp:107-136）。
- 判定：基本等价（顺序一致；C++ 有失败回滚）。
- 逐步对照：
  1. 引擎可用性（C++ cpp:110-114：`!m_Cde` → CollisionEngineFailure）——C++ 额外（Rust 类型系统保证 cde 存在）。
  2. PlacedItem 创建（C++ cpp:116-118 `PlacedItem::Create`）↔ Rust `PlacedItem::new`（rs:96）。判定：一致（见记录 25）。
  3. 键分配（C++ `AllocateHandle()` cpp:120）↔ slotmap `insert` 返回键（rs:94）。判定：一致。
  4. hazard 构造与注册（C++ `MakeHazard(*slot.placedItem, handle)`（cpp:97-105，HazardKind::PlacedItem + itemIndex + handle + transform，dynamic=true）→ `m_Cde->RegisterHazard`）↔ Rust rs:97-100。判定：一致（`(pk, pi).into()` 的 HazardEntity 携带键与变换，对应 C++ entity 的 handle+transform 字段）。
  5. 失败回滚（C++ cpp:126-132：注册失败 → `slot.placedItem.reset(); ++slot.generation; freeStack.push`）——C++ 额外。Rust `register_hazard` 返回值被忽略（rs:100），失败仅 debug 断言暴露。
  6. debug 断言（Rust rs:102 `layout_qt_matches_fresh_qt`）↔ C++ cpp:134 `DebugAssertLayout(*this)`（`#ifndef NDEBUG` 包裹，Assertions.cpp:378-385）。判定：一致（均为 debug-only；C++ 断言内容为 ValidateLayout 全量结构校验，Rust 为 quadtree 重建比对——校验手段不同但目标一致，见记录 28）。

### `remove_item`（Rust 108-120 行）
- C++ 对应：`Layout::RemoveItem`（cpp:138-157）。
- 判定：基本等价（多失败通道）。
- 逐步对照：无效键（Rust rs:110-112 `expect("key is not valid anymore")` panic ↔ C++ cpp:140-145 返回 InvalidInput）——panic→Result 约定；引擎注销（Rust `deregister_hazard_by_entity` rs:115 ↔ C++ `DeregisterHazard(slot->cdeHandle)` cpp:147-149，按句柄而非实体查——等价，因句柄即注册时返回值）；槽释放（C++ cpp:151-154：取走 placedItem、reset、`++generation`、空闲栈 push ↔ slotmap `remove` 内建）；debug 断言（rs:117 ↔ cpp:155 DebugAssertLayout）。判定：一致。

### `is_empty`（Rust 123-126 行）
- C++ 对应：无公开方法（可由 `PlacedItemCount() == 0` 表达）。
- 判定：C++ 缺失（表达等价存在）。

### `density` / `placed_item_area`（Rust 130-142 行）
- C++ 对应：`Layout::PlacedItemArea`（cpp:225-234，float 累加各占用槽 `placedItem->Area()`）+ `Layout::Density`（cpp:236-240，`PlacedItemArea() / containerArea`，float 除法后转 double）。
- 判定：一致。
- 证据：Rust `density = placed_item_area() / self.container.area()`（rs:131-133）；面积来源 `pi.item.area()`（rs:137-141）↔ C++ `slot.placedItem->Area()`（内部 `m_Item->Area()`，PlacedItem.cpp:53-56）。float 累加顺序：Rust 按 slotmap 迭代序，C++ 按槽位下标序——同一布局一致。

### `items`（Rust 145-150 行，按 idx 去重的迭代器）
- C++ 对应：`Layout::Items`（cpp:439-459）。
- 判定：一致。
- 证据：遍历放置项，按 `item.Index()`（cpp:449）首次出现去重，返回引用列表 ↔ Rust `unique_by(|item| item.idx)`（rs:149）。Rust 注释/实现均为"distinct by idx"；C++ 按首次出现顺序（cpp:443 槽序）与 slotmap 插入序对应。

### `cde`（Rust 153-156 行，暴露 `&CDEngine`）
- C++ 对应：无（引擎为私有 `m_Cde`，无访问器）。
- 判定：C++ 缺失（有意封装；外部分析需求经其他 API 承载）。

### `is_feasible`（Rust 160-168 行）
```rust
self.placed_items.iter().all(|(pk, pi)| {
    let hkey = self.cde.haz_key_from_pi_key(pk).expect(...);
    !self.cde.detect_poly_collision(&pi.shape, &hkey)
})
```
- C++ 对应：`Layout::IsFeasible`（cpp:202-223）。
- 判定：一致。
- 证据：对每个占用槽，构造 `HazardFilter::irrelevantHandle = [own](h){ return h == own; }`（cpp:213-217，排除自身 hazard）后 `DetectPolygonCollision(shape, filter)`（cpp:218-219）↔ Rust `detect_poly_collision(&pi.shape, &hkey)`（按自身 hazard 键排除）。任一碰撞即 false（cpp:220）。语义一致：所有放置物与其它 hazard 无碰撞。
- C++ 额外：`!m_Cde` 时返回 false（cpp:204-206）。

### `LayoutSnapshot::density` / `placed_item_area`（Rust 188-203 行）
- C++ 对应：`LayoutSnapshot::PlacedItemArea`（cpp:34-52）。
- 判定：一致（数值语义；C++ 显式排序）。
- 证据：C++ 先按 handle.index/generation 排序 placements（cpp:40-46）再 float 累加——为对齐 Rust SlotMap 按 (idx, version) 的迭代顺序，使浮点累加顺序一致。density（除以 container.Area()）在 `SPSolution::Density`（SPP/Solution.cpp:5-12）等处内联表达，无独立 LayoutSnapshot::Density 方法。

### C++ 额外 API
- `PlacedItemCount()`（cpp:431-437）：占用槽数。对应 Rust `layout.placed_items.len()`（调用方直写，如 SPP problem.rs:136）。判定：C++ 额外（便捷）。
- `RightmostCollisionX()`（cpp:461-473）：所有放置物碰撞形状 bbox.xMax 的最大值，optional<double>。
- 判定：C++ 额外（为 SPP/MSPP FitStrip 服务）。
- 说明：Rust 的 `fit_strip` 内联遍历 `placed_items.values().map(|pi| pi.shape.bbox.x_max).max()`（spp/problem.rs:44-51）；C++ 把这一步提炼为 Layout 的公开方法（SPP/Problem.cpp:175、MSPP/MspProblem.cpp:294 调用）。数值语义一致（取 bbox 右边界最大值；C++ 用 `BoundingBox().xMax`，且 `FitStrip` 里 `* 1.00001F + offset` 与 Rust `* 1.00001 + offset` 对齐）。
- `GetContainer()`（cpp:426-429）：访问器，对应 Rust 公有字段 `container`。判定：一致。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 句柄体系：slotmap PItemKey vs 手工 (index, generation) 槽位 + 奇偶代数 + 空闲栈 LIFO | 差异（表示法） | 行为等价；键不按位兼容 | Rust rs:20；C++ h:75-91, cpp:62-95 |
| 2 | 槽位 0 哨兵（generation=0 恒空） | C++ 额外 | 使生成键 index 恒非 0，供 Restore 校验；slotmap 无显式对应 | cpp:54-60, 317-322, 333 |
| 3 | Restore 失败时点：容器/槽元数据/null shape 在布局提交前校验；CDE 重建与一致性断言在布局提交后执行 | 具体边界差异 | 前置拒绝保留原布局；Debug CDE 断言可能留下已安装 placements 和部分 CDE 状态；Release 接受已测非空损坏快照 | cpp:263-386, 388-440; rs:74-87; S06 Restore cases |
| 4 | Restore 校验集（句柄合法/去重、代数奇偶、空闲栈全覆盖、hazard 与 placement 一致性回查） | C++ 额外；部分 Debug-only | slot 元数据与 null shape 是 C++ 表示层前置保护；hazard 一致性在恢复后检查，不能概括为所有非法快照都先拒绝 | cpp:277-386, 399-440 |
| 5 | Restore 兼容无槽位元数据快照（hasSlotMetadata 二分路径） | C++ 额外 | 支持外部构造的简化快照；Rust SlotMap 快照总含元数据 | cpp:298-312, 326-329, 388-397 |
| 6 | PlaceItem 注册失败回滚（reset 槽 + 代数递增 + 归还空闲栈） | C++ 额外 | Rust 忽略 register_hazard 返回值（rs:100），失败只在 debug 断言暴露 | cpp:126-132 |
| 7 | SwapContainer 失败原子性 + hazard↔placement 有效性校验 + cdeHandle 回填 | C++ 增强 | Rust swap 无失败路径；C++ 失败不动原状态 | cpp:159-200 vs rs:46-55 |
| 8 | `cde()` 与 `is_empty()` 未暴露 | C++ 缺失 | 引擎被封装（外部分析经 IsFeasible/RightmostCollisionX 等）；is_empty 可由 PlacedItemCount()==0 表达 | rs:123-126, 153-156；C++ h:37-92 |
| 9 | `RightmostCollisionX()`、`PlacedItemCount()` 公开方法 | C++ 额外 | Rust 侧为内联表达/公有字段；数值一致 | cpp:431-437, 461-473 |
| 10 | LayoutSnapshot 显式携带 slotGenerations/freeSlotIndices（Rust 为 SlotMap 隐式） | 差异（表示法） | 快照集合语义一致；C++ 可校验与重建槽元数据 | h:24-34 vs rs:179-186 |
| 11 | LayoutSnapshot::PlacedItemArea 显式按 handle 排序后累加 | C++ 额外（对齐性） | 固定浮点累加顺序对齐 slotmap 迭代序 | cpp:36-46 |
| 12 | from_snapshot 的 panic（expect）vs C++ 调用方"新容器+Restore"不可达失败 | 差异（承载） | 功能等价 | rs:37-43；SPP/Problem.cpp:242-249 |
| 13 | debug 断言：Rust layout_qt_matches_fresh_qt/snapshot_matches_layout（quadtree 重建比对）vs C++ DebugAssertLayout→ValidateLayout（结构校验） | 差异（形态） | 双方均 debug-only（C++ `#ifndef NDEBUG`）；校验手段不同、目标一致 | rs:86-87, 102, 117；Assertions.cpp:378-385 |
| 14 | Layout 不可拷贝（C++ delete 拷贝构造）vs Rust `#[derive(Clone)]` | 差异（API） | Rust 问题层靠 Clone 深拷贝（如 BPP restore 重建）；C++ 用移动/重建替代 | h:42-45 vs rs:15 |

## 结论
Layout 的核心动态行为（place/remove/save/swap/is_feasible/面积密度）双侧语义一致，C++ 用手工槽位结构复刻了 slotmap 的键分配与 LIFO 复用规则，并通过 f32 对齐保持数值路径一致。Restore 的容器、槽元数据和 null-shape 前置检查属于 C++ 表示层保护；CDE/布局一致性断言发生在布局和/或 CDE 状态更新之后，Debug 失败可能留下部分状态，Release 对已测非空损坏快照会接受。具体恢复顺序、profile 差异与覆盖边界见上文及 Sparrow S06 reference，不能概括为 C++ Restore 普遍更严格或原子。PlaceItem/SwapContainer 的额外失败保护仍单独记录。`cde()`/`is_empty()` 未暴露、`RightmostCollisionX`/`PlacedItemCount` 为 C++ 额外提炼。

## 补充审计（2026-09-29，逐行复核）

逐行清点 layout.rs（203 行）全部符号：struct Layout、new、from_snapshot、swap_container、save、restore、place_item、remove_item、is_empty、density、placed_item_area、items、cde、is_feasible、ContainerMismatch、struct LayoutSnapshot、density、placed_item_area。正文已全覆盖全部符号，无【遗漏】。逐行复核补充：

### 补-1【验证】`Layout::new` 的 CDEngine 共享语义差异（细节确认）
Rust（layout.rs:28）：`container.base_cde.as_ref().clone()` —— CDEngine **深克隆**（quadtree 树 + hazards_map + config），base_cde 与 layout 引擎独立；C++（Layout.cpp:54-60）：`m_Cde(m_Container.CreateCollisionEngine())` —— **新建**引擎并重新注册静态 hazard（Container.cpp:137-147）。每次 Layout::new 双侧都得到"独立可变"的引擎；Rust 克隆预建树（注册顺序即 base_cde 构建序），C++ 重新注册（同序）。判定：结果等价（正文已记，容器记录 24 差异 #1 的关联面）。

### 补-2【浅覆盖→补充】`place_item` 的 `(pk, pi).into()` 实体构造（与记录 16 补-2 联动）
Rust（layout.rs:98）：`Hazard::new((pk, pi).into(), pi.shape.clone(), true)` —— From 转换携带 `id: pi.item.idx, dt: pi.d_transf, pk`。C++（Layout.cpp:126 + MakeHazard:97-105）：MakeHazard 填 `itemIndex/placedItemHandle/transform`，`dynamic=true`，shape 为 `make_shared<SimplePolygon>(placedItem.CollisionShape())` **值拷贝**（Rust Arc 共享）。判定：结果等价/实现不同（Arc 共享 vs 值拷贝，同记录 23 差异 #4 的模式；行为等价因不可变）。

### 补-3【验证】`is_feasible` 的 filter 通道（细节确认）
Rust（layout.rs:166）：`detect_poly_collision(&pi.shape, &hkey)` —— filter 为 `impl HazardFilter for HazKey`（自身键排除）。C++（Layout.cpp:213-216）：`irrelevantHandle = [ownHazard](h){ return h == ownHazard; }`。判定：一致（正文已记；补充确认 ownHazard = slot.cdeHandle 即注册时返回的真实句柄，与 Rust hkey 语义逐点对应）。
**注意 2026-09-29 重写后**：DetectPolygonCollision 已改为短路 detect（isOutside + NodeCollidesWithEdge + DetectContainmentCollision，CollisionEngine.cpp:567-598），与 Rust detect_poly_collision 的三段结构（前提→边→containment）逐行对应（记录 22 补-6）。IsFeasible 语义不变。

### 补-4【验证】`items()` 的去重语义（细节确认）
Rust（layout.rs:145-150）：`unique_by(|item| item.idx)` —— itertools unique_by 保留**首次出现**。C++（Layout.cpp:439-459）：按槽序首个 itemIndex 未见过则收。判定：一致（正文已记；补充确认"首次出现"语义双侧一致，槽序↔slotmap 迭代序）。

### 补-5【遗漏】`LayoutSnapshot` 的 field 语义差（细节登记）
Rust LayoutSnapshot 持 `placed_items: SlotMap<PItemKey, PlacedItem>` **完整克隆**（含空闲槽/代数）；C++ 持 `placements: vector<LayoutPlacementSnapshot>` **仅占用项** + `slotGenerations`/`freeSlotIndices` 显式元数据（h:24-34）。快照语义：Rust 克隆后 restore 即整体覆盖；C++ restore 需从 placements+元数据重建（或无元数据时推断，正文差异 #5）。判定：正文差异 #10 已记，此补充明确"仅占用项+元数据"vs"整表克隆"的具体形态。无行为差（合法快照）。

### 补-6【验证】`RightmostCollisionX` 的数值路径（细节确认）
Rust fit_strip 内联（spp/problem.rs:44-51）：`max(pi.shape.bbox.x_max)`（f32 max_by partial_cmp）。C++（Layout.cpp:461-473）：遍历占用槽 `placedItem->CollisionShape().BoundingBox().xMax` 取 max（double 比较，bbox 为 f32 计算转 double）。f32 值域内 max 结果一致。判定：一致（正文已记）。
