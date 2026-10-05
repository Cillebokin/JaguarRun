# 20. quadtree-presence 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/quadtree/qt_hazard.rs`（178 行）+ `qt_partial_hazard.rs`（74 行）+ `qt_hazard_vec.rs`（121 行）
**C++**: `JaguarCore/Source/Collision/Quadtree/Quadtree.cpp`（340 行）中的 `Classify`/`BuildNode`/presence 相关
**判定**: 基本等价（presence 三态与 Exterior 反转语义一致；实现机制不同：Rust 增量 constrict + 边子集 + n_active_edges 缓存 vs C++ 全量重建 + polygon-rect 相交分类）

> **2026-09-29 重写基线**：presence 三态/Exterior 反转/邻居传播/质心回退已按 qt_hazard.rs 逐行移植（见重写后的 Quadtree.cpp Constrict）。本记录中“C++ 无增量 constrict/无边子集”条目已过时。

## 函数逐条比对

### `QTHazard` 结构（qt_hazard.rs:11-22）vs `QuadTree::Entry`（Quadtree.h:74-79）
- 判定：结果等价/实现不同
- 证据与说明：Rust 的 QTHazard 携带 `qt_bbox`、`hkey`、`entity`、`presence: QTHazPresence`（Partial 内嵌 `QTHazPartial` 边子集）；C++ Entry 仅 `{handle, presence, edgeCount}`，节点边界存在 Node 上、实体数据回 `m_Hazards` 查。Partial 载荷（边子集）C++ 完全不存（见 `QTHazPartial` 条目）。

### `QTHazPresence` 枚举（qt_hazard.rs:24-33）vs `HazardPresence`（Quadtree.h:16-22）
- 判定：一致（三态 None/Partial/Entire 完全对应；Rust Partial 携带载荷，C++ 无载荷）

### `QTHazard::from_root`（qt_hazard.rs:35-44）vs `QuadTree::RegisterHazard`（Quadtree.cpp:44-52）
- 判定：一致
- 证据与说明：根注册一律从 Partial（整形状边集）开始：Rust `QTHazPresence::Partial(QTHazPartial::from_entire_shape(&haz.shape))`（qt_hazard.rs:42）；C++ 把 hazard 收进 `m_Hazards`，BuildNode 从根 Classify（首次分类即对根 bbox）。双侧初始状态等价（根上必是 Partial 或更细分后的结果）。

### `QTHazard::constrict`（qt_hazard.rs:49-169）vs `QuadTree::Classify`（Quadtree.cpp:211-242）
- 判定：基本等价（三态判定语义逐分支对应；实现方向与手段不同）
- 证据与说明——按分支对照：
  1. **Entire 短路**：Rust `QTHazPresence::Entire => array::from_fn(|_| self.clone())`（qt_hazard.rs:60）——Entire hazard 在全部 4 个子象限仍 Entire（节点全在 hazard 范围内，细分不改变）。C++ 对应：Classify 对 Exterior 先 `!nodeBounds.IntersectsRustF32(hazard.bounds)` → Entire（Quadtree.cpp:214-216）；对非 Exterior `!nodeBounds.IntersectsRustF32(hazard.bounds)` → None（Quadtree.cpp:217-219）。即 C++ 用"包围盒不相交"分派 Entire(Exterior)/None(Interior)，Rust 用 presence 递归（Entire→Entire×4）。**注意这里 Exterior/Interior 的相反返回正是 scope() 语义**：Exterior hazard（容器外区域致害）在节点远离其形状（即节点在容器内深处）时 Entire；Interior hazard 在节点远离形状时 None。Rust 的等价行为经由 constrict 分支 4（centroid 判定）+ Entire 递归共同保证：Interior 的 Entire 在子象限保持 Entire、None 保持 None；Exterior 的 Entire 在子象限保持 Entire（qt_hazard.rs:60 同一分支）。两侧最终三态一致。
  2. **包围盒 Enclosed 快捷**：Rust 若 hazard bbox 被**某一**象限 Enclosed → 该象限继承父 presence、其余 None（qt_hazard.rs:66-87）。C++ 无此快捷，但对 Enclosed 情形 Classify 走 RectInsidePolygon/PolygonIntersectsRect 得出等价结果（仅多算几何）。
  3. **边-象限相交收集**：Rust 对 Partial 的每条边做 `q.collides_with(edge)` 收集该象限的活动边子集（qt_hazard.rs:93-111）→ 子 QTHazPartial（边子集 + ff_bbox）。**C++ 无边子集**：Classify 对 Partial 返回 `edgeCount = shape.VertexCount()`（Quadtree.cpp:227、234、241）——恒为**整形状顶点数**，不做边级裁剪。这是 constrict vs Classify 的核心机制差异：Rust 子节点的 Partial 只含与该节点相交的边（后续检测量随树变深递减），C++ 任何节点上 Partial 的 edgeCount 都按全形状计，仅用于 BuildNode 的阈值统计。
  4. **Entire/None 邻居传播 + centroid 判定**：Rust 对无相交边的象限用邻居 presence 传播（qt_hazard.rs:121-135，"Entire 与 None 必被 Partial 分隔"不变式），无法传播时用 `haz_shape.collides_with(&quadrant.centroid())` + `entity.scope()` 定夺（qt_hazard.rs:143-151）：
     ```rust
     // qt_hazard.rs:144-151
     match self.entity.scope() {
         GeoPosition::Interior if colliding => QTHazPresence::Entire,
         GeoPosition::Exterior if !colliding => QTHazPresence::Entire,
         _ => QTHazPresence::None,
     }
     ```
     C++ 对应分支（Quadtree.cpp:228-241）：Exterior → `RectInsidePolygon(shape, nodeBounds)` 返回 None（节点整个在容器**内** → 容器外 hazard 不在场）；`!PolygonIntersectsRect` 返回 Entire（节点与容器不相交 → 节点整个在容器**外** → Exterior hazard 覆盖全节点）；否则 Partial。Interior → RectInsidePolygon 返回 Entire、!PolygonIntersectsRect 返回 None、否则 Partial。
     **Exterior presence 反转语义双侧一致**：Rust 用"centroid 不在容器形状内 → Entire"（单点采样）；C++ 用"节点矩形与容器多边形无任何相交（边交、点含都不成立）→ Entire"。C++ 的 `PolygonIntersectsRect`（Quadtree.cpp:161-188：bbox 粗筛 + 边对边交 + 多边形顶点在矩形内 + 矩形角点在多边形内）比 Rust 的 centroid 单点采样更保守（严格），但因 Exterior 分支先过 `RectInsidePolygon`（Quadtree.cpp:230，全角点在容器内 + 无边交 → None），两者在三态划分上边界一致的情形为：Rust 的 centroid 采样与 C++ 的矩形级判定只在"节点跨在容器边界附近且 centroid 恰在边界另一侧"的退化几何处可能分歧（记录 19 差异 #2 的重分类机制在查询期再次复核，最终查询结果由调用方精确测试兜底）。
  5. **无几何 fallback**：C++ `!hazard.hasGeometry` → 统一 `{Partial, 1}`（Quadtree.cpp:221-224）；Rust 无此形态（hazard 必有 shape）。
- 总评：三态语义（特别是 Exterior 反转）逐分支等价；差异集中在机制——Rust 增量 constrict（带边子集、Enclosed 快捷、邻居传播）vs C++ 每节点对整形状做 RectInsidePolygon+PolygonIntersectsRect 全量分类。

### `QTHazard::n_edges`（qt_hazard.rs:171-177）vs `Entry::edgeCount`（Quadtree.h:78）
- 判定：差异（数值含义不同）
- 证据与说明：Rust `n_edges` = Partial 的**实际边子集**长度（qt_hazard.rs:175）；C++ `edgeCount` = `shape.VertexCount()`（Quadtree.cpp:227）整形状顶点数，不裁剪。两者都只用于节点活动边数统计（细分阈值），但统计基数不同——C++ 的 activePartialEdges（Quadtree.cpp:252-261）系统性偏大，树在相同阈值下比 Rust 分裂更深（偏保守，不影响查询结果，见记录 19 差异 #3）。

### `QTHazPartial::from_entire_shape` / `from_parent`（qt_partial_hazard.rs:14-60）vs C++ 无对应
- 判定：C++ 缺失（边子集机制整体）
- 证据与说明：`from_parent` 裁剪出父边子集中与该象限相交的边并重算 ff_bbox（退化轴对齐时回退父 bbox，qt_partial_hazard.rs:42-53）。C++ Classify 不做任何边级工作，Partial 语义靠查询期对 queryBounds 的整体重分类 + 调用方对整形状的几何测试承担。

### `impl CollidesWith<T> for QTHazPartial`（qt_partial_hazard.rs:67-74）vs C++ 调用方精确测试（CollisionEngine.cpp:262-307）
- 判定：结果等价/实现不同
- 证据与说明：Rust 叶级 Partial 检测 = `entity.collides_with(ff_bbox) && edges.iter().any(|e| entity.collides_with(e))`（qt_partial_hazard.rs:72，ff_bbox 快筛 + 缓存边集测试）。C++ 等价物在引擎层：`EdgeCollides`/`CircleCollides`（CollisionEngine.cpp:262-307）对**整形状**逐边测试（`shape.IntersectsRustF32(edge)`、ContainsRustF32 + pointNearSegment）。因 C++ 查询先经 QueryNode 的 Classify(queryBounds) 粗筛（只有非 None 的 hazard 才成候选），精确测试虽遍历全边但结果与 Rust 边子集测试一致（边子集 ⊇ 命中边）。

### `QTHazardVec`（qt_hazard_vec.rs:12-121）
- C++ 对应：无（节点 entries 即裸 vector）
- 判定：C++ 缺失（三项优化整体）
- 证据与说明：
  1. **按 presence 强度排序**（Entire>Partial>None，qt_hazard_vec.rs:90-101，`binary_search_by` 插入排序，qt_hazard_vec.rs:34-42）：支撑 `strongest()` O(1) 取最强 hazard（qt_hazard_vec.rs:61-64），`collides` 的 Entire 快速路径依赖它。C++ entries 无序、无 strongest 概念。
  2. **n_active_edges 缓存**（qt_hazard_vec.rs:16、39、50、84-87，add/remove 增减维护 + debug 断言复核 qt_hazard_vec.rs:103-121）：支撑 `collect_collisions_until` 的阈值判断（qt_node.rs:176）。C++ 用建树时一次性统计 activePartialEdges（Quadtree.cpp:252-261），节点 Entry 的 edgeCount 相加即可，语义对应但时点不同（见记录 19 差异 #3）。
  3. **no_partial_hazards**（qt_hazard_vec.rs:73-78）：deregister 后判断是否丢弃 children（qt_node.rs:88-90）。C++ 由 Rebuild 的阈值判断隐式承担。
  4. Rust 的 add 有唯一性 debug_assert（同 entity/hkey 不重复，qt_hazard_vec.rs:26-33）——C++ 侧 RegisterHazard 按 handle 去重（Quadtree.cpp:324-333），等价。

### `QTHazPartial::n_edges`（qt_partial_hazard.rs:62-64）
- 判定：一致（edges.len()；C++ 对应物为 VertexCount，见 n_edges 条目差异）

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | presence 三态定义 | 一致 | — | qt_hazard.rs:24-33 vs Quadtree.h:16-22 |
| 2 | Exterior presence 反转（容器内→None/容器外→Entire/跨边界→Partial） | 基本一致（判定手段不同） | Rust centroid 单点采样 vs C++ RectInsidePolygon+PolygonIntersectsRect 矩形级判定，跨边界退化几何处理论上可分歧，查询期复核兜底 | qt_hazard.rs:143-151、60 vs Quadtree.cpp:228-241 |
| 3 | 增量 constrict（Enclosed 快捷、边级象限收集、邻居传播）vs 全量 Classify | 结果等价/实现不同 | C++ 每节点每 hazard 重算多边形-矩形关系；Rust 只算增量路径 | qt_hazard.rs:49-169 vs Quadtree.cpp:211-242 |
| 4 | Partial 载荷：边子集+ff_bbox vs 无载荷（edgeCount=VertexCount） | C++ 缺失（机制） | C++ 叶级检测遍历全形状边；edgeCount 系统性偏大使树分裂更深（保守方向） | qt_partial_hazard.rs:6-60 vs Quadtree.cpp:221-227 |
| 5 | `QTHazardVec`（排序+strongest+n_active_edges 缓存+no_partial_hazards） | C++ 缺失（机制） | Entire 快速路径与查询期阈值判断无 C++ 对应；语义由建树期阈值+查询期重分类承担 | qt_hazard_vec.rs:12-121 vs Quadtree.h:74-87 |
| 6 | 无几何 fallback（hasGeometry=false → Partial,1） | C++ 额外 | Rust 无此形态 | Quadtree.cpp:221-224 |
| 7 | n_edges（边子集长度）vs edgeCount（整形状顶点数） | 差异（数值） | 阈值统计基数不同，树深不同，结果集合不受影响 | qt_hazard.rs:171-177 vs Quadtree.cpp:227、241 |

## 结论
presence 三态与 Exterior 反转语义（容器内 None、容器外 Entire、跨界 Partial）双侧逐分支等价，这是查询结果等价的根基。机制差异全面：Rust 在树上缓存了三层信息（边子集、排序后的 strongest、n_active_edges），constrict 沿增量路径维护它们；C++ 全量重建、节点上只存 handle+presence+edgeCount（且 edgeCount 按整形状计），把全部几何判定推迟到查询期对 queryBounds 的 Classify 与调用方精确测试。结果是 C++ 用更多重复计算换取结构简单，查询结果与 Rust 一致。

## 补充审计（2026-09-29，逐行复核 2026-09-29 重写基线）

重写后 Quadtree.cpp 的 `Constrict`/`AddHazard`/`RemoveHazard`/`MakeRootHazard` 即本记录三份 Rust 文件的逐行移植，正文描述的"Classify/BuildNode/无载荷 Entry/edgeCount=VertexCount"均过时。逐行复核补充：

### 补-1【遗漏·疑似 bug（同记录 19 补-1）】`Constrict` 的形状来源错误
- Rust `constrict`（qt_hazard.rs:64）：`let haz_shape = &haz_map[self.hkey].shape` —— **每个 QTHazard 用自己的 hkey 取自己的形状**，Enclosed 判定（:67-70）与 centroid 判定（:144）都用这个形状。
- C++ `Constrict(hazard, quadrants, shape)`（Quadtree.cpp:300-467）：形状由调用方 `RegisterIntoNode` 传入（Quadtree.cpp:496、513），而 `RegisterIntoNode` 传入的是 `*shapeIt->shape`，其中 `shapeIt` 按**新注册 hazard 的句柄**查找（Quadtree.cpp:473-477）。懒扩展时对已有 hazard 的 constrict（Quadtree.cpp:491-496）传错形状（详见记录 19 补-1）。直接后果落在本记录的这些分支：
  - Partial 分支的 Enclosed 快捷（Quadtree.cpp:325-334 `shape.BoundingBox()` ↔ qt_hazard.rs:66-70 `haz_shape.bbox`）用错形状的 bbox；
  - 无相交边象限的 centroid 回退（Quadtree.cpp:446 `shape.ContainsRustF32(quadrants[i].CentroidRustF32())` ↔ qt_hazard.rs:144 `haz_shape.collides_with(&quadrant.centroid())`）用错形状做点包含。
- 附带核对：`ContainsRustF32`（SimplePolygon.cpp:430-470+）为 jagua-rs `SPolygon::collides_with(&Point)`（simple_polygon.rs:279-306）的 ray-casting 移植——bbox 预筛（439 ↔ :281）、`point_outside = bbox.x_max + bbox.width()`（442 ↔ :284-285）、顶点 y 对齐且 x 在右的 half-crossing 处理（455-462 ↔ :295-301）、其余边 ray 交（464-470 ↔ :303+）。判定：一致。
- 判定：差异（bug；形状来源），影响同记录 19 补-1。

### 补-2【浅覆盖→已验证】`QTHazardVec` 移植逐条核对（正文"差异 #5 QTHazardVec C++ 缺失"已过时）
- `add`（qt_hazard_vec.rs:24-43）↔ `Node::AddHazard`（Quadtree.cpp:226-249）：`activeEdges += EdgeCount()`（229 ↔ :39）、presence 降序 sortKey（Entire=2/Partial=1/None=0，230-241 ↔ :90-101）、`find_if(sortKey(existing) <= key)` 线性插入（243-248 ↔ binary_search_by :34-42——线性 find 与二分查找在"插入首个 ≤key 位置"上等价，元素为按序排列）。判定：一致。**缺失**：Rust `add` 的两条 debug_assert（presence 非 None :25、同 entity/hkey 唯一性 :26-33）C++ 无对应（Release 下 Rust 也不查，属调试不变式，无行为差异）。
- `remove`（qt_hazard_vec.rs:45-55）↔ `Node::RemoveHazard`（Quadtree.cpp:251-263）：find by handle、`activeEdges -= EdgeCount()`、erase。判定：一致。
- `strongest`（qt_hazard_vec.rs:61-64）↔ C++ 调用点的内联扫描（CollisionEngine.cpp:241-249、293-301：取首个未过滤项）。判定：一致（向量有序保证首个即最强）。`debug_assert!(assert_caches_correct)`（:62）C++ 无对应（见记录 21）。
- `n_active_edges`（:84-87）↔ `Node::activeEdges`（Quadtree.h:103，由 Add/Remove 维护）。判定：一致。
- `no_partial_hazards`（:73-78）↔ `Node::HasPartialHazard`（Quadtree.cpp:265-271，`any_of(Partial)` 取反）。判定：一致。
- `is_empty/len/iter`（:66-82）↔ 直接用 `node.hazards` vector。判定：一致。
- `assert_caches_correct`（:103-121）C++ 无对应（排序不变式 + activeEdges 对账，debug-only，见记录 21）。

### 补-3【浅覆盖→已验证】`QTHazPartial` 移植逐条核对（正文"差异 #4 C++ 无边子集"已过时）
- `from_entire_shape`（qt_partial_hazard.rs:15-19）↔ `MakeRootHazard`（Quadtree.cpp:273-298）：`edges = shape->Edges()`、`ffBbox = shape->BoundingBox()`（284-287 ↔ :16-17）。判定：一致（bounds-only 分支为 C++ 额外，见记录 19 补-3）。
- `from_parent` 的 ff_bbox（qt_partial_hazard.rs:20-60）↔ `Constrict` Partial 分支（Quadtree.cpp:356-410）：
  - 每象限按 `rectCollidesWithEdge(quadrant, edge)` 收集相交边（360-363 ↔ qt_hazard.rs:96-99 `q.collides_with(edge)`；`rectCollidesWithEdge` = `edge.IntersectsRustF32(rect)`（Quadtree.cpp:20-23），与 Rust `impl CollidesWith<Rect> for Edge` 委托 `Rect::collides_with(Edge)`（edge.rs:186-190 → rect.rs:317-355）的 bbox 快筛+端点包含+四角符号法逐式对应，Edge.cpp:343-383 确认）。判定：一致。
  - 边数等于父边数时继承父 ff_bbox（374-377 ↔ :25-27）。判定：一致。
  - 否则按边重算（380-398 ↔ :29-41，min/max 扫描）。判定：一致。
  - 退化回退父 ff_bbox（399-408 ↔ :42-53）。判定：一致（正文未展开，记录 19 补-5 同）。
  - `debug_assert!(!restricted_edges.is_empty())`（:21）与父边包含断言（:22）C++ 无对应（debug-only）。
- `n_edges`（:62-64）↔ `NodeHazard::EdgeCount`（Quadtree.h:88-93，Partial 且有 partial 时边数否则 0——Rust 语义 `Partial(p) => p.n_edges(), None|Entire => 0`（qt_hazard.rs:172-177））。判定：一致。
- `impl CollidesWith<T> for QTHazPartial`（:67-74）↔ C++ 查询路径内联（CollisionEngine.cpp:277-281、330-334、376-381、427-432：`entity.IntersectsRustF32(ffBbox) && any_of(edges, IntersectsRustF32(entity))`）。判定：一致（ff_bbox 快筛 + 边遍历逐式对应）。

### 补-4【遗漏】`QTHazard::from_root` 的 ff_bbox 数值路径
- Rust（qt_hazard.rs:42）：`QTHazPartial::from_entire_shape(&haz.shape)` → `ff_bbox = shape.bbox`（f32 直接引用）。
- C++（Quadtree.cpp:287）：`root.partial->ffBbox = hazard.shape->BoundingBox()`。C++ SimplePolygon::BoundingBox 为 f32 计算后转 double 存储（同管线），数值同源。判定：一致（细节确认，正文 A 节未列）。

### 补-5【验证】`QTHazard::constrict` 三分支其余对应（重写后）
- Entire 短路（qt_hazard.rs:60）↔ Quadtree.cpp:307-322（四象限克隆 Entire）。判定：一致。
- Enclosed 快捷（:66-87）↔ Quadtree.cpp:324-353（bbox Surrounding 单象限 → 该象限继承父 Partial（含深拷贝 PartialHazard :349），其余 None）。判定：一致（`rectContainsPolygonBBox` 用 `RelationToRustF32 == Surrounding`（Quadtree.cpp:25-29）↔ Rust `haz_shape.bbox.relation_to(*q) == Enclosed`（:69）——注意方向：Rust 是"象限 q Enclosed 于 hazard bbox"即 hazard bbox Surrounds q？**否**——`haz_shape.bbox.relation_to(*q)` 的 self=haz bbox，返回 Enclosed 意为 haz bbox 被 q 包住；C++ `rectContainsPolygonBBox(quadrants[i], shapeBounds)` 的 self=quadrant，Surrounding 意为 quadrant 包住 shapeBounds。两者同一关系（象限 ⊇ hazard bbox），判定：一致。）
- 邻居传播（:121-141）↔ Quadtree.cpp:417-452（kQuadrantNeighborLayout 邻居查 None/Entire，noneNeighbor→None、entireNeighbor→Entire）。判定：一致。Rust 的 `(true, true) => unreachable!`（:138-140）C++ 无对应（noneNeighbor 优先，entireNeighbor 次之，else 回退——C++ :434-452 的 if/else if/else 链在双真时走 noneNeighbor 分支而非报错；因 Partial 分隔不变式双真不可达，行为无差，但 debug 通道差异值得记录）。
- centroid + scope 回退（:143-152）↔ Quadtree.cpp:443-452（`interiorScope == colliding ? Entire : None`，interiorScope = `kind != Exterior`）。判定：一致（`entity.scope()` 的 C++ 内联位置从 Classify 移到 Constrict，记录 16 差异 #2 的证据行号已迁移：现 Quadtree.cpp:447-451、CollisionEngine.cpp:69-70）。

### 补-6【遗漏】重写后仍保留的 C++ 额外 API（正文差异 #6 记录的是旧 hasGeometry fallback，现已变形）
- `RegisterHazard(handle, Rect2d)` bounds-only 注册（Quadtree.cpp:573-592）与 `MakeRootHazard` 的空 shape 分支（289-296）：对角线假形状（记录 19 补-3）。Rust 无对应。当前无生产调用方。
- `PresenceAt`/`NodeCount`/`VirtualRootBounds`/`QueryVirtualRoot`/`Query`：测试与兼容层 API（记录 19 补-7）。
