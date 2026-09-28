# 37. probs 三模型 io（SPP/BPP/MSPP）逐文件逻辑比对

**Rust**: `probs/spp/io/`（ext_repr.rs 41 行 + import.rs 80 行 + export.rs 15 行）、`probs/bpp/io/`（ext_repr.rs 51 行 + import.rs 67 行 + export.rs 21 行）、`probs/mspp/io/`（ext_repr.rs 49 行 + import.rs 57 行 + export.rs 39 行）
**C++**: `JaguarCore/Source/IO/JsonCodec.cpp`（prob 部分：`commonInstanceFields/Json` 1403-1435、SP 1437-1493、BP 1495-1634、MSP 1636-1744、三个 `External*FromSolution` 1779-1883）+ `Importer.cpp`（`importPositiveItems` 153-189、`ImportSPInstance` 517-535、`ImportSPSolution` 537-570、`ImportBPInstance` 590-642、`ImportBPSolution` 644-650、`ImportMSPInstance` 669-688、`ImportMSPSolution` 690-696）+ `ExternalRepr.h:176-280`
**判定**: 基本等价（三模型实例/解的 io 语义一致；差异集中在 run_time_sec 取值方式、min_item_separation 零值序列化、MSPP container_id 数组下标（eef0830 已改）、并行导入、BPP/MSPP solution import 双侧均不可用）

---

## 第一节 SPP（Strip Packing）

### `ExtSPInstance`（spp/ext_repr.rs:7-18）
- C++ 对应：`ExternalSPInstance`（ExternalRepr.h:185-195）+ `parseSPInstanceValue`/`spInstanceJson`（JsonCodec.cpp:1437-1461）。
- 判定：wire 基本一致（一处序列化差异）。
- 证据：`name: String` ↔ `std::string name`；`min_item_separation: f32`（`#[serde(default)]`）↔ `double minItemSeparation = 0.0`；`items: Vec<ExtItem>`（flatten demand）↔ `std::vector<ExternalDemandItem> items`；`strip_height: f32` ↔ `double stripHeight`。解析：`name`/`items`/`strip_height` 必填、`min_item_separation` 可缺省（cpp:1409-1421 `commonInstanceFields`）↔ Rust `#[serde(default)]` 同语义。
- **差异（min_item_separation 零值序列化）**：Rust 序列化侧无 skip 属性——总是输出 `min_item_separation`（含 0.0；`#[serde(default)]` 只影响反序列化缺省）；C++ `commonInstanceJson` 仅 `separation != 0.0` 才输出（JsonCodec.cpp:1431-1432）。零间距时 Rust 输出 `"min_item_separation":0.0`，C++ 省略该字段。读侧双侧兼容（缺省即 0）。

### `ExtSPSolution`（spp/ext_repr.rs:31-41）
- C++ 对应：`ExternalSPSolution`（ExternalRepr.h:198-208）+ `parseSPSolutionValue`/`spSolutionJson`（JsonCodec.cpp:1463-1493）。
- 判定：一致（`strip_width`/`layout`/`density`/`run_time_sec` 四字段，解析侧全必填含 `run_time_sec`，cpp:1477-1482）。

### `import_instance`（spp/import.rs:12-60）
- C++ 对应：`Importer::ImportSPInstance`（Importer.cpp:517-535）+ `importPositiveItems`（cpp:153-189）。
- 判定：基本等价（逐条语义一致；执行模型不同）。
- 证据：
  - separation 应用：`importer.with_min_item_separation(ext.min_item_separation)?`（import.rs:13）↔ `withSeparation` scoped 导入器（cpp:527），校验见记录 34。
  - item ID 唯一性：`ensure!(...all_unique(), "item IDs must be unique")`（import.rs:14-21）↔ `std::set<ItemId> ids` 重复即错 `"external item IDs must be unique"`（cpp:158-168）。一致（消息措辞略异）。
  - 过滤 demand>0 + `usize::try_from(demand)` + `ensure!(!items.is_empty(), "instance must have positive item demand")`（import.rs:22-36）↔ cpp:170-188 同逻辑（`external.demand == 0 continue`、`toSize`、空则 `"instance must have positive item demand"` 同文案）。
  - **执行模型**：Rust `into_par_iter()`（rayon 并行导入，import.rs:27）；C++ 顺序循环。仅性能差异。
  - **内部索引**：Rust `enumerate()` 以过滤后序号作 `idx`（import.rs:29）；C++ `items.size()` 同为过滤后序号（cpp:177）。一致。
  - 基准条带：Rust 计算 `total_item_area / fixed_height` 得 100% 密度基准宽度后 `Strip::new(fixed_height, cde_config, ShapeModifyConfig{offset, None, None}, width)`（import.rs:38-57）↔ C++ `SPInstance::Create(items, demands, stripHeight, cdeConfig, problemShapeConfig(offset))`（cpp:532-534）——基准宽度计算移入 `SPInstance::Create`（见记录 27），`ShapeModifyConfig` 仅保 offset 与 Rust 相同（simplify/narrow 置 None）。

### `import_solution`（spp/import.rs:63-80）
- C++ 对应：`Importer::ImportSPSolution`（Importer.cpp:537-570）。
- 判定：基本等价。
- 证据：`SPProblem::new(instance)` + `change_strip_width`（import.rs:64-65）↔ `SPProblem::Create` + `ChangeStripWidth`（cpp:545-551）；逐放置项查 item（`instance.item_idx(id).ok_or(unknown item ID)` import.rs:68-70 ↔ `findItemByExternalId` 失败报 `"SP solution references an unknown item ID"` cpp:552-560）；`ext_to_int_transformation`（import.rs:71-75）↔ `externalToInternal`（cpp:561-564，公式见记录 34）；`prob.place_item` ↔ `value.PlaceItem`（cpp:565-567）；`prob.save()` ↔ `value.Save()`（cpp:569）。
- 差异：C++ 额外校验 `strip_width` 有限且 >0（cpp:540-544，`SnapshotMismatch`）；Rust 依赖 `change_strip_width` 内部校验。错误路径等价。

### `export`（spp/export.rs:8-15）
- C++ 对应：`ExternalSPSolutionFromSolution`（JsonCodec.cpp:1779-1804）。
- 判定：基本等价（两处差异）。
- 证据：`strip_width: solution.strip.width` ↔ `solution.strip.width`（cpp:1802）；`layout: export_layout_snapshot(&snapshot, 0)`（**硬编码 0**，export.rs:11）↔ `ExternalLayoutFromSnapshot(snapshot, solution.layoutSnapshot.container.Id())`（cpp:1797-1798，容器实际 id）。实际取值：SPP 容器由 `Strip::CreateContainer` 创建，默认参数 `id = 0`（spp.h:35、Strip.cpp:40），调用点（Problem.cpp:103,161）未传 id——**双侧输出 container_id 均为 0**，差异仅在取值路径（字面量 0 vs 默认 0 的容器 id）。
- **差异 1（run_time_sec）**：Rust `solution.time_stamp.duration_since(epoch).as_secs()`（export.rs:13，`Instant` 由调用方传 epoch）；C++ `runTimeSec` 为函数参数（默认 0，JsonCodec.h:37），由调用方直接给秒数。C++ 无 `Instant`/时间戳体系（见记录 40）。
- **差异 2（导出前校验）**：C++ 先 `SPProblem::Create` + `Restore` + `ValidateSPSolution`（cpp:1784-1796），无效解拒绝导出；Rust 直接导出。C++ 额外（panic→Result/防御性系）。
- `density: solution.density()` ↔ `solution.Density()`（cpp:1803）。一致。

---

## 第二节 BPP（Bin Packing）

### `ExtBPInstance` / `ExtItem`(demand) / `ExtBin`（bpp/ext_repr.rs:6-38）
- C++ 对应：`ExternalBPInstance`（ExternalRepr.h:222-232）/`ExternalDemandItem`（h:176-182）/`ExternalBin`（h:211-219）+ `parseBPInstanceValue`/`bpInstanceJson`/`parseBinValue`/`binJson`（JsonCodec.cpp:1495-1574）。
- 判定：wire 基本一致。
- 证据：`ExtBin` flatten `ExtContainer` + `stock: usize` + `cost: u64` ↔ `ExternalBin { container, stock, cost }`；`binJson` 在 container 字段上追加 `stock`/`cost`（cpp:1513-1520）↔ Rust flatten 同构。`min_item_separation` 序列化差异同 SPP 节。

### `ExtBPSolution`（bpp/ext_repr.rs:41-51）
- C++ 对应：`ExternalBPSolution`（ExternalRepr.h:235-245）+ `parseBPSolutionValue`/`bpSolutionJson`（JsonCodec.cpp:1604-1634）。
- 判定：一致（`cost: u64`/`layouts: Vec<ExtLayout>`/`density: f32`/`run_time_sec: u64`）。

### `import_instance`（bpp/import.rs:10-61）
- C++ 对应：`Importer::ImportBPInstance`（Importer.cpp:590-642）。
- 判定：基本等价（逐条一致）。
- 证据：
  - separation/唯一性/demand 过滤：同 SPP 节。
  - bin 处理：Rust `entries.sort_by_key(id)` 排序后 `windows(2)` 查重（"bin IDs must be unique"）、`retain(stock > 0)`、`ensure!(!entries.is_empty(), "instance must have positive bin stock")`，再按排序后顺序 `Bin::new(idx, bin.base.id, import_container(...), stock, cost)`（import.rs:36-58）↔ C++ 先 `std::set` 查重（`"external bin IDs must be unique"`，cpp:606-615），再按 id 稳定排序 `order`（cpp:617-619），跳过 `stock == 0`，空则 `"instance must have positive bin stock"`（cpp:625-640），`Bin{bins.size(), id, container, stock, cost}`（cpp:633-634）。**差异（查重时点）**：Rust 在排序后查重（顺序无关，行为相同）；C++ 在排序前查重。两者最终都拒绝重复 id，语义一致。内部索引同为过滤后顺序号。
  - **差异（排序稳定性）**：Rust `sort_by_key` 为稳定排序；C++ `std::sort` 比较器仅比较 id——id 已查重唯一，故排序结果双侧一致（等价 id 不存在）。

### `import_solution`（bpp/import.rs:63-67）
- C++ 对应：`Importer::ImportBPSolution`（Importer.cpp:644-650）。
- 判定：一致（双侧均不可用）。
- 证据：Rust `unimplemented!("not yet implemented")`（import.rs:66）↔ C++ 返回 `{ErrorCode::UnsupportedOperation, "BPP solution import is not implemented in the reference crate"}`（cpp:647-649）。panic→Result 系。注：Rust 函数签名参数类型笔误为 `&ExtBPInstance`（应为解），C++ 注释明确对齐"reference crate 不实现"。

### `export`（bpp/export.rs:8-21）
- C++ 对应：`ExternalBPSolutionFromSolution`（JsonCodec.cpp:1806-1845）。
- 判定：基本等价。
- 证据：`cost: solution.cost(instance)` ↔ `solution.Cost(instance)`（cpp:1825）；`layouts: layout_snapshots.map(|(key, sl)| export_layout_snapshot(sl, instance.bins[layout_bins[key]].external_id))`（bin 的外部 id，import.rs:14-17）↔ `instance.GetBin(snapshot.binIndex).externalId`（cpp:1837-1839，binIndex 经映射后取 externalId）。container_id 语义一致（bin 外部 id）。`run_time_sec` 差异同 SPP 节（Instant ↔ 参数）；C++ 额外导出前校验（Restore + ValidateBPSolution，cpp:1811-1823）与 `binIndex` 越界拒绝（cpp:1831-1835）。字段顺序：Rust `cost/layouts/run_time_sec/density` vs C++ 输出 `cost/layouts/density/run_time_sec`（`bpSolutionJson` cpp:1626-1634）——JSON 对象键序无关紧要，wire 兼容。

---

## 第三节 MSPP（Multi-Strip Packing）

### `ExtMSPInstance` / `ExtMSPStrip`（mspp/ext_repr.rs:7-49）
- C++ 对应：`ExternalMSPInstance`（ExternalRepr.h:257-267）/`ExternalMSPStrip`（h:248-254）+ `parseMSPInstanceValue`/`mspInstanceJson`/`parseMSPStripValue`/`mspStripJson`（JsonCodec.cpp:1636-1684）。
- 判定：wire 基本一致（`strips: {height, max_width}` 必填对象，双侧同构；`min_item_separation` 差异同上）。

### `ExtMSPSolution`（mspp/ext_repr.rs:31-41）
- C++ 对应：`ExternalMSPSolution`（ExternalRepr.h:270-280）+ `parseMSPSolutionValue`/`mspSolutionJson`（JsonCodec.cpp:1714-1744）。
- 判定：一致（`layouts`/`containers`/`density`/`run_time_sec`）。

### `import_instance`（mspp/import.rs:10-51）
- C++ 对应：`Importer::ImportMSPInstance`（Importer.cpp:669-688）。
- 判定：基本等价。
- 证据：items 部分同 SPP/BPP。基准条带：Rust `Strip::new(max_width, height, cde_config, ShapeModifyConfig{offset,None,None}, max_width)`（import.rs:38-48，注意 SPP 用 `total_area/height` 而 MSPP 直接用 `max_width` 作初始宽）↔ C++ `MSPInstance::Create(items, demands, maxWidth, height, cdeConfig, problemShapeConfig(offset))`（cpp:684-687）。参数序（max_width, height）双侧一致（见记录 31）。

### `import_solution`（mspp/import.rs:53-57）
- C++ 对应：`Importer::ImportMSPSolution`（Importer.cpp:690-696）。
- 判定：一致（双侧均不可用：Rust `unimplemented!` ↔ C++ `UnsupportedOperation "MSPP solution import is not implemented in the reference crate"`）。

### `export`（mspp/export.rs:10-39）
- C++ 对应：`ExternalMSPSolutionFromSolution`（JsonCodec.cpp:1847-1883）。
- 判定：基本等价（**eef0830 后 container_id 已对齐为数组下标**；此前 C++ 用 `snapshot.layoutIndex` 槽位键，与 Rust 的枚举序下标不一致，本次提交改为循环下标）。
- 证据：
  - layouts：Rust `layout_snapshots.values().enumerate().map(|(index, ls)| export_layout_snapshot(ls, index as u64))`（**数组下标**，mspp/export.rs:12-17）↔ C++（eef0830 后）：
```cpp
for (std::size_t index = 0; index < solution.layouts.size(); ++index)
{
    const MSPLayoutSnapshot& snapshot = solution.layouts[index];
    const ContainerId containerId = static_cast<ContainerId>(index);
```
（JsonCodec.cpp:1870-1875）。**逐行验证：当前代码与 Rust `enumerate()` 下标语义一致。**（Rust `values()` 为 SlotMap 值迭代，即按槽位升序＝插入序；C++ `solution.layouts` 为 vector 按 strip 序。若存在删除后重插，SlotMap 值序与 vector 序可能不同——Rust MSPP 解按 strip 逐个追加、`layout_snapshots` 无删除路径，正常流程下双侧同序。）
  - containers：Rust 对每个条带输出 `ExtContainer { id: index, shape: Rectangle{bbox}, zones: [] }`（mspp/export.rs:19-36，用 `outer_orig.bbox()`）↔ C++ `externalRectangleContainer(snapshot.layoutSnapshot.container, containerId)`（cpp:39-48 + 1879-1880），`id` 同为数组下标、矩形取 `GetOriginalShape().shape.BoundingBox()`、`zones` 空。一致。
  - density / run_time_sec：同 SPP 节差异（Instant ↔ 参数；C++ 额外导出前校验 Restore + ValidateMSPSolution，cpp:1852-1864）。

### JSON 解析/序列化公共部分
- `commonInstanceFields`/`commonInstanceJson`（JsonCodec.cpp:1403-1435）：name/items 必填、min_item_separation 可选数值（null 被拒——`numberValue` 不接受 null，与 Rust 注释"null is invalid"一致）；`demandItemValue`（cpp:1352-1365）要求 demand 必填非负整数（Rust `demand: u64` serde 同拒负数/缺省）。`rejectDuplicateFields` 在各层级模拟 serde `deny_unknown_fields` 效果（对已知字段重复报错；未知字段因 required/size 校验组合被拒，比 serde 默认更严——Rust `ExtItem` 允许无关元数据，见记录 33 差异 4）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | run_time_sec 取值 | 差异 | Rust `Instant` 时间差（`duration_since(epoch).as_secs()`）；C++ 调用方传秒数（默认 0）。C++ 无 Instant/web_time 体系 | spp/export.rs:13 vs JsonCodec.h:37 + JsonCodec.cpp:1803 |
| 2 | min_item_separation 零值序列化 | 差异 | Rust 总是输出（`#[serde(default)]` 仅读侧）；C++ `separation != 0.0` 才输出。读侧双侧兼容 | spp/ext_repr.rs:12-13 vs JsonCodec.cpp:1425-1435 |
| 3 | MSPP container_id | 一致（eef0830 修正） | 双侧均为 layouts 数组下标；此前 C++ 用 `snapshot.layoutIndex` 已改 | mspp/export.rs:12-17 vs JsonCodec.cpp:1870-1875 |
| 4 | SPP container_id | 基本等价 | Rust 硬编码 0；C++ 取容器 id（`Strip::CreateContainer` 默认 0）→ 输出相同，路径不同 | spp/export.rs:11 vs JsonCodec.cpp:1797-1798 + Strip.cpp:40/spp.h:35 |
| 5 | BPP container_id | 一致 | 双侧均用 bin 外部 id | bpp/export.rs:15 vs JsonCodec.cpp:1837-1839 |
| 6 | BPP/MSPP solution import | 一致（双侧不可用） | `unimplemented!` ↔ `UnsupportedOperation`（panic→Result） | bpp/import.rs:66, mspp/import.rs:56 vs Importer.cpp:644-650,690-696 |
| 7 | SPP solution import | 基本等价 | 逐放置项查 id、逆预变换、place、save 全流程一致；C++ 额外 strip_width 前置校验 | spp/import.rs:63-80 vs Importer.cpp:537-570 |
| 8 | 实例导入公共流程（separation/唯一 id/demand 过滤/空集拒绝） | 一致 | 错误文案大量逐字相同（"instance must have positive item demand" 等） | spp/import.rs:12-36 等 vs Importer.cpp:153-189,517-535 等 |
| 9 | rayon 并行导入 vs 顺序 | 差异（性能） | Rust `into_par_iter()`；C++ 顺序循环。结果无序性不暴露（收集为 Vec 顺序一致） | spp/import.rs:27 等 vs Importer.cpp:170-183 |
| 10 | 导出前强制校验 | C++ 额外 | 三模型导出先 Restore + Validate*，无效解拒绝；Rust 直接导出 | 无 vs JsonCodec.cpp:1784-1796,1811-1823,1852-1864 |
| 11 | BPP bin 查重时点（排序前 vs 排序后） | 基本等价 | 均拒绝重复 id；排序稳定 + id 唯一保证最终序一致 | bpp/import.rs:37-44 vs Importer.cpp:606-619 |
| 12 | MSPP containers 伴随输出（矩形 bbox、空 zones、下标 id） | 一致 | eef0830 后逐字段对齐 | mspp/export.rs:19-36 vs JsonCodec.cpp:39-48,1879-1880 |
| 13 | JSON 字段重复/未知字段严格度 | 差异（C++ 更严） | `rejectDuplicateFields` + 已知字段集 vs serde 默认忽略未知字段（ExtItem 的 flatten 元数据例外） | JsonCodec.cpp:699-714,1403-1435 vs 各 ext_repr.rs serde 派生 |
| 14 | JSON 直入口（`Import*InstanceJson` 等 12 个） | C++ 额外 | 解析+导入组合入口；Rust 由调用方 serde 反序列化 | importer.h:39-83 vs 无 |

## 结论

三模型的实例导入（配置、id 唯一性、demand/stock 过滤、空集拒绝、基准条带参数）、SPP 解导入全流程与三模型导出的字段语义双侧一致，且大量错误文案逐字对齐。持久差异集中在四点：run_time_sec 的时间来源（Instant ↔ 显式传参）、min_item_separation 零值序列化、导出前的强制校验（C++ 额外）与并行导入（Rust 独有）。eef0830 已把 MSPP container_id 从槽位键改为数组下标，与 Rust `enumerate()` 对齐；BPP/MSPP 解导入双侧均为显式不可用状态，行为对称。
