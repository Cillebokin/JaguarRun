# 37. probs 三模型 io（SPP/BPP/MSPP）逐文件逻辑比对

**Rust**: `probs/spp/io/`（ext_repr.rs 41 行 + import.rs 80 行 + export.rs 15 行）、`probs/bpp/io/`（ext_repr.rs 51 行 + import.rs 67 行 + export.rs 21 行）、`probs/mspp/io/`（ext_repr.rs 49 行 + import.rs 57 行 + export.rs 39 行）
**C++**: `JaguarCore/Source/IO/JsonCodec.cpp`（prob 部分：`commonInstanceFields/Json` 1403-1435、SP 1437-1493、BP 1495-1634、MSP 1636-1744、三个 `External*FromSolution` 1779-1883）+ `Importer.cpp`（`importPositiveItems` 153-189、`ImportSPInstance` 517-535、`ImportSPSolution` 537-570、`ImportBPInstance` 590-642、`ImportBPSolution` 644-650、`ImportMSPInstance` 669-688、`ImportMSPSolution` 690-696）+ `ExternalRepr.h:176-280`
**判定**: 基本等价（三模型实例/解的 io 语义一致；2026-09-29 复核：min_item_separation 零值序列化与导出前强制校验两项差异已在 4540a76/291f057 消除，现存差异集中在 run_time_sec 取值方式、并行导入、SPP 正无穷宽度的安全失败映射、BPP/MSPP solution import 双侧均不可用；MSPP container_id 数组下标已由 eef0830 修正）

---

## 第一节 SPP（Strip Packing）

### `ExtSPInstance`（spp/ext_repr.rs:7-18）
- C++ 对应：`ExternalSPInstance`（ExternalRepr.h:185-195）+ `parseSPInstanceValue`/`spInstanceJson`（JsonCodec.cpp:1437-1461）。
- 判定：wire 基本一致（一处序列化差异）。
- 证据：`name: String` ↔ `std::string name`；`min_item_separation: f32`（`#[serde(default)]`）↔ `double minItemSeparation = 0.0`；`items: Vec<ExtItem>`（flatten demand）↔ `std::vector<ExternalDemandItem> items`；`strip_height: f32` ↔ `double stripHeight`。解析：`name`/`items`/`strip_height` 必填、`min_item_separation` 可缺省（cpp:1409-1421 `commonInstanceFields`）↔ Rust `#[serde(default)]` 同语义。
- **判定（一致，4540a76后）**：Rust serde总是输出`min_item_separation`（`#[serde(default)]`只控制反序列化缺省）；C++ `commonInstanceJson`现也无条件输出该字段（JsonCodec.cpp:1463-1466），含零值。

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

### `import_solution`（spp/import.rs:69-86）
- C++ 对应：`Importer::ImportSPSolution`（Importer.cpp:568-603）。
- 判定：普通有限输入主流程基本等价；非有限变换及f32极值量化引发几何退化时，失败通道有差异。
- 证据：`SPProblem::new(instance)` + `change_strip_width`（import.rs:70-71）↔ `SPProblem::Create` + `ChangeStripWidth`（cpp:578-584）；逐放置项查 item（`instance.item_idx(id).ok_or(unknown item ID)` import.rs:73-76 ↔ `findItemByExternalId` 失败报 `"SP solution references an unknown item ID"` cpp:585-593）；`ext_to_int_transformation`（import.rs:77-81）↔ `externalToInternal`（cpp:594-597，公式见记录34）；`prob.place_item`（import.rs:82）↔ `value.PlaceItem`（cpp:598-600）；`prob.save()`（import.rs:85）↔ `value.Save()`（cpp:602）。
- `strip_width` 边界（Stage189）：0、负值、负无穷及NaN在Rust `change_strip_width`返回`strip width must be positive`；C++移除Importer的宽泛前置校验，改由`ChangeStripWidth`/`Strip::SetWidth`返回同一`InvalidInput`错误。正无穷在Rust通过`> 0`检查后，于`Rect::try_new`构造中触发`NotNan` panic；C++仅保留正无穷的窄拦截，返回`SnapshotMismatch`，避免把Rust panic暴露成C++进程崩溃。该安全失败映射是明确差异。
- 对有限变换，逐项映射如上；对类型化Inf平移，Rust在`ext_to_int_transformation`矩阵乘法中由`Inf * 0 → NaN`触发`NotNan` panic，C++ `externalToInternal`返回`SnapshotMismatch`。同一JSON数值`3.5e38`在Rust反序列化为f32 Inf后panic；C++ `numberValue`在JSON入口收窄并拒绝，返回`SerializationFailure`。Rust Debug/Release `--import-boundary-parity-only`均exit 0；MSVC14.29 C++ Release全量runner exit 0，新增`TestIo`类型化与`TestJsonCodec` JSON用例，详见[记录34](34-import.md)与[Transformation记录](06-transformation.md)。
- rotation字段也已按同值补测：`rotation: 3.5e38`在Rust解析为Inf后由`cos(Inf)→NaN`触发panic；C++ JSON入口返回`SerializationFailure`，类型化`rotationDegrees=3.5e38`入口返回`SnapshotMismatch`。该结果只覆盖这一有限JSON数值和锁定实现路径。
- 负向溢出与最大有限值也已补测：rotation/translation的`-3.5e38`在Rust解析为`-Inf`后panic，C++ JSON/类型化入口分别返回`SerializationFailure`/`SnapshotMismatch`；`3.4028234663852886e38`在两侧解析并收窄为最大有限f32。最大有限值断言止于解析边界，不代表该值可在完整布局中成功放置。
- 临界舍入点配对：f64中点前一个值`340282356779733623858607532500980858880`在Rust收窄为±f32最大有限值，C++ JSON解析成功；中点`340282356779733661637539395458142568448`正负两侧在Rust收窄为±Inf，C++均返回`SerializationFailure`。正向前驱随后已进入完整`ImportSPSolutionJson`：rotation分支两侧成功且最终变换、4个顶点和包围盒raw bits完全一致；translation x分支Rust在布局构造时因退化包围盒panic，C++从Importer返回`InvalidInput: invalid rectangle`。详见[Transformation记录](06-transformation.md)。

### `export`（spp/export.rs:8-15）
- C++ 对应：`ExternalSPSolutionFromSolution`（JsonCodec.cpp:1779-1804）。
- 判定：基本等价（两处差异）。
- 证据：`strip_width: solution.strip.width` ↔ `solution.strip.width`（cpp:1802）；`layout: export_layout_snapshot(&snapshot, 0)`（**硬编码 0**，export.rs:11）↔ `ExternalLayoutFromSnapshot(snapshot, solution.layoutSnapshot.container.Id())`（cpp:1797-1798，容器实际 id）。实际取值：SPP 容器由 `Strip::CreateContainer` 创建，默认参数 `id = 0`（spp.h:35、Strip.cpp:40），调用点（Problem.cpp:103,161）未传 id——**双侧输出 container_id 均为 0**，差异仅在取值路径（字面量 0 vs 默认 0 的容器 id）。
- **差异 1（run_time_sec）**：Rust `solution.time_stamp.duration_since(epoch).as_secs()`（export.rs:13，`Instant` 由调用方传 epoch）；C++ `runTimeSec` 为函数参数（默认 0，JsonCodec.h:37），由调用方直接给秒数。C++ 无 `Instant`/时间戳体系（见记录 40）。
- **判定（一致，291f057后）**：C++保留`SPProblem::Create`与`Restore`，但不再调用`ValidateSPSolution`；序列化直接读取solution字段（JsonCodec.cpp:1840-1856），与Rust导出路径一致。
- `density: solution.density()` ↔ `solution.Density()`（cpp:1803）。一致。

---

## 第二节 BPP（Bin Packing）

### `ExtBPInstance` / `ExtItem`(demand) / `ExtBin`（bpp/ext_repr.rs:6-38）
- C++ 对应：`ExternalBPInstance`（ExternalRepr.h:222-232）/`ExternalDemandItem`（h:176-182）/`ExternalBin`（h:211-219）+ `parseBPInstanceValue`/`bpInstanceJson`/`parseBinValue`/`binJson`（JsonCodec.cpp:1495-1574）。
- 判定：wire 基本一致。
- 证据：`ExtBin` flatten `ExtContainer` + `stock: usize` + `cost: u64` ↔ `ExternalBin { container, stock, cost }`；`binJson` 在 container 字段上追加 `stock`/`cost`（cpp:1513-1520）↔ Rust flatten 同构。`min_item_separation` 零值序列化与SPP同为一致（4540a76后）。

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
- 证据：`cost: solution.cost(instance)` ↔ `solution.Cost(instance)`（JsonCodec.cpp:1873）；`layouts: layout_snapshots.map(|(key, sl)| export_layout_snapshot(sl, instance.bins[layout_bins[key]].external_id))`（bin 的外部 id，import.rs:14-17）↔ `instance.GetBin(snapshot.binIndex).externalId`（JsonCodec.cpp:1885-1887，binIndex 经映射后取 externalId）。container_id 语义一致（bin 外部 id）。`run_time_sec` 差异同 SPP 节（Instant ↔ 参数）；C++保留Restore但不调用`ValidateBPSolution`（JsonCodec.cpp:1864-1871），另拒绝越界`binIndex`（JsonCodec.cpp:1879-1884）。字段顺序：Rust `cost/layouts/run_time_sec/density` vs C++ 输出 `cost/layouts/density/run_time_sec`（`bpSolutionJson` cpp:1626-1634）——JSON 对象键序无关紧要，wire 兼容。

---

## 第三节 MSPP（Multi-Strip Packing）

### `ExtMSPInstance` / `ExtMSPStrip`（mspp/ext_repr.rs:7-49）
- C++ 对应：`ExternalMSPInstance`（ExternalRepr.h:257-267）/`ExternalMSPStrip`（h:248-254）+ `parseMSPInstanceValue`/`mspInstanceJson`/`parseMSPStripValue`/`mspStripJson`（JsonCodec.cpp:1636-1684）。
- 判定：wire 基本一致（`strips: {height, max_width}` 必填对象，双侧同构；`min_item_separation` 零值序列化与SPP一致（4540a76后））。

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
  - density / run_time_sec：时间来源差异同 SPP 节（Instant ↔ 参数）；C++保留Restore但不调用`ValidateMSPSolution`（JsonCodec.cpp:1900-1925）。

### JSON 解析/序列化公共部分
- `commonInstanceFields`/`commonInstanceJson`（JsonCodec.cpp:1403-1435）：name/items 必填、min_item_separation 可选数值（null 被拒——`numberValue` 不接受 null，与 Rust 注释"null is invalid"一致）；`demandItemValue`（cpp:1352-1365）要求 demand 必填非负整数（Rust `demand: u64` serde 同拒负数/缺省）。`rejectDuplicateFields` 只拒绝调用点列出的已知键重复，不会普遍拒绝未知键。Stage187 的六个 SPP 输入确认：Rust 与 C++ 都接受未知顶层字段、未知 item 元数据及重复未知键；都拒绝重复 `id` 和 `ExtRotation` 中的未知字段。该结论只覆盖这六类输入，不代表所有外部结构的未知字段规则一致。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | run_time_sec 取值 | 差异 | Rust `Instant` 时间差（`duration_since(epoch).as_secs()`）；C++ 调用方传秒数（默认 0）。C++ 无 Instant/web_time 体系 | spp/export.rs:13 vs JsonCodec.h:37 + JsonCodec.cpp:1803 |
| 2 | min_item_separation 零值序列化 | 一致（4540a76 后） | Rust serde 与 C++ 均输出零值；C++ `commonInstanceJson` 无条件加入该字段 | spp/ext_repr.rs:12-13 vs JsonCodec.cpp:1463-1466 |
| 3 | MSPP container_id | 一致（eef0830 修正） | 双侧均为 layouts 数组下标；此前 C++ 用 `snapshot.layoutIndex` 已改 | mspp/export.rs:12-17 vs JsonCodec.cpp:1870-1875 |
| 4 | SPP container_id | 基本等价 | Rust 硬编码 0；C++ 取容器 id（`Strip::CreateContainer` 默认 0）→ 输出相同，路径不同 | spp/export.rs:11 vs JsonCodec.cpp:1797-1798 + Strip.cpp:40/spp.h:35 |
| 5 | BPP container_id | 一致 | 双侧均用 bin 外部 id | bpp/export.rs:15 vs JsonCodec.cpp:1837-1839 |
| 6 | BPP/MSPP solution import | 一致（双侧不可用） | `unimplemented!` ↔ `UnsupportedOperation`（panic→Result） | bpp/import.rs:66, mspp/import.rs:56 vs Importer.cpp:644-650,690-696 |
| 7 | SPP solution import | 固定矩阵一致，保留已记录差异 | 主流程一致；非正/NaN宽度错误文案对齐；JSON中`3.5e38`在Rust转为Inf后panic，C++解析层以`SerializationFailure`安全拒绝（Stage190）；solution/layout/placed item/transformation字段严格度在Stage191-193固定矩阵中对齐 | spp/import.rs:69-86、pinned `io/ext_repr.rs` vs Importer.cpp:568-621, JsonCodec.cpp:747-767,1306-1371 |
| 8 | 实例导入公共流程（separation/唯一 id/demand 过滤/空集拒绝） | 一致 | 错误文案大量逐字相同（"instance must have positive item demand" 等） | spp/import.rs:12-36 等 vs Importer.cpp:153-189,517-535 等 |
| 9 | rayon 并行导入 vs 顺序 | 差异（性能） | Rust `into_par_iter()`；C++ 顺序循环。结果无序性不暴露（收集为 Vec 顺序一致） | spp/import.rs:27 等 vs Importer.cpp:170-183 |
| 10 | 导出前强制校验 | 一致（291f057 后） | C++ 保留 Restore 路径但不再调用 `Validate*Solution`；Rust 导出直接读取 solution 字段 | Rust `export.rs` vs JsonCodec.cpp:1835-1905 |
| 11 | BPP bin 查重时点（排序前 vs 排序后） | 基本等价 | 均拒绝重复 id；排序稳定 + id 唯一保证最终序一致 | bpp/import.rs:37-44 vs Importer.cpp:606-619 |
| 12 | MSPP containers 伴随输出（矩形 bbox、空 zones、下标 id） | 一致 | eef0830 后逐字段对齐 | mspp/export.rs:19-36 vs JsonCodec.cpp:39-48,1879-1880 |
| 13 | JSON 字段重复/未知字段严格度 | 固定矩阵一致（Stage187、191-198） | Stage187覆盖SPP item/rotation未知字段；Stage191-195覆盖SPP solution、instance和rotation多层字段；Stage196-197覆盖shape、container与quality zone；Stage198覆盖BPP Bin和MSPP Strip。仍不外推到所有外部结构 | `json_metadata_b8_inputs.txt`、`solution_import_b8_inputs.txt`; JsonCodec.cpp:730-745,841-987,1045-1124,1160-1209,1225-1295,1390-1487,1306-1371,1498-1518,1553-1623,1694-1733 vs pinned problem/io ext_repr.rs serde |
| 14 | JSON 直入口（`Import*InstanceJson` 等 12 个） | C++ 额外 | 解析+导入组合入口；Rust 由调用方 serde 反序列化 | importer.h:39-83 vs 无 |
| 15 | `ExtItem.min_quality=None` 序列化 | 一致（Stage188） | Rust `Option<usize>::None` 输出 JSON `null`；C++ 的 `minQuality` 无值时也输出 `null`，输入缺省与显式 null 都映射到 None | pinned `io/ext_repr.rs:15`; JsonCodec.cpp:1212-1222 |

## 结论

三模型的实例导入（配置、id 唯一性、demand/stock 过滤、空集拒绝、基准条带参数）、SPP 解导入全流程与三模型导出的主要字段语义双侧一致，且大量错误文案逐字对齐。当前明确差异包括 run_time_sec 的时间来源（Rust `Instant` ↔ C++ 显式传秒）、并行导入（Rust rayon ↔ C++ 顺序循环）、正无穷宽度/非有限变换的panic到Result安全映射，以及JSON f32溢出值导致的Rust panic与C++解析拒绝；零值 `min_item_separation` 序列化已对齐（4540a76），`ExtItem.min_quality=None` 输出 `null` 已于Stage188对齐，SPP非正/NaN宽度错误已于Stage189对齐；导出前 `Validate*Solution` 已移除（291f057）。未知/重复字段行为已在Stage187、191-198覆盖SPP实例/item元数据、rotation、shape、container、quality zone、BPP Bin、MSPP Strip与SPP解多层字段的固定矩阵；不外推到其他外部结构。eef0830已把MSPP container_id对齐为数组下标；BPP/MSPP solution import在固定入口上双侧均不可用，错误机制不同。

### 补充审计（2026-09-29）

对 spp/bpp/mspp 三组 io（ext_repr + import + export 共 9 文件 384 行）逐行复核，并核对 2026-09-29 后 JsonCodec.cpp 的两处改动（git 4540a76：`min_item_separation` 无条件输出；git 291f057：移除导出前 `Validate*Solution`）。原记录遗漏/需修正各点：

1. **【记录过期·重要】差异 2（min_item_separation 零值序列化）已消除（4540a76）**——当前 `commonInstanceJson`（JsonCodec.cpp:1425-1434）无条件 `object.emplace("min_item_separation", JsonValue(separation))`，原 `if (separation != 0.0)` 守卫已删除。与 Rust serde（`#[serde(default)]` 仅读侧、写侧总是输出）**一致**。SPP/BPP/MSP 三处 `*InstanceJson` 均经 `commonInstanceJson`，一次修正三模型对齐。
2. **【记录过期·重要】差异 10（导出前强制校验）已移除（291f057）**——三个 `External*FromSolution` 的 `Validate*Solution(problem.Value(), solution)` 调用已删除（保留 `Restore`），替换为注释 `// jagua-rs export is a pure function over the solution struct...`（JsonCodec.cpp:1791-1792, 1815, 1851）。原记录 SPP 节"差异 2"、BPP/MSPP 节及差异表 10 所述行为不再成立，改判**一致（291f057 后）**。BPP 的 `binIndex` 越界拒绝（cpp:1822-1827）保留。
3. **SPP `import_instance` 的 `total_item_area` 计算域（浅覆盖→补齐关键数值细节）**——Rust：`items.iter().map(|(item, demand)| item.area() * *demand as f32).sum::<f32>()`（import.rs:38-41）——**f32 域**逐项乘加；C++ `SPInstance::Create`（Instance.cpp:32-64）：`float totalArea`、`itemArea * static_cast<float>(demands[i])`、`totalArea = nextTotalArea`——同为 f32 逐项累加（含溢出检查 `isfinite(nextTotalArea)`，Rust 溢出为 inf 后续 `Strip::new(width=inf)` 报错，路径不同但都失败）。**注意**：C++ 的面积累加在 `SPInstance::Create` 内做（先于 `Strip::Create`），Rust 在 import_instance 显式算后传给 `Strip::new`——两侧 `width = total_area / fixed_height` 的除法同为 f32 域（C++ Instance.cpp:72-73 `static_cast<float>(fixedHeight)` 后 f32 除法）。原记录 30 行只说"基准宽度计算移入 SPInstance::Create"，未提 f32 逐项累加次序（items 顺序双侧一致：过滤后枚举序）与溢出检查差异。判定：一致（f32 域、次序相同）；差异（溢出诊断：C++ 显式 `"SPP item area is non-finite"` vs Rust inf 经 Strip 宽度校验失败）。
4. **SPP `import_instance` 的 `items` 传给 `SPInstance::new` 时的再次断言（遗漏）**——Rust `SPInstance::new` 内部 `assert!(indices correct)` + `assert!(external_id all_unique)`（instance.rs:19-24，debug 断言）；C++ `SPInstance::Create` 把两项升级为正式错误（Instance.cpp:38-50 `"SPP item indices must be dense"`/`"SPP item external IDs must be unique"`）。由于 importer 已保证唯一（记录差异 8），该层为冗余防御；C++ 升格为 Result。判定：基本等价（panic→Result，双层防御）。
5. **BPP `import_instance` bin 排序的 C++ `std::sort` 稳定性（原记录 65 行已分析，补充证实）**——C++ 比较器仅 `id`（cpp:617-619），id 已查重唯一 → 无并列元素，`std::sort` 不稳定无影响。与 Rust `sort_by_key`（稳定）结果一致。原分析成立，无修正。
6. **BPP `import_instance` 的 `Bin::new` 参数顺序（补齐）**——Rust `Bin::new(idx, bin.base.id, import_container(...), stock, cost)`（import.rs:49-55）；C++ `Bin{bins.size(), container.id, container.Value(), stock.Value(), cost}`（cpp:633-634）。字段一一对应（idx 为过滤后序号双侧一致）。一致。
7. **MSPP `import_instance` 的 `Strip::new` 参数序（浅覆盖→补齐核验结论）**——Rust MSPP `Strip::new(max_width, fixed_height, cde_config, shape_modify_config, width)`（mspp/entities/strip.rs:22-27，签名即此序），import.rs:38-48 传 `(ext_strip.max_width, ext_strip.height, cde_config, cfg, ext_strip.max_width)`——初始 `width = max_width`。C++ `MSPInstance::Create(items, demands, maxWidth, fixedHeight, ...)`（cpp:684-687）内部 `MultiStrip::Create(maxWidth, fixedHeight, cdeConfig, shapeModifyConfig, maxWidth)`（MspInstance.cpp:66-67）——**参数序与初始宽完全一致**（maxWidth 在前、初始 width=maxWidth）。且 Rust MSPP Strip 语义确认为"固定高度、宽 ∈ (0, max_width]"（strip.rs:7 注释），与 SPP 的 Strip（固定高度、宽可超基准）同向，**无条带方向相反问题**。原记录 92 行判定成立，本条补齐 `MultiStrip::Create(maxWidth, fixedHeight, ..., maxWidth)` 的行号证据（MspInstance.cpp:66-67）并消除"参数序相反"的疑点。
8. **`ExtSPSolution`/`ExtBPSolution`/`ExtMSPSolution` 的 serde 必填语义（补齐解析侧证据）**——Rust 三解结构体所有字段无 default → 缺任一键即错；C++ `parseSPSolutionValue`（cpp:1473-1481）/`parseBPSolutionValue`（cpp:1614-1622）/`parseMSPSolutionValue`（cpp:1724-1732）`requiredMember` 逐键必填 + `rejectDuplicateFields`。一致（补齐 BPP/MSP 的行号证据，原记录只给了 SP 的）。
9. **`ExtMSPStrip` 字段名 wire（补齐）**——Rust `strips: ExtMSPStrip { height, max_width }`；C++ `parseMSPStripValue`（cpp:1639-1648）键 `height`/`max_width`、对象名报错串用 `"strips"`。键名一致。
10. **BPP `export` 的 `solution.cost(instance)`（补齐 C++ 行号）**——原记录 75 行证据充分，补充：C++ `external.cost = solution.Cost(instance)`（cpp:1816），Rust `solution.cost(instance)`（export.rs:10）。一致。
11. **MSPP `export` 的 containers `zones: vec![]`（补齐序列化侧）**——Rust `ExtContainer { ..., zones: vec![] }` 序列化经 `skip_serializing_if` **省略 zones 键**；C++ `externalRectangleContainer`（cpp:39-48）构造空 zones，`containerJson`（cpp:1224-1236）同样空时省略。一致（补齐证据）。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | min_item_separation 零值序列化 | ~~差异~~ → **已对齐（4540a76）** | 无条件输出；三模型一次修正 | JsonCodec.cpp:1430-1431 vs spp/ext_repr.rs:12-13 |
| S2 | 导出前 Validate* 校验 | ~~C++ 额外~~ → **已移除（291f057）** | 纯函数导出对齐；BPP binIndex 拒绝保留 | JsonCodec.cpp:1791-1792,1822-1827 |
| S3 | total_item_area 溢出诊断 | 差异（轻微） | C++ 显式 "item area is non-finite"；Rust inf 经 Strip::new 宽度校验失败 | spp/import.rs:38-46 vs Instance.cpp:54-60 |
| S4 | SPInstance::new 双重断言 | 基本等价（panic→Result） | 冗余防御层双侧都有，C++ 升格 | instance.rs:19-24 vs Instance.cpp:38-50 |
| S5 | MSPP Strip 参数序与初始宽 | 一致（补齐核验） | 双侧 `(maxWidth, fixedHeight, ..., width=maxWidth)`；Rust Strip 语义"固定高度、宽∈(0,max_width]"，无方向相反问题 | mspp/import.rs:38-48 + mspp/entities/strip.rs:22-27 vs MspInstance.cpp:66-67 |
### B8 Stage186：BPP/MSPP `import_solution` 不可用边界差分（2026-10-29）

- Rust 基线：Rust 1.98.1、jagua-rs `f5e2b5f`；RustOracle `--solution-import-b8-parity-only` 分别构造 BPP/MSPP 内部实例与 `ExtBPInstance`/`ExtMSPInstance` 外部 DTO，并捕获两个 `import_solution` 的 `unimplemented!()` panic；pinned 签名的第二参数实际是 instance DTO。
- Fixture：`JaguarCore/Tests/Fixtures/RustReference/solution_import_b8.txt`，Debug 与 Release 各含两个布尔项，Rust 两种配置输出均为 `true`。
- C++ 对照：`CompareProblem.cpp` 主动读取该 fixture；对 BPP/MSPP 分别调用 typed 与 JSON solution-import 入口。Debug/Release 两入口均返回 `UnsupportedOperation`，消息分别为 `BPP solution import is not implemented in the reference crate`、`MSPP solution import is not implemented in the reference crate`；比较器将 typed 与 JSON 均不可用映射到 Rust 的 unimplemented 标志。
- 验证：RustOracle Debug/Release fixture 生成结果相同；MSVC 2019 x64 JaguarCore Debug/Release 的 `JaguarCoreTests.exe --problem-differential-only` 均退出码 0。完整测试套件未由本阶段运行。
- 判定：在固定输入上，双侧“BPP/MSPP solution import 不可用”的行为一致；错误机制不等价（Rust panic，C++ `Result`/`UnsupportedOperation`），因此这是明确记录的安全错误映射，不构成一般输入域上的完整错误等价证明。其他 B8 JSON/SVG 与错误边界仍需逐项覆盖。

### B8 Stage187：JSON未知字段与重复字段边界（2026-10-29）

- 输入fixture：`JaguarCore/Tests/Fixtures/RustReference/json_metadata_b8_inputs.txt`由RustOracle与C++ comparator共用，避免双侧手工维护两份输入。
- RustOracle `--json-metadata-b8-parity-only`通过pinned Rust 1.98.1 / jagua-rs `f5e2b5f`将六种解析结果写入 `json_metadata_b8.txt`。Stage187解析探针在Debug/Release共十二行均匹配；Stage188追加三类序列化探针后，合并fixture共十八行。
- 六例结果：未知顶层字段、未知item元数据、重复未知顶层键、重复未知item元数据均接受；重复已知 `id`与`ExtRotation`未知字段均拒绝。C++ `CompareProblem.cpp`读取相同输入和profile fixture，并与Rust两种profile状态比较。
- 验证：`JaguarCoreTests.exe --problem-differential-only`在MSVC 2019 x64 Debug与Release均退出码0；Release通过`/UNDEBUG`与`TestReleaseAsserts.h`保持断言。
- 裁决：将原“C++对未知字段更严格”的泛化结论改为六例范围内双侧一致；表中零值分离字段和导出校验两项旧差异也已按当前源码状态更新。其他JSON结构仍需独立覆盖，B8仍部分完成。
### B8 Stage188：`min_quality=None`的JSON序列化对齐（2026-10-29）

- RustOracle复用`json_metadata_b8_inputs.txt`中的三个item输入：字段缺省、显式`null`、`Some(2)`。pinned serde往返后，前两者都输出`min_quality:null`，第三者输出数值2；Debug/Release结果一致。
- 差分回归先复现了C++遗漏：`ExternalItem::minQuality`无值时，`itemJson`省略了`min_quality`键。typed parser把缺省和显式null都映射为无值，因此两条路径都有相同差异。
- C++修正：`itemJson`在`minQuality`无值时写出JSON`null`；有值时继续输出整数。`CompareProblem.cpp`比较三个序列化结果与Rust的18行合并profile fixture。
- 验证：MSVC 2019 x64 JaguarCore Debug/Release及测试runner重建成功；`JaguarCoreTests.exe --problem-differential-only`两配置退出码0，Release断言保持启用。
- 判定：`min_quality`缺省/null/有值的序列化字段语义在该item输入域内对齐；未据此声称其他Option字段或所有JSON结构都已覆盖，B8仍部分完成。

### B8 Stage189：SPP解宽度错误出口对齐（2026-10-29）

- Rust基线：Rust 1.98.1、jagua-rs `f5e2b5f`。`--solution-import-b8-parity-only`覆盖0、负值、负无穷、NaN、正无穷，以及0宽度同时引用未知item ID；Debug/Release中前四种和组合输入都先返回同一宽度错误，正无穷均在矩形构造时panic。
- C++修正：`Importer::ImportSPSolution`不再把所有非有限/非正宽度提前归为`SnapshotMismatch`；0、负值、负无穷及NaN继续进入`ChangeStripWidth`，`Strip::SetWidth`现返回与Rust相同的`InvalidInput: strip width must be positive`。正无穷保留单独拦截，避免C++下游几何构造崩溃。
- 差分与回归：扩展`solution_import_b8.txt`并由`CompareProblem.cpp`主动消费；`TestIo.cpp`另外覆盖上述五个类型化宽度输入，并以“零宽度+未知item”锁定宽度错误优先级。Rust Debug/Release oracle输出一致；JaguarCore Debug/Release及测试runner重建成功；`--problem-differential-only`和独立`runIoTests`探针两配置均退出码0。
- 判定：Rust可返回错误的无效宽度现已按错误文本对齐；正无穷仍是Rust panic与C++ `SnapshotMismatch`的安全失败通道差异。该结果只覆盖列出的固定宽度值，B8仍部分完成。

### B8 Stage190：SPP解导入JSON宽度边界（2026-10-29）

- 共享输入：新增`solution_import_b8_inputs.txt`，RustOracle与`CompareProblem.cpp`共用两个JSON输入：0宽度并引用未知item ID，以及`strip_width=3.5e38`。
- 0宽度组合：pinned Rust先在`change_strip_width`返回`strip width must be positive`，不进入未知item查找；C++ JSON解析成功，`ImportSPSolutionJson`返回同一`InvalidInput`错误，错误优先级一致。
- f32溢出组合：Rust serde把有限JSON数值`3.5e38`解为f32正无穷，随后SPP解导入触发panic；C++ `numberValue`在解析层发现double转f32后非有限，返回`SerializationFailure: strip_width must be finite`，不会进入Importer。该差异是明确的安全失败映射。
- 验证：RustOracle pinned Debug/Release均输出相同结果；MSVC 2019 x64 Debug/Release `--problem-differential-only`通过，活动比较器核对0宽度错误文案并断言溢出输入在Rust侧触发panic、C++侧被解析器拒绝。B8仍部分完成。

### B8 Stage191：SPP解JSON根节点与layout字段规则（2026-10-29）

- 共享输入：在`solution_import_b8_inputs.txt`中新增六种solution/layout JSON字段形态，由RustOracle和`CompareProblem.cpp`读取同一份输入。
- pinned Rust结果：未知solution根字段、重复未知根字段、未知layout字段、重复未知layout字段均解析成功；重复已知`strip_width`和重复已知`placed_items`均拒绝。Rust 1.98.1 / jagua-rs `f5e2b5f`的Debug/Release输出一致。
- C++对照：`ParseExternalSPSolutionJson`对六种输入得到相同接受/拒绝结果；`solution_import_b8.txt`每个profile新增六项，合计34条profile输出，比较器逐项消费并断言。
- 验证：MSVC 2019 x64 JaguarCore Debug/Release `JaguarCoreTests.exe --problem-differential-only`均退出码0，Release断言保持启用。此次仅核对六种固定JSON字段形态；其他外部结构和字段组合仍未覆盖，B8继续部分完成。

### B8 Stage192：SPP解JSON的PlacedItem与Transformation字段规则（2026-10-29）

- 共享输入：在`solution_import_b8_inputs.txt`追加八种嵌套PlacedItem/Transformation字段形态；RustOracle和`CompareProblem.cpp`共用同一JSON文本。
- pinned Rust结果：两层的未知字段与重复未知字段均接受；重复已知`item_id`、`transformation`、`rotation`、`translation`均拒绝。Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release逐项一致。
- C++对照：`ParseExternalSPSolutionJson`八例结果均匹配pinned Rust；`solution_import_b8.txt`扩为每个profile 25项、共50条profile输出，比较器逐项消费。
- 验证：RustOracle Debug/Release各25行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。覆盖限于SPP解对象中的这两层及所列字段，B8仍部分完成。

### B8 Stage193：SPP solution/layout全部已知字段的重复键拒绝（2026-10-29）

- 共享输入：补齐solution根节点的重复`layout`、`density`、`run_time_sec`，以及layout对象的重复`container_id`、`density`五种JSON输入；`strip_width`与`placed_items`已由前序阶段覆盖。
- pinned Rust结果：Serde对新增五种重复已知字段均拒绝，Debug/Release结果一致。
- C++对照：`ParseExternalSPSolutionJson`对五种输入均拒绝；`solution_import_b8.txt`扩为每个profile 30项、共60条profile输出，活动比较器逐项断言。
- 验证：RustOracle Debug/Release各30行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。至此SPP solution与layout的已知字段重复键固定矩阵覆盖完整；未知/重复未知字段及其他外部结构仍限于既定用例，B8继续部分完成。

### B8 Stage194：SPP实例与扁平item已知字段重复键（2026-10-29）

- 共享输入：`json_metadata_b8_inputs.txt`新增八例，覆盖`ExtSPInstance`的`name`、`min_item_separation`、`items`、`strip_height`，以及扁平`ExtItem`的`demand`、`orientation`、`shape`、`min_quality`重复键。
- pinned Rust结果：八例在serde中均作为重复已知字段拒绝，Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release一致。
- C++对照：`CompareProblem.cpp`经`ParseExternalSPInstanceJson`逐例比较；`json_metadata_b8.txt`扩为每个profile 17项、共34条profile输出，八例均匹配Rust。
- 验证：RustOracle Debug/Release各17行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。该轮只覆盖实例根和扁平item字段的重复键行为，其他JSON结构仍需单独覆盖，B8继续部分完成。

### B8 Stage195：ExtOrientation与ExtRotation重复键规则（2026-10-29）

- 共享输入：`json_metadata_b8_inputs.txt`新增四例，分别重复`ExtOrientation.rotation`、tagged `ExtRotation.mode`、`discrete`变体`angles`、`stepped`变体`step`。
- pinned Rust结果：四种重复已知字段均被serde拒绝；Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release一致。
- C++对照：`CompareProblem.cpp`经`ParseExternalSPInstanceJson`验证四例，`JsonCodec.cpp`的对应路径均拒绝重复键；`json_metadata_b8.txt`扩为每个profile 21项、共42条profile输出。
- 验证：RustOracle Debug/Release各21行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。覆盖仅限这四种rotation字段重复形态，B8继续部分完成。

### B8 Stage196：ExtShape与ExtPolygon已知字段重复键（2026-10-29）

- 共享输入：`json_metadata_b8_inputs.txt`新增八例，经SPP实例的item shape解析覆盖`type`、`data`、rectangle的`x_min/y_min/width/height`和polygon的`outer/inner`重复键。
- pinned Rust结果：八种重复已知字段均被serde拒绝；Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release一致。
- C++对照：`CompareProblem.cpp`经`ParseExternalSPInstanceJson`逐例检查，八例均拒绝；`json_metadata_b8.txt`扩为每个profile 29项、共58条profile输出。
- 验证：RustOracle Debug/Release各29行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。覆盖限于ExtShape/ExtPolygon所列字段；ExtContainer和quality zone仍待独立覆盖，B8继续部分完成。

### B8 Stage197：ExtContainer与ExtQualityZone字段规则（2026-10-29）

- 共享输入：`json_metadata_b8_inputs.txt`新增九例，经容器入口覆盖ExtContainer根字段`id/shape/zones`和ExtQualityZone字段`quality/shape`。
- pinned Rust结果：未知字段及重复未知字段均接受；重复的容器`id`、`shape`、`zones`及质量区`quality`、`shape`均拒绝。Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release一致。
- C++对照：`CompareProblem.cpp`使用`ParseExternalContainerJson`逐例比较，九例均匹配；`json_metadata_b8.txt`扩为每个profile 38项、共76条profile输出。
- 验证：RustOracle Debug/Release各38行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。B8仍部分完成，其他外部结构和更广输入组合尚未覆盖。

### B8 Stage198：BPP Bin与MSPP Strip字段规则（2026-10-29）

- 共享输入：新增13例，分别经`ExtBPInstance`覆盖扁平Bin的`id/shape/zones/stock/cost`及根`bins`，经`ExtMSPInstance`覆盖`strips.height/max_width`及根`strips`。
- pinned Rust结果：Bin与Strip中的未知字段、重复未知字段接受；Bin已知字段`id/shape/zones/stock/cost`、BPP根`bins`、Strip已知字段`height/max_width`及MSPP根`strips`重复时拒绝。Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release一致。
- C++对照：`CompareProblem.cpp`分别调用`ParseExternalBPInstanceJson`和`ParseExternalMSPInstanceJson`逐例比较，13例均匹配；`json_metadata_b8.txt`扩为每个profile 51项、共102条profile输出。
- 验证：RustOracle Debug/Release各51行与fixture顺序逐字匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。其他BPP/MSPP字段组合与外部结构仍需覆盖，B8继续部分完成。

### B8 Stage199：BPP/MSPP solution JSON根字段规则（2026-10-29）

- 入口核对：`ParseExternalBPSolutionJson`与`ParseExternalMSPSolutionJson`均为公开直接解析器；`ImportBPSolutionJson`和`ImportMSPSolutionJson`先解析JSON，再进入当前`UnsupportedOperation`出口，因此畸形字段输入不会被未实现状态短路。
- 共享输入：新增12例，覆盖BPP solution的`cost/layouts/density/run_time_sec`及MSPP solution的`layouts/containers/density/run_time_sec`重复键，并各覆盖未知字段与重复未知字段。
- pinned Rust结果：未知字段及重复未知字段接受，两个solution类型的全部列出字段重复时拒绝；Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release结果一致。
- C++对照：`CompareProblem.cpp`经两个solution直接解析器逐例比较；同时断言JSON Importer对重复已知字段先返回`SerializationFailure`，对合法JSON继续返回固定`UnsupportedOperation`。
- 验证：RustOracle Debug/Release各63行与fixture逐序匹配；MSVC 2019 x64定向runner Debug/Release重建及`--problem-differential-only`均退出码0，Release断言保持启用。BPP/MSPP solution JSON导入仍未实现；本阶段只补其可达解析规则，B8仍部分完成。

### B8 Stage200：BPP/MSPP实例根与扁平需求物品字段规则（2026-10-29）

- 共享输入：新增18例，经`ExtBPInstance`和`ExtMSPInstance`覆盖根未知/重复未知字段、`name/min_item_separation/items`重复字段，以及扁平需求物品的未知/重复未知字段和`id/demand`重复字段。
- pinned Rust结果：两类实例根及需求物品均接受未知字段和重复未知字段；重复的根已知字段与扁平需求物品`id/demand`均拒绝。Rust 1.98.1 / jagua-rs `f5e2b5f` Debug/Release一致。
- C++对照：`CompareProblem.cpp`通过`ParseExternalBPInstanceJson`和`ParseExternalMSPInstanceJson`逐例比较，18例全部匹配。
- 验证：RustOracle Debug/Release各81行与`json_metadata_b8.txt`逐序匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。当前fixture共81个共享输入、162条profile输出；BPP/MSPP其它字段组合及完整JSON序列化覆盖仍未闭合，B8保持部分完成。

### B8 Stage201：BPP/MSPP实例与solution JSON序列化（2026-10-29）

- 共享输入：新增四个带嵌套数据的实例/solution样例，覆盖BPP item、bin、quality zone、layout及MSPP item、strip、layout、container。
- pinned Rust：使用`serde_json::to_string`生成`ExtBPInstance`、`ExtMSPInstance`、`ExtBPSolution`、`ExtMSPSolution`四类输出基准。
- C++对照：分别调用四个`SerializeExternal*Json`入口；将Rust序列化结果与C++结果解析后规范化比较，并检查实例、solution及嵌套输出必需字段存在，不比较对象键顺序或浮点文本格式。
- 验证：RustOracle Debug/Release各85行与fixture逐序匹配；MSVC 2019 x64定向runner Debug/Release重建及`--problem-differential-only`均退出码0，Release断言保持启用。当前fixture共85个共享输入、170条profile输出；B8仍部分完成，其他输入组合和SVG几何层对拍仍需继续。

### B8 Stage202：BPP/MSPP扁平需求物品剩余已知字段（2026-10-29）

- 共享输入：新增六例，在BPP/MSPP扁平需求物品中分别重复`orientation`、`shape`和`min_quality`。
- pinned Rust结果：两类`ExtItem`均拒绝这三种重复已知字段；Debug/Release一致。
- C++对照：通过`ParseExternalBPInstanceJson`与`ParseExternalMSPInstanceJson`逐例比较，六例全部匹配。
- 验证：RustOracle Debug/Release各91行与fixture逐序匹配；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。累计91个共享输入、182条profile输出。扁平需求物品的已知字段重复矩阵已覆盖`id/demand/orientation/shape/min_quality`；B8其他IO组合及SVG几何层对拍仍待继续。

### B8 Stage203：BPP/MSPP默认值与数值范围边界（2026-10-29）

- 共享输入：新增12例，覆盖BPP/MSPP缺省与显式`null`的`min_item_separation`、`f32`溢出间距/条带尺寸，以及需求、库存、成本超出`u64`范围。
- pinned Rust结果：缺省间距接受、`null`拒绝；有限JSON数值`3.5e38`可被serde解析并窄化为`f32 +Inf`；超出`u64`的整数均拒绝。Debug/Release一致。
- C++对照：缺省、`null`及四类整数溢出共8例与Rust匹配。另有4例`f32`溢出在Rust解析层接受、C++ `numberValue`因窄化结果非有限而以`SerializationFailure`拒绝，按既有安全失败边界记录，不宣称解析器逐项等价。BPP/MSPP的`min_item_separation=+Inf`会在Rust `with_min_item_separation`处返回错误；MSPP `Strip::new`只检查尺寸大于零，源码上会接受`+Inf`，但本阶段没有把它延伸为后续几何/求解等价结论。
- 验证：RustOracle Debug/Release各103项输出逐序匹配fixture；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均通过，Release断言保持启用。累计103个共享JSON输入、206条Rust profile输出。B8仍部分完成，未覆盖的外部结构组合及SVG几何层对拍继续保留。

### B8 Stage204：BPP Bin与MSP Strip必需字段边界（2026-10-29）

- 共享输入：新增14例；BPP Bin覆盖扁平Container的`id`、`shape`与Bin字段`stock`、`cost`各自缺省/`null`，并覆盖可缺省的`zones`缺省/`null`；MSP Strip覆盖`height`、`max_width`各自缺省/`null`。
- pinned Rust结果：上述12个必需字段缺省/`null`输入均拒绝；`zones`缺省接受、显式`null`拒绝。Debug/Release一致。
- C++对照：14例通过`ParseExternalBPInstanceJson`或`ParseExternalMSPInstanceJson`逐例对照，均匹配pinned Rust。
- 验证：RustOracle Debug/Release各117项输出逐序匹配fixture；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均通过，Release断言保持启用。累计117个共享JSON输入、234条Rust profile输出。结论限于这些Bin/Strip字段组合；其他IO结构与SVG几何层对拍仍待继续。

### B8 Stage205：BPP/MSPP错误类型与根集合形态（2026-10-29）

- 共享输入：新增11例，覆盖BPP Bin的字符串`id`/`shape`、小数`stock`、负数`cost`、对象`zones`及根`bins`为`null`/对象；覆盖MSP Strip的字符串`height`、布尔`max_width`及根`strips`为`null`/数组。
- pinned Rust结果：11例全部拒绝，Debug/Release一致。
- C++对照：新增样例经`ParseExternalBPInstanceJson`或`ParseExternalMSPInstanceJson`逐例比较，拒绝集合匹配pinned Rust。
- 验证：RustOracle Debug/Release各128项输出逐序匹配fixture；MSVC 2019 x64 JaguarCore Debug/Release `--problem-differential-only`均通过，Release断言保持启用。累计128个共享JSON输入、256条Rust profile输出。结论限于固定错误类型/根集合样例；其他IO结构与SVG几何层对拍仍待继续。

### B8 Stage206：BPP/MSPP实例根与需求item错误类型（2026-10-29）

- 共享输入：新增18例，覆盖BPP/MSPP实例根字段`name`错误类型、`min_item_separation`字符串、`items`为`null`/对象，以及扁平需求item的`id`字符串、`demand`小数、`orientation`/`shape`/`min_quality`字符串。
- pinned Rust结果：18例全部拒绝，Debug/Release一致；RustOracle每个profile的146项输出均与fixture逐项匹配。
- C++对照：18例由活动`CompareProblem.cpp`比较器经`ParseExternalBPInstanceJson`或`ParseExternalMSPInstanceJson`逐项消费，结果匹配Rust。MSVC 2019 x64 Debug/Release `--problem-differential-only`均通过，Release断言保持启用。
- 累计146个共享JSON输入、每个profile 146项输出（共292条）；本阶段未修改生产解析逻辑。B8仍为部分完成，结论限于当前固定JSON输入矩阵。
### B8 Stage207：BPP/MSPP嵌套orientation/rotation必需字段与tag边界（2026-10-29）

- 共享输入：新增24例，BPP/MSPP各12例，覆盖需求item缺少、`null`或空`orientation`，缺少、`null`或空`rotation`，`mode`缺少/`null`/未知值，continuous附加字段，以及discrete的`angles`、stepped的`step`缺少或为`null`。
- pinned Rust结果：24例全部拒绝，Debug/Release一致；RustOracle每个profile的170项输出均与fixture逐项匹配。
- C++对照：24例由活动`CompareProblem.cpp`经BPP/MSPP实例JSON parser逐项比较，结果匹配Rust。MSVC 2019 x64 Debug/Release `--problem-differential-only`均通过，Release断言保持启用。
- 累计170个共享JSON输入、每个profile 170项输出（共340条）；未修改生产解析逻辑。结论限于该固定嵌套旋转JSON矩阵，B8仍为部分完成。
### B8 Stage208：BPP/MSPP item的ExtShape与Rectangle字段边界（2026-10-29）

- 共享输入：新增34例，BPP/MSPP各17例。覆盖item的`shape`缺失/`null`，`type`缺失/`null`/数值/未知标签，Rectangle `data`缺失/`null`/数组，以及Rectangle的`x_min`、`y_min`、`width`、`height`各自缺失/`null`。
- pinned Rust结果：34例全部拒绝，Debug/Release一致；RustOracle每个profile的204项输出均与fixture逐项匹配。
- C++对照：34例由活动`CompareProblem.cpp`经BPP/MSPP实例JSON parser逐项比较，结果匹配Rust。MSVC 2019 x64 Debug/Release `--problem-differential-only`均通过，Release断言保持启用。
- 累计204个共享JSON输入、每个profile 204项输出（共408条）；未修改生产解析逻辑。Polygon顶点/孔洞结构和完整B8 IO范围仍待覆盖。
### B8 Stage209：BPP/MSPP嵌套Polygon payload边界（2026-10-29）

- 共享输入：新增46例，BPP/MSPP各23例。覆盖SimplePolygon的`data`类型、点tuple长度与坐标类型；Polygon的payload/`outer`/`inner`错误形态；MultiPolygon列表元素与嵌套字段错误形态。另含三种结构接受控制：空SimplePolygon点数组、缺省Polygon `inner`、空MultiPolygon列表，BPP/MSPP各一例。
- pinned Rust结果：40例拒绝、6例接受，Debug/Release一致；接受控制仅表示serde数据结构可反序列化，不表示空几何可导入或可参与求解。RustOracle每个profile的250项输出均与fixture逐项匹配。
- C++对照：46例由活动`CompareProblem.cpp`经BPP/MSPP实例JSON parser逐项比较，接受/拒绝集合与Rust相同。MSVC 2019 x64 Debug/Release `--problem-differential-only`均通过，Release断言保持启用。
- 累计250个共享JSON输入、每个profile 250项输出（共500条）。B8仍为部分完成；本阶段不推导一般几何或Importer等价。
### B8 Stage210：空Polygon payload经过BPP/MSPP Importer（2026-10-29）

- 范围：复用Stage209中6个JSON层接受的fixture，分别经过pinned Rust `import_bpp_instance`/`import_mspp_instance`与C++ `Importer::ImportBPInstanceJson`/`ImportMSPInstanceJson`，未新增解析输入。
- Rust结果：空SimplePolygon点数组及`inner`缺省、`outer=[]`的Polygon均返回无效几何；空MultiPolygon返回不支持形状。BPP/MSPP和Debug/Release分类一致。
- C++结果：前两类返回`ErrorCode::InvalidPolygon`，MultiPolygon返回`ErrorCode::UnsupportedShape`；与Rust归一后的`invalid_geometry`/`unsupported_shape`分类一致。诊断文字不同：Rust报告`Simple polygon must have at least 3 points: []`与`No support for multipolygons yet`，C++报告`simple polygon needs at least three points`与`item multipolygons are unsupported`。
- 验证：MSVC 2019 x64 Debug/Release `--problem-differential-only`均通过，Release断言保持启用；Rust Debug/Release各256项输出与fixture逐项匹配。累计250个JSON输入、512条profile输出（每profile含250条解析结果和6条Importer结果）。这些空payload仅能通过反序列化，不可作为可用几何；B8整体仍部分完成。
### B8 Stage211：有效Polygon顶层导入与导入后形状状态（2026-10-29）

- 共享输入：新增4个BPP/MSPP实例JSON，分别使用`SimplePolygon`和无孔`Polygon`表示同一个CCW矩形`[(0,0),(4,0),(4,2),(0,2)]`；BPP提供有效Bin，MSPP提供有效Strip。
- Rust参考：Rust 1.98.1、锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。四例在Debug/Release的解析结果和BPP/MSPP顶层Importer结果均为接受/`success`。每例另外输出导入后的原始轮廓和碰撞轮廓状态：顶点数、面积、质心和有序顶点坐标。
- C++对照：`CompareProblem.cpp`逐例调用BPP/MSPP顶层JSON Importer；验证原始形状保持4顶点、面积8、质心`(2,1)`，碰撞形状为4顶点、面积8、质心`(0,0)`，并比较两组有序顶点坐标。`json_metadata_b8.txt`扩为每个profile 268项、共536项。
- 验证：RustOracle Debug/Release各268行逐项生成fixture；MSVC 2019 x64 JaguarCore `--problem-differential-only` Debug/Release均退出码0，Release断言保持启用。
- 范围：本阶段确认四个固定BPP/MSPP矩形输入的成功导入和导入后几何状态，不推广为任意多边形、凹形、多孔形或一般Importer等价；B8仍部分完成。
### B8 Stage212：斜边凸形与凹形成功Importer对拍（2026-10-29）

- 共享输入：新增4个BPP/MSPP实例JSON。`SimplePolygon`使用CCW斜边凸四边形`[(0,0),(6,0),(5,3),(1,4)]`；无孔`Polygon`使用CCW L形`[(0,0),(5,0),(5,2),(2,2),(2,5),(0,5)]`。BPP Bin与MSPP Strip均为有效矩形边界。
- Rust参考：Rust 1.98.1、锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。四例在Debug/Release解析接受且顶层Importer均成功。斜边四边形原始面积17.5、质心约`(2.857142925,1.647619009)`；凹L形面积16、质心`(1.9375,1.9375)`。fixture同时记录原始与居中碰撞轮廓的顶点数、面积、质心和有序坐标。
- C++对照：活动`CompareProblem.cpp`对四例执行BPP/MSPP顶层JSON Importer，逐字段与Rust shape-state对拍。斜边碰撞轮廓的质心约为`(-0.000000073,0.000000050)`，保留f32平移舍入残差并由`AlmostEqualRustF32`匹配；凹形的顶点顺序与中心化轮廓也匹配。
- 验证：RustOracle Debug/Release各280项，fixture共560项；MSVC 2019 x64 JaguarCore `--problem-differential-only` Debug/Release均退出码0，Release断言保持启用。
- 范围：仅确认这两个固定轮廓经BPP/MSPP有效入口导入后的状态；不推广到任意凸/凹多边形、孔洞、任意坐标尺度或一般Importer等价。B8仍部分完成。

### B8 Stage213：BPP/MSPP正间距Offset导入复核（2026-10-29）

- 共享输入：新增4个矩形实例JSON，分别覆盖BPP/MSPP与`SimplePolygon`/无孔`Polygon`组合；`min_item_separation=0.5`，原始CCW矩形为`[(0,0),(4,0),(4,2),(0,2)]`。
- Rust参考：Rust 1.98.1、锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。四例在Debug/Release均解析接受且顶层Importer成功；原始形状保持4顶点、面积8，碰撞形状扩为68顶点、面积`11.196034431`，中心化质心约为`(0.000000005,0.000000020)`。
- C++对照：活动`CompareProblem.cpp`逐例调用BPP/MSPP顶层JSON Importer，检查碰撞轮廓面积大于原始面积，并将原始与碰撞轮廓的顶点数、面积、质心和有序顶点坐标与Rust Debug/Release逐字段对拍。
- 验证：RustOracle Debug/Release各292项且逐序匹配fixture，共584项；MSVC 2019 x64 JaguarCore `--problem-differential-only` Debug/Release均退出码0，Release断言保持启用。
- 范围：确认这四个固定矩形输入中正间距触发了物品碰撞轮廓Offset；没有单独比较BPP Bin收缩后的容器轮廓或MSPP Strip边界状态，也不推广为一般Offset算法等价。B8仍部分完成。

### B8 Stage214：正间距容器收缩与坍缩阈值（2026-10-29）

- 共享输入：新增4个JSON用例，覆盖BPP Bin宽度与MSPP Strip高度分别为精确临界值`0.5`和略高值`0.5009765625`，均设置`min_item_separation=0.5`。另复用Stage213的4个正常尺寸输入，比较容器原始与碰撞轮廓状态。
- Rust参考：锁定Rust 1.98.1与jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。四例解析均接受。BPP Bin宽度恰为0.5时，容器Deflate得到空轮廓，实例Importer返回`offset_failure`；略高时导入成功。MSPP Strip的实例Importer在两个尺寸上都成功；恰为0.5时后续Strip→Container返回`offset_failure`，略高时转换成功。
- 几何对照：Stage213的BPP 20×20 Bin碰撞面积为380.25，MSPP 20×10 Strip碰撞面积为185.25。临界上方样例的容器原始面积为2.00390625、碰撞面积为0.003417969；C++与Rust对拍原始/碰撞轮廓顶点数、面积、质心和有序顶点。
- C++对照：BPP沿顶层实例Importer读取Bin容器状态；MSPP先导入实例，再调用`BaseStrip().CreateContainer()`，分别保留实例导入与容器生成结果。C++ `OffsetFailed`与Rust空Offset结果按`offset_failure`归一后匹配。
- 验证：RustOracle Debug/Release各308项并逐序匹配fixture，共616项；MSVC 2019 x64 JaguarCore `--problem-differential-only` Debug/Release均退出码0，Release断言保持启用。
- 范围：仅覆盖矩形Bin/Strip、精确坍缩和略高于临界值的固定输入，不代表一般形状或所有Offset边界等价；B8仍部分完成。
### B8 Stage215：正间距下带孔容器与质量区的Importer状态（2026-10-29）

- 共享输入：新增3个BPP实例JSON，`min_item_separation=0.5`。成功例使用20×20 Polygon Bin、一个4×4内孔和一个2×2 quality 2质量区；失败例分别使用两点内孔、两点SimplePolygon质量区。
- pinned Rust结果：Rust 1.98.1与锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。三个JSON均通过反序列化；成功例顶层Importer返回`success`，两种退化形状均返回`invalid_geometry`。成功例原始/碰撞状态为：外轮廓面积400/380.25；quality 0洞4顶点、面积16/68顶点、面积20.196031570；quality 2质量区4顶点、面积4/68顶点、面积6.196032047。
- C++对照：活动`CompareProblem.cpp`调用BPP顶层Importer，逐字段比较外轮廓及quality 0/2原始与碰撞形状的顶点数、面积、质心和有序顶点；Debug/Release差分运行通过。两个退化形状都按`invalid_geometry`归一后匹配。
- 表示差异：Rust对此例保留10个quality索引槽位（包括8个空组），C++的`QualityZones()`与`OriginalQualityZones()`仅保存2个非空组。活动比较器按quality索引将C++稀疏组展开为10槽，仅比较组索引和几何状态；两侧内部存储布局不同，空组对仓内消费者的影响由Stage216另行裁决。
- 验证：Rust Debug/Release各315项逐序匹配fixture，共630项；累计269个JSON输入，其中262个解析输入、7个序列化专用输入。每个profile含262条解析结果、25条Importer结果、12条物品shape-state、6条外轮廓shape-state、1条容器及质量区shape-state、2条MSPP Strip转Container结果和7条序列化输出。MSVC 2019 x64 Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。
- 范围：仅覆盖该固定BPP孔洞/质量区组合及两类无效几何出口；正间距几何状态与错误分类匹配，不据此推断任意孔洞、质量区或Importer等价。B8仍部分完成。
### B8 Stage216：空质量组的消费者影响与API表示裁决（2026-10-29）

- pinned Rust源码：`io/import.rs`对`0..N_QUALITIES`逐级分组并无条件构造10个`InferiorQualityZone`；`Container::new`把传入组放入`[Option<InferiorQualityZone>; 10]`，因此Importer路径的每个槽位均为`Some`，未使用的质量等级对应空shape列表。`InferiorQualityZone::to_hazards`只映射`shapes_cd`中的shape，空组不产生hazard。
- C++源码：`Importer::ImportContainer`只把非空`grouped[quality]`写入`QualityZone` vectors；`Container::CreateWithOriginalQualityZones`只对每个zone的实际shape创建静态hazard。空组在Importer表示中缺席，也不会产生hazard。
- 消费者审查：Rust hazard collector/filter基于已生成hazard工作；Rust SVG遍历quality zone的shape列表，碰撞高亮按collision entity读取其所属quality和shape索引。C++ SVG对现存quality组按quality查找并遍历shape，碰撞高亮也只在已有zone hazard上按quality和zoneIndex查shape。对Importer生成的容器，空组不改变碰撞、质量筛选或SVG shape输出；Stage176固定质量边界fixture已验证质量过滤和碰撞高亮路径。
- 裁决：保留C++稀疏vector表示，不改生产数据结构。当前仓内算法消费者的空组语义一致；但Rust公开固定数组字段、C++公开返回vector的getter，调用方读取长度或槽位存在性时可观察10槽与仅非空组的差异。这是公开API表示差异，不宣称结构布局等价；API映射表已补充该边界。
- 范围：结论来自锁定revision源码和仓内调用点审查，不代表任意外部调用者的观察等价；外部调用者直接检查组数时仍需按各自API表示处理。B8仍部分完成。
### B8 Stage217：多质量组顺序与Hazard zoneIndex对拍（2026-10-29）

- 共享输入：新增1个BPP实例JSON，`min_item_separation=0.5`；40×30 Polygon Bin含两个按输入顺序排列的孔洞，另有一个显式quality 0区、三个输入顺序为右/中/左的quality 2区和一个quality 5区。quality 2项与其他quality交错排列。
- pinned Rust结果：Rust 1.98.1与锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。实例解析接受、Importer成功；输出外轮廓及quality 0/2/5原始和碰撞shape-state。真实`base_cde.collect_poly_collisions`探针映射为`explicit_q0=hole:0;hole_a=hole:1;hole_b=hole:2;q2_a=quality:2:0;q2_b=quality:2:1;q2_c=quality:2:2;q5=quality:5:0`。
- C++对照：活动`CompareProblem.cpp`通过BPP顶层Importer对拍各quality下原始/碰撞shape的数量、顺序和完整几何字段；再通过`Container::CreateCollisionEngine`与真实`CollectPolygonCollisions`逐区探测HazardKind、quality和zoneIndex。7个探针均返回且与Rust实体映射一致。
- 顺序结论：显式quality 0区先于两个追加的Polygon洞；洞按Polygon输入顺序得到index 1/2；交错输入中的三个quality 2区保持原相对顺序并得到index 0/1/2。该结果与Rust按质量筛选且保留原序、C++按quality稳定排序并保留同组相对序的源码规则一致。
- 验证：Rust Debug/Release各319项逐序匹配fixture，共638项；累计270个JSON输入（263个解析输入、7个序列化专用输入）。每profile含263条解析结果、26条Importer结果、12条物品shape-state、6条外轮廓shape-state、2条容器及质量区shape-state、1条Hazard probe映射、2条MSPP Strip转Container结果和7条序列化输出。MSVC 2019 x64 Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。`cargo fmt --check`仍因RustOracle其他既存位置的格式差异退出1，本轮新增代码段不在剩余差异列表中。
- 范围：确认该固定多洞、多质量组及正间距输入的shape顺序和hazard索引映射；不推广为任意Importer顺序或Overlay等价。B8仍部分完成。

### B8 Stage218：BPP多bin排序与零库存过滤（2026-10-29）

- 共享输入：新增1个BPP JSON，bin输入顺序为外部ID `30/5/20/10`；ID 5的stock为0，且其SimplePolygon只有2个点，作为若被错误导入就会失败的哨兵。其余三种bin分别带有可区分的容器几何、stock和cost。
- pinned Rust行为：`probs/bpp/io/import.rs`先按bin外部ID升序排序并检查ID唯一，再移除stock为0的bin；随后按保留顺序分配稠密index并导入容器。因此无效几何的零库存bin不会触发几何导入失败。
- C++对照：`CompareProblem.cpp`通过同一BPP JSON入口检查解析接受和Importer成功；明确断言导入结果为3个bin，依次为`index/id/stock/cost = 0/10/1/101、1/20/3/202、2/30/2/303`，面积依次为24/48/80，并将三种容器原始与碰撞shape完整状态对拍Rust Debug/Release。
- 验证：Rust Debug/Release各322项逐序匹配fixture，共644项；累计271个JSON输入（264个解析输入、7个序列化专用输入）。每profile含264条解析结果、27条Importer结果、12条物品shape-state、6条外轮廓shape-state、2条容器及质量区shape-state、1条Hazard probe映射、1条BPP bin集合状态、2条MSPP Strip转Container结果和7条序列化输出。MSVC 2019 x64 Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。
- 范围：确认一个固定BPP多bin输入中的ID排序、零库存过滤、稠密索引和对应容器状态；不据此推广为所有多bin输入的Importer等价。B8仍部分完成。

### B8 Stage219：重复bin ID与无正库存错误出口（2026-10-29）

- 共享输入：新增两个BPP JSON。其一让stock为0且含两点SimplePolygon的bin与同ID有效bin并存；其二包含两个唯一ID、但stock都为0的有效矩形bin。
- pinned Rust行为：两例JSON均可解析。Importer先检查所有输入bin的ID唯一性，再过滤零库存bin；因此第一例在跳过无效零库存几何前返回`duplicate_bin_ids`。第二例过滤后没有可用bin，返回`no_positive_bin_stock`。
- C++对照：`CompareProblem.cpp`比较Rust Debug/Release与C++的解析结果及规范化Importer错误类别，并显式断言C++错误码为`InvalidInput`。C++重复ID消息为`external bin IDs must be unique`，Rust为`bin IDs must be unique`；无正库存消息两侧均为`instance must have positive bin stock`。
- 验证：Rust Debug/Release各326项逐序匹配fixture，共652项；累计273个JSON输入（266个解析输入、7个序列化专用输入）。每profile含266条解析结果、29条Importer结果、12条物品shape-state、6条外轮廓shape-state、2条容器及质量区shape-state、1条Hazard probe映射、1条BPP bin集合状态、2条MSPP Strip转Container结果和7条序列化输出。MSVC 2019 x64 Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。
- 范围：锁定的是重复ID检测相对零库存过滤的顺序，以及全零库存出口；不据此推广为任意BPP多bin错误优先级等价。B8仍部分完成。

### B8 Stage220：BPP多bin整数边界与精确排序（2026-10-29）

- 共享输入：新增1个BPP JSON，三个bin按外部ID逆序/混合顺序输入，ID分别为`u64::MAX`、`9007199254740993`和`9007199254740992`；最大ID的stock与cost也为`u64::MAX`。
- pinned Rust行为：在当前x64 Rust目标上，三例数值均可解析并成功导入；按整数ID排序为`9007199254740992 < 9007199254740993 < 18446744073709551615`，最大stock和cost均保持原值。
- C++对照：同一Importer入口解析成功；比较器将导入后的`index/id/stock/cost`直接格式化为十进制文本，避免经过f32或double导致大整数失真。Debug/Release均匹配Rust的`0/9007199254740992/1/9007199254740994;1/9007199254740993/1/9007199254740995;2/18446744073709551615/18446744073709551615/18446744073709551615`。
- 验证：Rust Debug/Release各329项逐序匹配fixture，共658项；累计274个JSON输入（267个解析输入、7个序列化专用输入）。每profile含267条解析结果、30条Importer结果、12条物品shape-state、6条外轮廓shape-state、2条容器及质量区shape-state、1条Hazard probe映射、1条BPP bin集合状态、1条BPP整数元数据状态、2条MSPP Strip转Container结果和7条序列化输出。MSVC 2019 x64 Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。
- 范围：精确最大stock结论只适用于本次64位Rust与MSVC x64目标；不覆盖32位`usize`转换，也不据此推广为所有BPP整数边界等价。B8仍部分完成。

### B8 Stage221：BPP大整数JSON序列化往返（2026-10-29）

- 共享输入：复用Stage220的BPP多bin大整数输入，不增加JSON输入数。检查未导入排序的外部bin原始顺序`u64::MAX/9007199254740993/9007199254740992`。
- Rust参考：对外部`ExtBPInstance`执行序列化再反序列化；Debug/Release输出完整JSON序列化结果及bin整数状态。三个bin维持原始顺序，ID、stock和cost精确保留。
- C++对照：通过`ParseExternalBPInstanceJson → SerializeExternalBPInstanceJson → ParseExternalBPInstanceJson`往返；将Rust/C++完整JSON解析后规范化比较，并直接检查最大ID、stock、cost及大于`2^53`的整数文本和往返状态`id/stock/cost`。
- 验证：Rust Debug/Release各331项逐序匹配fixture，共662项；累计274个JSON输入（267个解析输入、7个序列化专用输入）。每profile有9条序列化输出；其他解析、Importer与shape-state行数保持Stage220计数。MSVC 2019 x64 Debug/Release `--problem-differential-only`均退出码0，Release断言保持启用。
- 范围：确认一个固定BPP外部实例的u64/usize大整数序列化往返和bin顺序；不推广为所有JSON数值或外部结构序列化等价。B8仍部分完成。
