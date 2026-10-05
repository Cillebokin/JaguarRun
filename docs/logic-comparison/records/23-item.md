# 23. Item 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/entities/item.rs`（59 行）
**C++**: `JaguarCore/Include/JaguarCore/Entities/Item.h`（82 行）+ `JaguarCore/Source/Entities/Item.cpp`（110 行）
**判定**: 基本等价（构造管线一致；RotationRange 表示法不同且 C++ 增加表示合法性校验；Arc 共享变按值拷贝）

## 函数逐条比对

### `struct Item` + 字段（Rust 12-27 行）
- C++ 对应：`Item.h:36-81` 的 `class Item`。
- 判定：一致（字段一一对应）。
- 证据：
  - `idx: usize` ↔ `m_Index`（h:74，`ItemIndex = std::size_t`，h:18）
  - `external_id: u64` ↔ `m_ExternalId`（h:75，`ItemId = std::uint64_t`，h:16）
  - `shape_orig: Arc<OriginalShape>` ↔ `m_OriginalShape`（h:76，按值持有）
  - `shape_cd: Arc<SPolygon>` ↔ `m_CollisionShape`（h:77，按值持有）
  - `allowed_rotation: RotationRange` ↔ `m_AllowedRotation`（h:80，类型见下条）
  - `min_quality: Option<usize>` ↔ `m_MinQuality: std::optional<std::size_t>`（h:79）
  - `surrogate_config: SPSurrogateConfig` ↔ `m_SurrogateConfig`（h:78）
- 差异说明：Rust 用 `Arc` 共享不可变形状（多个 `PlacedItem`/`Layout` 快照共享同一份 `shape_cd`），C++ 按值拷贝（`Item.cpp:63` `make_shared<const Item>(items[i])` 等处每次复制）。语义等价（不可变数据），内存布局与共享性不同。

### `Item::new`（Rust 30-53 行）
- C++ 对应：`Item.cpp:40-69` 的 `Item::Create`。
- 判定：基本等价（管线一致，C++ 额外做旋转范围合法性校验与 f32 归一化）。

按步骤对照：

**步骤 1：内部形状转换**（Rust 40 行 `shape_orig.convert_to_internal()?`）
- C++：`Item.cpp:56-58` `originalShape.ConvertToInternal()`，失败透传错误。
- 判定：一致（`convert_to_internal` ↔ `ConvertToInternal` 管线见记录 09）。

**步骤 2：代理形状生成**（Rust 41 行 `shape_int.generate_surrogate(surrogate_config)?`）
- C++：`Item.cpp:60-62` `collisionShape.Value().GenerateSurrogate(surrogateConfig)`，失败透传。
- 判定：一致（注意 C++ 是对已转换的内部形状对象调用，与 Rust 顺序相同：先 convert 后 surrogate）。

**步骤 3：字段装配**（Rust 44-52 行）
- C++：`Item.cpp:64-68` 私有构造 + `FromValue`。
- 判定：一致（无额外逻辑）。

**C++ 额外校验 1：rotationRangeRepresentable**（`Item.cpp:9-20`，在 `Create` cpp:48-52 调用）
```cpp
case RotationMode::None:
case RotationMode::Continuous:
    return rotation.radians.empty();
case RotationMode::Discrete:
    return true;
```
- 判定：C++ 额外。
- 说明：Rust `RotationRange` 为枚举（`geo_enums.rs:22-28`：`None | Continuous | Discrete(Vec<f32>)`），非法组合（None 带 radians）在类型层面不可能；C++ 用 `RotationMode` 枚举 + `std::vector<double> radians` 两个字段表示（`Item.h:29-33`），组合可能不一致，故运行时校验：None/Continuous 必须 `radians` 为空，否则返回 `InvalidInput`。这是表示法差异带来的必要防护，不影响合法输入的行为。

**C++ 额外校验 2：弧度 f32 归一化**（`Item.cpp:53-54`）
```cpp
for (double& angle : allowedRotation.radians)
    angle = static_cast<double>(static_cast<float>(angle));
```
- 判定：C++ 额外（对齐性措施）。
- 说明：Rust 直接存 `Vec<f32>`；C++ 存 `double` 但强制舍入到 f32 可表示值，保证后续数值路径与 Rust f32 一致。

### `area(&self) -> f32`（Rust 55-58 行）
- C++ 对应：`Item.cpp:106-109` `Item::Area()`，`return m_OriginalShape.shape.Area();`。
- 判定：一致（Rust `shape_orig.area()` 同样委托原始形状面积）。

### 访问器（Rust 公有字段直接访问）
- C++ 对应：`Index()`（cpp:71-74）、`ExternalId()`（cpp:76-79）、`GetOriginalShape()`（cpp:81-84）、`CollisionShape()`（cpp:86-89）、`GetSurrogateConfig()`（cpp:91-94）、`MinQuality()`（cpp:96-99）、`AllowedRotation()`（cpp:101-104）。
- 判定：一致（纯 getter，逐字段对应，无逻辑差异）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | RotationRange 表示法：Rust 枚举 `None/Continuous/Discrete(Vec<f32>)` vs C++ `RotationMode` 枚举 + `vector<double> radians` | 差异（表示法） | 无行为影响（合法输入下等价）；C++ 需运行时校验组合合法性 | Rust `geo_enums.rs:22-28`；C++ `Item.h:20-33` |
| 2 | C++ `rotationRangeRepresentable` 表示合法性校验 | C++ 额外 | 非法组合（None/Continuous 带 radians）被拒绝（`InvalidInput`）；Rust 类型层面不可能出现 | `Item.cpp:9-20, 48-52` |
| 3 | C++ 弧度舍入到 f32 | C++ 额外 | 对齐 Rust f32 精度；合法 f32 输入无差异 | `Item.cpp:53-54` |
| 4 | Arc 共享 vs 按值拷贝持有 shape_orig/shape_cd | 差异（内存语义） | 不可变数据，行为等价；C++ 复制开销更高 | Rust `item.rs:18-20`；C++ `Item.h:76-77` |
| 5 | C++ Create 参数顺序：minQuality/surrogateConfig 位置互换（surrogateConfig 必填在前，minQuality/allowedRotation 带默认值在后） | 差异（API 形状） | 调用方差异，无逻辑影响 | Rust `item.rs:30-37`；C++ `Item.h:40-46` |

## 结论
`Item` 的构造管线（convert_to_internal → generate_surrogate → 装配）与面积/访问器在双侧逐行对应，核心逻辑等价。差异集中在 `RotationRange` 的表示法（枚举 vs mode+vector）及由此引入的 C++ 运行时校验与 f32 归一化，均属防御性/对齐性措施，不改变合法输入下的行为。

## 补充审计（2026-09-29，逐行复核）

逐行清点 item.rs（59 行）全部符号：struct Item 七字段、Item::new、Item::area。正文已全覆盖，无【遗漏】。逐行复核确认与补充：

### 补-1【验证】构造管线顺序与 surrogate 生成对象（细节确认）
Rust（item.rs:39-43）：`shape_orig.convert_to_internal()` 得到中间 SPolygon，再对其 `generate_surrogate(surrogate_config)`——**surrogate 生成在内部形状上、就地修改**（`let mut shape_int = ...; shape_int.generate_surrogate(...)?`）。C++（Item.cpp:57-62）：`originalShape.ConvertToInternal()` → `collisionShape.Value().GenerateSurrogate(surrogateConfig)`——同为对转换后的内部形状生成。判定：一致（正文步骤 1/2 的结论经当前代码行号复核成立）。
注意 Rust 的 `shape_cd` = generate_surrogate 之后的**同一对象**（surrogate 字段挂在 SPolygon 上）；C++ `m_CollisionShape` 同为 GenerateSurrogate 后的对象。双侧 shape_cd 均内嵌 surrogate。判定：一致。

### 补-2【验证】rotationRangeRepresentable 注释（细节）
Item.cpp:9-20 当前版本在 Discrete 分支带注释"jagua-rs Item::new 不校验角数量；1..=65536 规则在 Importer 层执行"——正文引用的旧代码块无此注释，判定不变（C++ 额外校验），补充说明该差异的注释证据链完整。

### 补-3【遗漏】`Item::area` 的 C++ 数值路径（细节）
Rust（item.rs:56-58）：`self.shape_orig.area()` → `OriginalShape::area()`（原始形状面积，含 pre_transform 前的 caller 坐标）。C++（Item.cpp:106-109，正文已记）：`m_OriginalShape.shape.Area()`——注意 C++ OriginalShape 的 area 取 `.shape.Area()`，是否包含 pre_transform 的影响需核对 record 09 的 OriginalShape 语义；Rust OriginalShape::area 同样不含 pre_transform（面积在原始坐标系计算）。判定：一致（正文已记；此补充确认两侧 area 均不经过 pre_transform/modify）。

### 补-4【验证】Create 参数与字段对应复核（正文差异 #5）
Item::Create 签名（Item.cpp:42-47）：`(index, externalId, originalShape, surrogateConfig, minQuality={}, allowedRotation={})`——surrogateConfig 必填在前、minQuality/allowedRotation 带默认值。Rust `Item::new(idx, external_id, original_shape, allowed_rotation, min_quality, surrogate_config)` 全必填、顺序不同。判定维持正文差异 #5（API 形状，无逻辑影响）。
