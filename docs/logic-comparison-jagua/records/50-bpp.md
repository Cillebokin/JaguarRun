# probs/bpp：装箱实体、问题状态与 IO

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 对照**：`JaguarCore/Include/JaguarCore/Problems/Bpp.h`、`Source/Problems/BPP/*`、`Source/IO/Importer.cpp`、`Source/IO/JsonCodec.cpp`  
**关联工作项**：B7、B8、B13、B14  
**证据类型**：pinned Rust 与当前 C++ 源码静态核对；未运行 BPP 测试或对拍

## `jagua-rs/src/probs/bpp/entities/bin.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `Bin`/`new` (5–36) | 普通值类型，保存密集实例索引、外部 bin id、Container、stock、cost；`new` 直接赋值，没有校验/错误分支 | `Bpp.h::Bin` 保存对应字段，名称为 `index/externalId`；构造由聚合初始化/`BPInstance::Create` 承担，C++ 在 instance 层检查 bin index 连续，Rust 在 `BPInstance::new` 的 assertion helper 中检查 |

## `jagua-rs/src/probs/bpp/entities/instance.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `BPInstance`/`new` (9–23) | 保存 `Vec<(Arc<Item>, demand)>` 与 bins；先 `assert!(instance_item_bin_ids_correct)`（helper 要求 item/bin idx 均等于 vector 位置），再 `assert!` item external_id 唯一。任一失败直接 panic；不检查空实例、stock 或 demand 的可行性 | `BPInstance::Create`（`BppInstance.cpp:17–59`）检查 item/demand 长度、dense item/bin indices 和 item external ID 唯一，以 `Result<BPInstance>` 返回 InvalidInput；另外将 Item 值复制进 shared_ptr |
| `item_area` (25–32) | `sum(original area * quantity as f32)`；面积与累计均为 f32 | `BPInstance::TotalItemArea`（`BppInstance.cpp:105–114`）也以 float 累加，但公开返回 double；转换/溢出边界归 B7 |
| `item_qty/item/bins/bin_qty` (34–46) | 直接按索引访问 Vec；越界走 Rust 索引 panic；`bins()` 按原顺序返回迭代器 | `ItemDemand/GetItem/GetBin` 直接访问 C++ vector，声明 noexcept 且无显式范围错误；`RemainingDemand/RemainingBinStock` 是 Problem 的安全查询并越界返回 0，二者勿混淆 |
| `total_item_qty/item_idx` (48–66) | total demand 对 quantity 求和；`item_idx(external_id)` 线性查 external ID 并返回该 Item 自带的 dense `idx`，未找到返回 None | `TotalDemand/ItemIndexFromExternalId` 对应；C++ 同样用 Item::Index 返回映射 |

## `jagua-rs/src/probs/bpp/entities/problem.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `BPProblem/new` (18–40) | 公共字段含 instance、`SlotMap<LayKey,Layout>`、layout→source-bin SecondaryMap、剩余 demand/stock 副本；无构造错误出口 | `BPProblem::Create`（`BppProblem.cpp:77–87`）以 Result 返回并用向量槽位/代数实现 Layout key；C++ 状态私有化，Rust 调用方可直接改写公开状态 |
| `remove_layout` (42–45) | 委托 `deregister_layout`；不存在 LayKey 或其 bin 关联缺失会在 helper 的 `expect` panic | `BPProblem::RemoveLayout`（`BppProblem.cpp:260–290`）校验 layout/bin、返还内部 item demand 和 bin stock，错误以 Result 返回 |
| `place_item` (47–66) | `BPLayoutType::Open(lkey)` 使用已有 layout；`Closed{bin_id}` 读取 bin、创建 Layout 并 register；随后按 item_idx 取 Item、Layout.place_item、递减 demand，返回 `(LayKey,PItemKey)`。无效 key/bin/item 会索引或 SlotMap panic；库存与 demand 无前置可行性校验，usize 减法在 Debug 下可能 panic、Release 下可能回绕 | `BPProblem::PlaceItem`（`BppProblem.cpp:166–221`）返回 Result，先检查 item index；库存检查在 `OpenLayout`；开放布局句柄过期会返回 InvalidInput。C++ 仅在 Debug 检查 demand 已耗尽；目标枚举命名与 Rust 相反：Rust `Open(key)`=已有、`Closed{bin}`=新建，C++ `LayoutTargetKind::Open` 注释为新布局、`Closed` 为已有布局，实际需按字段组合读取 |
| `remove_item` (68–80) | 移除 item、恢复 demand；若 Layout 空，查 source bin 并 `deregister_layout`，返回 `Closed{bin_id}`，否则返回 `Open(lkey)`。无效 layout/key 与缺 bin mapping 会 panic | `RemoveItem`（`BppProblem.cpp:223–258`）以组合 handle 定位、返回 Result；删除最后一件时释放布局槽和 stock，并返回 Closed 目标 |
| `save` (82–100) | 按当前 SlotMap key 保存每个 LayoutSnapshot，clone layout_bins，记录 `Instant::now()`；Debug 下执行完整 `problem_matches_solution` assertion | `BPProblem::Save`（`BppProblem.cpp:293–305`）保存 vector layouts 和 binIndex；C++ BPSolution 本体无对应时间戳字段。Debug 验证边界在 `DebugAssertBPSolution` |
| `restore` (102–168) | 对当前 key：snapshot 有同 key 时先恢复 bin mapping，再尝试 layout.restore，失败则 `Layout::from_snapshot` 重建；snapshot 缺少则移除。对 solution 新 key 则从快照建 Layout 并插入 SlotMap（新 key 可能重分配），再映射 bin。最后从 instance 全量重置 demand/stock，并按现存 layout 和 placements 扣减；超额/非法 index 可触发索引或减法失败，Debug assertion 检验全局一致性。返回是否有 layout keys 改变 | `BPProblem::Restore`（`BppProblem.cpp:307–453`）先完整校验 layout key、bin/base、item definition/demand 与重复 slot，再构建新状态并 `Result<bool>` 原子提交；保存的 key 若无法保持则报告 changed。C++ 与 Rust 都按 layout 恢复 counters，但错误类型、校验强度、key 分配过程不同 |
| `density/item_placed_qtys/bin_used_qtys/bin_cost` (170–208) | density=所有已用容器毛面积下的放置 item 原始面积，`0/0` 可产生 NaN；used qty 由原始需求/库存减剩余量；cost 为已打开 bin 类型 cost×数量累加 u64 | `BPProblem::Density/BinCost`（`BppProblem.cpp:486–510`）；C++ 用 double 对外暴露 density，是否保持相同 float 累加见实现/对拍 |
| `register_layout/deregister_layout` (210–232) | register 时先扣 stock，已有内容逐项扣 demand，再插入 SlotMap 与 bin map；deregister 通过两次 `expect` 移除布局和源 bin、恢复 stock、逐项恢复 demand | C++ `OpenLayout/ReleaseLayoutSlot` 管理槽位与代数；`RemoveLayout` 先验证快照内容后返还 counters，避免 Rust helper 的缺失映射 panic |
| demand/stock helpers, `n_placed_items` (234–254) | counters 直接 `-/+1`；无边界检查；placed count 汇总所有 layout 的 SlotMap len | C++ 对外 query 有越界返回 0，修改入口返回错误；数量语义见 `PlacedItemCount/OpenLayoutCount` |
| `BPPlacement/BPLayoutType` (256–285) | Placement 保存目标 enum、item_idx、DTransformation；from_placed_item 由 PlacedItem 提取 idx/transform；Open 代表既有 key，Closed 代表待打开 bin | `Bpp.h::BPPlacement/LayoutTargetKind` 用扁平 `targetKind/layoutIndex/binIndex`；枚举命名反向且 C++ 还需组合布局/容器索引表达 Rust 的两种 variant |

## `jagua-rs/src/probs/bpp/entities/solution.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `BPSolution` (9–16) | 以 `SecondaryMap<LayKey,LayoutSnapshot>` 和对应 bin map 保存 keyed snapshot，并记录 Instant 时间戳 | C++ `BPSolution`（`Bpp.h:103–119`）为 vector `{layoutIndex,binIndex,layoutSnapshot}`，无同形时间戳；集合顺序与 key 表示不同 |
| `density` (18–34) | 汇总全部 snapshot 的 Container area 与 placed_item_area，以 f32 相除；无 layout 时 0/0 | C++ `BPSolution::Density`（`BppSolution.cpp:37–49`）按槽位顺序先排序，再以 float 累加/相除转 double；Rust SecondaryMap 迭代顺序可能一致，但跨实现顺序仍应对拍 |
| `cost` (36–43) | 对 `layout_bins.values()` 每项累加对应 Bin cost；key/map 无效会 panic | C++ `BPSolution::Cost`（`BppSolution.cpp:51–60`）遍历 snapshots，对 binIndex 做上界检查，越界布局静默不计成本；Rust/C++ 非法快照合同不同 |

## `jagua-rs/src/probs/bpp/io/ext_repr.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `ExtBPInstance` (4–17) | name/items/bins；min_item_separation 缺省为 0，serde default；字段类型 f32、ExtItem/ExtBin arrays | `ExternalBPInstance`（`ExternalRepr.h`）数据字段对应，C++ 使用 double min separation 和 `ExternalDemandItem/ExternalBin` |
| `ExtItem/ExtBin` (19–38) | flatten base DTO；item demand u64，bin stock usize、cost u64 | `ExternalDemandItem/ExternalBin`（`ExternalRepr.h`）类似；JsonCodec 负责字段 flatten 映射 |
| `ExtBPSolution` (40–51) | cost、layouts、density f32、run_time_sec u64 | `ExternalBPSolution` DTO 对应，C++ density 用 double；JSON schema 由 `JsonCodec.cpp` 解析，须按 B8 比较精度与错误 |

## `jagua-rs/src/probs/bpp/io/import.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `import_instance` (9–61) | 将 min separation 交给 Importer 校验；要求 item external IDs 唯一；过滤 demand=0 后并行导入、再 enumerate dense idx，u64→usize 用 checked conversion；要求至少一项正需求。bins 先按 external id 排序、查 duplicate，再过滤 stock=0 并要求有正库存；并行导入 Container，按排序后的顺序赋 dense idx；任一导入/数值错误通过 Result 传播；BPInstance::new 仍可能 assert/panic | `Importer::ImportBPInstance`（`Importer.cpp:590–642`）类似校验、排序和过滤，C++ 将 conversion/shape 错误返回 Result；并行策略不同。边界字段须由 B14 fixture 比较 |
| `import_solution` (63–67) | **pinned 声明参数类型是 `&ExtBPInstance`，函数体 `unimplemented!`，调用必 panic**；不是可用的 solution importer | C++ 有 `ImportBPSolution(const BPInstance&, const ExternalBPSolution&)` 和 JSON wrapper（`Importer.cpp:644–668`），当前函数体返回 `UnsupportedOperation`。差异包括 DTO 参数类型以及 panic 对 Result error；不应表述成 C++ 已实现 BPP solution restore |

## `jagua-rs/src/probs/bpp/io/export.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `export` (6–21) | 导出 cost；对每个 keyed snapshot 用 `layout_bins[key]` 找源 bin external_id，再调用基础 `export_layout_snapshot`；elapsed seconds 取 `time_stamp.duration_since(epoch).as_secs()`；最后写密度。错误 key/bin 或 epoch 晚于 timestamp 可 panic | `JsonCodec::ExternalBPSolutionFromSolution`（`JsonCodec.cpp:1802–1835`）将 C++ snapshot 转 DTO 并返回 Result；C++ 时间由外部调用参数/DTO 提供，Rust 导出函数本身绑定 BPSolution Instant |

## B7/B8 结论边界

- 本记录覆盖 BPP 7 个原先待记源文件；模块 assertion 文件单列在 `10-utilities-and-assertions.md`，因此 BPInstance 构造与 snapshot 验证依赖并未在本文件重复展开。
- Rust BPP 的多数实体操作是直接索引、SlotMap、计数器增减，缺 key/越界/库存不足主要依赖调用者不变量；C++ 改为 Result 并加入范围、库存、快照内容校验。Debug-only demand 检查不代表 Release 合同已变成防御式。
- BPP `import_solution` pinned Rust 是 panic-prone `unimplemented!`，C++ 有接口但当前返回 UnsupportedOperation；不把“接口存在”记录成可导入。
- 本轮只静态登记，`TestBpp.cpp` 与 JsonCodec/Importer 测试未运行。

## 证据位置

- pinned Rust：`probs/bpp/entities/{bin,instance,problem,solution}.rs`、`probs/bpp/io/{export,ext_repr,import}.rs`；构造不变量另见 `probs/bpp/util/assertions.rs:7–32`。
- C++：`JaguarCore/Include/JaguarCore/Problems/Bpp.h`、`JaguarCore/Source/Problems/BPP/{BppInstance,BppProblem,BppSolution}.cpp`、`JaguarCore/Source/IO/{Importer,JsonCodec}.cpp`。
- 现有测试入口：`JaguarCore/Tests/TestBpp.cpp`；本轮未执行。
