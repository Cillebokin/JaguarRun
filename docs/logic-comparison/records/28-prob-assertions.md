# 28. probs 层校验断言 逐文件逻辑比对（SP / BP）
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/probs/spp/util/assertions.rs`（27 行）+ `CodeResource/jagua-rs/jagua-rs/src/probs/bpp/util/assertions.rs`（33 行）
**C++**: `JaguarCore/Source/Support/Assertions.cpp` 的 SP/BP 校验部分（`ValidateSPSolution` 225-253 行、`ValidateBPSolution` 255-319 行、公共辅助 16-207 行、`DebugAssert*` 378-412 行；头文件 `Include/JaguarCore/assertions.h`）
**判定**: 基本等价（校验目标一致；C++ 将 assert 型断言升级为返回 Result 的公共 Validate API + DebugAssert 包装，debug-only 语义保留；C++ 附加独立快照重建校验与需求/库存精确对账）

## 总体形态差异（适用于 SP/BP 两节）

Rust `problem_matches_solution` 系列：以 `assert!`/`assert_eq!` 实现，仅在 `debug_assert!` 调用点（problem.rs save/restore 内）生效，失败即 panic，无返回值语义（返回 `true` 供 `debug_assert!` 使用）。

C++ `Validate*Solution` 系列：返回 `Result<void>`，结构比对失败返回 `SnapshotMismatch` 错误；另提供 `DebugAssert*` 包装（Assertions.cpp:378-412）：
```cpp
JAGUARCORE_API void DebugAssertSPSolution(const SPProblem& problem)
{
#ifndef NDEBUG
    assert(ValidateSPSolution(problem, problem.Save()).HasValue());
#else
    (void)problem;
#endif
}
```
即：**DebugAssert 与 Rust debug_assert! 同为 debug-only；但 Validate* 本身是公共 API，Release 下可被外部显式调用**。Rust 无对应公共校验入口（assertions 函数虽 pub 但断言内嵌 panic 语义）。

## 第一节：SP 校验（spp/util/assertions.rs ↔ ValidateSPSolution）

### `problem_matches_solution(spp, sol)`（Rust spp/assertions.rs:7-19）
```rust
assert_eq!(*strip, spp.strip);
assert!((spp.density() - sol.density()).abs() <= f32::EPSILON);
assert!(snapshot_matches_layout(&spp.layout, layout_snapshot));
```
- C++ 对应：`ValidateSPSolution`（Assertions.cpp:225-253）。
- 判定：基本等价（各项校验均有对应，实现方式不同）。

逐项对照：

**1. strip 相等**（Rust rs:14 `assert_eq!(*strip, spp.strip)`，逐字段严格相等——Strip 为 `PartialEq` 且 f32 精确比较）
- C++：cpp:229-233 `near(fixedHeight) && near(width) && sameCdeConfig && sameShapeModifyConfig`。
- 判定：差异（比较方式）。
- 说明：Rust 是逐字段**精确**相等（含 cde_config/shape_modify_config 的派生 PartialEq）；C++ 数值字段用容差 `near`（`NumericPolicy::DefaultEqualityTolerance()`，cpp:18-23），配置字段用结构化比较（sameCdeConfig cpp:25-31：quadtreeDepth + cdThreshold + itemSurrogateConfig；sameShapeModifyConfig cpp:41-56：simplifyTolerance/offset 容差 + narrowConcavityCutoff pair 容差）。容差比较更宽松，但校验的调用场景（刚 Save 出来的解 vs 当前状态）数值恒等，两侧都通过。

**2. 密度一致**（Rust rs:15 `(spp.density() - sol.density()).abs() <= f32::EPSILON`）
- C++：**无独立密度比较**。密度一致性被结构性比较覆盖（strip 相等 + layoutSnapshot 相同 ⇒ 密度必然一致）。
- 判定：C++ 缺失（被更强校验覆盖）。
- 说明：Rust 用 f32::EPSILON（约 1.19e-7）作为绝对容差；C++ 靠 sameLayoutSnapshot（容器/placements/引擎快照逐一匹配）间接保证。数学上 C++ 路径更严格（快照相同 → 密度完全相同而非容差内）。

**3. 快照与布局匹配**（Rust rs:16 `snapshot_matches_layout(&spp.layout, layout_snapshot)`——util/assertions.rs:17-34：base_cde 指针相等 + 快照每个放置项在布局中存在同 (item.idx, d_transf) 项）
- C++：cpp:234 `sameLayoutSnapshot(current.layoutSnapshot, solution.layoutSnapshot)`（cpp:129-169：容器 SameBase + placements 句柄与内容双向匹配 + slotGenerations/freeSlotIndices 相等 + 引擎动态 hazard 集合匹配）。
- 判定：差异（更强）。
- 说明：Rust 比较方向是"快照 ⊆ 布局"（单向包含，且布局侧多放的项不查）；C++ 比较当前 Save 与传入解的两个快照（双向、含槽位元数据与 hazard 集合）。C++ 输入是 `problem.Save()` 与待验解，语义为"解与当前状态完全一致"，覆盖并强于 Rust 的单向包含。

**4. C++ 额外校验 A：placements 与 instance 对账**（cpp:235-236 `placementsMatchInstance`，cpp:108-121：每个放置项 itemIndex 在范围内且与实例物品定义一致——sameItemDefinition cpp:85-106 比对 Index/ExternalId/碰撞形状逐顶点）
- 判定：C++ 额外。Rust 无（信任 Layout 中 item 与 instance 同源）。

**5. C++ 额外校验 B：需求精确对账**（cpp:237-250 `demandMatches(required, remaining, placements)`，cpp:187-207：`used[i] <= required[i] && required[i] - used[i] == remaining[i]`）
- 判定：C++ 额外。Rust 无对应显式校验（需求正确性由 place/remove/restore 的记账逻辑保证，debug 断言不覆盖）。

**6. C++ 额外校验 C：独立快照可重建性**（cpp:252 `validateStandaloneSnapshot(solution.layoutSnapshot)`，cpp:176-185：用快照容器新建 Layout、执行 Restore、再 Save，要求 `sameLayoutSnapshot(原快照, 重建后再 Save 的快照)`）
- 判定：C++ 额外。
- 说明：这是 Rust `layout_qt_matches_fresh_qt`（util/assertions.rs:98-111：克隆 base_cde 重建四叉树比对）在问题层的对应精神——Rust 在 Layout 层做"引擎重建比对"，C++ 在解层面做"布局重建往返比对"。手段不同（四叉树逐节点 vs 快照往返），目标一致（状态可由静态基重建）。

**7. time_stamp**：Rust rs:11 解构中显式忽略 `time_stamp: _`；C++ SPSolution 无该字段（记录 27 差异 1）。判定：一致（均不参与校验）。

### `instance_item_indices_correct(items)`（Rust spp/assertions.rs:22-27）
- C++ 对应：无独立函数；等价校验内联于 `SPInstance::Create`（Instance.cpp:38-42、105-109 "SPP item indices must be dense"）。
- 判定：一致（校验位置从断言层移至构造层；SPInstance::new 的 assert 调用点 rs(instance):19-22 ↔ C++ Create 内联检查，双侧 Release 均生效——Rust 侧是 `assert!` 非 `debug_assert!`，本来就常驻；C++ 一致常驻）。

## 第二节：BP 校验（bpp/util/assertions.rs ↔ ValidateBPSolution）

### `problem_matches_solution(bpp, sol)`（Rust bpp/assertions.rs:7-27）
```rust
assert!((bpp.density() - sol.density()).abs() <= f32::EPSILON);
assert_eq!(bpp.layouts.len(), layout_snapshots.len());
assert_eq!(bpp.bin_cost(), sol.cost(&bpp.instance));
assert!(bpp.layouts.iter().all(|(key, layout)| {
    layout_snapshots.iter().any(|(saved_key, snapshot)| {
        bpp.layout_bins[key] == layout_bins[saved_key]
            && snapshot_matches_layout(layout, snapshot)
    })
}));
```
- C++ 对应：`ValidateBPSolution`（Assertions.cpp:255-319）。
- 判定：基本等价（四项断言各有对应或被覆盖）。

逐项对照：

**1. 布局数量相等**（Rust rs:15）
- C++：cpp:259-260 `current.layouts.size() != solution.layouts.size()` → 错误。判定：一致。

**2. 每个问题布局在解中有匹配快照（同 bin + 快照匹配）**（Rust rs:19-24，`layout_bins[key] == layout_bins[saved_key]` + `snapshot_matches_layout`）
- C++：cpp:261-282 对 `current.layouts` 逐个在 `solution.layouts` 中找 `binIndex` 相同且 `sameLayoutSnapshot` 的未匹配项；找不到报错。
- 判定：一致（匹配判据等价：bin 一致 + 快照一致；C++ 用二分匹配防重复配对，Rust 的 `any` 不防同一快照被两个布局匹配——理论上 Rust 断言更宽松，但配合数量相等断言后等价）。
- 附带：C++ 对每个匹配的解快照再跑 `validateStandaloneSnapshot`（cpp:278-281，同 SP 第 6 项）。C++ 额外。

**3. bin 成本一致**（Rust rs:18 `bpp.bin_cost() == sol.cost(&bpp.instance)`——`bin_used_qtys` 加权求和 vs 解布局的 cost 求和）
- C++：无独立成本比较。
- 判定：C++ 缺失（被更强校验覆盖）。
- 说明：C++ cpp:303-316 做**库存精确对账**：`usedBins[i] <= stock[i] && stock[i] - usedBins[i] == RemainingBinStock(i)`。布局数匹配 + 每布局 binIndex 匹配 + 库存对账 ⇒ 成本必然一致（成本是布局→bin 的确定性函数）。Rust 的等价强度：布局数相等 + 每布局有匹配 bin + （需求对账无）→ 成本一致。两侧均能推出成本一致，但 C++ 的对账显式且更强（还覆盖库存簿记本身的正确性）。

**4. 密度一致**（Rust rs:14）
- C++：无独立比较（同 SP 第 2 项，被快照结构匹配覆盖）。判定：C++ 缺失（被覆盖）。

**5. C++ 额外：placements 与 instance 对账 + 需求精确对账**（cpp:283-301：聚合所有解布局的 placements 后 `placementsMatchInstance` + `demandMatches(required, remaining, placements)`）
- 判定：C++ 额外（同 SP 第 4/5 项）。

**6. time_stamp**：Rust rs:11 忽略；C++ BPSolution 无字段。判定：一致（不参与）。

### `instance_item_bin_ids_correct(items, bins)`（Rust bpp/assertions.rs:30-33，item.idx 与 bin.idx 均须等于位置）
- C++ 对应：无独立函数；等价校验内联于 `BPInstance::Create`（BppInstance.cpp:30-46："BPP item indices must be dense" + 外部 ID 唯一；cpp:48-55 "BPP bin indices must be dense"）。
- 判定：一致（断言层 → 构造层，双侧常驻生效）。

## 双侧共有基础设施对照（仅列与 SP/BP 相关项）

| Rust（util/assertions.rs） | C++（Assertions.cpp） | 判定 |
|---|---|---|
| `snapshot_matches_layout`（17-34，base_cde 指针 + 单向放置项包含） | `sameLayoutSnapshot`（129-169，双向全结构） | 差异（C++ 更强，见 SP 第 3 项） |
| `layout_qt_matches_fresh_qt`（98-111，克隆 base_cde 重建四叉树比对） | `validateStandaloneSnapshot`（176-185，快照重建往返比对） | 差异（精神对应、手段不同） |
| `collision_hazards_sorted_correctly` / `qt_contains_no_dangling_hazards` / `quadrants_have_valid_layout` / `print_layout`（37-95, 233-279） | 无对应 | C++ 缺失（collision_detection 层内部 debug 工具，见记录 26 差异 13；不属 probs 层职责） |

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 断言形态：Rust `assert!` panic 型（经 debug_assert! 调用，debug-only）vs C++ `Validate*` 返回 Result 的公共 API + `DebugAssert*`（#ifndef NDEBUG）debug-only 包装 | 差异（形态） | debug 构建行为一致（失败即中止）；C++ Validate 在 Release 可显式调用 | spp rs:7-19；Assertions.cpp:225-253, 387-394 |
| 2 | strip 相等比较：Rust 逐字段精确 vs C++ near 容差 + sameCdeConfig/sameShapeModifyConfig | 差异（比较方式） | Save-后立即校验场景下双侧均通过；容差更宽松 | spp rs:14；Assertions.cpp:229-233 |
| 3 | 密度一致断言（f32::EPSILON）无 C++ 直接对应 | C++ 缺失（被覆盖） | 由快照全结构匹配隐式保证 | spp rs:15；Assertions.cpp 无对应行 |
| 4 | BP bin 成本一致断言无 C++ 直接对应 | C++ 缺失（被覆盖） | 由布局匹配 + 库存对账隐式保证 | bpp rs:18；Assertions.cpp:303-316 |
| 5 | C++ 附加：placements 与 instance 物品定义对账（sameItemDefinition） | C++ 额外 | 拦截"解含实例外物品"类错误 | Assertions.cpp:108-121, 235-236, 291 |
| 6 | C++ 附加：需求/库存精确对账（demandMatches / usedBins 对账） | C++ 额外 | 拦截簿记错误（Rust debug 断言不覆盖需求记账） | Assertions.cpp:187-207, 237-250, 293-316 |
| 7 | C++ 附加：独立快照可重建性校验（validateStandaloneSnapshot 往返） | C++ 额外 | 对应 Rust layout_qt_matches_fresh_qt 的精神（重建一致性） | Assertions.cpp:176-185, 252, 278-281 |
| 8 | snapshot_matches_layout 单向包含 vs sameLayoutSnapshot 双向全结构（含槽位元数据与 hazard 集合） | 差异（C++ 更强） | C++ 能发现"布局比快照多物"的偏差（Rust 单向不查） | util rs:17-34；Assertions.cpp:129-169 |
| 9 | instance_item_indices_correct / instance_item_bin_ids_correct 断言移至构造期（Create 内联常驻校验） | 差异（位置） | 双侧 Release 均生效（Rust 本为 assert! 非 debug_assert!）；等价 | spp rs:22-27 / bpp rs:30-33；SPP/Instance.cpp:38-50、BppInstance.cpp:30-55 |
| 10 | Rust collision 层 debug 工具（qt 排序/悬挂 hazard/象限布局/print_layout）无 C++ 对应 | C++ 缺失 | 非 probs 层职责；C++ 以结构校验替代四叉树比对 | util rs:37-95, 233-279；Assertions.cpp 全文无对应 |

## 结论
SP/BP 校验的目标完全对齐：解与当前问题状态一致（strip/布局/bin/数量），且双侧都以 debug-only 方式接入问题层（C++ DebugAssert* 的 #ifndef NDEBUG 与 Rust debug_assert! 语义对齐）。实现上 C++ 走得更远：把 panic 断言重构为返回 Result 的公共 Validate API，用容差+结构化比较替代逐字段相等，并附加了 Rust 没有的三类校验（物品定义对账、需求/库存精确对账、快照独立可重建性），而 Rust 的密度/成本相等断言在 C++ 中被更强的结构匹配隐式覆盖。
