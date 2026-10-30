# 24. Container 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/entities/container.rs`（132 行）
**C++**: `JaguarCore/Include/JaguarCore/Entities/Container.h`（67 行）+ `JaguarCore/Source/Entities/Container.cpp`（148 行）
**判定**: 基本等价（构造校验与静态 hazard 生成一致；base_cde 预建 vs CreateCollisionEngine 按需、质量区形状未保留 orig、质量区顺序展平 vs 传入顺序、InferiorQualityZone.area 缺失）

## 函数逐条比对

### `struct Container` + 字段（Rust 15-24 行）
- C++ 对应：`Container.h:25-66` + `Container::Data`（`Container.cpp:34-43`）。
- 判定：基本等价。
- 证据：`outer_orig: Arc<OriginalShape>` ↔ `data->originalOuter`（shared_ptr，cpp:37）；`outer_cd: Arc<SPolygon>` ↔ `data->collisionOuter`（cpp:38）；`quality_zones: [Option<InferiorQualityZone>; N_QUALITIES]` ↔ `data->qualityZones`（`vector<QualityZone>`，按传入顺序，cpp:39）；`base_cde: Arc<CDEngine>` ↔ 无预建引擎（见下）。
- C++ 额外字段：`id`（ContainerId，cpp:36）、`cdeConfig`（cpp:40）、`bounds`（方形包围盒，cpp:41）、`staticHazards`（cpp:42，预生成的静态 hazard 列表）。Rust 无 id 字段（见差异 6）。

### `Container::new`（Rust 27-71 行）
- C++ 对应：`Container.cpp:50-100` 的 `Container::Create`。
- 判定：基本等价（校验与 hazard 生成逻辑一致；引擎构建时机不同）。

按步骤对照：

**步骤 1：外轮廓转换**（Rust 32 行 `convert_to_internal()?`）
- C++：`Container.cpp:56-58`，失败透传。判定：一致。

**步骤 2：质量区唯一性校验**（Rust 34-37 行 `ensure!(quality_zones.iter().map(|qz| qz.quality).unique().count() == len)`）
- C++：`Container.cpp:60-70`，`qualitySeen[zone.quality]` 重复或 `zone.quality >= MAX_QUALITIES` 均返回 `InvalidInput`。
- 判定：一致（两校验合并为单循环；错误消息文本 "container quality zones must have unique qualities below 10" 融合了 Rust 两条 ensure 的语义）。

**步骤 3：质量区存储**（Rust 45-52 行，按 quality 展平到 `[Option<_>; N_QUALITIES]` 固定数组）
- C++：`Container.cpp:78` `data->qualityZones = std::move(qualityZones)`，按调用方传入顺序保存。
- 判定：差异。
- 说明：Rust 按 quality 值索引（quality 升序定序，未设置的槽为 None）；C++ 保留传入顺序。后果：`base_cde`/`staticHazards` 中质量区 hazard 的注册顺序，Rust 固定按 quality 升序（`iter().flatten()` 按数组下标即 quality 升序），C++ 按传入顺序。对碰撞检测语义无影响（hazard 集合相同），但 quadtree 内部 hazard 排列顺序可能不同（仅影响枚举顺序，不影响结果集合）。另注意 C++ `QualityZone.shapes` 为 `vector<SimplePolygon>`（`Container.h:21`），原始 `OriginalShape` 未保留（见差异 3）。

**步骤 4：静态 hazard 生成**（Rust 54-63 行）
```rust
let mut hazards = vec![Hazard::new(HazardEntity::Exterior, outer.clone(), false)];
let qz_hazards = quality_zones.iter().flatten().flat_map(InferiorQualityZone::to_hazards);
hazards.extend(qz_hazards);
```
- C++：`Container.cpp:82-97`：先 Exterior（cpp:82-83），再遍历 `qualityZones`，每个 shape 生成 Hole（quality==0，entity idx=shapeIndex）或 InferiorQualityZone（quality>0，携带 quality+shapeIndex）hazard，均为 `dynamic=false`。
- 判定：一致（Exterior 打头 + 质量区逐一转换；Hole/InferiorQualityZone 的判定与 idx 含义逐行对应，Rust 118-123 行 ↔ C++ 90-95 行）。

**步骤 5：CDEngine 构建**（Rust 61 行 `CDEngine::new(outer.bbox.inflate_to_square(), hazards, cde_config)`，构造期即建 `base_cde`）
- C++：不在 Create 中建引擎；改为预存 `bounds`（cpp:80，`squareBounds(collisionOuter->BoundingBox())`）+ `staticHazards`（步骤 4 结果）+ `cdeConfig`，由 `CreateCollisionEngine()` 按需构建（见下）。
- 判定：差异（时机与归属）。
- 说明：`squareBounds`（cpp:13-31）实现 `inflate_to_square` 语义：短边向外扩半差值成方形，float 中间量与 Rust f32 对齐。Rust 的 `base_cde` 作为容器身份锚点（`Layout::restore` 用 `Arc::ptr_eq(&base_cde)` 判容器一致，layout.rs:75-78）；C++ 改用 `shared_ptr<const Data>` 指针等价性（`SameBase`，cpp:132-135）承载同一职责——注意：Rust 中**相同 OuterShape 参数**重复构造 `Container::new` 会得到**不同**的 `base_cde`（Arc 不同），C++ 中同样参数重复 Create 得到不同 Data 指针，行为对齐。

### `area(&self) -> f32`（Rust 74-77 行）
- C++ 对应：`Container.cpp:107-110` `Area()`，`return m_Data->originalOuter->shape.Area();`。
- 判定：一致（均为原始外轮廓毛面积，不减孔与质量区）。

### `N_QUALITIES = 10`（Rust 81 行）
- C++ 对应：`Container.cpp:11` `constexpr std::size_t MAX_QUALITIES = 10;`（匿名命名空间，未导出）。
- 判定：一致（常量值相同；C++ 为内部常量而非公开 API）。

### `InferiorQualityZone::new`（Rust 95-112 行）
- C++ 对应：无独立构造函数；`QualityZone` 为聚合结构（`Container.h:18-22`），由调用方（IO/Importer）填充。
- 判定：差异（C++ 缺失构造校验）。
- 说明：Rust 构造时 `assert!(quality < N_QUALITIES)`（rs:96-99）并做 `convert_to_internal`；C++ 的合法性校验（quality<10）移到 `Container::Create` 中统一检查，形状转换由 Importer 层负责。`Container::Create` 收到的 `QualityZone::shapes` 已是 `SimplePolygon`（内部形），即 `shapes_cd`；`shapes_orig`（Rust rs:89）无对应物。

### `InferiorQualityZone::to_hazards`（Rust 115-126 行）
- C++ 对应：内联于 `Container::Create`（`Container.cpp:85-97`）。
- 判定：一致（quality==0 → Hole{idx}，否则 InferiorQualityZone{quality, idx}，idx 为形状下标；`dynamic=false`）。

### `InferiorQualityZone::area`（Rust 129-131 行，`shapes_orig` 面积求和）
- C++ 对应：无。
- 判定：C++ 缺失。
- 影响：因 `shapes_orig` 未保留（差异 3），C++ 无法按原始形状求面积；当前 JaguarCore 内无调用方需要该值，未实现。

### C++ 额外 API
- `Id()`（cpp:102-105）：返回构造时传入的 ContainerId；Rust Container 无 id 字段（差异 6）。
- `GetOriginalShape()`、`CollisionShape()`、`Bounds()`：纯访问器，Rust 为公有字段直取，语义一致。`QualityZones()`：Rust公开按质量索引的`[Option<InferiorQualityZone>; 10]`；Importer路径会为10个等级都保留`Some`组，即使shape列表为空。C++返回仅含Importer非空组的vector。仓内hazard与SVG消费者只使用实际shape，空组不改变这些运行行为；外部调用者直接读取长度或槽位存在性仍可观察差异。详见B8 Stage216。
- `SameBase(other)`（cpp:132-135）：`m_Data == other.m_Data`，等价承载 Rust `Arc::ptr_eq(&base_cde, ...)` 的容器一致性判定职责（layout.rs:75-78 的对应物）。
- `CreateCollisionEngine()`（cpp:137-147）：
```cpp
CollisionEnginePtr engine{new CollisionEngine(m_Data->bounds, m_Data->cdeConfig)};
for (const Hazard& hazard : m_Data->staticHazards)
    if (!engine->RegisterHazard(hazard).HasValue())
        return nullptr;
```
- 判定：C++ 额外（等价承载）。
- 说明：对应 Rust `base_cde` 的"空布局初始引擎"角色。每次调用新建引擎并逐个注册静态 hazard（RegisterHazard 返回句柄被忽略，与 Rust 构造期注册对齐）；任一注册失败返回 nullptr（调用方 Layout 构造/swap 容错）。Rust 的 `CDEngine::new` 在构造时直接接收 hazards 列表；C++ 分两步（构造 + 注册）但顺序与集合一致。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | base_cde 预建（Rust 构造期建 CDEngine 并克隆给 Layout）vs C++ 预存 bounds/staticHazards/cdeConfig，由 `CreateCollisionEngine()` 按需建 | 差异（时机） | 行为等价；容器一致性锚点由 `Arc<base_cde>` 指针等价改为 `shared_ptr<Data>` 指针（SameBase） | Rust rs:23, 54-63；C++ cpp:80-97, 137-147 |
| 2 | 质量区注册顺序：Rust 按 quality 升序展平（固定数组索引序）vs C++ 传入顺序 | 差异 | hazard 集合相同，仅枚举/注册顺序不同；对碰撞结果无影响 | Rust rs:45-52, 56-59；C++ cpp:78, 85-97 |
| 3 | `shapes_orig`（质量区原始形状）未保留，C++ `QualityZone::shapes` 只存内部碰撞形 | C++ 缺失 | `InferiorQualityZone::area()` 因此无法实现；当前无功能调用方受影响 | Rust rs:89-91, 129-131；C++ Container.h:18-22 |
| 4 | `InferiorQualityZone::area()`（原始形状面积求和） | C++ 缺失 | 无调用方；Rust 侧亦仅公共 API | Rust rs:128-131 |
| 5 | `InferiorQualityZone::new` 构造期校验（quality<N_QUALITIES + 逐形状 convert） | 差异（位置移动） | C++ 将 quality 上界校验移至 `Container::Create`，形状转换移至 Importer；最终防线等价 | Rust rs:95-112；C++ cpp:60-70 |
| 6 | C++ Container 携带 `ContainerId id`，Rust 无 id 字段 | C++ 额外 | 纯元数据，不参与任何逻辑 | C++ h:15, cpp:36, 102-105 |
| 7 | C++ 导出 `Bounds()`（方形包围盒）；Rust 的 inflate 结果只藏在 base_cde 内部 | C++ 额外 | 服务于 CreateCollisionEngine；无行为影响 | C++ cpp:41, 80, 127-130 |
| 8 | N_QUALITIES 常量公开性：Rust `pub const`（可被外部引用）vs C++ 匿名命名空间内部常量 | 差异（API） | 上限值 10 双侧一致；C++ 调用方无法直接引用该常量 | Rust rs:81；C++ cpp:11 |

## 结论
Container 的构造校验（质量唯一且 <10）、静态 hazard 生成（Exterior + Hole/InferiorQualityZone 按 quality 判别）与面积语义双侧一致。主要差异是 CDEngine 的构建时机（Rust 预建 base_cde vs C++ 按需 CreateCollisionEngine，配合 SameBase 指针等价判定）、质量区存储方式（quality 升序展平 vs 传入顺序，仅影响枚举顺序）以及 `shapes_orig`/质量区面积未保留。

## 补充审计（2026-09-29，逐行复核）

逐行清点 container.rs（132 行）全部符号：struct Container 四字段、Container::new、Container::area、N_QUALITIES、struct InferiorQualityZone、InferiorQualityZone::new/to_hazards/area。正文已全覆盖全部符号，无【遗漏】。补充：

### 补-1【验证】`squareBounds` ↔ `inflate_to_square` 逐式核对（正文步骤 5 未展开公式）
Rust（rect.rs:96-112）：`width = x_max-x_min; height = y_max-y_min; height<width → dy=(width-height)/2; width<height → dx=(height-width)/2;` 展开四边。C++（Container.cpp:13-31）：同公式 f32 中间量（`* 0.5F` ↔ `/ 2.0`——f32 乘 0.5 与除 2 等价（2 的幂），数值一致）。判定：一致。
调用点：Rust `CDEngine::new(outer.bbox.inflate_to_square(), ...)`（container.rs:61）；C++ `data->bounds = squareBounds(collisionOuter->BoundingBox())`（Container.cpp:80）→ `CreateCollisionEngine` 用 `m_Data->bounds`（cpp:141）。判定：一致。

### 补-2【验证】`Container::area` 的口径（细节确认）
Rust（container.rs:75-77）：`outer_orig.area()`——OriginalShape 毛面积（未 deflate、不减孔/质量区）。C++（Container.cpp:107-110）：`m_Data->originalOuter->shape.Area()`。判定：一致（正文已记）。

### 补-3【浅覆盖→补充】质量区注册顺序差异的实际影响面（正文差异 #2 补充证据）
Rust 展平为 `[Option; 10]` 后 `iter().flatten()` 按 quality 升序注册；C++ 按传入顺序。2026-09-29 四叉树重写后，注册顺序影响 `m_Registered` 与 Node::AddHazard 的向量插入位置（Quadtree.cpp:738-749 SetRegistered、AddHazard 排序插入）——由于 AddHazard 按 presence 强度排序而非注册序，且查询对向量按序扫描仅在早停时受顺序影响（记录 19 补-2 的象限顺序差异同源），质量区 hazard 的注册顺序差异**仍仅影响枚举顺序**，结果集合不变。判定维持正文差异 #2。

### 补-4【验证】`CreateCollisionEngine` 的注册失败路径（细节确认）
Container.cpp:137-147：任一 RegisterHazard 失败返回 nullptr（正文已记）。补充：注册顺序 = Exterior 首位（staticHazards[0]，cpp:82-83）+ 质量区随后——与 Rust hazards 向量顺序一致（container.rs:55-59 `vec![Exterior] + qz_hazards`）。判定：一致。

### 补-5【遗漏】`InferiorQualityZone::to_hazards` 的 Hole 判定（细节登记）
Rust（container.rs:117-123）：`quality == 0 → Hole{idx}，否则 InferiorQualityZone{quality, idx}`，idx 为**形状下标**（`shapes_cd.iter().enumerate()`）。C++（Container.cpp:85-97）：`zone.quality == 0 ? HazardKind::Hole : InferiorQualityZone` + `zoneIndex = shapeIndex`（形状在 zone 内下标）。判定：一致（正文步骤 4 已记，此补充明确 idx 语义为 zone 内形状下标，非全局）。
注意：**Hole/InferiorQualityZone 的 idx 语义在双侧一致为"该 quality zone 内的形状序号"**——多个 zone 各自从 0 计数，HazardEntity 相等性靠 kind+quality+zoneIndex 区分（同 quality 内 idx 唯一即可）。判定：一致。
