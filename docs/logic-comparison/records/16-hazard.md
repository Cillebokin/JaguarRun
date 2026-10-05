# 16. hazard 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/hazards/hazard.rs`（79 行）
**C++**: `JaguarCore/Include/JaguarCore/Collision/hazard.h`（83 行）
**判定**: 基本等价（键机制由 slotmap key 改为 index+generation 句柄；scope() 语义分散到调用点）

## 函数逐条比对

### `HazKey`（hazard.rs:9-12，`new_key_type!`）
- C++ 对应：`HazardHandle`（hazard.h:15-26）
- 判定：结果等价/实现不同
- 证据与说明：Rust 用 slotmap 的 `new_key_type!` 生成不透明 key，由 `SlotMap<HazKey, Hazard>` 统一分配/回收；C++ 用 `{index, generation}` 二元组，`operator==` 比较 index 与 generation（hazard.h:21-25）。generation 使被回收槽位上的旧句柄自动失效（dangling key 检测），slotmap key 本身也具备该性质。语义等价，但 C++ 把分配/回收职责上移到 `CollisionEngine::AllocateHandle/DeregisterHazard`（CollisionEngine.cpp:106-118、162-178），见记录 22。

### `Hazard` 结构体 + `Hazard::new`（hazard.rs:16-35）
- C++ 对应：`struct Hazard`（hazard.h:77-82）
- 判定：一致
- 证据与说明：字段一一对应：`entity`→`HazardEntity entity`、`shape: Arc<SPolygon>`→`std::shared_ptr<const SimplePolygon> shape`、`dynamic: bool`→`bool dynamic`。Rust 的 `new()` 即聚合构造，C++ 为聚合初始化，无逻辑差异。

### `HazardEntity` 枚举（hazard.rs:37-53）
- C++ 对应：`HazardKind` 枚举（hazard.h:29-35）+ `HazardEntity` 结构体（hazard.h:38-74）
- 判定：结果等价/实现不同（Rust enum 变体携带字段，C++ 用 kind 判别 + 平铺字段）
- 证据与说明：四个变体完全对应：
  - `PlacedItem { id, dt, pk }` → `HazardKind::PlacedItem` + `itemIndex` + `transform` + `placedItemHandle`（hazard.h:41、44-45）
  - `Exterior` → `HazardKind::Exterior`（hazard.h:32）
  - `Hole { idx }` → `HazardKind::Hole` + `zoneIndex`（hazard.h:33、43）
  - `InferiorQualityZone { quality, idx }` → `HazardKind::InferiorQualityZone` + `quality` + `zoneIndex`（hazard.h:34、42-43）
  - Rust 的 `pk: PItemKey`（placed_item slotmap key）对应 C++ 的 `placedItemHandle: HazardHandle`。
  - 注意 `quality`/`zoneIndex` 为平铺字段：非 InferiorQualityZone 的 HazardEntity 上它们也存在（默认 0），而 Rust 里变体不携带时根本不存在——仅是表示法差异，判别式由 kind 保证。

### `HazardEntity` 相等性（hazard.rs:37 `#[derive(PartialEq, Eq, Hash)]`）
- C++ 对应：`operator==`（hazard.h:48-73）
- 判定：差异（细节）
- 证据与说明：
  - `PlacedItem`：Rust 比较 `id`+`dt`+`pk` 全等（含变换精确位级相等）。C++ 比较 `itemIndex`+`placedItemHandle`+`transform`，但变换分量经 `static_cast<float>` 后比较（hazard.h:58-63），即 double 分量按 float 截断比较——与 Rust `DTransformation`（内部即 f32 分解变换）语义对齐，等价性成立的前提是 C++ Transform2d 分量本身来自同一 RustF32 管线。
  - `Hole`：Rust 比较 `idx`；C++ 比较 `zoneIndex`（hazard.h:67）。一致。
  - `InferiorQualityZone`：Rust 比较 `quality`+`idx`；C++ 比较 `quality`+`zoneIndex`（hazard.h:69-70）。一致。
  - `Exterior`：双方都恒真（Rust 无字段变体；C++ `return true`，hazard.h:64-65）。
  - Rust 额外 derive `Hash`（供 assertions 的 HashSet 用，util/assertions.rs:132），C++ 无对应 Hash——C++ 断言体系不依赖哈希（见记录 21）。

### `HazardEntity::scope()`（hazard.rs:55-66）
- C++ 对应：无独立函数；语义在两处以 `entity.kind == HazardKind::Exterior` 分支内联
- 判定：C++ 缺失（作为 API；语义已内联）
- 证据与说明：Rust：
  ```rust
  // hazard.rs:58-65
  pub fn scope(&self) -> GeoPosition {
      match self {
          HazardEntity::PlacedItem { .. }
          | HazardEntity::Hole { .. }
          | HazardEntity::InferiorQualityZone { .. } => GeoPosition::Interior,
          HazardEntity::Exterior => GeoPosition::Exterior,
      }
  }
  ```
  "Interior 实体在形状内部致害、Exterior 实体在形状外部致害"的语义在 C++ 分散为：
  - `QuadTree::Classify` 的 Exterior 分支（Quadtree.cpp:214-234）：RectInsidePolygon→None、不相交→Entire，与 Interior 分支（237-241）方向相反——即 presence 反转（见记录 20）。
  - `CollisionEngine::HazardCollides` 的 Exterior 分支（CollisionEngine.cpp:216-237）：`return !contained` / `!polygonInsideContainer(...)`，与 `detect_containment_collision` 中 `(Exterior, false) => true`（cd_engine.rs:253-255）对应（见记录 22）。
  - `QuadTree::Classify` 里 `entity.kind != HazardKind::Exterior`（Quadtree.cpp:217）等判断即 `scope() == Interior` 的内联。
  结论：scope 的**语义**双侧一致，但 C++ 无单一可复用入口，靠调用点手写 kind 判断表达。

### `impl From<(PItemKey, T: Borrow<PlacedItem>)> for HazardEntity`（hazard.rs:68-79）
- C++ 对应：无独立函数；等价构造内联在 `Container.cpp:93` 一带（InferiorQualityZone 构造）与 `Layout`/`PlacedItem` 注册路径（HazardEntity{HazardKind::PlacedItem, ...}）
- 判定：结果等价/实现不同
- 证据与说明：Rust 从 `(pk, &placed_item)` 生成 `PlacedItem { id: pi.item.idx, dt: pi.d_transf, pk }`。C++ 各注册点手工填 `itemIndex = placedItem.GetItemIndex()`、`transform = placedItem.Transform()`、`placedItemHandle`。构造来源与字段一致，属构造方式差异。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 键机制：slotmap `HazKey` vs `{index, generation}` 句柄 | 结果等价/实现不同 | 句柄分配/回收职责移至 CollisionEngine（Slot/generation/LIFO 见记录 22） | hazard.rs:9-12 vs hazard.h:15-26 |
| 2 | `scope()` 独立 API | C++ 缺失（语义内联） | 调用点重复展开 kind==Exterior 判断，行为等价 | hazard.rs:55-66 vs Quadtree.cpp:214-241、CollisionEngine.cpp:216-237 |
| 3 | PlacedItem 变换相等性：精确 vs float 截断比较 | 基本一致（细节） | 依赖 C++ 变换分量与 Rust 同源；跨精度构造时可能误判相等 | hazard.rs:37 derive vs hazard.h:58-63 |
| 4 | enum 变体携带字段 vs kind+平铺字段 | 结果等价/实现不同 | 无行为影响 | hazard.rs:40-53 vs hazard.h:38-45 |
| 5 | `Hash` derive | C++ 缺失 | 仅影响 Rust 调试断言实现（记录 21） | hazard.rs:37 |

## 结论
Hazard 领域模型（实体四分类、三字段载荷、scope 语义）双侧一一对应，行为等价。主要形态差异是 slotmap key→generation 句柄、enum 变体→kind 判别式，以及 `scope()` 在 C++ 中被内联到 Classify/HazardCollides 的 Exterior 分支而不作为独立 API 存在。

## 补充审计（2026-09-29，逐行复核）

逐行清点 hazard.rs（79 行）全部符号：HazKey new_key_type、Hazard{entity,shape,dynamic}、Hazard::new、HazardEntity 四变体、PartialEq/Eq/Hash derive、scope()、From<(PItemKey,T)>。正文已全覆盖，无【遗漏】函数。以下为逐行复核后的细节修正与补充：

### 补-1【修正】scope() 的 C++ 内联证据行号已随 2026-09-29 四叉树重写迁移
正文差异 #2 引用 `Quadtree.cpp:214-241`（旧 Classify 的 Exterior 分支）与 `CollisionEngine.cpp:216-237`（旧 HazardCollides Exterior 分支）均已不存在。当前对应位置：
- `QuadTree::Constrict` 的 centroid 回退分支（Quadtree.cpp:447-451）：`interiorScope = hazard.entity.kind != HazardKind::Exterior; presence = interiorScope == colliding ? Entire : None` —— 即 `scope()` 与 qt_hazard.rs:145-151 的 match 逐式对应（Interior+colliding→Entire、Exterior+!colliding→Entire）。
- `DetectContainmentCollision`（CollisionEngine.cpp:69-70）：`interiorScope == contained` —— 对应 cd_engine.rs:253-255 的 `(scope, contained)` 组合表（Interior+true | Exterior+false → true，逻辑等价）。
判定维持：结果等价/实现不同（无独立 API，语义内联于上述两处）。记录 20 补-5 同步登记。

### 补-2【修正】`From<(PItemKey, T)>` 的 C++ 对应物当前为 `Layout::MakeHazard`
正文引用"Container.cpp:93 一带"有误（该处是 InferiorQualityZone hazard 的构造，不是 PlacedItem 转换）。Rust layout.rs:98 `(pk, pi).into()` 的真正 C++ 对应是 `Layout::MakeHazard`（Layout.cpp:97-105）：
```cpp
// Layout.cpp:97-105
Hazard{ {HazardKind::PlacedItem, placedItem.GetItemIndex(), 0, 0,
         handle, placedItem.Transform()},
        std::make_shared<const SimplePolygon>(placedItem.CollisionShape()), true };
```
与 Rust `HazardEntity::from((pk, pi))` 的字段来源逐项对应：`id = pi.item.idx` ↔ `GetItemIndex()`、`dt = pi.d_transf` ↔ `Transform()`、`pk` ↔ `handle`。layout.rs:115 的 `(pk, &pi).into()`（deregister 用）对应 RemoveItem 走 cdeHandle 直接注销（Layout.cpp:146-147，不经实体转换）。判定维持：结果等价/实现不同。

### 补-3【验证】HazardEntity::operator== 的 PlacedItem 变换比较（细节确认）
hazard.h:58-63 对 rotationRad/translation 逐分量 `static_cast<float>` 后比较。核对 Rust：`DTransformation` 为分解式（rot: f32 + translation: Point），derive(PartialEq) 逐字段 f32 精确比较；C++ Transform2d 分量为 double（经 rustTransform f32 归一后存储）。当分量恒经 f32 管线构造（PlacedItem::Create 的 rustTransform 归一，PlacedItem.cpp:19-22）时双侧比较语义一致。判定维持正文差异 #3。

### 补-4【验证】Hash derive 的用途（细节确认）
Rust `Hash` 供 util/assertions.rs 的 `layout_qt_matches_fresh_qt` 走 HashSet/HashMap（util/assertions.rs:218-231 hazards_match 之外未见；实际用途在 collector 相关 debug 检查）——正文引用 "util/assertions.rs:132" 处实为 `qt_contains_no_dangling_hazards` 附近。C++ 无 Hash、无对应断言体系（记录 21），影响面维持"仅 debug 断言"。判定：无变化。
