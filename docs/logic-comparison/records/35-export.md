# 35. export（导出）逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/io/export.rs`（48 行）
**C++**: `JaguarCore/Source/IO/JsonCodec.cpp` 导出部分（`ExternalLayoutFromLayout` 1747-1777 行、`ExternalLayoutFromSnapshot` 29-37 行）+ `JsonCodec.h:29-47` 声明
**判定**: wire 基本一致（导出换算公式一致；SPP container_id 已对齐为"传参 0/容器实际 id"双路径；C++ 导出前强制校验与 Result 化为额外）

## 逐条比对（函数）

### `export_layout_snapshot(layout, container_id) -> ExtLayout`（Rust 8-30 行）
- C++ 对应：`ExternalLayoutFromLayout(const Layout&, optional<ContainerId>)`（JsonCodec.cpp:1747-1777）+ 内部 `ExternalLayoutFromSnapshot`（cpp:29-37）。
- 判定：基本等价（字段与换算一致；C++ 无快照入口版本差异）。

逐项对照：
- **container_id 语义**：Rust 签名即"caller supplies the external bin/sheet ID"（export.rs:6 注释），`ExtLayout { container_id, ... }` 直接使用参数（export.rs:25-26）。C++ `containerId.value_or(layout.GetContainer().Id())`（cpp:1751）：调用方未传时用容器自身保留的 id（import 时写入的外部 id，见记录 34 差异 5）。 probs 层调用形态见记录 37（SPP 传 `solution.layoutSnapshot.container.Id()`，即 Rust 硬编码 0 ↔ C++ 容器 id——SPP 路径 `Strip::CreateContainer` 默认 `id=0`（Strip.cpp:40，spp.h:35 默认参数），故双侧实际输出都是 0；差异仅在"显式 0 vs 默认 0 的容器 id"这一取值路径）。
- **placed_items 枚举**：Rust `layout.placed_items.values().map(...)`（export.rs:9-23，按 SlotMap 插入序）↔ C++ `snapshot.placements`（cpp:1754-1756，快照数组序）。放置项顺序双侧都由插入顺序决定（Rust SlotMap 值迭代序 = 插入槽序；C++ `Layout::Save` 按放置顺序生成 placements），一致（细节见记录 26）。
- **每项变换**：`int_to_ext_transformation(&pi.d_transf, &item.shape_orig.pre_transform)` → `ExtPlacedItem { item_id: item.external_id, transformation: abs_transf.into() }`（export.rs:15-21）↔ C++ cpp:1759-1774：`preTransform.FollowedByRustF32(placedItem.Transform())`、`{placedItem.GetItem().ExternalId(), {ToDegreesRustF32(...), transform.translation}}`。一致（公式见下条）。
- **非有限拒绝**：C++ 在变换非有限时返回 `SerializationFailure`（cpp:1762-1768）；Rust `export_layout_snapshot` 无对应显式校验。若非有限f32到达锁定的`serde_json 1.0.151`序列化器，该字段写为JSON `null`，不会在此处报错。因此这是异常数据的出口差异；本记录未证明非有限值经过完整Rust布局导出链的可达性。
- **density**：`layout.density()`（export.rs:28）↔ `layout.Density()`（cpp:1752）。一致（计算见记录 26）。

### `int_to_ext_transformation(int_transf, pre_transf)`（Rust 37-48 行）
- C++ 对应：`ExternalLayoutFromLayout` 内联（JsonCodec.cpp:1759-1761）。
- 判定：一致（合成公式相同）。
- 证据：Rust：
```rust
//1. apply the pre-transform
//2. apply the internal transformation
Transformation::empty()
    .transform_from_decomposed(pre_transf)
    .transform_from_decomposed(int_transf)
    .decompose()
```
（export.rs:44-47）↔ C++ `item.GetOriginalShape().preTransform.FollowedByRustF32(placedItem.Transform())`，即 `T_int ∘ T_pre`（先施加预变换再内部变换），与 Rust `transform_from_decomposed` 逐次左乘语义一致（复合矩阵乘法次序同为后参数先作用）。这是记录 34 中 `ext_to_int_transformation`（`T_ext ∘ T_pre⁻¹`）的逆过程，双侧互逆关系保持。

### 度数换算（`From<DTransformation> for ExtTransformation`，Rust ext_repr.rs:171-178）
- C++ 对应：`detail::ToDegreesRustF32(transform.rotationRad)`（JsonCodec.cpp:1770，RustF32.h:12-17，f32 常数 `57.2957795130823208767981548141051703F` 乘法）。
- 判定：一致（与 Rust `f32::to_degrees()` 的 f32 域结果对齐）。

### `ExternalLayoutFromSnapshot`（JsonCodec.cpp:29-37，C++ 内部助手）
- 判定：C++ 额外（快照→Layout 重建→`ExternalLayoutFromLayout` 的桥接；Rust `export_layout_snapshot` 直接吃 `&LayoutSnapshot`、`Layout::from_snapshot` 由 SVG 路径承担——Rust 的 export 直接从快照读 `placed_items`，C++ 先 Restore 成 Layout 再 Save。中间重建无副作用，行为等价）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | container_id 取值路径 | 差异（形式） | Rust 导出函数强制调用方传 id（SPP 传 0）；C++ 默认用容器保留 id，SPP 调用点传 `container.Id()`。SPP 容器经 `Strip::CreateContainer(默认 id=0)` 创建，实际输出双侧均为 0 | export.rs:8,25 vs JsonCodec.cpp:1751,1798 + Strip.cpp:40/spp.h:35 |
| 2 | placed_items 顺序 | 一致 | 双侧均按放置顺序（SlotMap 槽序 ↔ placements 数组序，见记录 26） | export.rs:9 vs JsonCodec.cpp:1754-1756 |
| 3 | pre_transform 合成公式 `T_int ∘ T_pre` | 一致 | 与 import 侧逆公式互逆，双侧对称 | export.rs:44-47 vs JsonCodec.cpp:1759-1761 |
| 4 | 度数换算 f32 域 | 一致 | `ToDegreesRustF32` 复刻 `to_degrees()` | ext_repr.rs:174 vs RustF32.h:12-17 |
| 5 | 非有限变换拒绝 | C++ 额外 | C++导出入口返回`SerializationFailure`；Rust转换无显式拒绝，若非有限f32到达serde_json 1.0.151则序列化为`null`。完整Rust导出链可达性未证 | export.rs:15-21 vs JsonCodec.cpp:1782-1797；serde_json 1.0.151 ser.rs:155-165 |
| 6 | 返回类型 `ExtLayout` vs `Result<ExternalLayout>` | 基本等价 | panic→Result 约定的体现；Rust `#[must_use]` 直接返回 | export.rs:8 vs JsonCodec.cpp:1747 |
| 7 | min_item_separation 零值序列化 | 差异（实例层，关联项） | Rust 实例序列化总是输出 `min_item_separation`（无 skip 属性，probs/*/io/ext_repr.rs `#[serde(default)]` 只管读侧）；C++ `commonInstanceJson` 仅在 `separation != 0.0` 时输出（JsonCodec.cpp:1431-1432）。本条属 probs 实例导出（记录 37），因跨记录高频提及在此交叉引用 | spp/io/ext_repr.rs:12-13 vs JsonCodec.cpp:1425-1435 |

## 结论

导出侧核心逻辑（放置项枚举、`T_int ∘ T_pre` 合成、度数换算、density、item_id）对有限布局双侧一致，SPP container_id 经"传参 0 ↔ 默认 id=0 的容器"两条路径实际输出相同。C++的非有限保护仅在异常变换出口体现；若非有限f32直接进入Rust serde_json序列化，则输出`null`。C++快照桥接与Result化属于接口形态差异。`min_item_separation`零值序列化差异位于实例层，详见记录37；数值文本格式不构成字节级对拍结论，详见记录38。

### 补充审计（2026-09-29）

对 export.rs 48 行逐行复核（2026-09-29 后 JsonCodec.cpp 有两处改动：`commonInstanceJson` 改为无条件输出 `min_item_separation`；三个 `External*FromSolution` 移除了导出前的 `Validate*Solution` 强制校验——见 git 4540a76/291f057），原记录遗漏/需修正各点：

1. **【记录过期】原记录差异表未收录"导出前校验已移除"（git 291f057，2026-09-29）**——原记录 17 行、44 行及记录 37 差异 10 所述"C++ 导出前 Restore + Validate*，无效解拒绝导出"**已不再成立**：当前 `ExternalSPSolutionFromSolution`/`ExternalBPSolutionFromSolution`/`ExternalMSPSolutionFromSolution` 只保留 `Restore`（cpp:1786-1790, 1810-1814, 1846-1850），`Validate*Solution` 调用已删除，替换为注释 `// jagua-rs export is a pure function over the solution struct; it does not re-validate strip/container consistency before serializing.`（JsonCodec.cpp:1791-1792）。这使 C++ 与 Rust 的"纯函数导出"语义**完全对齐**。本条差异由"C++ 额外"改判为"一致（291f057 后）"。注意 BPP 的 `binIndex` 越界拒绝（cpp:1822-1827）与变换非有限拒绝（cpp:1761-1767）**保留**，仍为 C++ 额外。
2. **`export_layout_snapshot` 的 `#[must_use]`（遗漏）**——Rust 标记纯函数；C++ 返回 `Result<ExternalLayout>`。形态差异，原记录差异 6 已涵盖，无新增。
3. **`ExtLayout` 字段构造顺序（遗漏的对照）**——Rust `ExtLayout { container_id, placed_items, density }`；C++ `ExternalLayout{}` 先赋 `containerId`/`density`（cpp:1750-1751）再填 `placedItems`（cpp:1754-1774）。JSON 键序由 `layoutJson`（cpp:1341-1348）固定为 `container_id/placed_items/density`，与 Rust serde 结构体字段序一致。键序对 JSON 语义无关，双侧 wire 兼容（补齐证据）。
4. **`int_to_ext_transformation` 的调用点唯一性（补齐）**——Rust 仅两处调用：export.rs:16 与 layout_to_svg.rs:280；C++ 对应 `ExternalLayoutFromLayout`（cpp:1758-1760）与 SvgExport.cpp:179-181。双侧调用点集合对称（记录 36 交叉引用）。
5. **`layout.density()`（export.rs:28）vs `layout.Density()`（cpp:1752）在快照路径下的取值（补齐）**——Rust 直接读 `LayoutSnapshot.density` 字段？核实：Rust `export_layout_snapshot` 参数为 `&LayoutSnapshot`，调用 `layout.density()` 实为 `LayoutSnapshot::density()`（entities 层快照方法）；C++ `ExternalLayoutFromLayout` 接 `const Layout&`、`layout.Density()`（cpp:1752）经 `Save()` 快照后由 Layout 计算——两条路径最终都源自快照 density 计算（记录 26 差异 5 的 C++ Density 缓存语义），数值一致。原记录 18 行已判一致，此处补充两侧类型路径（Snapshot 方法 ↔ Layout 方法经内部快照）。
6. **`ExternalLayoutFromSnapshot` 桥接（原记录已有，补充失败传播细节）**——C++ cpp:29-37：`Layout layout(snapshot.container)` + `Restore` 失败透传错误（不吞）；Rust 无此桥接（直接读快照字段）。Restore 只会把放置项逐个放回，无变换运算失败路径（变换非有限已在源头 ImportItem 时拒绝），桥接为纯结构重建。判定不变：C++ 额外、行为等价。
7. **`min_item_separation` 关联项更新（修正记录 37 差异 2/本记录差异 7 的现状）**——2026-09-29（git 4540a76）后 C++ `commonInstanceJson` **无条件输出** `min_item_separation`（JsonCodec.cpp:1430-1431，`object.emplace("min_item_separation", JsonValue(separation))`），与 Rust serde 序列化（无 skip 属性，总是输出）**一致**。原"零值省略"差异已消除，详见记录 37 补充审计。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | 导出前 Validate* 强制校验 | ~~C++ 额外~~ → **已移除（291f057）** | 三模型导出不再拒绝无效解，与 Rust 纯函数导出对齐；BPP binIndex 越界与变换非有限拒绝保留 | git 291f057 diff + JsonCodec.cpp:1791-1792 |
| S2 | min_item_separation 零值序列化 | ~~差异~~ → **已对齐（4540a76）** | 无条件输出，与 Rust 一致 | JsonCodec.cpp:1430-1431（原 1431-1432 条件输出已删） |
| S3 | ExtLayout JSON 键序 | 一致（补齐） | `container_id/placed_items/density` 双侧同序 | export.rs:25-29 vs JsonCodec.cpp:1341-1348 |
| S4 | density 取值路径 | 一致（补齐） | Snapshot 方法 ↔ Layout 方法（内部经快照） | export.rs:28 vs JsonCodec.cpp:1752 |
