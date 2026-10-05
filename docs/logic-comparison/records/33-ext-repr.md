# 33. ext_repr（外部表示）逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/io/ext_repr.rs`（184 行）
**C++**: `JaguarCore/Include/JaguarCore/io/ExternalRepr.h`（281 行）+ `JaguarCore/Source/IO/ExternalRepr.cpp`（51 行）
**判定**: wire 基本一致（字段集合与 JSON wire 格式一致；orientation 由嵌套 enum 扁平化为 kind+数据域的 `RotationPolicy`，并引入 `None` 哨兵，序列化 `None`→`discrete [0.0]`；C++ 额外包含 probs 层的 Ext 结构体，实际 JSON 编解码在 JsonCodec.cpp，见记录 37）

> 说明：Rust 侧 `io/ext_repr.rs` 只定义 entities 层外部表示；SPP/BPP/MSPP 的 `ExtSPInstance` 等在 `probs/*/io/ext_repr.rs`（记录 37）。C++ 把全部外部表示集中放在 `ExternalRepr.h`，本记录只比对 entities 层部分（`ExternalRectangle`…`ExternalLayout`，h:17-173），probs 部分（h:176-280）在记录 37 覆盖。JSON 序列化/反序列化行为在 `JsonCodec.cpp`，本记录只关注数据模型形状；wire 行为细节在记录 35/37 覆盖。

## 逐条比对（结构体）

### `ExtItem`（Rust 6-16 行）
- C++ 对应：`ExternalRepr.h:114-124` 的 `struct ExternalItem`。
- 判定：基本等价（字段一一对应，`orientation` 建模不同）。
- 证据：
  - `id: u64` ↔ `ItemId id = 0`（h:117）；
  - `orientation: ExtOrientation` ↔ `RotationPolicy rotation{}`（h:119）——见下条；
  - `shape: ExtShape` ↔ `ExternalShape shape{}`（h:121）；
  - `min_quality: Option<usize>` ↔ `std::optional<std::size_t> minQuality`（h:123）。
- Rust 的 `Deserialize` 实现（43-81 行）额外拒绝 legacy 字段 `allowed_orientations`/`allowed_rotations`/`allowed_reflection_axes` 并允许无关元数据字段；C++ 在 `JsonCodec.cpp:1130-1146`（`parseItemValue` 的 `rejectDuplicateFields` + legacy 检查）实现同一语义，但 C++ 对**未知字段**整体拒绝（对象成员必须在已知集合内才能通过各 `rejectDuplicateFields` + required 组合校验），Rust serde 默认忽略未知字段——该 wire 差异属 JsonCodec 范畴（记录 35/37），此处存档。

### `ExtOrientation`（Rust 19-24 行）
- C++ 对应：无独立结构体，扁平化为 `ExternalItem::rotation`（`RotationPolicy`，h:94-111）。
- 判定：差异（建模方式）。
- 证据：Rust 为嵌套结构 `ExtOrientation { rotation: ExtRotation }`，serde 生成 `{"orientation": {"rotation": {...}}}`；C++ `RotationPolicy` 直接挂在 item 上，`JsonCodec.cpp:1121-1123` 的 `rotationJson` 在序列化时手工重建 `orientation` 包裹层：
```cpp
JsonValue::Object orientation;
orientation.emplace("rotation", JsonValue(std::move(rotationObject)));
```
解析侧 `parseRotationValue`（JsonCodec.cpp:1007-1090）同样要求外层对象只含 `rotation` 一个键（cpp:1016-1019）。wire 兼容，内存表示不同。

### `ExtRotation`（Rust 28-41 行，`Discrete`/`Stepped`/`Continuous` 三变体）
- C++ 对应：`RotationPolicyKind` 枚举（h:81-91）+ `RotationPolicy` 的 `anglesDegrees`/`stepDegrees` 数据域（h:98-101）。
- 判定：差异（多出 `None` 哨兵）。
- 证据：
  - Rust 三个变体：`Discrete { angles: Vec<f32> }`、`Stepped { step: f32 }`、`Continuous {}`，注释明确"No null or empty-list sentinel is used"（39 行）。
  - C++ 四个 kind：`None`/`Discrete`/`Stepped`/`Continuous`（h:83-90），`None` 是内部哨兵（对应内部 `RotationRange::None`），不出现在 wire 上：`rotationJson`（JsonCodec.cpp:1097-1101）把 `None` 序列化为 `{"mode":"discrete","angles":[0.0]}`：
```cpp
case RotationPolicyKind::None:
    rotationObject.emplace("mode", JsonValue(std::string("discrete")));
    rotationObject.emplace("angles", JsonValue(JsonValue::Array{JsonValue(0.0)}));
```
  - 解析侧不存在逆映射：输入 `discrete [0.0]` 经 `Importer::ImportRotation`（Importer.cpp:387-389）归一化为内部 `RotationMode::None`，但 `ExternalItem.rotation` 本身保持 `Discrete`——即 Rust 的 `[0.0] → RotationRange::None` 规则（import.rs:238-239）在 C++ 拆到了 Importer 层，表示层多了个 Rust 没有的可构造状态。
  - 数值类型：Rust `Vec<f32>`/`f32`；C++ `std::vector<double>`/`double`（h:99,101）。JSON 解析时 C++ `numberValue` 会先收窄到 f32 再存 double（JsonCodec.cpp:728），数值语义一致。

### `ExtShape`（Rust 97-114 行，tag="type" content="data" 四变体）
- C++ 对应：`ExternalShape` + `std::variant`（h:53-78）。
- 判定：一致。
- 证据：`Rectangle { x_min, y_min, width, height }` ↔ `ExternalRectangle`（h:17-27）；`SimplePolygon(ExtSPolygon)` ↔ `ExternalSimplePolygon`（h:30-34）；`Polygon(ExtPolygon)` ↔ `ExternalPolygon`（h:37-43）；`MultiPolygon(Vec<ExtPolygon>)` ↔ `ExternalMultiPolygon`（h:46-50）。wire tag `type`/`content` `data` 由 `JsonCodec.cpp:873-953`（`parseShapeValue`，类型串 `rectangle`/`simple_polygon`/`polygon`/`multi_polygon`）与 `shapeJson`（cpp:967-1005）对称实现。静态工厂（ExternalRepr.cpp:7-29）为 C++ 额外的构造便利层，无 Rust 对应，不构成行为差异。

### `ExtPolygon`（Rust 117-124 行）
- C++ 对应：`ExternalPolygon`（h:37-43）。
- 判定：一致。
- 证据：`outer: ExtSPolygon` + `#[serde(default)] inner: Vec<ExtSPolygon>` ↔ `outer` + `holes`（默认空，`Polygon(..., holes = {})` h:74）。字段名 wire 上 Rust 是 `inner`，C++ JSON 层同样用 `"inner"`（JsonCodec.cpp:848-869），内存命名 `holes` 与 Rust 容器 holes 语义对齐。

### `ExtSPolygon`（Rust 129 行，`Vec<(f32, f32)>` 元组结构）
- C++ 对应：`ExternalSimplePolygon::points: std::vector<Point2d>`（h:33）。
- 判定：一致（形态差异：独立类型 vs 仅作为 `simple_polygon` 变体载荷；Rust 该类型同时充当 polygon 顶点列表，C++ 直接用 `std::vector<Point2d>` 承载 `outer`/`holes` 元素）。

### `ExtQualityZone`（Rust 132-138 行）
- C++ 对应：`ExternalQualityZone`（h:127-133）。
- 判定：一致。`quality: usize` ↔ `std::size_t quality`；`shape: ExtShape` ↔ `ExternalShape shape{}`。

### `ExtContainer`（Rust 85-94 行）
- C++ 对应：`ExternalContainer`（h:136-144）。
- 判定：基本等价（`zones` 的 skip 语义由 JSON 层承担）。
- 证据：`id: u64` ↔ `ContainerId id`；`shape` ↔ `shape`；`#[serde(skip_serializing_if = "Vec::is_empty", default)] zones` ↔ `std::vector<ExternalQualityZone> zones`——C++ 结构体无 skip 标记，但 `containerJson`（JsonCodec.cpp:1224-1236）在 `zones` 非空时才输出，解析侧 `zones` 缺省为空（cpp:1255-1258），wire 行为一致。

### `ExtLayout`（Rust 143-150 行）
- C++ 对应：`ExternalLayout`（h:165-173）。
- 判定：一致。`container_id: u64` ↔ `ContainerId containerId`；`placed_items: Vec<ExtPlacedItem>` ↔ `placedItems`；`density: f32` ↔ `double density`。

### `ExtPlacedItem`（Rust 154-160 行）
- C++ 对应：`ExternalPlacedItem`（h:156-162）。
- 判定：一致。`item_id` ↔ `itemId`；`transformation: ExtTransformation` ↔ `transformation`。

### `ExtTransformation`（Rust 163-169 行）
- C++ 对应：`ExternalTransformation`（h:147-153）。
- 判定：一致。`rotation: f32`（度）↔ `rotationDegrees`；`translation: (f32, f32)` ↔ `Point2d translation`。

### `impl From<DTransformation> for ExtTransformation`（Rust 171-178 行）
- C++ 对应：`ExternalLayoutFromLayout` 中的内联换算（JsonCodec.cpp:1769-1770 `detail::ToDegreesRustF32`）。
- 判定：基本等价（无独立转换函数；`ToDegreesRustF32` 用 f32 常数乘法模拟 Rust `to_degrees()`，见 RustF32.h:12-17）。

### `impl From<ExtTransformation> for DTransformation`（Rust 180-184 行）
- C++ 对应：`Importer.cpp:98-102` 的 `radiansRustF32`（私有助手）+ `externalToInternal`（cpp:131-151）中的度→弧度换算。
- 判定：基本等价（`degrees * (pi / 180.0F)` 在 f32 域计算后升为 double，与 Rust `to_radians()` 的 f32 结果一致；同样无独立公共转换函数）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `ExtOrientation` 嵌套结构 | 差异 | 内存建模不同；wire 由 JsonCodec 手工补 `orientation` 包裹层，格式一致 | ext_repr.rs:19-24 vs ExternalRepr.h:94-111 + JsonCodec.cpp:1121-1123 |
| 2 | `ExtRotation` 三变体 vs `RotationPolicyKind` 四值（多 `None`） | 差异 | C++ 表示层多一个内部哨兵状态；序列化 `None`→`discrete [0.0]`，`[0.0]→None` 归一化移到 Importer | ext_repr.rs:30-41 vs ExternalRepr.h:83-97 + JsonCodec.cpp:1097-1101 + Importer.cpp:387-389 |
| 3 | f32 vs double 存储 | 差异（数值路径） | C++ 全部用 double 存储，但 JSON 解析先经 f32 收窄（JsonCodec.cpp:728 `static_cast<float>`），数值语义与 Rust f32 对齐；直接以 API 构造 `ExternalItem` 时 double 全精度可能偏离 Rust | ext_repr.rs:33,38 vs ExternalRepr.h:99,101 |
| 4 | legacy orientation 字段拒绝位置 | 基本等价 | Rust 在 `Deserialize`（含 flatten demand item 场景）拒绝；C++ 在 `parseItemValue` 拒绝，覆盖含 demand 的所有入口；未知字段容忍度不同（serde 忽略未知字段 vs C++ 逐级已知字段校验） | ext_repr.rs:53-66 vs JsonCodec.cpp:1130-1146 |
| 5 | `zones` 空时省略输出 | 一致 | Rust `skip_serializing_if` ↔ C++ `containerJson` 条件输出 | ext_repr.rs:92 vs JsonCodec.cpp:1224-1236 |
| 6 | 形状/容器/布局/放置项/变换各结构体 | 一致 | 字段与 wire 名逐一对应 | ext_repr.rs:85-169 vs ExternalRepr.h:127-173 |
| 7 | `ExternalRepr.cpp` 静态工厂（51 行） | C++ 额外 | 纯构造便利层（`Rectangle`/`Polygon`/`None` 等），无行为 | ExternalRepr.cpp:7-50 |

## 结论

entities 层外部表示的字段集合、wire 名称与 JSON 形状双侧一致，可互操作。核心建模差异是 orientation：Rust 的嵌套 `ExtOrientation`+三变体 `ExtRotation` 在 C++ 扁平化为 `RotationPolicy`（kind + angles/step 数据域）并增加 `None` 哨兵，序列化侧以 `discrete [0.0]` 保持 wire 兼容，`[0.0]→None` 的归一化从表示层移到 Importer。C++ 用 double 存储（解析时经 f32 收窄）与静态工厂构造层为附加形态，不影响 wire 一致性。

### 补充审计（2026-09-29）

对 ext_repr.rs 184 行逐行复核（serde 语义含 Serialize 侧），原记录遗漏/需修正各点：

1. **`ExtRotation` 的 `deny_unknown_fields`（serde 属性，ext_repr.rs:29-30）在序列化侧的对照缺失**——原记录只比对了反序列化侧的未知字段拒绝。C++ 解析侧三个 mode 分支分别用 `object->size() != 1 / != 2` 显式拒绝额外键（JsonCodec.cpp:1036-1039 `continuous`、cpp:1048-1051 `discrete`、cpp:1075-1078 `stepped`），与 serde enum 变体的 deny_unknown_fields 等价（`Continuous` 空变体只能有 `mode` 一个键，`Discrete` 只能 `mode+angles`，`Stepped` 只能 `mode+step`）。补齐后判定：一致（接受边界）。注意 serde tagged enum 本身默认拒绝未知字段（变体字段集固定），`deny_unknown_fields` 在此处是显式化声明。
2. **`ExtRotation::Discrete` 序列化侧的"透传不清洗"语义（浅覆盖→补齐）**——Rust `Serialize` 派生对 `angles: Vec<f32>` 原样输出（ExtItem 的 `ExtRotation` 无 `Serialize` 派生，实际经由 `rotationJson`；但 Rust 侧 `ExtSPInstance` 等容器序列化路径中 `ExtItem` 本身只有 `Deserialize` 手工实现、`Serialize` 派生存在——派生序列化会原样输出 angles，不排序不去重不归一化）。C++ `rotationJson`（JsonCodec.cpp:1102-1110）同样原样输出 `anglesDegrees`。一致：**序列化是表示层的透传**，排序/去重/`rem_euclid` 归一化只发生在 Importer 层（记录 34）。原记录已隐含此点，此处显式化，防止读者误以为 `rotationJson` 会做归一化。
3. **`ExtItem::min_quality` 的 `Option` 序列化 skip 语义（遗漏）**——Rust `Option<usize>` 无 `skip_serializing_if`，`None` 时 serde 输出 `"min_quality": null`；C++ `itemJson`（JsonCodec.cpp:1184-1185）在 `minQuality` 无值时**整个键省略**（不输出 null）。这是原记录遗漏的 wire 差异：Rust 序列化 ExtItem（作为独立 JSON，Rust 生态中该路径实际未用于实例导出——实例导出走 spp 层 demand item flatten，而 flatten 中 `Option` 同样输出 null）↔ C++ 键省略。读侧：C++ 解析侧（cpp:1160-1174）把 `null` 显式视为"无 min_quality"（`holds_alternative<nullptr_t>` 检查），缺省键同样无值——即 **C++ 读侧同时接受 null 与缺省两种形态；Rust serde 读侧 `Option<usize>` 也同时接受 null 与缺省**。故读侧双侧一致，写侧差异（null vs 省略键）仅出现在直接序列化单个 ExtItem 的路径上。判定：差异（写侧形态，读侧兼容）。
4. **`ExtContainer::zones` 的 `#[serde(default)]`（读侧）与 `skip_serializing_if`（写侧）组合（已有条目，补充读侧细节）**——原记录差异 5 只写了写侧。读侧：Rust `#[serde(default)]` 缺省空 vec；C++ `parseContainerValue`（cpp:1255-1258）`zones` 键缺省时保持空。一致。
5. **`ExtPolygon::inner` 的 `#[serde(default)]`（读侧）写侧行为（遗漏）**——Rust 序列化 `ExtPolygon` 时 `inner` **总是输出**（无 skip 属性，空 vec 输出 `"inner": []`）；C++ `polygonJson`（JsonCodec.cpp:958-964）**无条件输出 `inner` 键**（空时输出空数组）。一致（补齐证据：双侧都输出空数组，此点与 `zones` 的条件输出形成对照，易混淆，显式化）。
6. **`ExtShape::MultiPolygon` 序列化（原记录判定"一致"未给序列化侧逐字段证据，补齐）**——Rust `MultiPolygon(Vec<ExtPolygon>)` 序列化为 `{"type":"multi_polygon","data":[{outer,inner},...]}`；C++ `shapeJson` 多边形分支（JsonCodec.cpp:995-1001）同构。一致。
7. **`impl From<ExtTransformation> for DTransformation`（180-184 行）与 `From<DTransformation>`（171-178 行）的调用方补充**——原记录给出 JsonCodec/Importer 的内联对应，另核实 spp 解导入路径（Rust import_solution import.rs:71-75 显式调用 `DTransformation::from(ext_placement.transformation)`；C++ `externalToInternal` cpp:134-148 直接读 `external.rotationDegrees/translation` 度→弧度）。一致。
8. **`ExtLayout`/`ExtPlacedItem`/`ExtTransformation` 无 deny_unknown_fields（遗漏的对照点）**——Rust 这三个结构体 serde 默认**忽略未知字段**；C++ `parseLayoutValue`/`parseTransformationValue`（cpp:1302,1274）的 `rejectDuplicateFields` 只查重复键，未知键因 `object->size()` 未被校验而在 C++ 侧同样被忽略（与 `parseRotationValue` 的 size==N 校验不同——layout/transformation 层无 size 校验）。**修正原记录差异 4 的表述**：C++ 对 layout/transformation 层的未知字段同样容忍，并非"整体拒绝未知字段"；只有 item/orientation/rotation/shape/quality zone/bin/instance/solution 层有 size 校验拒绝未知键。判定修正：未知字段容忍度**按层级不同**，两侧在 layout/transformation 层一致（均忽略），在 item/orientation 层 C++ 更严。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | `min_quality` 为 None 时：Rust 输出 `null`，C++ 省略键 | 差异（写侧） | 读侧双侧兼容（均接受 null/缺省） | ext_repr.rs:15 vs JsonCodec.cpp:1160-1174,1184-1185 |
| S2 | 未知字段容忍度按层级不同（layout/transformation 层双侧均忽略） | 修正原差异 4 | 原记录"C++ 整体拒绝未知字段"表述过强 | ext_repr.rs:142-169 vs JsonCodec.cpp:1274,1302（无 size 校验） |
| S3 | `ExtPolygon.inner` 写侧无条件输出空数组 | 一致（显式化） | 与 zones 的条件输出形成对照 | ext_repr.rs:122-123 vs JsonCodec.cpp:958-964 |
| S4 | ExtRotation 各变体额外键拒绝（size 校验） | 一致 | continuous/discrete/stepped 三分支逐一对照 | ext_repr.rs:29-30 vs JsonCodec.cpp:1036-1078 |
