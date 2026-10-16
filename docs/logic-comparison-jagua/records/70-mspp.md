# MSPP 逐文件静态映射

基线：pinned `jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。本记录逐项覆盖 MSPP 的 7 个实体与 IO 实现文件；只记录源码可证的静态行为，不据此宣称运行等价。

## `jagua-rs/src/probs/mspp/entities/instance.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `MSPInstance/new` (11–27) | 存储 `(Arc<Item>, demand)` 与 base strip；断言 item 内部 idx 与位置一致、external ID 唯一。非法数据会 panic | `MSPInstance::Create`（`MspInstance.cpp:48–98`）检查 item/demand 数量、dense idx、ID 唯一和 strip 配置，以 `Result` 返回错误；验证条件大体对应，错误合同不同 |
| area 与查询 (30–62) | `item_area` 对原始 item area×demand 求和，使用 f32；`item_qty`、`item` 用下标访问，越界 panic；`total_item_qty` 求需求总和；`item_idx` 按 external ID 线性查找并返回 item 自带 idx。该方法本身不排除零需求项，IO 导入会先过滤 | `TotalItemArea/TotalDemand/GetItem/ItemDemand/ItemIndexFromExternalId`（`MspInstance.cpp:100–143`）对应；C++ 下标 getter 标为 `noexcept`，没有和 Rust panic/Result 等价的边界错误合同 |

## `jagua-rs/src/probs/mspp/entities/strip.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `Strip/new`、`set_width` (11–43) | `new` 只要求 height、max_width、width 均大于 0；初始构造不检查 `width <= max_width`。`set_width` 才用 assert 检查上下界，违反时 panic | `MultiStrip::Create/SetWidth`（`MspStrip.cpp:5–50`）也分别检查正值和最大宽度，但 C++ 先转 f32 并以 `Result` 报错；Create 同样不比较 width 与 max_width |
| `TryFrom<Strip> for Container` (46–60) | 建 `[0,0,width,fixed_height]` 矩形，作为 Deflate `OriginalShape`，无 zones，再以 CDE 配置创建容器；矩形或容器失败通过 anyhow `Result` 传播 | `MultiStrip::CreateContainer`（`MspStrip.cpp:52–63`）执行对应构造，可接收 ContainerId；失败通过 JaguarCore `Result` 传播 |

## `jagua-rs/src/probs/mspp/entities/problem.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `MSPProblem/new`、`add_layout_from_strip`、`remove_layout` (19–56) | demand 初值从 instance 复制；add 将 Strip 转 Container 后注册 Layout 和 Strip，转换失败返回 `Result`；remove 通过 `deregister_layout` 删除布局并恢复其中 item demand，失效 key 在 `expect` 处 panic | `MSPProblem::Create`（`MspProblem.cpp:76–83`）、`AddLayout/RemoveLayout`（`142–181`）对应；C++ 对失效 key、布局 item 不属于实例等情况返回错误。Rust 删除路径没有显式错误出口 |
| `fit_strip` (58–77) | 取布局中最大 bbox.x_max，`partial_cmp().unwrap()` 处理比较，空布局 `max_by(...).unwrap()` panic；拟合宽度为 `x_max * 1.00001 + offset`，再改宽并 Debug 断言可行性不变 | `MSPProblem::FitStrip`（`MspProblem.cpp:262–286`）对空布局返回 `Infeasible`，校验计算值有限且为正；宽度公式在 f32 中计算。Rust 对空布局及不可比较数值走 panic |
| `place_item`、`remove_item` (79–104) | 直接索引 Layout 与 Item；放置成功后才递减 demand，但不预检剩余需求，`usize -= 1` 在 Debug 下可 panic、Release 下可能回绕；非法布局/item key 也可 panic。移除成功后恢复 demand | `MSPProblem::PlaceItem/RemoveItem`（`MspProblem.cpp:183–236`）验证 item index 和 layout key，以 Layout 的 `Result` 决定是否扣减 demand；Debug 下额外拒绝已耗尽需求。对外均为 `Result`，Rust 失败合同更弱 |
| `change_strip_width` (181–193) | 先在局部 Strip 上检查 `0 < width <= max_width`，建 Container 成功后 swap 并提交 Strip；构造失败时不提交新 Strip。Layout 的 swap 没有显式 Result | `MSPProblem::ChangeStripWidth`（`MspProblem.cpp:238–260`）对 key、SetWidth、Container 创建及 swap 分别传播 `Result`；两边都按成功后更新 Strip 的顺序提交 |
| `save`、`restore` (106–179) | Save 记录各 layout snapshot、strip 和 `Instant::now()`，Debug 下检查一致性。Restore 对现存 key 尝试原地恢复，失败则从 snapshot 重建；删除不存在于 snapshot 的布局，并为缺失布局重新插入 SlotMap key；重置 demand 后逐项递减，无 `Result`，错误 key、缺失 strip、索引越界或 demand underflow 可 panic/回绕 | `MSPProblem::Save/Restore`（`MspProblem.cpp:288–437`）保存 key、strip 和 snapshot，不带时间戳。Restore 先验证 key 唯一性、slot、item 定义与 demand，再准备并提交新状态；失败通过 `Result<bool>` 返回，并报告 key 是否改变 |
| density 与查询 (221–267) | problem density 用各 Layout 的 container area 作分母，item area 作分子；无布局时为 `0/0`。提供 layouts、未达 max width 的 extendable strips、总宽和已放置 item 数；`MSPPlacement` 保存 layout key、item idx、变换 | `MSPProblem::Density/ExtendableStrips/TotalStripWidth/PlacedItemCount/RemainingDemand`（`MspProblem.cpp:439–505`）对应，C++ 也用 f32 累加并转 double 返回；C++ 失效 strip 查询可返回 0，Rust 通过键索引时可能 panic |

## `jagua-rs/src/probs/mspp/entities/solution.rs`

| Rust 函数/类型/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `MSPSolution` (7–17) | 以 `SecondaryMap<LayKey, …>` 保存 layout snapshots 与 strips，并保存创建时刻 `Instant` | `MSPSolution`、`MSPLayoutSnapshot`（`Mspp.h:108–129`）保存带 layout key 的 strip 与 LayoutSnapshot；C++ solution 不保存时间戳 |
| `density/total_strip_width` (19–42) | density 分母为各 strip 的 `fixed_height * width`，与 problem density 使用的 container area 口径不同；总宽从 strip map 求和；空 solution density 为 `0/0` | `MSPSolution::Density/TotalStripWidth`（`MspSolution.cpp:37–57`）对应，按 slot 顺序以 f32 累加后转 double；空 solution 同样存在零分母 |

## `jagua-rs/src/probs/mspp/io/ext_repr.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `ExtMSPInstance`、`ExtItem` (5–28) | instance 包含 name、默认值为 0 的 f32 `min_item_separation`、需求 item 列表与 strip DTO；item 将通用 item 字段 flatten，并带 u64 demand | `ExternalMSPInstance/ExternalDemandItem`（`ExternalRepr.h`、`JsonCodec.cpp`）对应，C++ DTO 使用 double/整型字段；字段 JSON 接受规则应以 JsonCodec 实现为准 |
| `ExtMSPSolution`、`ExtMSPStrip` (30–49) | solution 为 layouts、containers、f32 density、u64 runtime 秒；strip 为 f32 height/max_width | `ExternalMSPSolution/ExternalMSPStrip`（`ExternalRepr.h`）对应；C++ exporter 的 runtime 由调用者传入 |

## `jagua-rs/src/probs/mspp/io/import.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `import_instance` (9–51) | 配置 min separation；对所有 external IDs（包括零需求项）要求唯一；过滤零需求项，并行导入、按新顺序生成 dense idx；u64 demand 转 usize 失败时返回错误；拒绝无正需求实例。base strip 初始宽度设为 max_width，只保留 shape offset，simplify/narrow 配置设为 None | `Importer::ImportMSPInstance` 与 `importPositiveItems`（`Importer.cpp:669–688`、`156–190`）同样检查 separation/ID、跳过零需求并检查 demand 转换；之后以 max_width 和 `problemShapeConfig(offset)` 建 MSPInstance。入口与配置路径静态对应 |
| `import_solution` (53–56) | 函数参数类型为 `ExtMSPInstance`，函数体是 `unimplemented!`，调用必 panic，当前无 solution 导入逻辑 | `Importer::ImportMSPSolution` 接收正确的 `ExternalMSPSolution` 类型，但明确返回 `UnsupportedOperation`（`Importer.cpp:690–696`）；两边都未实现导入，失败合同不同，且 Rust 签名与 solution DTO 不匹配 |

## `jagua-rs/src/probs/mspp/io/export.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `export` (8–38) | 逐 snapshot 导出 layout，并按迭代序号赋 container id；container 从 snapshot 的 `outer_orig` bbox 生成无 zones 的矩形；输出 solution density。运行秒数取 `time_stamp.duration_since(epoch)`，若 epoch 晚于创建时间会 panic；函数无 `Result` | `ExternalMSPSolutionFromSolution`（`JsonCodec.cpp:1838–1869`）先创建 MSPProblem 并 Restore solution，再逐项导出 layout/container；恢复或 layout 导出失败通过 `Result` 返回，runtime 秒由调用者传入。C++ 多了导出前 snapshot 验证与错误传播 |

## B6/B7/B8/B14 结论边界

- 本记录覆盖 MSPP 7 个待记实现文件；模块导出与 `probs/mspp/util/assertions.rs` 分别见 `00-module-exports.md`、`10-utilities-and-assertions.md`。
- 需要进入后续行为对拍的差异包括：空条带 `fit_strip` 的 panic/`Infeasible`、需求耗尽放置、无效 key 与 restore 错误传播、MSPP solution 导入的 `unimplemented!`/`UnsupportedOperation`，以及 export 的时间戳与 snapshot 验证合同。
- 静态映射未执行 Rust 或 C++ 测试，也未验证 JSON round-trip；不得据此判定 MSPP 运行等价。

## 证据位置

- pinned Rust：`probs/mspp/entities/{instance,problem,solution,strip}.rs`、`probs/mspp/io/{export,ext_repr,import}.rs`。
- C++：`JaguarCore/Include/JaguarCore/Problems/Mspp.h`、`JaguarCore/Source/Problems/MSPP/{MspInstance,MspProblem,MspSolution,MspStrip}.cpp`、`JaguarCore/Source/IO/{Importer,JsonCodec}.cpp`。
- 本轮只更新静态记录与清单，未执行构建或测试。
