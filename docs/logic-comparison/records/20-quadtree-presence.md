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
