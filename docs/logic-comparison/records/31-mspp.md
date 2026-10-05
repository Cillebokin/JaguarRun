# 31. MSPP 逐文件逻辑比对（Strip / MSPInstance / MSPProblem / MSPSolution + util/assertions.rs）
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/probs/mspp/entities/{strip,instance,problem,solution}.rs`（61 / 64 / 263 / 44 行）+ `CodeResource/jagua-rs/jagua-rs/src/probs/mspp/util/assertions.rs`（36 行）
**C++**: `JaguarCore/Include/JaguarCore/Problems/MSpp.h`（212 行）+ `JaguarCore/Source/Problems/MSPP/{MspStrip,MspInstance,MspProblem,MspSolution}.cpp`（65 / 150 / 545 / 59 行）
**判定**: 常规条带/布局逻辑基本等价；超需求 Debug 状态与 Restore 的不一致宽度接受边界仍不等价；C++ 保留额外快照校验和接口差异

> **2026-09-29 更新**：`MSPProblem::Restore` 已删除 strip 数值合法性与 sameStripContainer 前置校验（对齐 Rust：快照 strip 直接生效；Rust Debug 的一致性断言在状态变更后 panic，不属于验收契约）。本记录中“C++ 两配置均拒绝宽度不一致”条目已过时。

**2026-09-28 oracle 补充：** `--model-boundary-parity-only` 显示需求 0 的 MSPP 放置在 Rust Debug 先插入条目，再于需求下溢时 panic；捕获 panic 后为 `remaining=0, placed=1`。JaguarCore Debug 在放置前返回 `Infeasible`、布局不变；Release 两侧都成功放置并将需求回绕为 `usize::MAX`。此 Debug 部分状态差异需按待确认的 panic→Result 口径处理。

## 函数逐条比对

### A. Strip（strip.rs ↔ MspStrip.cpp，C++ 名 MultiStrip）

#### `struct Strip` + `new`（Rust strip.rs:11-37）
- C++ 对应：`MSpp.h:15-39` + `MultiStrip::Create`（MspStrip.cpp:5-32）。
- 判定：基本等价。
- 证据：字段 `max_width/fixed_height/cde_config/shape_modify_config/width` ↔ `maxWidth/fixedHeight/cdeConfig/shapeModifyConfig/width`（h:16-24）。校验 `fixed_height > 0 && max_width > 0 && width > 0`（rs:27-29）↔ cpp:12-17 三条合并；C++ 额外做 f32 舍入校验（cpp:18-26，同记录 27 A 节模式）。

#### `set_width`（Rust strip.rs:39-43）
- C++ 对应：`MultiStrip::SetWidth`（MspStrip.cpp:34-50）。
- 判定：一致（`width <= max_width`（f32 舍入比较）与 `width > 0` 两条断言 ↔ 两条错误返回；注意 C++ cpp:38 先做 `rustWidth <= rustMaxWidth` 再做正数校验，Rust rs:40-41 先 max 后正——顺序不同不影响结果集合）。

#### `From<Strip> for Container`（Rust strip.rs:46-60）
- C++ 对应：`MultiStrip::CreateContainer`（MspStrip.cpp:52-63）。
- 判定：一致（矩形 (0,0)-(width,fixedHeight)、Identity 预变换、Deflate、无质量区、cde_config；与 SPP 版逐行同构；ContainerId 默认 0 为 C++ 额外参数）。

### B. MSPInstance（instance.rs ↔ MspInstance.cpp）

#### `MSPInstance::new`（Rust instance.rs:19-28）
- C++ 对应：`MSPInstance::Create` 两个重载（MspInstance.cpp:48-98）。
- 判定：基本等价。
- 证据：`instance_item_indices_correct` + external_id 唯一 ↔ `copyInstanceItems`（cpp:10-36，idx 稠密 + ID 唯一，两个重载共用）。
- C++ 额外（Create(maxWidth,...) 重载，cpp:66-67）：以 `MultiStrip::Create(maxWidth, ..., maxWidth)` 即 **width = maxWidth** 构造基础条带（Rust 无此重载，Strip 必须显式传入）。

#### `item_area` / `item_qty` / `total_item_qty` / `item` / `item_idx`（Rust instance.rs:30-62）
- C++ 对应：`TotalItemArea`（cpp:134-143）、`ItemDemand`（cpp:121-124）、`TotalDemand`（cpp:126-132）、`GetItem`（cpp:105-108）、`ItemIndexFromExternalId`（cpp:110-119）。判定：一致（与 SPP/BPP 的 Instance 同构）。

### C. MSPProblem（problem.rs ↔ MspProblem.cpp）

#### 布局键基础设施（Rust problem.rs:11-14 LayKey + SlotMap<LayKey, Layout> + SecondaryMap<LayKey, Strip>）
- C++ 对应：`LayoutSlot`（MSpp.h:182-190：optional<Layout> + strip + generation）+ `makeLayoutKey/splitLayoutKey/GetLayout/AllocateLayoutSlot/ReleaseLayoutSlot`（MspProblem.cpp:16-40, 109-164）。
- 判定：差异（表示法）——与 BPP（记录 29 差异 1/2）完全同构：`generation<<32|slotIndex` 编码、奇偶代数、LIFO 空闲栈、槽 0 哨兵（cpp:94-98, 350）。不按位兼容 slotmap 键。

#### `MSPProblem::new`（Rust problem.rs:31-40）
- C++ 对应：`MSPProblem::Create` + 私有构造（MspProblem.cpp:91-107）。判定：一致（item_demand_qtys ← instance；layouts/strips 空）。

#### `add_layout_from_strip`（Rust problem.rs:44-49）
- C++ 对应：`MSPProblem::AddLayout`（MspProblem.cpp:166-181）。
- 判定：一致。
- 证据：`Layout::new(Container::from(strip))` + `register_layout`（空布局无放置项可登记）+ `strips.insert(lk, strip)` ↔ C++ `MultiStrip::Create` 归一化（cpp:168-173，重新校验，C++ 额外）→ `strip.CreateContainer()` → `AllocateLayoutSlot`（strip 存入槽内，cpp:178-179）。

#### `remove_layout`（Rust problem.rs:52-54 → deregister_layout rs:198-206）
- C++ 对应：`MSPProblem::RemoveLayout`（MspProblem.cpp:183-205）。
- 判定：基本等价（放置项需求加回 + 移除 strips 映射 + 释放槽；C++ 额外 itemIndex 越界校验 cpp:194-199）。

#### `fit_strip`（Rust problem.rs:57-74）
- C++ 对应：`MSPProblem::FitStrip`（MspProblem.cpp:286-310）。
- 判定：基本等价（与 SPP FitStrip 同构；差异同记录 27 差异 2/3）。
- 证据：`max(x_max) * 1.00001 + offset` ↔ `itemXMax * 1.00001F + offset`（cpp:300-303）；空布局 panic（rs:66 `unwrap()`）↔ `Infeasible "cannot fit an empty MSPP strip"`（cpp:295-299）；**前后 is_feasible debug_assert_eq（rs:58, 73）在 C++ 无对应**；C++ 额外 isfinite/>0 校验（cpp:304-308）。

#### `place_item`（Rust problem.rs:78-88）
- C++ 对应：`MSPProblem::PlaceItem`（MspProblem.cpp:207-236）。
- 判定：Release 状态行为一致；Debug 下 C++ 在放置前返回 Infeasible，Rust 放置后需求下溢 panic，状态不等价。
- 证据：itemIndex 越界（cpp:210-214）、`#ifndef NDEBUG` 需求耗尽（cpp:215-221，"MSPP item demand is exhausted"；Rust 无校验）、layoutIndex 无效（cpp:222-227，Rust 索引 panic）、放置成功后 `--m_RemainingDemand`（cpp:232 ↔ rs:85）。debug 断言：Rust 无 ↔ C++ cpp:233 DebugAssertMSPSolution（debug-only）。

#### `remove_item`（Rust problem.rs:92-101）
- C++ 对应：`MSPProblem::RemoveItem`（MspProblem.cpp:238-260）。判定：一致（移除 → 需求加回 → 返回 MSPPlacement{lk, item_idx, d_transf} ↔ {layoutIndex, itemIndex, Transform}；C++ 额外 itemIndex 越界校验 cpp:251-255。**注意 MSPP remove 不闭布局**（与 BPP 不同，空布局保留），双侧一致——Rust rs:92-101 无 is_empty 分支，C++ 同）。

#### `save`（Rust problem.rs:105-115）
- C++ 对应：`MSPProblem::Save`（MspProblem.cpp:312-324）。
- 判定：基本等价。
- 证据：每布局 (lkey → layout.save() + strips[lk]) ↔ `MSPLayoutSnapshot{makeLayoutKey(slotIndex, generation), slot.strip, slot.layout->Save()}`。**time_stamp**：Rust rs:109 `Instant::now()`；C++ MSPSolution 无（MSpp.h:120-129）——C++ 缺失。debug 断言 rs:112 ↔ C++ mutating 方法内 DebugAssertMSPSolution。

#### `restore`（Rust problem.rs:119-176）
- C++ 对应：`MSPProblem::Restore`（MspProblem.cpp:326-469）。
- 判定：基本等价（保留键策略同 BPP；**eef0830 后确认：C++ 已无条带配置一致性校验**——快照 strip 直接生效；仍保留纯量合法性校验）。
- `--restore-boundary-parity-only` 证实：strip.width 与 layout snapshot 宽度不一致时，Rust Release 接受并保留不一致状态，Debug 则在状态已更新后触发一致性断言；当前 C++ 两配置均由 `sameStripContainer` 原子拒绝。当前结论应限定为“合法且相互一致的快照行为对齐”，该接受边界尚未对齐。
- 逐步对照：
  1. 预验证（C++ 额外，cpp:341-364）：键合法（槽非 0、代数奇、无重复键/槽）、strip 纯量校验（`isfinite` 且 maxWidth/fixedHeight/width > 0，cpp:353-358）、`sameStripContainer`（快照容器矩形与 strip 宽高逐顶点 f32 匹配，cpp:67-88）、独立重建 Layout 并 Restore（cpp:365-368）、placement 不超需求且 sameItemDefinition（cpp:369-381）。
  2. **条带配置一致性**：当前代码 MspProblem.cpp 中 Restore 对 strip 仅做上述纯量校验，**没有**与 base_strip 或任何全局配置的比对，保留槽直接 `preserved.strip = layout.strip`（cpp:453）、新槽直接 `LayoutSlot{..., layout.strip, ...}`（cpp:460-461）——即快照 strip 值直接生效。这与 Rust 行为一致（Rust rs:132 `self.strips[lk] = solution.strips[lk]` 同样直接赋值，仅 layout.restore 失败才替换布局；Rust 的配置一致性仅在 debug 断言 `prob_strip == sol_strip`（mspp assertions.rs:19-25）里覆盖，release 不查）。判定：一致（eef0830 删除了旧版"条带配置一致性校验"使 C++ 与 Rust 对齐；当前两侧 Release 均接受任意合法纯量的快照 strip）。
  3. 保留键/排序/分配/提交（cpp:386-462）：与 BPP Restore（记录 29 差异 8/9）逐行同构（std::sort 按槽序注释、preservesKey、原地 `current->layout->Restore(layout.layout.Save())` 优先、失败替换、原子提交）↔ Rust rs:125-155（解中有的先原地 restore 失败则 from_snapshot；无的 remove；缺的新建插入）。判定：一致（含"keys changed"口径）。
  4. 需求重建：Rust rs:157-171（instance 值 - 所有布局放置数）↔ C++ cpp:337-339, 369-381（预验证时同步递减 remaining）。判定：一致。
  5. debug 断言：rs:174 ↔ C++ cpp:467 DebugAssertMSPSolution。判定：一致（debug-only）。

#### `change_strip_width`（Rust problem.rs:180-188）
- C++ 对应：`MSPProblem::ChangeStripWidth`（MspProblem.cpp:262-284）。
- 判定：基本等价。
- 证据：Rust `assert!(new_width > 0)` → `strip.set_width`（内部再断言 <= max_width）→ `swap_container(Container::from(*strip))` ↔ C++ `next.SetWidth(width)`（含 max 与正数校验，错误返回）→ `CreateContainer` → `SwapContainer` → 成功后 `slot->strip = next`（原子提交）。判定：一致（合法输入下；C++ 失败不动状态）。

#### `density` / `all_layouts`（Rust problem.rs:217-231）
- C++ 对应：`MSPProblem::Density`（MspProblem.cpp:525-538）。
- 判定：一致（分母 `sum(layout.container.area())` ↔ `slot.layout->GetContainer().Area()` 累加；分子 placed_item_area；f32 除法转 double。**问题层口径：container.area()**，与解层 fixed_height*width 区分，见 D 节）。
- `all_layouts()`（rs:229-231，返回 `impl Iterator<Item=&Layout>`）：C++ 无对应迭代器访问器（Layout 均私有于槽内，无 GetLayout 公开）。判定：C++ 缺失（调用方可经 PlacedItemCount/StripWidth/Density 等聚合 API 覆盖典型用途）。

#### `extendable_strips`（Rust problem.rs:234-239，width < max_width 的 lk 迭代器）
- C++ 对应：`MSPProblem::ExtendableStrips`（MspProblem.cpp:502-512，返回 `vector<size_t>` 键列表）。
- 判定：一致（判据 `width < max_width` 相同；槽序枚举对齐 SecondaryMap 迭代序）。

#### `total_strip_width`（Rust problem.rs:242-245）
- C++ 对应：`MSPProblem::TotalStripWidth`（MspProblem.cpp:514-523）。
- 判定：一致（f32 累加 `strip.width`，转 double）。

#### `n_placed_items`（Rust problem.rs:248-250）
- C++ 对应：`PlacedItemCount`（MspProblem.cpp:477-486）。判定：一致。C++ 额外：`LayoutCount`（cpp:488-494，对应 Rust `layouts.len()` 表达）、`StripWidth(layoutIndex)`（cpp:496-500，对应 `strips[lk].width` 单点查询）。

#### `MSPPlacement`（Rust problem.rs:254-262）
- C++ 对应：`MSpp.h:89-97`。判定：一致（lk + item_idx + d_transf ↔ layoutIndex + itemIndex + transform）。

### D. MSPSolution（solution.rs ↔ MspSolution.cpp）

#### `struct MSPSolution`（Rust solution.rs:10-17：layout_snapshots + strips 双 SecondaryMap + time_stamp）
- C++ 对应：`MSpp.h:120-129`（`vector<MSPLayoutSnapshot{layoutIndex, strip, layoutSnapshot}>`；无 time_stamp）。判定：基本等价（C++ 缺 time_stamp）。

#### `density`（Rust solution.rs:21-36）
```rust
let total_container_area = self.strips.values().map(|s| s.fixed_height * s.width).sum::<f32>();
let total_item_area = ...map(LayoutSnapshot::placed_item_area).sum::<f32>();
```
- C++ 对应：`MSPSolution::Density`（MspSolution.cpp:37-49）。
- 判定：一致。
- 证据：**分母用 `fixedHeight * width` 而非 container.area()**（cpp:44-45 `static_cast<float>(strip.fixedHeight) * static_cast<float>(strip.width)` ↔ rs:24-27 完全同式）；分子 placed_item_area；f32 除法转 double。**与 BPP 解层（container.area()）及 MSPP 问题层（container.area()）的口径区分在双侧均一致**（container.area() 为 deflate 前矩形毛面积 = width*height，数值上与 fixed_height*width 相等——因 Strip 生成的 Container 矩形未经 deflate 修改 modify 只作用于物品……实际上 Container::area() 取 originalOuter（矩形），故两口径数值恒等，仅表达式不同；两侧各自保持 Rust 的原始表达式，一致性无差异）。
- C++ 额外：先按槽序排序（cpp:15-34 layoutsInSlotOrder）再累加，对齐 Rust SecondaryMap 迭代序（f32 累加顺序一致）。

#### `total_strip_width`（Rust solution.rs:39-42）
- C++ 对应：`MSPSolution::TotalStripWidth`（MspSolution.cpp:51-57）。判定：一致（f32 累加，同样先排序）。

### E. MSPP 校验断言（mspp/util/assertions.rs ↔ Assertions.cpp ValidateMSPSolution）

MSPP 的断言量小（2 个函数），不值得单独成档（records/32 省略），并入本节。

#### `problem_matches_solution(mspp, sol)`（Rust mspp/assertions.rs:7-28）
```rust
assert!((mspp.density() - sol.density()).abs() <= f32::EPSILON);
assert_eq!(mspp.layouts.len(), layout_snapshots.len());
assert_eq!(mspp.strips.len(), strips.len());
assert!(mspp.layouts.iter().all(|(p_lk, l)| {
    let prob_strip = &mspp.strips[p_lk];
    layout_snapshots.iter().any(|(s_lk, ls)| {
        let sol_strip = &strips[s_lk];
        prob_strip == sol_strip && snapshot_matches_layout(l, ls)
    })
}));
```
- C++ 对应：`ValidateMSPSolution`（Assertions.cpp:321-376）。
- 判定：基本等价（结构与 SP/BP 版（记录 28）同构）。
- 逐项对照：
  1. 布局数量相等（rs:15）↔ cpp:325-326（`current.layouts.size() != solution.layouts.size()`）。判定：一致。
  2. strips 数量相等（rs:16）：C++ 无独立比较——C++ 的 MSPLayoutSnapshot 将 strip 内嵌于每布局（MSpp.h:109-117），布局数相等即 strip 数相等（SecondaryMap 双表同键）。判定：一致（被结构覆盖）。
  3. 每布局匹配（strip 相等 + 快照匹配，rs:19-25）↔ cpp:327-354：对每个当前布局在解中找 `near(maxWidth/fixedHeight/width) + sameCdeConfig + sameShapeModifyConfig + sameLayoutSnapshot` 的未匹配项。判定：一致（Rust `prob_strip == sol_strip` 为精确相等，C++ 数值容差 + 配置结构化比较——同记录 28 差异 2）。
  4. 每匹配项独立重建校验（cpp:350-353 validateStandaloneSnapshot）：C++ 额外。
  5. 密度一致（rs:14）：无独立比较，被结构匹配覆盖（同记录 28 差异 3）。C++ 缺失（被覆盖）。
  6. placements 与 instance 对账 + 需求精确对账（cpp:355-374，placementsMatchInstance + demandMatches）：C++ 额外（同记录 28 差异 5/6；注意 MSPP 无 bin/库存对账，与 BPP 版的差异点）。
  7. time_stamp：Rust rs:11 忽略；C++ 无字段。判定：一致（不参与）。

#### `instance_item_indices_correct(items)`（Rust mspp/assertions.rs:31-36）
- C++ 对应：内联于 `MSPInstance::Create`（MspInstance.cpp:10-36 copyInstanceItems "MSPP item indices must be dense" + 外部 ID 唯一）。判定：一致（断言层→构造层，双侧常驻）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | LayKey 表示：slotmap 不透明键 vs `generation<<32\|slotIndex` + LayoutSlot/代数/LIFO/槽 0 哨兵（与 BPP 同构） | 差异（表示法） | 行为等价；键不按位兼容 | rs(problem):11-14；MspProblem.cpp:16-40, 94-98, 109-164 |
| 2 | PlaceItem 需求耗尽：C++ Debug 预检返回 Infeasible，Rust Debug 在布局放置后下溢 panic；Release 双侧放置并回绕 | Debug 状态差异 | Rust panic 后 `remaining=0, placed=1`；C++ 错误前布局不变 | MspProblem.cpp:215-221 vs rs(problem):78-88；`--model-boundary-parity-only` |
| 3 | MSPSolution 无 time_stamp | C++ 缺失 | 同记录 27/29 | rs(solution):16；MSpp.h:120-129 |
| 4 | `all_layouts()` 迭代器未暴露 | C++ 缺失 | 典型用途由聚合 API（Density/PlacedItemCount/ExtendableStrips 等）覆盖 | rs(problem):229-231；MSpp.h 公开 API 无对应 |
| 5 | Restore 条带配置一致性校验已删除，快照 strip 直接生效（eef0830 确认） | 一致（已对齐） | 双侧 Release 均接受任意合法纯量 strip；Rust 仅 debug 断言覆盖 strip 相等 | MspProblem.cpp:442-461 vs rs(problem):119-155；mspp assertions.rs:19-25 |
| 6 | Restore 保留的纯量/结构校验（键合法、strip isfinite/>0、sameStripContainer、独立重建、物品定义、需求对账）+ 原子提交 | C++ 增强 | 非法快照被拒且状态不动；Rust 依赖内部约定 | MspProblem.cpp:326-469 |
| 7 | Restore 前 `std::sort(prepared)` 按槽序 | C++ 额外（对齐性） | 复现 SecondaryMap 槽序确定性 | MspProblem.cpp:386-392（含注释） |
| 8 | fit_strip 空布局 panic vs Infeasible Result | 差异（panic→Result） | 同记录 27 差异 2 | rs(problem):61-67；MspProblem.cpp:294-299 |
| 9 | fit_strip 前后 is_feasible debug_assert_eq | C++ 缺失 | 同记录 27 差异 3 | rs(problem):58, 73；MspProblem.cpp:286-310 无对应 |
| 10 | density 口径：问题层与解层均与 Rust 逐式一致（问题层 container.area()，解层 fixed_height*width；两口径数值恒等仅表达式不同） | 一致 | 无行为差异；表达式保真 | rs(problem):217-227 / rs(solution):21-36；MspProblem.cpp:525-538 / MspSolution.cpp:37-49 |
| 11 | MSPSolution::Density / TotalStripWidth 先按槽序排序再累加 | C++ 额外（对齐性） | f32 累加顺序与 Rust 迭代序一致 | MspSolution.cpp:15-34, 39-57 |
| 12 | C++ 额外 API：LayoutCount、StripWidth(lk)、AddLayout 前的 MultiStrip::Create 归一化 | C++ 额外 | 便捷/防御，无逻辑影响 | MspProblem.cpp:166-181, 488-500 |
| 13 | 校验断言（problem_matches_solution）：C++ 附加独立重建 + 物品对账 + 需求对账；密度比较被结构覆盖；strip 相等用容差 | 差异（同记录 28 模式） | debug 构建行为一致；C++ Validate 为公共 API | Assertions.cpp:321-376；mspp assertions.rs:7-28 |

## 结论
MSPP 与 BPP 的常规布局槽位/需求记账及 Release 超需求回绕一致；Debug 超需求放置的 panic 后状态不同。条带配置变化本身双侧可接受，但 strip.width 与布局快照容器不一致时，Rust Release 接受、Debug 在状态变更后断言失败，C++ Debug/Release 均预检拒绝。Restore 的纯量/键/需求校验、`all_layouts` 与 `time_stamp` 等仍需区分记录；不能将快照配置对齐误扩大为 Restore 输入边界已对齐。

## 补充审计（2026-09-29，逐行复核）

逐行清点 MSPP 四文件 + assertions 全部符号：Strip{new,set_width,From<Container>}；MSPInstance{new,item_area,item_qty,total_item_qty,item,item_idx}；MSPProblem{new,add_layout_from_strip,remove_layout,fit_strip,place_item,remove_item,save,restore,change_strip_width,register_layout,deregister_layout,register/deregister_included_item,density,all_layouts,extendable_strips,total_strip_width,n_placed_items}、LayKey、MSPPlacement；MSPSolution{density,total_strip_width}；assertions{problem_matches_solution,instance_item_indices_correct}。正文已全覆盖全部符号，无【遗漏】函数。逐行复核补充：

### 补-1【修正·重要】Restore 前置校验范围：正文结论段与差异 #5/#6 的部分表述过时
当前 MspProblem.cpp:302-469 的 Restore 前置校验仅剩：键合法（槽 0 哨兵/代数奇/去重，326-333）、独立重建（334-337）、placements 对账（338-350：itemIndex 越界/需求耗尽/sameItemDefinition）。**正文步骤 1 引用的 "strip isfinite/>0 纯量校验（cpp:353-358）" 与 "sameStripContainer（cpp:67-88）" 在当前代码中已不存在**（2026-09-29 更新注记已声明删除，但正文步骤 1、差异 #6 与结论段仍引用）。快照 strip 直接进 `PreparedLayout{..., snapshot.strip, ...}`（350），保留槽 `preserved.strip = layout.strip`（453 附近）、新槽 `LayoutSlot{..., layout.strip, ...}`。结论段"strip.width 与布局快照容器不一致时 C++ Debug/Release 均预检拒绝"**已过时**——当前 C++ 与 Rust 一样直接接受（strip 赋值生效，layout 失败走 from_snapshot 替换）。
当前仍存的 Rust↔C++ Restore 差异：Rust 无键合法/独立重建/placements 对账前置（信任 SecondaryMap 键与内部约定）。判定：C++ 额外（防御性缩小后的集合）。

### 补-2【验证】`add_layout_from_strip` 的注册顺序（细节确认）
Rust（problem.rs:44-49）：`Layout::new(Container::from(strip))` → `register_layout`（空布局无登记）→ `strips.insert(lk, strip)`。C++（MspProblem.cpp:166-181）：MultiStrip::Create 归一（168-173，C++ 额外重校验）→ CreateContainer → AllocateLayoutSlot（strip 入槽，178-179）。判定：一致（正文已记；确认 C++ 把 strips 映射内嵌于 LayoutSlot，无独立 SecondaryMap——键语义等价）。

### 补-3【验证】`change_strip_width` 的 assert 顺序（细节确认）
Rust（problem.rs:180-188）：外层 `assert!(new_width > 0)` → `strip.set_width`（内部 `assert!(width <= max_width)` + `assert!(width > 0)`）→ swap_container。C++（MspProblem.cpp:262-284）：`next.SetWidth(width)`（含两条校验，错误返回）→ CreateContainer → SwapContainer → 成功才提交 `slot->strip = next`。判定：一致（合法输入；C++ 失败原子性为额外，正文已记）。

### 补-4【验证】MSPP remove 不闭布局（关键语义，细节确认）
Rust（problem.rs:92-101）：remove_item 无 is_empty 分支——**空布局保留**。C++（MspProblem.cpp:238-260）：RemoveItem 同样无闭箱分支（仅 RemoveLayout 显式关）。与 BPP（remove_item 空则闭）形成对比，双侧一致地保留了该差异面。正文已记（67 行）。判定维持。

### 补-5【验证】`extendable_strips` 判据（细节确认）
Rust（problem.rs:234-239）：`s.width < s.max_width`。C++（MspProblem.cpp:502-512 附近）：同判据（`slot.strip.width < slot.strip.maxWidth`），槽序枚举。判定：一致（正文已记）。

### 补-6【验证】`problem_matches_solution` 的 strips.len 断言（细节确认）
Rust（mspp assertions.rs:16）：`assert_eq!(mspp.strips.len(), strips.len())`——C++ 因 strip 内嵌 LayoutSnapshot 无独立表，布局数相等即覆盖（正文 E 节第 2 项已记）。补充：Rust 的 SecondaryMap 双表（layout_snapshots + strips）理论上可出现键集不一致（双表独立插入），C++ 单表结构不可能——Rust 断言正是防此（debug-only）。判定维持正文。
