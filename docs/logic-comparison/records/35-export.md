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
- **非有限拒绝**：C++ 在变换非有限时返回 `SerializationFailure`（cpp:1762-1768）；Rust 无校验。C++ 额外（panic→Result 系）。
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
| 5 | 非有限变换拒绝 | C++ 额外 | C++ 返回 `SerializationFailure`；Rust 无校验（NaN 序列化为 JSON 时才由 serde 报错或输出 null） | 无 vs JsonCodec.cpp:1762-1768 |
| 6 | 返回类型 `ExtLayout` vs `Result<ExternalLayout>` | 基本等价 | panic→Result 约定的体现；Rust `#[must_use]` 直接返回 | export.rs:8 vs JsonCodec.cpp:1747 |
| 7 | min_item_separation 零值序列化 | 差异（实例层，关联项） | Rust 实例序列化总是输出 `min_item_separation`（无 skip 属性，probs/*/io/ext_repr.rs `#[serde(default)]` 只管读侧）；C++ `commonInstanceJson` 仅在 `separation != 0.0` 时输出（JsonCodec.cpp:1431-1432）。本条属 probs 实例导出（记录 37），因跨记录高频提及在此交叉引用 | spp/io/ext_repr.rs:12-13 vs JsonCodec.cpp:1425-1435 |

## 结论

导出侧核心逻辑（放置项枚举、`T_int ∘ T_pre` 合成、度数换算、density、item_id）双侧一致，SPP container_id 经"传参 0 ↔ 默认 id=0 的容器"两条路径实际输出相同。C++ 的快照桥接、非有限拒绝与 Result 化为 panic→Result 系约定的常规附加。`min_item_separation` 零值序列化差异位于实例层，详见记录 37。
