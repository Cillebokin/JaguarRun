# 25. PlacedItem 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/entities/placed_item.rs`（39 行）
**C++**: `JaguarCore/Include/JaguarCore/Entities/PlacedItem.h`（42 行）+ `JaguarCore/Source/Entities/PlacedItem.cpp`（57 行）
**判定**: 基本等价（变换合成与形状变换一致；Rust 的 DTransformation 保留分解式 vs C++ 直接存 Transform2d；键类型 slotmap PItemKey vs HazardHandle；C++ Create 多一层 Result 错误通道）

## 函数逐条比对

### `PItemKey`（Rust 11-14 行，slotmap `new_key_type!`）
- C++ 对应：`PlacedItem.h:12` `using PlacedItemHandle = HazardHandle;`（`HazardHandle` = {index, generation}，`collision/Hazard.h:15-19`）。
- 判定：差异（表示法）。
- 说明：Rust 用 slotmap 类型系统生成不透明键（idx+version 打包，按 slotmap 内部规则）；C++ 用结构化 (index, generation) 二元组复用 Hazard 句柄类型。功能等价（唯一标识布局内的放置实例、失效可检测），但**不按位兼容**：slotmap 键的位布局是私有实现细节，与 C++ 的 (index<<32|gen 之类) 编码不同。跨语言序列化句柄不可直接互换。

### `struct PlacedItem` + 字段（Rust 17-25 行）
- C++ 对应：`PlacedItem.h:15-41`。
- 判定：一致（字段对应）。
- 证据：`item: Arc<Item>` ↔ `m_Item: shared_ptr<const Item>`（h:38）；`d_transf: DTransformation` ↔ `m_Transform: Transform2d`（h:39）；`shape: Arc<SPolygon>` ↔ `m_CollisionShape: SimplePolygon`（h:40，按值）。
- 差异说明：Rust 保留 `d_transf`（分解式变换：旋转+平移分离存储，`compose()` 时合成）与已变换的 `shape` 两个字段；C++ `m_Transform` 直接以合成后的 Transform2d（rotation+translation）承载（DTransformation↔Transform2d 等价承载见记录 06）。`Arc` 共享 vs 按值/`make_shared` 拷贝同记录 23 差异 4。

### `PlacedItem::new`（Rust 28-38 行）
```rust
let transf = d_transf.compose();
let shape = item.shape_cd.transform_clone(&transf);
```
- C++ 对应：`PlacedItem.cpp:16-31` `PlacedItem::Create`。
- 判定：基本等价（核心两步一致；C++ 有 f32 对齐与 Result 通道）。
- 证据：
  - 变换对齐（cpp:19-22）：先把入参 Transform2d 的 rotation/translation 逐分量舍入到 f32 再转回 double（`rustTransform`），对齐 Rust f32 的 `compose()` 数值路径。
  - 形状变换（cpp:23-24）：`item.CollisionShape().TransformedRustF32(rustTransform)`，对应 Rust `item.shape_cd.transform_clone(&transf)`（`TransformedRustF32` 即 f32 精度的变换克隆，见记录 05/09）。失败返回错误（cpp:25-26）；Rust `transform_clone` 失败为 panic 路径（panic→Result 约定差异的承接点）。
  - 持有 item（cpp:28）：`std::make_shared<const Item>(item)` 值拷贝，对应 Rust `Arc::clone(item)` 共享。
  - 存储合成变换（cpp:30）：存 `rustTransform`（已 f32 对齐），对应 Rust 存原始 `d_transf`（分解式）。注意：C++ `Transform()` 返回的是 f32 对齐后的变换，而 Rust `d_transf` 字段是调用方传入的原始分解式——当调用方传入非 f32 精确值时，C++ 侧序列化/导出的变换值会与 Rust 有舍入级差别；经 PlaceItem 链路（内部所有量均 f32 化）通常一致。
- 关键点：Rust `PlacedItem::new` 不返回 Result、不失败（transform_clone 在合法变换下不 panic）；C++ 增加失败通道但语义同为"合法输入必成功"。

### C++ 额外 API
- `GetItem()`（cpp:33-36）：返回 `const Item&`。对应 Rust 公有字段 `pi.item`。判定：一致（访问器化）。
- `GetItemIndex()`（cpp:38-41）：`m_Item->Index()`。对应 Rust `pi.item.idx` 直取。判定：一致。
- `Transform()`（cpp:43-46）：返回 `m_Transform`。对应 Rust `pi.d_transf`（注意上条：分解式 vs 合成式的表示差异，`compose()` 后数值等价）。判定：一致。
- `CollisionShape()`（cpp:48-51）：返回变换后的碰撞形状。对应 Rust `pi.shape`。判定：一致。
- `Area()`（cpp:53-56）：`return m_Item->Area();`。
- 判定：C++ 额外（便捷方法）。
- 说明：Rust 侧等价表达是 `pi.item.area()`（调用方直接写）。C++ 提供了一层委托，被 `Layout::PlacedItemArea`、`LayoutSnapshot::PlacedItemArea`、断言模块大量使用。数值来源相同（item 原始形状面积）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 键类型：Rust slotmap `PItemKey`（不透明打包键）vs C++ `PlacedItemHandle = HazardHandle {index, generation}` | 差异（表示法） | 功能等价（唯一性+失效检测）；位级不兼容，跨语言句柄不可直接序列化互换 | Rust rs:11-14；C++ PlacedItem.h:12 |
| 2 | 变换存储：Rust 保留分解式 `d_transf`（`compose()` 按需合成）vs C++ 直接存合成后 `Transform2d` | 差异（表示法） | 数值路径等价（C++ 存 f32 对齐后的合成值）；API 形态不同 | Rust rs:22, 30；C++ h:39, cpp:30 |
| 3 | C++ Create 显式 f32 对齐（rotation/translation 逐分量舍入） | C++ 额外（对齐性） | 保证与 Rust f32 compose/transform 数值一致 | cpp:19-22 |
| 4 | `PlacedItem::Area()` 委托方法 | C++ 额外（便捷） | 数值同 `item.area()`；Rust 调用方直写 | cpp:53-56 |
| 5 | 错误通道：C++ Create 返回 Result（TransformedRustF32 失败），Rust new 无失败路径 | 差异（panic→Result 约定） | 合法输入下双侧均成功；非法变换 C++ 报错、Rust panic | cpp:16-31 |
| 6 | Arc 共享 item/shape vs shared_ptr 拷贝/按值 | 差异（内存语义） | 不可变数据行为等价 | Rust rs:20, 24；C++ h:38-40, cpp:28 |

## 结论
PlacedItem 的核心逻辑（compose 变换 → 变换碰撞形状 → 持有 item 引用）双侧一致，C++ 通过 `rustTransform` 的逐分量 f32 舍入保证数值对齐。差异集中在表示法层面：键（slotmap 打包 vs index/generation 二元组）、变换（分解式 vs 合成式），以及 C++ 的 Result 错误通道与 Area() 便捷方法，均不改变放置实例的语义行为。
