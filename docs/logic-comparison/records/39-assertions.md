# 39. util/assertions（调试断言）逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/util/assertions.rs`（279 行）
**C++**: `JaguarCore/Include/JaguarCore/assertions.h`（51 行）+ `JaguarCore/Source/Support/Assertions.cpp`（413 行）
**判定**: 基本等价（语义迁移：Rust 的 debug 布尔断言集 → C++ 的正式 `Result` 校验 API；`snapshot_matches_layout`/`layout_qt_matches_fresh_qt`/demand 一致性等核心检查语义保留，四项 quadtree 专项自检缺失（另见记录 21），`collision_hazards_sorted_correctly`/`quadrants_have_valid_layout`/`print_layout` 无对应；C++ 校验用 NumericPolicy 1e-9 而非 FPA）

> 范围说明：Rust `util/assertions.rs` 是 entities 层调试自检；quadtree 专项断言在 `collision_detection/quadtree/assertions.rs`（记录 21 已覆盖），probs 层断言在 `probs/*/util/assertions.rs`（记录 28-30 已覆盖）。C++ 把三处合并为单一 `Assertions.cpp`（`ValidateLayout*` 对应本记录 + quadtree 部分 + `Validate*Solution` 对应 28-30）。本记录只比对 `util/assertions.rs` 的对应部分。

## 逐条比对（函数）

### `snapshot_matches_layout(layout, snapshot) -> bool`（Rust 17-34 行）
- C++ 对应：`ValidateLayoutSnapshot`（Assertions.cpp:210-217）+ 私有 `sameLayoutSnapshot`（cpp:129-169）+ `validateStandaloneSnapshot`（cpp:176-185）。
- 判定：基本等价（更强）。
- 证据：Rust 两个检查：(1) `Arc::ptr_eq(&layout.container.base_cde, &snapshot.container.base_cde)`（容器 CDE 同源）；(2) 快照每个放置项在 layout 中存在（`item.idx` 与 `d_transf` 双匹配）。C++ `sameLayoutSnapshot`：(1) `first.container.SameBase(second.container)`（cpp:132，记录 24：base_cde 改按需创建后的同源判定）；(2) 逐放置项按 handle + `samePlacedItem`（itemIndex + `sameTransform` 1e-9 近似 + `samePolygon` 1e-9 逐顶点）匹配（cpp:140-150）——比 Rust 的 idx+精确相等更细（多边形比对、近似容差）。C++ `validateStandaloneSnapshot` 额外做"快照 Restore→Save 幂等"检查（cpp:176-185），Rust 无。差异：Rust 单向（快照⊆layout）；C++ 还比较数量（cpp:133 `placements.size()` 不等即 false，双向）。
- 容差体系：Rust `d_transf ==`（DTransformation 精确 f32 相等）；C++ `sameTransform` 用 `NumericPolicy` 1e-9（cpp:18,71-76）。见记录 38 差异 7。

### `collision_hazards_sorted_correctly(hazards) -> bool`（Rust 37-55 行）
- C++ 对应：无（`QTHazardVec` 内部结构不暴露，且记录 20 指出 presence 实现机制不同）。
- 判定：C++ 缺失（quadtree 专项，记录 21 差异 1 的组成部分）。
- 证据：检查 hazards 列表 `Entire` 不出现在 `Partial` 之后、`None` 不入列（Rust 37-53，含 `panic!("None hazard should never be collision hazard vec")`）。

### `qt_contains_no_dangling_hazards(cde) -> bool`（Rust 58-95 行）
- C++ 对应：无。
- 判定：C++ 缺失（记录 21 差异 2；C++ quadtree 为全量重建架构（记录 19），悬挂 hazard 模式不适用）。

### `layout_qt_matches_fresh_qt(layout) -> bool`（Rust 98-111 行）
- C++ 对应：无直接对应；`validateStandaloneSnapshot` 的 Restore→Save 幂等检查（cpp:176-185）部分承担"状态可重建"语义，但不重建 quadtree 对比。
- 判定：C++ 缺失（记录 21 差异 3）。
- 证据：Rust 从 base_cde 克隆重建 fresh CDE、逐项 register_hazard，然后 `qt_nodes_match` + `hazards_match`（Rust 98-111）。

### `qt_nodes_match` / `hazards_match`（Rust 113-231 行）
- C++ 对应：无（随 `layout_qt_matches_fresh_qt` 缺失；逐节点 hazard 集合对比、四种 children 组合的递归规约（含 partial-hazard 容忍分支，Rust 172-215）无移植）。
- 判定：C++ 缺失（记录 21）。

### `quadrants_have_valid_layout(quadrants) -> bool`（Rust 235-260 行）
- C++ 对应：无独立函数；`Quadtree` 分裂逻辑内部约定承担（记录 19）。
- 判定：C++ 缺失（记录 21 差异 4）。检查四象限与 `QUADRANT_NEIGHBOR_LAYOUT` 邻居各共享两点（Rust 235-259，`assert_eq!(2, ...)`）。

### `print_layout(layout)`（Rust 263-279 行，调试代码打印器）
- C++ 对应：无。
- 判定：C++ 缺失（纯调试工具，生成重建 layout 的 Rust 代码）。

### C++ 侧对应入口
- `ValidateLayout(layout)`（cpp:219-223）：`Save()` 后 `ValidateLayoutSnapshot`——即 Rust `snapshot_matches_layout` 的"当前快照对自身"退化调用 + 独立快照校验。
- `DebugAssertLayout/DebugAssertSPSolution/DebugAssertBPSolution/DebugAssertMSPSolution`（cpp:378-412）：`#ifndef NDEBUG` + `assert(Validate*(...).HasValue())`——显式复刻 Rust `debug_assert!` 的 debug-only 语义（与多数模块"debug 断言升为正式错误"的方向相反，此处保留原语义）。
- `ValidateSPSolution`/`ValidateBPSolution`/`ValidateMSPSolution`（cpp:225-376）：对应 `probs/*/util/assertions.rs`，记录 28-30 已覆盖（demand/stock 一致性 `demandMatches` cpp:187-207、bin 库存 cpp:303-316 等）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `snapshot_matches_layout`（快照↔布局一致） | 基本等价（C++ 更强） | C++ 双向 + 数量 + 多边形逐顶点 1e-9 比对 + Restore 幂等；Rust 单向 idx+精确相等 | assertions.rs:17-34 vs Assertions.cpp:129-185,210-217 |
| 2 | 比对容差：精确相等 vs NumericPolicy 1e-9 | 差异 | `d_transf ==` ↔ `sameTransform`/`samePolygon` 1e-9 近似；C++ 校验层自有容差体系（记录 38） | assertions.rs:28 vs Assertions.cpp:18,58-83 |
| 3 | `collision_hazards_sorted_correctly` | C++ 缺失 | hazards 排序不变量无运行时自检 | assertions.rs:37-55 vs 无 |
| 4 | `qt_contains_no_dangling_hazards` | C++ 缺失 | 全量重建架构下不适用（记录 19/21） | assertions.rs:58-95 vs 无 |
| 5 | `layout_qt_matches_fresh_qt` + `qt_nodes_match` + `hazards_match` | C++ 缺失 | "重建 quadtree 等价"自检整体缺失；Restore 幂等检查部分承担 | assertions.rs:98-231 vs Assertions.cpp:176-185 |
| 6 | `quadrants_have_valid_layout` | C++ 缺失 | 象限邻接不变量无自检 | assertions.rs:235-260 vs 无 |
| 7 | `print_layout` | C++ 缺失 | 调试代码打印器 | assertions.rs:263-279 vs 无 |
| 8 | debug-only 语义保留（`DebugAssert*`） | 一致 | `#ifndef NDEBUG + assert` 显式复刻 `debug_assert!`；与本系列多数模块"升为正式错误"方向相反 | assertions.rs:14（用途注释）vs Assertions.cpp:378-412 |
| 9 | 返回形态 `bool` vs `Result<void>` | 基本等价 | 布尔断言 → 错误码（`SnapshotMismatch`）+ 消息；C++ 可作为正式 API 调用（导出前校验，记录 37 差异 10） | assertions.rs:17 vs assertions.h:20-50 |

## 结论

Rust 的 entities 层调试自检中，快照↔布局一致性检查被 C++ 升格为正式校验 API 且判定更强（双向、逐顶点、带 Restore 幂等），debug-only 语义另由 `DebugAssert*` 家族显式保留；但四项 quadtree 专项自检（排序不变量、悬挂 hazard、重建等价、象限布局）与 `print_layout` 未移植，其中前两项因 C++ quadtree 采用全量重建架构而部分不适用。C++ 校验采用 NumericPolicy 1e-9 绝对容差而非 Rust 的精确/FPA 比较，属体系性差异（记录 38）。

### 补充审计（2026-09-29）

对 assertions.rs 279 行逐行复核（8 个公开/私有函数全覆盖：`snapshot_matches_layout` 17-34、`collision_hazards_sorted_correctly` 37-55、`qt_contains_no_dangling_hazards` 58-67、`qt_node_contains_no_dangling_hazards` 69-95、`layout_qt_matches_fresh_qt` 98-111、`qt_nodes_match` 113-216、`hazards_match` 218-231、`quadrants_have_valid_layout` 235-260、`print_layout` 263-279）。原记录逐函数条目齐全，无整函数遗漏；补充遗漏/需修正各点：

1. **`snapshot_matches_layout` 的 `Arc::ptr_eq` 指针等价语义（浅覆盖→补齐 C++ 对应细节）**——Rust 检查 `layout.container.base_cde` 与 `snapshot.container.base_cde` **Arc 指针相同**（同一分配，含引用计数对象），assertions.rs:18-22；C++ `SameBase`（Container.cpp:132-135）实现为 `m_Data == other.m_Data`——**同为指针同一性判定**（`shared_ptr` 的 `==` 即指针比较）。判定修正：**一致**（双侧均为同一性而非结构等价性；语义维度相同）。原记录 14 行引用的 `SameBase` 已在记录 24 覆盖，此处补充实现行号证据（Container.cpp:132-135）。
2. **`qt_nodes_match` 的 `hashable` 闭包 presence 映射（遗漏的常量细节）**——Rust 把 `QTHazPresence` 折叠为 `None→0, Partial→1, Entire→2`（assertions.rs:114-121），**丢弃 Partial 的具体数值载荷**（Partial(f32) 的容差参数不参与比对）——即两个 Partial 容差不同的 hazard 视为相同。C++ 无对应函数（整体缺失，原记录已判）。补充：若未来移植需注意该"载荷丢弃"是有意设计（比对只关心存在性+档位）。
3. **`qt_nodes_match` 的 `(None, None) => panic!("Both nodes are none")`（遗漏）**——Rust 递归守卫 panic（assertions.rs:168）；`println!("{}", from_1 == from_2)`（assertions.rs:149）为**调试残留输出**（打印布尔值，无格式化说明，明显是开发期残留）。两者均无 C++ 对应（函数整体缺失）。判定：随函数缺失，无独立影响；`println!` 残留属 Rust 代码质量问题。
4. **`hazards_match` 的 `println!("Hazard vecs don't match")`（遗漏）**——与上条同类的调试残留（assertions.rs:227）。C++ 无对应。
5. **`qt_contains_no_dangling_hazards` 的 `println!("Node contains dangling hazard")`（遗漏）**——同上（assertions.rs:82）。原记录未提这三个 `println!`（与 `error!` 日志并存，`error!` 有提）。
6. **`quadrants_have_valid_layout` 的 `assert_eq!` 而非返回 false（浅覆盖→补齐）**——Rust 该函数返回 bool 但内部用 `assert_eq!(2, ...)` panic 而非返回 false（assertions.rs:244-257）——**panic 优先于返回值**，调用方拿到 false 的路径实际不存在（除了 4 象限全对时返回 true）。C++ 无对应（原记录已判缺失）。补充：该函数是"半断言"形态（标题说返回检查结果，实际 assert），移植时若按签名照抄会漏 panic 语义。
7. **`print_layout` 的输出格式细节（原记录判"C++ 缺失"，补齐格式说明）**——生成 `let mut layout = Layout::new(container.clone());` + 每项 `layout.place_item(instance.item({idx}), &DTransformation::new({rot:.6},({tx:.6},{ty:.6})));`（assertions.rs:264-278）——**生成的代码是 jagua-rs Rust 代码**，对 C++ 移植版无意义（C++ 调试时无法直接执行 Rust 代码）。判定维持 C++ 缺失，补充"即便移植也需改写为目标语言代码生成器"的备注。
8. **C++ `sameLayoutSnapshot` 比对项超出 Rust（补齐证据）**——C++ 额外比对 `slotGenerations`/`freeSlotIndices`/`cdeSnapshot.dynamicHazards`（cpp:134-137,152-167）——slotmap 槽位元数据与动态 hazard 集合的多重匹配（`std::vector<bool> matched` 防重复配对）。Rust `snapshot_matches_layout` 只查 container ptr + 放置项子集。原记录 14 行说"比 Rust 更细"，此处补齐具体比对项清单与 `sameHazard`（`dynamic && entity && samePolygon`，cpp:123-127）的行号证据。
9. **C++ `demandMatches` 的 `used > required` 检查（补齐）**——`used[i] > required[i] || required[i] - used[i] != remaining[i]`（cpp:203）：超量放置（used 超需求）单独拒绝。该函数属 probs 断言（记录 28-30 范畴），本记录交叉引用补行号。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | `Arc::ptr_eq` 指针同一性 vs `SameBase` | 一致（补齐证据） | 双侧均为指针同一性判定（`shared_ptr::==` 即指针比较） | assertions.rs:18-22 vs Container.cpp:132-135 |
| S2 | `qt_nodes_match` presence 载荷丢弃（Partial(f32)→1） | 随函数缺失（设计备注） | 比对只关心档位不关心容差值 | assertions.rs:114-121 |
| S3 | 三处 `println!` 调试残留 | 遗漏条目 | Rust 代码质量问题，无 C++ 对应（函数缺失） | assertions.rs:82,149,227 |
| S4 | `quadrants_have_valid_layout` 半断言形态（assert_eq 优先于返回值） | 遗漏细节 | 直接照签名移植会漏 panic 语义 | assertions.rs:244-257 |
| S5 | `sameLayoutSnapshot` 比对项清单（slotGenerations/freeSlots/dynamicHazards 多重匹配） | C++ 更强（补齐证据） | 快照元数据级比对 | Assertions.cpp:123-167 |
| S6 | `print_layout` 生成 Rust 代码 | C++ 缺失（备注：移植需换目标语言） | 纯调试工具 | assertions.rs:263-279 |
