# 29. BPP 逐文件逻辑比对（Bin / BPInstance / BPProblem / BPSolution）
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/probs/bpp/entities/{bin,instance,problem,solution}.rs`（37 / 68 / 286 / 44 行）
**C++**: `JaguarCore/Include/JaguarCore/Problems/Bpp.h`（202 行）+ `JaguarCore/Source/Problems/BPP/{BppBin,BppInstance,BppProblem,BppSolution}.cpp`（2 / 116 / 517 / 62 行）
**判定**: 常规放置与布局状态逻辑基本等价；超需求 Debug 的部分状态尚不等价；Restore 防御层、公开聚合 API、time_stamp 与键表示仍有差异

**2026-09-28 oracle 补充：** `--model-boundary-parity-only` 显示需求 0 的 BPP 放置在 Rust Debug 先开箱并插入条目，再于需求下溢时 panic；捕获 panic 后为 `remaining=0, placed=1, stock=0`。JaguarCore Debug 在放置前返回 `Infeasible`，不改变布局/库存；Release 两侧都成功放置并将需求回绕为 `usize::MAX`。该 Debug 部分状态差异需要按本轮待确认的 panic→Result 口径处理，不能只比较失败类型。

## 函数逐条比对

### A. Bin（bin.rs ↔ Bpp.h/BppBin.cpp）

#### `struct Bin` + `Bin::new`（Rust bin.rs:5-36）
- C++ 对应：`Bpp.h:15-27` `struct Bin`（聚合结构，无构造函数；BppBin.cpp 仅 1 行 include，无实现）。
- 判定：一致（字段一一对应：idx↔index、external_id↔externalId、container↔container、stock↔stock、cost↔cost；Rust new 为纯装配无校验，C++ 由 BPInstance::Create 校验 index 稠密性）。

### B. BPInstance（instance.rs ↔ BppInstance.cpp）

#### `BPInstance::new`（Rust instance.rs:18-23）
- C++ 对应：`BPInstance::Create`（BppInstance.cpp:17-59）。
- 判定：一致。
- 证据：`instance_item_bin_ids_correct`（items idx 稠密 + bins idx 稠密）↔ cpp:30-46（"BPP item indices must be dense" + 外部 ID 唯一）与 cpp:48-55（"BPP bin indices must be dense"）；items/demands 尺寸校验 cpp:22-26 为 C++ 额外（Rust 由 zip/索引兜底）。

#### `item_area` / `item_qty` / `bins` / `bin_qty` / `total_item_qty` / `item` / `item_idx`（Rust instance.rs:25-67）
- C++ 对应：`TotalItemArea`（cpp:105-114，f32 累加同 Rust）、`ItemDemand`（cpp:82-85）、`BinCount`（cpp:87-90）、`GetBin`（cpp:92-95）、`TotalDemand`（cpp:97-103）、`GetItem`（cpp:66-69）、`ItemIndexFromExternalId`（cpp:71-80）。
- 判定：一致（含 `bins()` 迭代器 ↔ 遍历 `GetBin/BinCount` 的等价表达；`bin_qty` ↔ `GetBin(id).stock`）。

### C. BPProblem（problem.rs ↔ BppProblem.cpp）

#### 布局键基础设施（Rust problem.rs:11-14 `new_key_type! LayKey` + `SlotMap<LayKey, Layout>` + `SecondaryMap<LayKey, usize>`）
- C++ 对应：`LayoutSlot`（Bpp.h:166-175：optional<Layout> + binIndex + generation）、`m_Layouts: vector<LayoutSlot>`、`m_FreeLayoutIndices`、键编码 `makeLayoutKey/splitLayoutKey`（BppProblem.cpp:14-38：`generation << 32 | slotIndex`，各取 32 位）、`GetLayout`（cpp:89-112，解码+代数+占用校验）、`AllocateLayoutSlot`（cpp:114-130，LIFO 弹栈 + `generation | 1`，或追加）、`ReleaseLayoutSlot`（cpp:132-144，`generation+1`、入栈）。
- 判定：差异（表示法）——功能对齐 slotmap：键含代数、失效可检、LIFO 复用、奇数代占用/偶数代空闲的约定（分配 `|1`、释放 `+1`）与 Layout 层（记录 26 差异 1/2）同一套手工复刻。**slotmap 的 SlotMap 键位布局是私有的，与 C++ 的 `gen<<32|idx` 编码不按位兼容**。
- 哨兵：构造 `m_Layouts(1)` + `front().generation = 0`（cpp:66-75），槽 0 恒不分配（Restore 校验 `requestedSlot == 0` 拒绝，cpp:334）。slotmap 无显式对应。

#### `BPProblem::new`（Rust problem.rs:29-40）
- C++ 对应：`BPProblem::Create` + 私有构造（BppProblem.cpp:66-87）。
- 判定：一致（item_demand_qtys ← items qty；bin_stock_qtys ← bins stock；layouts 空）。

#### `remove_layout`（Rust problem.rs:43-45 → `deregister_layout` rs:221-232）
- C++ 对应：`BPProblem::RemoveLayout`（BppProblem.cpp:260-291）。
- 判定：基本等价。
- 证据：注销布局 → 库存加回（close_bin）→ 各放置项需求加回。Rust `deregister_layout`（remove、close_bin、遍历 deregister_included_item）；C++ 先 Save 快照、校验 itemIndex 在范围内（cpp:275-283）、`++m_RemainingDemand`（cpp:284-285）、`++m_RemainingBinStock`（cpp:287）、释放槽（cpp:288）。判定：一致（C++ 的 itemIndex 越界校验为防御性额外）。

#### `place_item`（Rust problem.rs:48-66）
- C++ 对应：`BPProblem::PlaceItem`（BppProblem.cpp:166-221）。
- 判定：Release 状态行为一致；Debug 下 C++ `Infeasible` 发生在开箱/放置前，Rust 下溢 panic 发生在布局更新后，状态不等价。
- 逐步对照：
  1. 布局选择：Rust `BPLayoutType::Open(lkey)` 直用 / `Closed{bin_id}` 时 `Layout::new(bin.container)` + `register_layout`（rs:49-57）↔ C++ `targetKind == Closed` 时 `OpenLayout(binIndex)`（cpp:184-191 → cpp:146-164：bin 越界/库存耗尽校验 → 新建 Layout → AllocateLayoutSlot → `--m_RemainingBinStock`）。判定：一致（注意 **LayoutTargetKind 命名/注释与 Rust 语义相反**：Rust `Open(lkey)`=已有布局、`Closed{bin_id}`=新开箱；C++ `LayoutTargetKind::Open` 注释"放入新布局"、`Closed` 注释"放入已有布局"（Bpp.h:71-77），而实现中 `Closed` 分支走 OpenLayout 开新布局（cpp:184-191）、RemoveItem 返回 `Open` 表示"放入已有布局"（cpp:245-255 闭空布局时改回 Closed）。**即 C++ 的枚举值语义与 Rust 完全对齐（Closed=开新箱），仅头文件注释写反**——文档错误，非行为差异。）
  2. 需求耗尽校验：C++ `#ifndef NDEBUG` 包裹（cpp:174-180，"BPP item demand is exhausted" → Infeasible）；**Rust 无校验**（`item_demand_qtys[item_idx] -= 1` 直接执行，rs:63/register_included_item rs:234-236）。判定：差异（同记录 27 差异 4；Release 双侧均不拦截）。
  3. itemIndex/layoutIndex 越界：C++ cpp:169-173（item）、cpp:193-203（open 后 GetLayout 失败时**回滚**：`++m_RemainingBinStock` + ReleaseLayoutSlot）；Rust 靠索引 panic。判定：差异（C++ 额外回滚）。
  4. 放置失败回滚（**C++ 额外**）：Rust 的 `layout.place_item` 不失败（Layout::place_item 无 Result，rs:61）；C++ `slot->layout->PlaceItem` 可失败（Record 26 差异 6），失败时若刚开箱则回滚库存与槽位（cpp:207-215）。判定：C++ 额外。
  5. 记账顺序：Rust 先 `register_included_item`（rs:63，open 新箱时 register_layout 内部也对空布局无操作）后放置完成；C++ 放置成功后 `--m_RemainingDemand`（cpp:217）。判定：一致（结果等价）。
  6. debug 断言：Rust 无（problem 层 place 无断言）↔ C++ cpp:218 `DebugAssertBPSolution`（debug-only，Assertions.cpp:396-403）。判定：C++ 额外（debug-only）。

#### `remove_item`（Rust problem.rs:69-80）
- C++ 对应：`BPProblem::RemoveItem`（BppProblem.cpp:223-258）。
- 判定：基本等价。
- 证据：移除 → 需求加回 → 空则闭箱（Rust `is_empty()` → deregister_layout → 返回 `Closed{bin_id}` placement；C++ `PlacedItemCount() == 0` → 库存加回 + ReleaseLayoutSlot + 返回 Closed，cpp:248-255）。非空返回 Open(lkey)（rs:78 ↔ cpp:245-247）。判定：一致（闭箱判据等价：is_empty ↔ PlacedItemCount()==0）。

#### `save`（Rust problem.rs:84-100）
- C++ 对应：`BPProblem::Save`（BppProblem.cpp:293-305）。
- 判定：基本等价。
- 证据：每布局 (lkey, layout.save()) + layout_bins 克隆 ↔ `BPLayoutSnapshot{makeLayoutKey(slotIndex, generation), binIndex, layout->Save()}` 逐占用槽收集。**time_stamp**：Rust rs:94 `Instant::now()`；C++ BPSolution 无该字段（Bpp.h:115-124）——C++ 缺失（同记录 27 差异 1）。Rust debug 断言 rs:97 ↔ C++ Save 不调用（mutating 方法内 DebugAssertBPSolution）。

#### `restore`（Rust problem.rs:104-168）
- C++ 对应：`BPProblem::Restore`（BppProblem.cpp:307-454）。
- 判定：基本等价（保留键策略、增删布局、需求/库存重建一致；C++ 大幅增强校验与两阶段构建）。
- 逐步对照：
  1. 逐解布局预验证（**C++ 额外**，cpp:329-368）：键合法（槽非 0、代数奇）、无重复键/槽、bin 有效、**库存非 0 且 `SameBase(实例 bin 容器)`**（cpp:337-340）、独立重建 Layout 并 Restore 成功（cpp:346-349）、placement 不超需求且物品定义一致（cpp:351-363，`sameItemDefinition`）。Rust 无（信任 SecondaryMap 键与快照一致性；bin 一致性由 layout.restore 的 base_cde 检查在替换路径兜底）。
  2. 需求/库存初始化对账（C++ cpp:318-323, 367 预先递减）↔ Rust 在最后统一重建（rs:142-163）。判定：一致（公式相同：required - placed = remaining；stock - opened = remaining）。
  3. 保留键判定（cpp:370-406）：解中键在当前问题中仍有效（`GetLayout(requestedKey)`）→ 保留该槽（preservesKey）；当前占用但不被保留 → 代数 +1、入空闲栈、`layoutKeysChanged = true`。↔ Rust（rs:110-139）：问题中布局在解中无快照 → remove（键变）；解中有 → 原地 restore 或重建。判定：一致（"键变化"的判定口径等价：布局集合有增删即 true）。
  4. 排序确定性（C++ cpp:372-376）：`std::sort(prepared, ...)` 按槽序——注释明言"Rust iterates its SecondaryMap by slot index. Keep stale-key reinsertions deterministic even when a C++ solution vector is ordered differently"。即对齐 Rust SecondaryMap 按槽序迭代的插入顺序。判定：C++ 额外（对齐性，因 C++ 解是 vector 可乱序）。
  5. 新布局分配（cpp:408-446）：非保留键从空闲栈 LIFO 弹出或追加，代数 `|1`；保留键尝试原地 `current->layout->Restore(layout.layout.Save())`（cpp:429-441）——对应 Rust rs:115-117 的"原地 restore 失败则 Layout::from_snapshot 替换"。判定：一致（含原地优先、替换兜底）。
  6. 提交（cpp:448-451：全构建完毕后一次性赋值）↔ Rust 逐句赋值。判定：C++ 原子性增强（同记录 26 差异 3）。
  7. debug 断言：Rust rs:166 ↔ C++ cpp:452 `DebugAssertBPSolution`。判定：一致（debug-only 双侧）。
- 注：Rust restore 的每布局 `layout_bins[lkey] = solution.layout_bins[lkey]`（rs:113）↔ C++ 保留槽 `preserved.binIndex = layout.binIndex`（cpp:437）/新槽 `LayoutSlot{..., layout.binIndex, ...}`（cpp:444-445）。判定：一致。

#### `density`（Rust problem.rs:171-185）
- C++ 对应：`BPProblem::Density`（BppProblem.cpp:486-499）。
- 判定：一致（`sum(layout.container.area())` 为分母 ↔ `slot.layout->GetContainer().Area()` 累加；`sum(placed_item_area)` 分子；f32 除法转 double。**口径同 Rust：用 container.area()**，与 MSPP 解层 fixed_height*width 口径的区分双侧一致，见记录 31）。

#### `item_placed_qtys` / `bin_used_qtys`（Rust problem.rs:187-199，迭代器：`instance qty - remaining`）
- C++ 对应：无直接对应（`RemainingDemand`/`RemainingBinStock` 单点查询存在，BppProblem.cpp:456-465；聚合迭代器未暴露）。
- 判定：C++ 缺失（可由 RemainingDemand 逐项表达）。
- 影响：`bin_cost` 在 C++ 直接遍历布局求和（cpp:501-510，`GetBin(slot.binIndex).cost` 累加），不经 item_placed/bin_used 迭代器；数值与 Rust `bin_cost()`（rs:203-208，`bins[id].cost * used` 加权和）**一致**（每个开布局恰用 1 个 bin，两种求和等价）。

#### `bin_cost`（Rust problem.rs:203-208）
- C++ 对应：`BPProblem::BinCost`（BppProblem.cpp:501-510）。判定：一致（如上）。

#### `n_placed_items`（Rust problem.rs:251-253）
- C++ 对应：`PlacedItemCount`（BppProblem.cpp:467-476）。判定：一致（各布局放置数求和）。
- C++ 额外：`OpenLayoutCount`（cpp:478-484，开布局数，对应 Rust `layouts.len()` 的表达性等价物）。

#### `BPPlacement` + `from_placed_item`（Rust problem.rs:256-276）
- C++ 对应：`Bpp.h:80-92` `struct BPPlacement`（targetKind + layoutIndex + binIndex + itemIndex + transform；from_placed_item 的等价物内联于 RemoveItem cpp:245-247）。
- 判定：一致（数据对应；Closed 布局下 layoutIndex 字段闲置的约定与 Rust `BPLayoutType::Closed{bin_id}` 不带 lkey 一致）。

#### `BPLayoutType`（Rust problem.rs:279-285）
- C++ 对应：`LayoutTargetKind`（Bpp.h:71-77）。
- 判定：一致（行为）/差异（注释）。
- 说明：Rust `Open(LayKey)`=已有布局、`Closed{bin_id}`=待开新箱；C++ 枚举成员 `Open`/`Closed` 与之逐值对应（Closed 走 OpenLayout 开新箱），**但头文件注释写反**（"Open // 放入新布局；Closed // 放入已有布局"）——实现以 Closed=开新箱运作（cpp:184-191、253），与 Rust 对齐。属文档错误。

### D. BPSolution（solution.rs ↔ BppSolution.cpp）

#### `struct BPSolution`（Rust solution.rs:8-16：SecondaryMap 双表 + time_stamp）
- C++ 对应：`Bpp.h:115-124`（`vector<BPLayoutSnapshot>`；无 time_stamp）。判定：基本等价（C++ 缺 time_stamp；SecondaryMap↔vector+显式 layoutIndex 键）。

#### `density`（Rust solution.rs:20-34，`sum(ls.container.area())` 分母）
- C++ 对应：`BPSolution::Density`（BppSolution.cpp:37-49）。
- 判定：一致（分母 `layoutSnapshot.container.Area()` ↔ `ls.container.area()`；分子 placed_item_area；**先按槽序排序再累加**（cpp:15-34 layoutsInSlotOrder）对齐 Rust SecondaryMap 迭代序，保证 f32 累加顺序一致——C++ 额外的对齐措施）。

#### `cost(instance)`（Rust solution.rs:37-42，每布局 `instance.bins[idx].cost` 求和）
- C++ 对应：`BPSolution::Cost`（BppSolution.cpp:51-60）。判定：一致（binIndex 越界跳过为 C++ 防御性差异，Rust 会 panic）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | LayKey 表示：Rust slotmap 不透明键 vs C++ `generation<<32\|slotIndex` 编码 + LayoutSlot/代数/LIFO 空闲栈手工复刻 | 差异（表示法） | 行为等价；键不按位兼容 | rs(problem):11-14；BppProblem.cpp:14-38, 89-144 |
| 2 | 布局键编码中 C++ 槽 0 哨兵（generation=0 恒空，Restore 拒绝槽 0） | C++ 额外 | 与 slotmap 无显式对应的防御位 | BppProblem.cpp:74, 334 |
| 3 | PlaceItem 需求耗尽：C++ Debug 预检返回 Infeasible，Rust Debug 在开箱并放置后下溢 panic；Release 双侧放置并回绕 | Debug 状态差异 | Rust panic 后 `remaining=0, placed=1, stock=0`；C++ 错误前状态不变 | BppProblem.cpp:174-180 vs rs(problem):48-66；`--model-boundary-parity-only` |
| 4 | PlaceItem 失败回滚（open 后 GetLayout 失败 / layout->PlaceItem 失败 → 库存与槽位回滚） | C++ 额外 | Rust layout.place_item 无失败路径，无需回滚 | BppProblem.cpp:193-215 |
| 5 | BPSolution 无 time_stamp | C++ 缺失 | 同记录 27 差异 1 | rs(solution):15；Bpp.h:115-124 |
| 6 | item_placed_qtys() / bin_used_qtys() 迭代器未暴露 | C++ 缺失 | 可由 RemainingDemand/RemainingBinStock 逐项表达；bin_cost 已按等价方式实现 | rs(problem):187-199；BppProblem.cpp:456-465, 501-510 |
| 7 | LayoutTargetKind 头文件注释与实现（及 Rust 语义）相反（Open 注释"放入新布局"实际表示已有布局） | 差异（文档） | 行为与 Rust 对齐（Closed=开新箱）；注释误导维护者 | Bpp.h:71-77 vs rs(problem):279-285；BppProblem.cpp:184-191 |
| 8 | Restore 两阶段构建 + 预验证（键合法性/去重、bin SameBase、库存非 0、物品定义、独立重建）+ 全量校验后原子提交 | C++ 增强 | Rust 依赖 SecondaryMap 键语义与内部约定 | BppProblem.cpp:307-454 vs rs(problem):104-168 |
| 9 | Restore 前 `std::sort(prepared)` 按槽序 | C++ 额外（对齐性） | 复现 Rust SecondaryMap 按槽迭代的确定性（C++ vector 可乱序） | BppProblem.cpp:370-376（含注释） |
| 10 | BPSolution::Density 先按槽序排序再累加 | C++ 额外（对齐性） | f32 累加顺序与 Rust 迭代序一致 | BppSolution.cpp:15-34, 39-48 |
| 11 | RemoveLayout/RemoveItem/Restore 的 itemIndex 越界与 binIndex 校验（SnapshotMismatch/InvalidInput） | C++ 额外 | Rust 依赖索引 panic | BppProblem.cpp:238-242, 268-272, 275-283 |
| 12 | C++ Create/PlaceItem 等全链路 Result 化（bin 越界、库存耗尽等） | 差异（形态） | 合法输入行为一致 | BppProblem.cpp:146-164, 169-173 |

## 结论
BPP 的常规动态语义、Release 超需求回绕和键复用顺序一致；但 Debug 超需求调用的可观察状态不同：Rust 已开箱并放入条目后 panic，C++ 在放置前返回错误。Restore 预验证/原子提交、C++ 失败回滚、未暴露的 placed/used 迭代器与 time_stamp 仍属差异或接口缺口；不可据全套测试通过宣称完全等价。

## 补充审计（2026-09-29，逐行复核）

逐行清点 BPP 四文件全部符号：Bin{new}；BPInstance{new,item_area,item_qty,bins,bin_qty,total_item_qty,item,item_idx}；BPProblem{new,remove_layout,place_item,remove_item,save,restore,density,item_placed_qtys,bin_used_qtys,bin_cost,register_layout,deregister_layout,register/deregister_included_item,open_bin,close_bin,n_placed_items}、LayKey、BPPlacement{from_placed_item}、BPLayoutType；BPSolution{density,cost}。正文已全覆盖全部符号，无【遗漏】函数。逐行复核补充：

### 补-1【验证】PlaceItem 回滚链（细节确认）
BppProblem.cpp:171-217：itemIndex 越界（170-174）→ `#ifndef NDEBUG` 需求耗尽（175-181）→ Closed 分支 OpenLayout（183-190）→ GetLayout 失败回滚 `++m_RemainingBinStock` + ReleaseLayoutSlot（194-203）→ PlaceItem 失败同样回滚（205-215）→ 成功后 `--m_RemainingDemand`（216）。与正文步骤 1-4 一致，行号经复核准确。判定维持。

### 补-2【验证】RemoveItem 的闭箱判定与 placement 构造（细节确认）
BppProblem.cpp:223-258：removed → itemIndex 越界容错（238-243）→ `++m_RemainingDemand`（244）→ 构造 BPPlacement（Open, layoutIndex, binIndex, itemIndex, transform）（246-249）→ 空则 `++m_RemainingBinStock` + ReleaseLayoutSlot + 改 Closed 且 layoutIndex=0（250-257）。Rust remove_item（problem.rs:69-80）：`is_empty()` → deregister_layout → `BPPlacement::from_placed_item(Closed{bin_id})`。差异：Rust Closed placement 仍携带 bin_id；C++ result.binIndex 恒填（Open/Closed 均有），layoutIndex 在 Closed 时清 0（Rust Closed 变体无 lkey 概念，由枚举保证）。判定：一致（正文已记；补充 C++ 侧 layoutIndex=0 的清零细节）。

### 补-3【验证】OpenLayout 的库存扣减时点（细节确认）
BppProblem.cpp:146-164（正文步骤 1 引用）：校验 bin 越界/库存 → `Layout(bin.container)` → AllocateLayoutSlot → `--m_RemainingBinStock`（163）。Rust register_layout（problem.rs:210-217）：`open_bin(bin_idx)` 先扣库存 → 遍历放置项登记（新开布局为空，无操作）→ insert。判定：一致（扣减时点同为成功路径末段）。

### 补-4【遗漏】BPSolution::Cost 的密度口径核对（细节）
Rust cost（solution.rs:37-42）：`layout_bins.values().map(|idx| instance.bins[*idx].cost).sum()`。C++（BppSolution.cpp:51-60）：遍历 layouts 取 `instance.GetBin(binIndex).cost` 累加，**越界跳过**（Rust 越界 panic）。正文已记该差异（112 行）；补充确认 Rust 的 SecondaryMap<LayKey,usize> 的 values() 迭代序（槽序）↔ C++ vector 顺序——cost 为 u64 整数加法无浮点序差，顺序不影响结果。判定维持。

### 补-5【验证】restore 的键保留排序注释（细节确认）
BppProblem.cpp:370-376 的 std::sort 注释与正文差异 #9 一致。Rust 侧 SecondaryMap 迭代序=槽序（slotmap 文档语义），C++ sort 按 `requestedKey & kLayoutKeyPartMask`（槽序）对齐。判定维持。
