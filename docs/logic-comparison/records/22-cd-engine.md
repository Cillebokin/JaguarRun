# 22. cd-engine 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/cd_engine.rs`（417 行）
**C++**: `JaguarCore/Include/JaguarCore/Collision/CollisionEngine.h`（169 行）+ `JaguarCore/Source/Collision/CollisionEngine.cpp`（727 行）
**判定**: 结果等价/实现不同（结果矩阵等价：本记录基于 2026-09-28 eef0830 后代码逐行验证；主要差异为 DetectPolygonCollision 的 collect-any 非短路路径、HazardCollides(Exterior, depth>0) 全顶点检查、Restore 全量重建、Slot/generation/LIFO 句柄管理；detectParallel/SoA/AVX2/ThreadPool 为 C++ 额外）

## 函数逐条比对

### `CDEngine::new`（cd_engine.rs:34-56）vs `CollisionEngine::CollisionEngine`（CollisionEngine.cpp:63-70）
- 判定：差异（形态）
- 证据与说明：Rust 构造即注册全部静态 hazard 并断言存在 Exterior（cd_engine.rs:44-48 `expect("No exterior hazard registered in the CDE")`）。C++ 构造只建树+槽位数组（`m_Slots(1)` 且 slot0.generation=0 作哨兵），静态 hazard 由调用方逐个 `RegisterHazard`（Exterior 是其中之一）。Rust 的"必须有 Exterior"约束在 C++ 由 Container 层保证；`m_Bounds` 经 `normalizeBoundsRustF32`（CollisionEngine.cpp:18-24）截到 f32 网格，与 Rust Rect 直接 f32 一致。
- `CDEConfig`（cd_engine.rs:403-411 vs CollisionEngine.h:33-38）：字段一致（quadtree_depth/quadtreeDepth、cd_threshold/cdThreshold、item_surrogate_config/itemSurrogateConfig）；C++ 有默认值（4/32），Rust 无默认（调用方必填）。

### `register_hazard`（cd_engine.rs:59-69）vs `RegisterHazard`（CollisionEngine.cpp:79-104）+ `AllocateHandle`（CollisionEngine.cpp:106-118）
- 判定：基本等价（差异：错误通道 + 句柄机制）
- 证据与说明：双侧都拒绝同实体重复注册：Rust `debug_assert!`（cd_engine.rs:60-63）→ C++ `Result` 错误（CollisionEngine.cpp:86-96）；C++ 额外拒绝空 shape（CollisionEngine.cpp:81-85，Rust 类型上不可能空）。Rust 用 SlotMap 自动分配 HazKey；C++ 用 `Slot{optional<Hazard>, shapeCache, generation}` + `m_FreeSlotIndices` LIFO 空闲栈（CollisionEngine.h:133-138）：释放时 `++generation`（Deregister，CollisionEngine.cpp:175），复用时 `generation |= 1`（Allocate，CollisionEngine.cpp:113）——奇数为活跃代、句柄带代数校验（Get，CollisionEngine.cpp:120-128）。C++ 额外构建 `PolygonSoA` shapeCache（SoA/AVX2 通道用，见差异 #14）。

### `deregister_hazard_by_entity` / `deregister_hazard_by_key`（cd_engine.rs:72-96）vs `DeregisterHazard`（CollisionEngine.cpp:162-178）
- 判定：基本等价
- 证据与说明：Rust 按实体线性找 key（找不到 panic）；C++ 只按 handle（无效 handle 返回 Result 错误）。`FindHazardHandle`（CollisionEngine.cpp:140-150）补上按实体查句柄的能力。双侧都先树后 map：Rust `quadtree.deregister_hazard(hkey)` 再 `hazards_map.remove`；C++ `m_QuadTree.DeregisterHazard` 再清槽位。Rust 每步后 debug_assert 无悬挂（cd_engine.rs:82、93）；C++ 无对应（架构豁免，记录 21）。

### `save`（cd_engine.rs:98-107）vs `Save`（CollisionEngine.cpp:575-584）
- 判定：一致（都收集 dynamic hazard 列表；C++ 按槽位序确定，Rust 按 SlotMap 迭代序）

### `restore`（cd_engine.rs:110-150）vs `Restore`（CollisionEngine.cpp:586-721）
- 判定：结果等价/实现不同
- 证据与说明：Rust：对 dynamic hazard 做集合 diff——留存的按 key **原地** 保留（不触树），移除的逐个 deregister、新增的逐个 register（cd_engine.rs:115-144），静态 hazard 完全不动。C++：同样做实体匹配 diff（CollisionEngine.cpp:621-653，快照实体 ↔ 现役 dynamic 实体），但随后**全量重建**：静态 hazard（659-671）+ 匹配留存的 dynamic（673-685）逐个重注册进全新 `rebuiltTree`，未匹配现役 dynamic 槽位 generation+1 回收（687-692），新增 dynamic 从空闲栈/尾部分配（694-715）。C++ 额外前置校验快照合法性（空 shape/非 dynamic/重复实体 → Result 错误，588-619；Rust 无此检查，靠 debug_assert 与构造约束）。行为等价（最终 dynamic hazard 集合与 Rust 相同）；差异为全量重建成本 + 槽位 generation 变化（Rust 留存 hazard 的 key 不变，C++ 留存者槽位不变但被移除者代数递增）。

### `hazards()`（cd_engine.rs:153-155）vs 无直接公共 API（`FindHazardHandle`/`Get` 部分覆盖）
- 判定：C++ 缺失（便捷性；Layout 层不直接遍历 hazards）

### `detect_poly_collision`（cd_engine.rs:161-198）vs `DetectPolygonCollision`（CollisionEngine.cpp:412-418）
- 判定：结果等价/实现不同（非短路 collect-any）
- 证据与说明：Rust 短路边遍历：
  1. 包围盒 Surrounding 前提：`bbox().relation_to(shape.bbox) != Surrounding` → 立即 true（cd_engine.rs:162、194-197，**此即"implicit exterior 提前 return"——注意它在 Rust 里就在**）。
  2. virtual root 内逐边 `collides(edge, filter)`，任一命中即 true（cd_engine.rs:167-171）。
  3. Partial hazard 逐个 containment 检测（`detect_containment_collision`），命中即 true（cd_engine.rs:174-191）。
  C++（eef0830 后现状）：
  ```cpp
  // CollisionEngine.cpp:412-418
  if (isOutside(m_Bounds, shape.BoundingBox()))
      return true;
  return !CollectPolygonCollisions(shape, filter).empty();
  ```
  - `isOutside`（CollisionEngine.cpp:26-31）= Disjoint||Intersecting||Enclosed，恰好是 `!= Surrounding` 的补集——与 Rust 前提判定**一致**。
  - 但实现走 `CollectPolygonCollisions`（collect-any）：收集全部碰撞再判空，无 Rust 的边短路。**确认：isOutside 提前 return true 当前仍在（CollisionEngine.cpp:415-416）**，这与 Rust detect_poly_collision 的 else 分支（cd_engine.rs:194-197）对应，语义等价——两侧都在形状超出 CDE 包围盒时直接判碰撞（因为 Exterior hazard 必然被触发；Rust 中若 Exterior 被 filter 排除，此提前返回仍为 true，见下"重要分叉点"）。
  - **重要分叉点（双侧共有）**：Rust detect_poly_collision 的提前 return true **不查 filter**——若调用方把 Exterior 标为 irrelevant（如 from_irrelevant_qzones 场景），Rust 仍返回 true。C++ isOutside 同样不查 filter。故此处双侧行为一致（都"错"得一致）。collect 路径（下条）则双侧都做 filter 检查。

### `collect_poly_collisions` / `collect_poly_collisions_until`（cd_engine.rs:263-321）vs `CollectPolygonCollisions`/`CollectPolygonCollisionsUntil`（CollisionEngine.cpp:309-410）
- 判定：结果等价/实现不同
- 证据与说明：Rust 语义：
  1. 前置哨兵：`bbox != Surrounding` 且 collector 未含 exterior → 先插入 exterior hazard（cd_engine.rs:283-290）——**注意 Rust 里这个哨兵仍在**。
  2. virtual root 逐边 collect_collisions_until（cd_engine.rs:295-299）。
  3. Partial hazard containment 补查（cd_engine.rs:302-318）。
  C++（eef0830 后现状，CollisionEngine.cpp:341-401）：
  1. 显式 exterior 哨兵仍在（**确认存在**）：`isOutside(m_Bounds, shape.BoundingBox())` 时报告 exterior——先找**已注册**的 Exterior hazard（360-369），找到用其真实 handle/entity；找不到才用合成 handle `{SIZE_MAX, UINT32_MAX}` + 空 ExteriorEntity（347-349、378）。与 Rust 差异：Rust 哨兵用 `hkey_exterior`（构造时必已注册的 Exterior，cd_engine.rs:44-48），不存在"未注册 Exterior"状态；C++ 允许引擎无注册 Exterior 时合成哨兵（**C++ 额外的容错路径**，正常 Layout 流程中 Exterior 总已注册，两者重合）。
  2. `QueryVirtualRoot(shape.BoundingBox())` 取候选（384），逐个 `HazardCollides(shape, *hazard)` 精测（396）。**无 Rust 的 containment 补查独立阶段**——containment 语义并入 HazardCollides（见下条）。
  3. filter/collector 检查经 `report` lambda（352-358），与 Rust 的 `collector.contains_key` + filter 检查等价。
  4. C++ 收集结果按 (kind,itemIndex,quality,zoneIndex) 排序后返回（329-337）——Rust 无排序（调用方自行处理）。
  - 边-碰撞与 containment 的等价性：Rust 步骤 2/3 分离（边命中或 containment 命中都算）；C++ `HazardCollides` = `ShapesCollide`（边交 + POI containment，见下条）一次覆盖两者，结果集合等价。

### `detect_containment_collision`（cd_engine.rs:233-257）vs `ShapesCollide` 的 containment 尾段（CollisionEngine.cpp:196-204）
- 判定：一致（逐分支对应）
- 证据与说明：Rust：`almost_relation_to` 判 Surrounding → `haz_shape.collides_with(&shape.poi.center)`；Enclosed → 反向；其余 false；再按 scope() 组合（Interior+contained / Exterior+!contained → true，cd_engine.rs:246-255）。C++ ShapesCollide 尾段：`AlmostRelationToRustF32` 后 Surrounding → `first.ContainsRustF32(second.POI)`、Enclosed → 反向（CollisionEngine.cpp:196-204）。**scope 组合**不在此函数而在 HazardCollides 的 Exterior 分支（`return !contained`，见下条）。逐分支等价（Rust 的 Exterior+contained=false → true 即 C++ 的 !contained）。

### `detect_surrogate_collision`（cd_engine.rs:205-224）vs `DetectSurrogateCollision`（CollisionEngine.cpp:476-521）
- 判定：结果等价/实现不同（eef0830 后语义已对齐）
- 证据与说明：eef0830 移除了 C++ 的包围盒预检短路（旧 `!m_Bounds.Contains(pole)` → 直接 true）与隐式 exterior 哨兵（见 git diff：删除 CircleOutside/implicitExteriorHandle/exteriorIsRelevant 段）。**当前行为（逐行验证）**：
  - Rust：`quadtree.collides(&t_pole, filter)`——树内表达：Exterior hazard 的 presence 三态在树中注册；pole 完全在容器内→该节点对 Exterior 为 None（不命中）、完全在容器外→Entire（命中，cd_engine 前提未满足时 rust detect 的包围盒检查在 poly 路径，surrogate 路径无预检）、跨边界→Partial（按 ff_bbox+边测试，即 pole 圆与容器边相交才命中）。
  - C++：对每个 transformed pole 调 `m_QuadTree.QueryVirtualRoot(pole.BoundingBox())` 取候选（CollisionEngine.cpp:493），逐个 `CircleCollidesWithHazard`：非 Exterior → `CircleCollides`（ContainsRustF32(center) || pointNearSegment，296-307）；Exterior → **仅** `pointNearSegment(center, boundaryEdge, radius)`（288-293，eef0830 后）。
  - **等价性核验（含 quadtreeDepth=0 与根外情形）**：
    1. *pole 在容器外（深外）*：Rust——pole bbox 落在被 Exterior hazard Entire 覆盖的节点（constrict 时 centroid 不在容器形状内 → Entire，qt_hazard.rs:147-149），`collides` 取 strongest=Entire → 命中。C++——`QueryVirtualRoot`：VirtualRoot 下降要求子节点 Surrounding pole bbox；pole 在根 bounds 外时 VirtualRoot 停在 m_Root（Quadtree.cpp:124-143 无子可包住），QueryNode 根上 Classify(pole bbox) 对 Exterior：`!nodeBounds.IntersectsRustF32(hazard.bounds)` 走不到（根 bounds=容器 bbox 与 Exterior hazard bounds（容器 bbox）相交）；quadtreeDepth=0 时树无子，根 entries 含 Exterior（Classify 根：RectInsidePolygon(容器, 根bbox)——根=容器 bbox 全在自身内且无边交 → **None**，Quadtree.cpp:230-231）——**Exterior hazard 不进根 entries，pole 在容器外且 depth=0 时 C++ 查不到 Exterior！** 但此时 pole 与容器 bbox 相交检查：QueryNode 首行 `node.bounds.IntersectsRustF32(queryBounds)`（281）——pole 在容器外时其 bbox 与根 bounds **Disjoint**，QueryNode 直接 return，候选为空，返回 false。而 Rust depth=0 时：quadtree 根无 children，collides 走 strongest——Exterior hazard 在根的 presence 是 Partial（from_root 恒 Partial，且 depth=0 不再 constrict，cd_engine.rs:35、qt_node.rs:61-63 `level > 0` 才分裂）→ 叶级检测 `p_haz.collides_with(pole)` = pole 与容器形状边集测试（ff_bbox+边）——pole 深在容器外时 ff_bbox 不相交 → 不命中 → false。**两侧同为 false，一致**（surrogate 是保守筛查，深外 pole 在两侧都不经 Exterior 报警——Rust 靠 poly 路径的包围盒前提兜底，C++ 同样由 DetectPolygonCollision 的 isOutside 兜底，调用链上 surrogate 检测总是伴随后续 poly 检测）。
    2. *pole 跨容器边界*：Rust Partial → pole 圆与容器边相交即命中（含圆心在容器内但圆缘压边，qt_partial_hazard.rs:72）。C++ Exterior 分支 `pointNearSegment` 逐容器边测距（288-293）——CircleCollides 非 Exterior 分支还含 `ContainsRustF32(center)`，Exterior 分支刻意只测边距（在容器内深处不触发）。与 Rust 一致：圆心在容器内且圆不压边 → 双侧不命中；圆压边 → 双侧命中。
    3. *pole 在容器外但圆缘压不到边（浅外）*：Rust——Exterior 在叶级为 Partial 时同上只测边；若 pole bbox 已出到所在节点被判 Entire 的区域则命中。C++——pole bbox 与根相交才进 QueryNode，Exterior 的 Classify(poleBBox) 若 Entire（pole bbox 不与容器形状相交，Quadtree.cpp:232-233）则成候选，`CircleCollidesWithHazard(Exterior)` 只测边距——**浅外且不压边时 C++ 候选成但精测不过 → false**；Rust 同样只有 Partial 边测试 → false（Entire presence 只在 constrict 后的子节点出现，且 pole bbox 必须与该子节点相交——子节点整个在容器外时 pole 落其中 → strongest=Entire → 命中 true）。**分歧点**：Rust 树 constrict 后容器外存在 Entire 叶/节点，pole 落在容器外区域（bbox 与该节点相交）即命中；C++ QueryNode 对 Exterior 的候选条件是 `Classify(hazard, poleBBox) != None`——pole bbox 全在容器外时 `!PolygonIntersectsRect` → **Entire** → 入候选，但精测 `pointNearSegment` 圆心到容器边距离 > radius → false。**即：pole 完全在容器外（不贴边）时 Rust quadtree.collides 返回 true（Entire presence 命中），C++ 返回 false（只测边界边）——此为真实语义分歧**，见差异汇总 #5；深度=0 时 Rust 也为 false（见上 1）。实际影响受 surrogate 筛查的保守性定位与上层 poly 检测兜底约束（两侧 surrogate 漏报最终都由 detect_poly_collision 的 isOutside/哨兵路径捕获，端到端结果仍一致）。
  - piers：Rust 循环 `ff_piers()` 逐个 `collides`（cd_engine.rs:217-222）；C++ 同样逐 pier `Edge2d::Create` 后查树精测（503-519，`EdgeCollidesWithHazard`：Exterior 仅边界边相交，268-280，eef0830 后现状确认）。注意 pier 变换失败 `continue`（508-509）为 C++ 容错（Rust 变换不失败）。
  - filter：Rust 在树查询内排除 irrelevant；C++ 在候选循环排除（497、515）。

### `collect_surrogate_collisions` / `collect_surrogate_collisions_until`（cd_engine.rs:327-365）vs `CollectSurrogateCollisionsUntil`（CollisionEngine.cpp:523-573）
- 判定：基本等价（差异：Rust collect 路径含 piers，C++ 仅 poles；Rust 从 shape.surrogate 取，C++ 显式传参）
- 证据与说明：Rust：`shape.surrogate` 存在则对 `ff_poles()` 前缀逐 pole `collect_collisions_until`（cd_engine.rs:353-364）——**注意 Rust 此路径也只有 poles，无 piers**（piers 只在 detect_surrogate_collision，cd_engine.rs:217-222）。C++：`surrogate.FailFastPoleCount()` 前缀逐 pole `QueryVirtualRoot` + `CircleCollidesWithHazard` + report（544-562，eef0830 后已无 bounds 预检/隐式哨兵，git diff 确认删除）。双侧均为 poles-only，语义对齐。C++ 多 Result 包装与 transform.IsFinite 校验（530-534）。

### `get_virtual_root`（cd_engine.rs:370-383）vs `QuadTree::VirtualRoot`（Quadtree.cpp:124-143）
- 判定：一致（记录 19 已比；C++ 另暴露 QueryVirtualRoot/VirtualRootBounds 公共 API）

### `bbox()`（cd_engine.rs:385-388）vs `Bounds()`（CollisionEngine.cpp:723-726）— 一致。

### `haz_key_from_pi_key`（cd_engine.rs:391-399）vs `FindHazardHandle`（CollisionEngine.cpp:140-150）
- 判定：基本等价（差异：Rust 按 PItemKey 匹配 PlacedItem 变体；C++ 按整实体匹配，调用方需自组 entity）

### `HazardCollides(SimplePolygon)` 的 Exterior 分支（CollisionEngine.cpp:216-239）
- 判定：差异（C++ 额外的深度分流；与 Rust POI 捷径不同）
- 证据与说明：
  ```cpp
  // CollisionEngine.cpp:216-219
  if (hazard.entity.kind == HazardKind::Exterior) {
      if (m_Config.quadtreeDepth > 0U)
          return !polygonInsideContainer(shape, *hazard.shape);
  ```
  - `polygonInsideContainer` = 全部 shape 顶点在容器内 + 无边交（CollisionEngine.cpp:33-51）→ "shape 整个在容器内" 才不碰撞；任一顶点出容器或压边即碰撞。
  - Rust 对应 `detect_containment_collision(shape, haz_shape, Exterior)`：`almost_relation` → POI 单点包含检查（cd_engine.rs:242-250）——更便宜的捷径。**语义差异**：Rust 用 POI（形状最大内切圆心）判定"shape 在容器内"近似；C++ 全顶点+边交为精确定义。凸/常规几何两者一致；POI 在容器内但某顶点越界的病态凹形（如细长突出部）下 Rust 误判不碰撞、C++ 判碰撞——C++ 更严格。
  - depth==0 时 C++ 走 `AlmostRelation`+POI（221-236），与 Rust 完全一致。
  - SoA 版 `HazardCollides(PolygonSoA)`（241-260）Exterior 分支同全顶点+边交逻辑（double 精度），非 Rust 对应（AVX2 通道，差异 #14）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | DetectPolygonCollision 实现：collect-any（收集全部再判空）vs Rust 边短路遍历 | 结果等价/实现不同 | 性能：C++ 无早退；结果一致 | cd_engine.rs:167-191 vs CollisionEngine.cpp:412-418 |
| 2 | DetectPolygonCollision 的 isOutside 提前 return true | 一致（对应 Rust != Surrounding → true） | 双侧均不查 filter，行为一致 | cd_engine.rs:194-197 vs CollisionEngine.cpp:26-31、415-416 |
| 3 | CollectPolygonCollisionsUntil 的 exterior 哨兵 | 基本一致（双侧均在） | C++ 额外"无注册 Exterior 时合成句柄"容错；Rust 构造期保证必注册 | cd_engine.rs:283-290 vs CollisionEngine.cpp:347-349、360-382 |
| 4 | HazardCollides(Exterior, depth>0)：polygonInsideContainer 全顶点+边交 vs Rust POI 捷径 | 差异 | 病态凹形（POI 在内但顶点越界）判定相反：C++ 碰、Rust 不碰；C++ 更严格 | CollisionEngine.cpp:33-51、216-239 vs cd_engine.rs:242-250 |
| 5 | Surrogate+Exterior：pole 完全在容器外且不贴边 | 差异（depth>0 时） | Rust Entire presence 命中→true；C++ 只测边界边距→false；depth=0 双侧 false；上层 poly 检测兜底使端到端一致 | qt_node.rs:107-109、qt_hazard.rs:147-149 vs CollisionEngine.cpp:288-293、232-233 |
| 6 | Restore：Rust in-place diff（留存 hazard 不触树）vs C++ 全量重建+槽位代数更替 | 结果等价/实现不同 | C++ 成本高；移除者 generation+1、句柄代际变化；快照校验为 C++ 额外 | cd_engine.rs:110-150 vs CollisionEngine.cpp:586-721 |
| 7 | Slot 存储：generation/LIFO 空闲栈 vs SlotMap | 结果等价/实现不同 | 句柄失效检测语义等价；奇偶代数为 C++ 特有 | CollisionEngine.cpp:106-118 vs slotmap |
| 8 | CDEConfig 默认值（4/32） | C++ 额外 | Rust 必填 | CollisionEngine.h:33-38 vs cd_engine.rs:403-411 |
| 9 | DetectPolygonCollisions（批量）/DetectPolygonCollisionsParallel（线程池并行） | C++ 额外 | Rust 无批量/并行 API | CollisionEngine.h:79-86、CollisionEngine.cpp:420-474 |
| 10 | PolygonSoA/AVX2 shapeCache + ShapesCollide(SoA) 双精度通道 | C++ 额外 | AVX2 有无走 Fallback（SoaGeometryAvx2Fallback.cpp）；Rust 无 SoA | CollisionEngine.cpp:100-101、207-211、241-260 |
| 11 | transform.IsFinite 校验、Result 错误通道、pier 变换失败 continue | C++ 额外（错误处理形态） | Rust panic/类型保证 | CollisionEngine.cpp:481-485、508-509 vs cd_engine.rs |
| 12 | 收集结果排序（kind/itemIndex/quality/zoneIndex） | C++ 额外 | 输出顺序确定化 | CollisionEngine.cpp:329-337 |
| 13 | bounds normalizeBoundsRustF32 截断 | 一致（f32 网格对齐） | — | CollisionEngine.cpp:18-24 |

## 结论
eef0830 后 surrogate 两侧的 Exterior 语义已实质对齐：DetectSurrogateCollision/CollectSurrogateCollisionsUntil 已无包围盒预检与隐式哨兵（git diff 逐行确认删除），EdgeCollidesWithHazard(Exterior) 只做边界边相交、CircleCollidesWithHazard(Exterior) 只做 pointNearSegment（当前代码 CollisionEngine.cpp:268-294 确认）。逐行验证确认的真实分歧集中在：HazardCollides(Exterior, depth>0) 的全顶点检查（比 Rust POI 捷径更严格，病态凹形判定相反）、以及 pole 深在容器外时 Rust 树的 Entire presence 命中 vs C++ 只测边界边（受上层 poly 检测兜底，端到端仍一致）。DetectPolygonCollision 的 isOutside 提前 true 与 CollectPolygonCollisionsUntil 的 exterior 哨兵**当前均仍在**且与 Rust 对应行为等价。其余为性能/形态差异（collect-any、全量重建 Restore、Slot/generation）与 C++ 额外能力（并行批量、SoA/AVX2、ThreadPool）。
