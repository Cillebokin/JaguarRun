# probs/spp：条带实体、问题状态与 IO

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 对照**：`JaguarCore/Include/JaguarCore/Problems/Spp.h`、`Source/Problems/SPP/*`、`Source/IO/Importer.cpp`、`Source/IO/JsonCodec.cpp`  
**关联工作项**：B6、B7、B8、B13、B14  
**证据类型**：pinned Rust 与当前 C++ 源码静态核对；未运行 SPP 测试或对拍

## `jagua-rs/src/probs/spp/entities/instance.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `SPInstance/new` (9–26) | 保存 item `(Arc<Item>, demand)` 列表和 base strip；assert item index 与 vector 位置一致，再 assert external item IDs 唯一；失败 panic，不检查 empty/zero-demand | `SPInstance::Create`（`Instance.cpp:18–125`）以 Result 校验长度、dense index、唯一 ID 与 strip 配置；错误可恢复 |
| `item_area` (28–35) | 原始 item area×demand 的 f32 累加 | `SPInstance::TotalItemArea`（`Instance.cpp:161–169`）float 累加后对外 double |
| `item_qty/item/total_item_qty/item_idx` (37–61) | 直接索引查询越界会 panic；total quantity 求和；外部 ID 线性查找，返回 Item 自带 idx，未知 ID 返回 None | `ItemDemand/GetItem/TotalDemand/ItemIndexFromExternalId` 对应；C++ getters 未提供相同 Rust 借用形式 |

## `jagua-rs/src/probs/spp/entities/strip.rs`

| Rust 声明/函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `Strip/new` (10–32) | Copy 值包含 fixed_height、CDE 配置、形状修改配置和 width；高度/宽度必须 `>0`，否则 anyhow error。正无穷先通过正值检查，后续矩形/Container 构造仍可能失败 | `Strip::Create`（`Strip.cpp:5–26`）检查输入和 f32 转换后仍为正，并存为 f32 可表示的 double；两边错误类型/精度入口不同 |
| `set_width` (34–37) | `assert!(width>0)`，违反时 panic；仅写 width，不重建容器 | `Strip::SetWidth`（`Strip.cpp:28–38`）检查 f32 可表示的正数并返回 Result；只更新 Strip 值 |
| `TryFrom<Strip> for Container` (40–54) | 按 `[0,0,width,fixed_height]` 建 Rect/SPolygon，创建 Deflate `OriginalShape`，不带 quality zones，再用 CDE config 建 Container；矩形/容器错误通过 Result 传播 | `Strip::CreateContainer`（`Strip.cpp:40–51`）同样建矩形并 Deflate；C++ 可带 ContainerId、全数值以 RustF32 方式量化 |

## `jagua-rs/src/probs/spp/entities/problem.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `SPProblem/new` (12–32) | 公开 instance/strip/layout/剩余 demand；用 base_strip 转 Container 并建立 Layout，转换失败以 Result 返回；demand 初值拷贝自 instance | `SPProblem::Create`（`Problem.cpp:19–28`）同样通过 Strip→Container；C++ 状态私有并返回 Result |
| `change_strip_width` (34–44) | 先拒绝 `new_width <=0`；在局部 Strip 上改宽并尝试转换 Container，失败前不改 problem；成功后调用 `layout.swap_container` 保留动态物品 hazards，再提交 Strip。该函数返回 Result，但 swap/register 无显式错误出口 | `SPProblem::ChangeStripWidth`（`Problem.cpp:73–90`）在 next Strip、Container、Layout::SwapContainer 都成功后提交 strip；C++ 对每步失败返回 Result，整体强回滚 |
| `fit_strip` (46–66) | 先记 `layout.is_feasible()`；取所有碰撞形状 bbox 最大 x，`partial_cmp().unwrap()`；空布局 `max_by(...).unwrap()` panic。宽度=`item_x_max*1.00001 + offset.unwrap_or(0)`；调用 change，再 Debug 断言改变宽度前后 feasibility 相等 | `SPProblem::FitStrip`（`Problem.cpp:92–111`）空布局返回 Infeasible，拒绝非有限/非正计算值；计算在 f32 中进行后转 double。Rust `max_by` 遇不可比较值会 unwrap panic，边界输入路径不同 |
| `place_item` (68–74) | 先 decrement demand，再按 index 取 Item 并放入 Layout，返回 PItemKey；不校验 idx、remaining demand 或碰撞可行性，失败/无效输入可 panic，且先减计数意味着后续 panic 前状态已变化 | `SPProblem::PlaceItem`（`Problem.cpp:30–53`）先校验 index，Debug 下检查需求，再调用 Layout::PlaceItem，成功后才减 demand；结果用 handle+Result |
| `remove_item` (76–85) | Layout.remove 可能对无效 key panic；成功后恢复 demand，输出原 item idx/变换 | `SPProblem::RemoveItem`（`Problem.cpp:55–71`）传播 layout Result；还检查移除 item 是否属于实例，失败为 SnapshotMismatch |
| `save` (87–99) | 保存 LayoutSnapshot、Strip 和 `Instant::now()`；Debug 下检查 `problem_matches_solution` | `SPProblem::Save`（`Problem.cpp:113–116`）保存 strip/layout snapshot，无时间戳；C++ 外部 exporter 接收时间值 |
| `restore` (101–122) | 尝试在当前 Layout 上 restore，若 ContainerMismatch 则用 `Layout::from_snapshot` 整体重建；设置 strip；从 instance 重置需求，再按放置项 index 递减，缺 index/超量可 panic/usize underflow；Debug assertion 检查 solution。返回 `()` | `SPProblem::Restore`（`Problem.cpp:118–158`）同源时原地恢复，不同源时建临时 Layout 再提交；C++ 越界 item index 抛 `out_of_range`，Debug demand underflow 抛异常，其他恢复失败以 Result；C++/Rust 错误类型不等价 |
| counters and queries (124–153) | item demand 直接增减；density 委托 Layout；strip_width 与 placed count 为简单查询；需求计数未在 place 时检查 | `RemainingDemand/PlacedItemCount/Density/StripWidth`（Spp.h / Problem.cpp:160–182）对应，C++ demand query 越界返回 0，density 类型 double |
| `SPPlacement` (148–153) | 只含 item_idx 和 DTransformation | `Spp.h::SPPlacement` 含 itemIndex/Transform2d；Rust/C++ transform 表示和精度归 B1 |

## `jagua-rs/src/probs/spp/entities/solution.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `SPSolution` (7–12) | 保存 strip、LayoutSnapshot、创建时 Instant | C++ `SPSolution`（`Spp.h`）只保存 strip 与 layout snapshot，没有时间戳 |
| `density/strip_width` (14–23) | density 委托 snapshot，strip_width 直接读 strip.width | C++ `SPSolution::Density/StripWidth`（`Solution.cpp:5–20`）同类查询，double 返回 |

## `jagua-rs/src/probs/spp/io/ext_repr.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `ExtSPInstance` (5–18) | name、可选默认 0 的 f32 `min_item_separation`、flatten item-demand 列表、f32 strip_height；serde default 对 omission 生效，null 不等于省略 | `ExternalSPInstance`（`ExternalRepr.h`）对应字段为 double/ExternalDemandItem；字段级 JSON 合同由 JsonCodec 实现 |
| `ExtItem` (20–28) | flatten 通用 item DTO 加 u64 demand | `ExternalDemandItem` 对应 |
| `ExtSPSolution` (30–41) | strip_width、单个 layout、f32 density、u64 runtime seconds | `ExternalSPSolution` 对应，density/width 使用 double；`run_time_sec` 序列化由 JsonCodec 处理 |

## `jagua-rs/src/probs/spp/io/import.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `import_instance` (10–66) | 先按 min separation 配置 Importer；要求 external item IDs 唯一；过滤 demand=0 后并行导入并重新枚举 dense idx，需求 u64→usize checked；拒绝无正需求实例。strip width 初始化为 `separation + totalItemArea/(fixedHeight-separation)`，先验证 usable height>0；Strip 的 shape config 只保留 importer offset，simplify/narrow 设 None；最后 SPInstance::new assertion dense idx/unique IDs | `Importer::ImportSPInstance`（`Importer.cpp:517–534`）相同调用 separation 和正需求 item 导入，再交给 `SPInstance::Create`；Rust 初始 width 与 shape config 是关键迁移边界，C++ 计算/精度应以源码和 fixture 核对 |
| `import_solution` (68–86) | 新建 SPProblem；改到外部 strip_width；依序解析每个 external item_id，未知 id 转 anyhow error；外部变换经 `ext_to_int_transformation` 还原中心 pre-transform；place item 并 save。此处不验证 DTO density/runtime，也没有显式检查 demand 上限或 collision feasibility | `Importer::ImportSPSolution`（`Importer.cpp:537–570`）同样重建问题、改宽、查 id、变换并放置；C++ 无效宽度、unknown id 或 PlaceItem 错误返回 Result，Rust 某些错误/超量路径会 panic/计数回绕 |

## `jagua-rs/src/probs/spp/io/export.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `export` (6–15) | 输出 solution strip width、导出基础 layout（container_id 固定 0）、density 与 `time_stamp.duration_since(epoch).as_secs()`；没有 Result，epoch 晚于 timestamp 会 panic | `ExternalSPSolutionFromSolution`（`JsonCodec.cpp:1778–1798`）生成 DTO 并返回 Result；C++ 的布局导出 metadata 和运行时间来源由调用者输入 |

## B6/B7/B8/B14 结论边界

- 本记录覆盖 SPP 7 个原先待记文件；`probs/spp/util/assertions.rs` 另在 `10-utilities-and-assertions.md`，模块导出另在 `00-module-exports.md`。
- Rust 对 `SPProblem::change_strip_width` 的局部构造顺序与 C++ 的提交顺序目标一致，但 Rust Layout::swap_container 无 Result；Rust place/remove/empty fit 与 C++ 可恢复错误合同不同。
- SPP import/export 中 external IDs、centering transform、min separation 与初始化 width 属 B8/B14 的对拍范围；本轮未运行测试或 JSON round-trip。

## 证据位置

- pinned Rust：`probs/spp/entities/{instance,problem,solution,strip}.rs`、`probs/spp/io/{export,ext_repr,import}.rs`；实例断言见 `probs/spp/util/assertions.rs`。
- C++：`JaguarCore/Include/JaguarCore/Problems/Spp.h`、`JaguarCore/Source/Problems/SPP/{Instance,Problem,Solution,Strip}.cpp`、`JaguarCore/Source/IO/{Importer,JsonCodec}.cpp`。
- 现有测试入口：`JaguarCore/Tests/TestSpp.cpp`、`TestIo.cpp`；本轮未执行。
