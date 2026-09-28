# 27. SPP 逐文件逻辑比对（Strip / SPInstance / SPProblem / SPSolution）
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/probs/spp/entities/{strip,instance,problem,solution}.rs`（55 / 62 / 146 / 24 行）
**C++**: `JaguarCore/Include/JaguarCore/Problems/Spp.h`（157 行）+ `JaguarCore/Source/Problems/SPP/{Strip,Instance,Problem,Solution}.cpp`（53 / 177 / 282 / 19 行）
**判定**: 基本等价（需求记账、快照/恢复、fit_strip 数值一致；C++ 新增多种输入校验；SPSolution 无 time_stamp；fit_strip 空布局 panic vs Result；PlaceItem 需求耗尽校验 debug-only 双侧确认）

## 函数逐条比对

### A. Strip（strip.rs ↔ Strip.cpp）

#### `struct Strip` + `new`（Rust strip.rs:10-32）
- C++ 对应：`Spp.h:15-36` + `Strip::Create`（Strip.cpp:5-26）。
- 判定：基本等价。
- 证据：字段 `fixed_height/cde_config/shape_modify_config/width` ↔ `fixedHeight/cdeConfig/shapeModifyConfig/width`（h:17-24）。校验 `fixed_height > 0`、`width > 0`（rs:24-25）↔ cpp:11-15 同两条合并（"strip height and width must be positive"）。
- C++ 额外（对齐性）：cpp:16-22 将 double 入参舍入到 f32 再转回 double（`rustFixedHeight/rustWidth`），并校验舍入后仍为正（"dimensions exceed f32 range"）。Rust 天然 f32。判定：一致（数值路径对齐）。

#### `set_width`（Rust strip.rs:34-37）
- C++ 对应：`Strip::SetWidth`（Strip.cpp:28-38）。
- 判定：一致（`width > 0` 断言 ↔ `InvalidInput` 错误；同样 f32 舍入校验）。

#### `From<Strip> for Container`（Rust strip.rs:40-54）
- C++ 对应：`Strip::CreateContainer`（Strip.cpp:40-51）。
- 判定：一致。
- 证据：矩形 `(0,0)-(width,fixed_height)`（Rust `Rect::try_new(0.0, 0.0, s.width, s.fixed_height)` rs:44 ↔ C++ 顶点表 `{0,0},{width,0},{width,fixedHeight},{0,fixedHeight}` cpp:42-43）；`pre_transform: DTransformation::empty()` ↔ `Transform2d::Identity()`（cpp:48）；`modify_mode: Deflate` ↔ `ShapeModifyMode::Deflate`（cpp:49）；无质量区、携带 cde_config（rs:49-50 ↔ cpp:50）。
- 差异：Rust `.unwrap()`（Container::new 失败即 panic）；C++ 返回 Result 透传。另 C++ 带 `ContainerId id = 0` 参数（默认 0），Rust 无 id 概念（记录 24 差异 6）。

### B. SPInstance（instance.rs ↔ Instance.cpp）

#### `struct SPInstance` + `new`（Rust instance.rs:9-26）
- C++ 对应：`Spp.h:39-82` + `SPInstance::Create`（两个重载，Instance.cpp:18-125）。
- 判定：基本等价（校验内容一致，形态不同）。
- 证据：
  - Rust `assert!(instance_item_indices_correct(&items))`（rs:19-22，item.idx == 位置）↔ C++ "SPP item indices must be dense"（cpp:38-42 与 cpp:105-109，两个重载都有）。
  - Rust `assert!(items.iter().map(...external_id).all_unique())`（rs:24）↔ C++ O(n²) 外部 ID 唯一性检查（cpp:43-50 与 cpp:110-118）。
  - 判定：一致（assert→Result 升级为双侧常态）。
- C++ 额外校验（Create(height,...) 重载，cpp:25-30, 66-70）：items 非空、items/demands 尺寸一致、fixedHeight 有限且 >0、**至少一个正需求且总面积为正**（"SPP instance must have positive demand"）。Rust `SPInstance::new` 无这些检查（demand 可全 0，由上游 io 层保证）。eef0830 后确认：**本文件已无 demand 溢出检查**（旧版本的 sum 溢出校验已删除，现为有限性检查 cpp:55-59 `isfinite`，仅防 NaN/Inf 累加，非溢出防护）。
- C++ 额外便利（cpp:72-78）：无显式 Strip 时自动以 `totalArea / fixedHeight` 为初始宽度构造 base strip。Rust 无此构造重载（Strip 必须显式传入）。

#### `item_area`（Rust instance.rs:29-35）
- C++ 对应：`SPInstance::TotalItemArea`（Instance.cpp:161-170）。
- 判定：一致（`item.shape_orig.area() * qty` f32 累加 ↔ `Area() * demands[i]` f32 累加；注意 C++ 用 Item.Area()（原始形状面积，Item.cpp:106-109），等价 Rust `shape_orig.area()`）。

#### `item_qty` / `total_item_qty` / `item` / `item_idx`（Rust instance.rs:38-60）
- C++ 对应：`ItemDemand`（cpp:148-151）、`TotalDemand`（cpp:153-159）、`GetItem`（cpp:132-135）、`ItemIndexFromExternalId`（cpp:137-146）。
- 判定：一致（含 `item_idx` 线性查找语义与"未知返回 None/nullopt"）。注：Rust 注释"returning None for unknown or zero-demand items"中 zero-demand 部分描述与实现不符（rs:55-60 未过滤 zero-demand，仍返回其 idx）——双侧实现均不过滤，行为一致，Rust 注释为文档瑕疵。

### C. SPProblem（problem.rs ↔ Problem.cpp）

#### `SPProblem::new`（Rust problem.rs:19-31）
- C++ 对应：`SPProblem::Create` + 私有构造（Problem.cpp:90-109）。
- 判定：一致（demand 快照 + strip 取 base_strip + `Layout::new(strip.into())` ↔ `strip.CreateContainer()` 建 Layout）。

#### `change_strip_width`（Rust problem.rs:34-37）
- C++ 对应：`SPProblem::ChangeStripWidth`（Problem.cpp:154-171）。
- 判定：基本等价。
- 证据：`strip.set_width` + `layout.swap_container(strip.into())`（rs:35-36）↔ C++ `nextStrip.SetWidth` → `CreateContainer` → `SwapContainer`，成功后才 `m_Strip = nextStrip`（cpp:157-168）。C++ 失败时条带与布局都不改（原子）；Rust set_width 是 assert、swap 无失败路径。判定：一致（合法输入下）。

#### `fit_strip`（Rust problem.rs:40-58）
```rust
let item_x_max = ...map(|pi| pi.shape.bbox.x_max).max()...unwrap() * 1.00001;
let fitted_width = item_x_max + self.strip.shape_modify_config.offset.unwrap_or(0.0);
self.change_strip_width(fitted_width);
debug_assert_eq!(feasible_before, self.layout.is_feasible());
```
- C++ 对应：`SPProblem::FitStrip`（Problem.cpp:173-192）。
- 判定：基本等价（数值公式一致；空布局与失败处理不同）。
- 逐步对照：
  1. 最右物品边界：C++ `m_Layout.RightmostCollisionX()`（cpp:175，Layout.cpp:461-473，取碰撞形状 bbox.xMax 最大值）↔ Rust 内联 max（rs:44-51）。数值一致。
  2. 空布局：Rust `.unwrap()`（rs:50）在无放置物时 **panic**；C++ 返回 `Result`（`Infeasible, "cannot fit an empty strip"`，cpp:176-180）。判定：差异（panic→Result）。
  3. 容差与 offset：C++ `itemXMax * 1.00001F + offset`（cpp:182-185，`offset.value_or(0.0)` ↔ rs:54 `unwrap_or(0.0)`）↔ Rust `* 1.00001 + offset`。判定：一致。注意 Rust 是 `(max * 1.00001) + offset`，C++ 同序。
  4. 合法性：C++ 额外校验 `isfinite && > 0`（cpp:186-190）。
  5. **fit_strip 前后 is_feasible 的 debug_assert_eq**：Rust rs:41, 57（`feasible_before` 缓存 + 事后比对）；C++ FitStrip **无**此断言（FitStrip 末尾不经 DebugAssertSPSolution 检验可行性不变，仅 ChangeStripWidth 内部 cpp:169 有 DebugAssertSPSolution——它校验的是解一致性而非 feasible 不变）。判定：C++ 缺失（Rust debug-only 校验在 C++ 无对应）。

#### `place_item`（Rust problem.rs:61-66）
- C++ 对应：`SPProblem::PlaceItem`（Problem.cpp:111-134）。
- 判定：基本等价（eef0830 后确认：需求耗尽校验为 debug-only）。
- 逐步对照：
  1. Rust `register_included_item`（rs:62，先扣需求）↔ C++ **先校验后放置再扣需求**（cpp:127-131 `PlaceItem` 成功后 `--m_RemainingDemand`）。顺序差异仅在失败路径上有意义（Rust 先扣后放、layout.place_item 无失败；C++ 放失败不扣）。判定：一致（结果等价）。
  2. 需求耗尽校验：C++ `#ifndef NDEBUG` 包裹（cpp:119-125，"SPP item demand is exhausted" → `Infeasible`）；**Rust 无任何耗尽校验**（既非 assert 也非 debug_assert，`item_demand_qtys[item_idx] -= 1` 直接下溢 panic 或回绕）。判定：差异（C++ debug-only 额外校验；Release 下双侧均不拦截——Rust usize 下溢 panic 于 debug、release 回绕；C++ release 时 `--` 到 0 下溢为 size_t 回绕。行为对齐于"不拦截"，但失败形态不同）。
  3. itemIndex 越界：C++ cpp:114-118 显式校验（InvalidInput）；Rust 靠 `self.instance.item(idx)` 索引 panic。判定：一致（防御形态）。
  4. debug 断言：Rust 无（problem 层 place 无 debug_assert；layout 层有，见记录 26）↔ C++ cpp:132 `DebugAssertSPSolution(*this)`（debug-only，Assertions.cpp:387-394）。判定：C++ 额外（debug-only 校验对应 Rust 的 save/restore 时机断言位置不同，见记录 28）。

#### `remove_item`（Rust problem.rs:69-77）
- C++ 对应：`SPProblem::RemoveItem`（Problem.cpp:136-152）。
- 判定：一致（移除 → 需求加回 → 返回 placement（item_idx + d_transf ↔ itemIndex + Transform）；C++ 额外 itemIndex 越界容错 cpp:143-147；debug 断言同上）。

#### `save`（Rust problem.rs:81-91）
- C++ 对应：`SPProblem::Save`（Problem.cpp:194-197）。
- 判定：基本等价。
- 证据：`SPSolution{layout_snapshot, strip, time_stamp: Instant::now()}`（rs:82-86）↔ C++ `SPSolution{m_Strip, m_Layout.Save()}`（cpp:196，**无 time_stamp**）。
- 差异：SPSolution 无 time_stamp（Rust rs:10-11 `time_stamp: Instant` 为公开字段，求解侧可比较时间；C++ 无对应物）。Rust `debug_assert!(problem_matches_solution)`（rs:88）↔ C++ Save 无断言（调用方自行 Validate）。
- 另注意：Rust `save` 先构造 solution 再断言（断言用 density/strip 相等）；C++ DebugAssertSPSolution 在各 mutating 方法内调用（cpp:132, 149, 169, 253），Save 本身不调用。校验覆盖时机不同但语义目标一致。

#### `restore`（Rust problem.rs:94-114）
- C++ 对应：`SPProblem::Restore`（Problem.cpp:199-255）。
- 判定：基本等价（重建逻辑对应；C++ 增加多重快照校验）。
- 逐步对照：
  1. 容器不一致回退：Rust `if self.layout.restore(&ls).is_err() { self.layout = Layout::from_snapshot(&ls); }`（rs:96-98）↔ C++ cpp:242-249（先在快照容器上重建验证 restored.Restore，再尝试原地 m_Layout.Restore，失败则整体替换）——C++ 多一步"预验证"（预构建的 restored 若 Restore 失败直接报错误而不回退，cpp:244-245），随后原地失败才替换。判定：一致（含回退路径）。
  2. strip 赋值：rs:99 ↔ cpp:251（校验后）。
  3. 需求重建：Rust `item_demand_qtys = instance 值 - 布局放置数`（rs:101-112）↔ C++ cpp:224-239 `remaining` 同公式，且在遍历 placements 时同步做 itemIndex 越界/需求耗尽/物品一致性（sameItemDefinition）校验。
  4. C++ 额外前置校验（cpp:201-222）：strip 数值合法（有限、宽>0、高与 base_strip 相等）+ cde/shapeModify 配置与 base_strip 一致（sameCdeConfig/sameShapeModifyConfig）+ `sameStripContainer`（快照容器矩形与 strip 宽高逐顶点匹配，cpp:52-69）。Rust 无（容器不一致由 layout.restore 的 base_cde 指针检查兜底；strip 配置一致性由 debug 断言 `assert_eq!(*strip, spp.strip)`（assertions.rs:14）在 debug 下覆盖，release 无）。判定：差异（C++ Release 也拦截配置不一致的解）。
  5. debug 断言：rs:113 ↔ C++ cpp:253 `DebugAssertSPSolution`。判定：一致（debug-only 双侧）。

#### 密度/宽度/数量（Rust problem.rs:125-137）
- C++ 对应：`Density`（cpp:267-270，委托 Layout::Density）、`StripWidth`（cpp:272-275）、`PlacedItemCount`（cpp:262-265）。判定：一致（Rust `density` 委托 layout.density（rs:125-127），`n_placed_items` = placed_items.len() ↔ PlacedItemCount）。注意：SPP 的 density 用 **layout.container.area()**（即当前 strip 的 Container 面积，deflate 前矩形面积 = width*height），见记录 26——与 MSPP/BPP 的口径区分一致，双侧同口径。

#### `SPPlacement`（Rust problem.rs:141-145）
- C++ 对应：`Spp.h:85-91` `struct SPPlacement`。判定：一致（item_idx + d_transf ↔ itemIndex + transform）。

### D. SPSolution（solution.rs ↔ Solution.cpp）

#### `struct SPSolution`（Rust solution.rs:6-12）
- C++ 对应：`Spp.h:94-105`。判定：基本等价（strip + layout_snapshot 对应；**time_stamp 无对应**，见差异表）。

#### `density`（Rust solution.rs:16-18）
- C++ 对应：`SPSolution::Density`（Solution.cpp:5-12）。
- 判定：一致（`layout_snapshot.density()` = placed_item_area / container.Area()，f32 除法转 double，与 Rust f32 一致；container 为解的容器而非固定 base）。

#### `strip_width`（Rust solution.rs:20-22）
- C++ 对应：`SPSolution::StripWidth`（Solution.cpp:14-17）。判定：一致。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | SPSolution 无 time_stamp 字段 | C++ 缺失 | Rust 公开 `time_stamp: Instant`（求解侧可比时间）；C++ 解无时间信息 | rs(solution):10-11；Spp.h:94-105 |
| 2 | fit_strip 空布局：Rust unwrap panic vs C++ Infeasible Result | 差异（panic→Result） | 空条带调用时 C++ 优雅报错；Rust panic | rs(problem):44-51；Problem.cpp:175-180 |
| 3 | fit_strip 前后 is_feasible debug_assert_eq（Rust rs:41,57） | C++ 缺失 | Rust debug-only 自检"缩条不破坏可行性"；C++ 无对应校验 | Problem.cpp:173-192 无相应断言 |
| 4 | PlaceItem 需求耗尽校验：C++ `#ifndef NDEBUG` 包裹（debug-only 错误），Rust 无校验 | 差异 | Release 下双侧均不拦截（eef0830 确认）；Debug 下 C++ 报 Infeasible、Rust 仍继续（usize 下溢 debug panic） | Problem.cpp:119-125 vs rs(problem):61-66 |
| 5 | SPInstance::Create(height) 重载的附加校验（items 非空、正需求、面积有限为正）与自动条宽 `totalArea/fixedHeight` | C++ 额外 | Rust 由 io 层保证；C++ 在库层拦截空实例 | Instance.cpp:25-30, 66-78 |
| 6 | Restore 的解合法性校验（strip 配置一致、容器-条带匹配、placement 不超需求、物品一致性）Release 生效 | C++ 额外 | Rust 仅 debug 断言（assertions.rs:14-16）覆盖，release 接受任意 strip | Problem.cpp:201-240 |
| 7 | From<Strip> for Container 的 unwrap vs Result | 差异（panic→Result） | 合法输入无差异 | rs(strip):52 ↔ Strip.cpp:40-51 |
| 8 | CreateContainer 带 ContainerId 参数（默认 0） | C++ 额外 | Rust 无容器 id 概念 | Strip.cpp:40；记录 24 差异 6 |
| 9 | C++ Instance/Problem 全 API 返回 Result（含 items/demands 尺寸、index 稠密、外部 id 唯一） | 差异（形态） | Rust 侧为 assert（debug panic）/上游保证；合法输入行为一致 | Instance.cpp:25-125, Problem.cpp:114-118 |
| 10 | Rust `item_idx` 注释声称过滤 zero-demand，实现不过滤（双侧实现一致均不过滤） | 差异（文档） | 无行为影响 | rs(instance):53-60 |

## 结论
SPP 四个实体的核心数值路径（矩形容器构建、需求记账、fit_strip 的 1.00001 容差公式、密度口径）双侧一致；eef0830 后需求耗尽校验确认为 C++ debug-only、Rust 无校验，Release 行为对齐。主要差异为防御层级：C++ 在 Instance/Create 与 Restore 上叠加了 Release 生效的输入校验，并把空布局 fit_strip、Container 构造等 panic 点转为 Result；SPSolution 缺 time_stamp、fit_strip 的可行性自检断言未移植。
