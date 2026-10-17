# 19. quadtree-node 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/quadtree/qt_node.rs`（211 行）+ `qt_traits.rs`（208 行）
**C++**: `JaguarCore/Include/JaguarCore/Collision/Quadtree.h`（122 行）+ `JaguarCore/Source/Collision/Quadtree/Quadtree.cpp`（340 行）
**判定**: 结果等价/实现不同（查询结果集合等价；架构差异：Rust 增量 constrict 树 + presence 驱动查询 vs C++ 每次注册/注销全量重建 + per-query 重分类；cd_threshold→edgeThreshold 语义同源）

> **2026-09-29 重写基线**：C++ 四叉树已重写为 jagua-rs quadtree 的逐行结构移植（Node 持 presence 降序 NodeHazard + PartialHazard 边子集 + ffBbox；注册懒扩展 + Constrict 邻居传播；注销递归移除 + 无 Partial 折叠；activeEdges 缓存恢复 cd_threshold 查询时语义；根节点一律 Partial）。本记录中所有“全量重建/不存边子集/edgeThreshold 建树语义”相关条目已过时，仅作历史比对存档。当前实现与 Rust 为结构级逐行对应，notch/pier oracle 与 Debug/Release 全量测试通过。

## 函数逐条比对

### 节点结构 `QTNode`（qt_node.rs:13-25）vs `QuadTree::Node`（Quadtree.h:81-87）
- 判定：结果等价/实现不同
- 证据与说明：
  - Rust 节点持 `level`（根为 quadtree_depth，子节点递减，qt_node.rs:69）、`bbox`、`children: Option<Box<[QTNode;4]>>`、`hazards: QTHazardVec`（含 presence 与边子集，见记录 20）、`cd_threshold`。
  - C++ 节点持 `bounds`、`depth`（根为 0，子节点递增，Quadtree.cpp:271）、`entries: vector<Entry>`（仅 handle+presence+edgeCount，Quadtree.h:74-79）、`children: array<unique_ptr<Node>,4>`。
  - 差异：C++ 节点**不存边子集**（Entry 只有 edgeCount 数字），任何几何判定都要回 `m_Hazards` 找原始多边形重算（`Classify`，Quadtree.cpp:211-242）；Rust 节点自带 partial hazard 的边子集。`cd_threshold` 在 C++ 是树级 `m_EdgeThreshold`（Quadtree.h:119）。
  - level/depth 语义相反但判定同构：Rust `self.level > 0` 才可分裂（qt_node.rs:62），C++ `node.depth >= maxDepth` 停止分裂（Quadtree.cpp:264）。

### `QTNode::new`（qt_node.rs:28-37）vs `QuadTree::QuadTree`（Quadtree.cpp:35-42）
- 判定：一致（构造根节点 + 阈值配置；C++ 额外记 maxDepth）。

### `QTNode::register_hazard`（qt_node.rs:39-82）vs `QuadTree::RegisterHazard`（Quadtree.cpp:44-70）+ `SetRegistered`（Quadtree.cpp:322-334）+ `Rebuild`（Quadtree.cpp:336-339）
- 判定：结果等价/实现不同（本组最大架构差异之一）
- 证据与说明：
  - Rust：**增量**。把 hazard 转成根 QTHazard（Partial，全边），向下 `constrict`：只有当节点收到 `Partial` presence 且 `level > 0` 时才**懒生成** children（qt_node.rs:61-76），已有 hazard 一并 constrict 下放；Entire/None 递归终止（qt_node.rs:51-57）。
  - C++：**全量重建**。`SetRegistered` 更新 `m_Hazards`（去重按 handle）后无条件 `Rebuild()` → `BuildNode(m_Root, m_Hazards, ...)`（Quadtree.cpp:333-338），从根开始对**所有**已注册 hazard 重新 `Classify` 递归建树。
  - 结果等价性：两侧树中某节点上某 hazard 的 presence 判定一致（见记录 20 的 constrict vs Classify 比对），故查询结果集合一致；差异在每次注册 O(全量) vs O(路径)。C++ 另有公开导出的 `RegisterHazard(handle, const Rect2d&)` bounds-only 扩展（Quadtree.h:46–47、Quadtree.cpp:579–598）：以边界对角线作为唯一 Edge 代理，不保存完整几何；Rust 无对应入口，且该扩展不计入上述 parity 结论（B5）。
  - Rust 注册前 `debug_assert` 同实体不重复（cd_engine.rs:60-63）；C++ 同样在 CollisionEngine 层查重（CollisionEngine.cpp:86-96，见记录 22）。

### `QTNode::deregister_hazard`（qt_node.rs:84-95）vs `QuadTree::DeregisterHazard`（Quadtree.cpp:72-81）
- 判定：结果等价/实现不同
- 证据与说明：Rust 按 hkey 从 QTHazardVec 移除，若无 Partial hazard 残留则**丢弃 children**（qt_node.rs:88-90），否则向 children 递归——即树可以缩回。C++ 从 `m_Hazards` erase 后全量 `Rebuild()`（Quadtree.cpp:80）。最终树形态一致（Rust 懒生成 + 无 Partial 即收缩 ≙ C++ 重建时 `activePartialEdges <= edgeThreshold` 即停，Quadtree.cpp:264）。

### `QTNode::collides`（qt_node.rs:100-145）vs C++ 无单点"collides"——由 `Query(Rect2d)`/`Query(Edge2d)` + 调用方过滤复合
- 判定：结果等价/实现不同
- 证据与说明：Rust 二值查询语义：
  1. `hazards.strongest(filter)` 取最强 presence（Entire>Partial>None，向量按强度排序，qt_hazard_vec.rs:90-101）；
  2. `Entire` → 立即返回该实体（节点被 hazard 完全覆盖，任何查询必碰撞，qt_node.rs:109）；
  3. `Partial` → 若有 children：`collides_with_quadrants` 筛出相交象限，按 `quadrant_order` 启发式顺序优先查最可能命中的子节点，`find(Option::is_some)` 短路（qt_node.rs:112-124）；无 children 则对每个 Partial hazard 做 `p_haz.collides_with(entity)`（ff_bbox 预筛 + 边遍历，qt_partial_hazard.rs:70-73）。
  - C++：`Query`（Quadtree.cpp:83-89）从 `VirtualRoot(queryBounds)` 开始 `QueryNode`：节点 bbox 与查询相交才进入；对节点 entries 的每个 handle 回 `m_Hazards` 找原始 hazard，用 `Classify(hazard, queryBounds)` **对查询矩形现场重分类**，非 None 即入结果（Quadtree.cpp:284-294）；递归 children；最后 SortUnique。二值判定由调用方（如 `DetectSurrogateCollision`）对候选集逐个做精确几何测试（CircleCollidesWithHazard/EdgeCollidesWithHazard，CollisionEngine.cpp:282-294、268-280）。
  - 等价性：两侧对同一查询最终命中的 hazard 集合一致（Entire presence ≙ Classify 对 queryBounds 返回 Entire/Partial；Partial ≙ 精确几何复核）。差异：Rust 的 Entire 快速路径（节点全被覆盖即免几何测试）在 C++ 中仍需对 queryBounds 重分类 + 调用方几何测试，无短路收益；C++ `QueryNode` 对每个 entry 做 O(顶点数) 的 Classify，Rust 用缓存的边子集。
  - 注意 C++ `QueryNode` 用 `Classify(*hazard, queryBounds)`（Quadtree.cpp:292）——分类对象是**查询矩形**而非节点矩形，等效于 Rust 中"查询实体与 hazard presence 的交"，是结果等价的关键机制。

### `QTNode::collect_collisions` / `collect_collisions_until`（qt_node.rs:147-210）vs `QueryNode`（Quadtree.cpp:276-300）+ 调用方收集循环
- 判定：结果等价/实现不同
- 证据与说明：
  - Rust：`perform_cd_now = hazards.n_active_edges() <= cd_threshold`（qt_node.rs:176，**n_active_edges 缓存**驱动）；有 children 且未达阈值 → 递归相交象限，`any` 支持由 `stop_after_collision` 回调提前停止（qt_node.rs:178-190）；否则叶级遍历 hazards：Entire→true、Partial→`p_haz.collides_with(entity)`，插入 collector 并回调（qt_node.rs:193-208）。
  - C++：`QueryNode` 无阈值判断（**细分阈值只在 BuildNode 建树时用一次**，Quadtree.cpp:264），收集循环在 `CollisionEngine::CollectSurrogateCollisionsUntil`（CollisionEngine.cpp:544-562）等处，`report` lambda 承担 filter+collector.Contains+stop 回调（CollisionEngine.cpp:536-542）。
  - 等价性：见记录 22 对 `collect_surrogate_collisions_until` 的逐行比对（poles 循环等价；piers 在 C++ collect 路径缺失）。`collect_poly_collisions_until` 的 C++ 对应也走 `QueryVirtualRoot` + `HazardCollides` 全量循环，见记录 22。
  - cd_threshold 语义对照：Rust 阈值作用于**查询时**（节点边数少就在该节点做叶级检测）且建树时无阈值（只要 Partial 就分裂到 level 0）；C++ 阈值作用于**建树时**（`activePartialEdges <= edgeThreshold` 停止分裂，Quadtree.cpp:264），查询时无阈值。两者最终"在哪些节点做精确检测"的树形态不同（Rust 树更深、C++ 树在低边数处截断），但**结果集合一致**——精确几何测试双侧都会执行，仅执行位置不同。

### `QTQueryable::collides_with_quadrants` 默认实现（qt_traits.rs:13-16）vs `Node::bounds.IntersectsRustF32(queryBounds)` 逐子节点判断（Quadtree.cpp:281）
- 判定：结果等价/实现不同
- 证据与说明：Rust 对 Circle/Rect 用 `collides_with(q)`（圆-矩形/矩形-矩形相交）；Edge 有专门优化实现（见下条）。C++ `QueryNode` 对每个 child 递归前查 `child->bounds.IntersectsRustF32(queryBounds)`（Quadtree.cpp:281、295-299）——C++ 的"查询体"恒为矩形/包围盒（Edge 查询先取 BoundingBox，Quadtree.cpp:91-94），与 Rust Rect 路径等价；Rust Circle 路径的圆-矩形精确相交在 C++ 查询阶段退化为包围盒，精确性由调用方 `CircleCollidesWithHazard` 兜底。

### `impl QTQueryable for Edge::collides_with_quadrants`（qt_traits.rs:53-129）vs C++ 无对应（Edge 查询用 BoundingBox）
- 判定：C++ 缺失（作为优化；结果由两层矩形过滤+调用方边测试保证）
- 证据与说明：Rust 对边-四象限分类做了专门优化：端点在象限内快判（qt_traits.rs:70-72）+ 利用象限共享边的 `half_intersect` 分摊判定（qt_traits.rs:103-118）+ f64 oracle 断言（qt_traits.rs:121-126）。C++ `Query(Edge2d)` 直接 `Query(edge.BoundingBox())`（Quadtree.cpp:91-94），无边级象限分类。

### `QTQueryable::quadrant_order` 启发式（qt_traits.rs:18-48）vs C++ 无对应
- 判定：C++ 缺失（性能启发式）
- 证据与说明：Rust Circle 按"圆心所在象限优先"重排搜索顺序（qt_traits.rs:26-48，如 right&&top → [0,1,3,2]），用于 `collides` 短路查询（qt_node.rs:118-124）。C++ `QueryNode` 固定 0..3 顺序遍历（Quadtree.cpp:295-299），且 C++ 查询本就返回全集合（SortUnique 后），无需顺序启发式。**注意**：Rust 的 quadrant_order 只影响 `collides`（二值短路）的搜索顺序，不影响 `collect_collisions_until`（固定 [0,1,2,3] 过滤 colliding_quadrants，qt_node.rs:183-187），故缺失不造成结果差异。

### `get_virtual_root`（cd_engine.rs:370-383）vs `QuadTree::VirtualRoot`（Quadtree.cpp:124-143）
- 判定：一致（算法逐行对应）
- 证据与说明：双侧都是"从根下行，找到完全 Surrounding 查询 bbox 的唯一子节点则下降，否则停"：Rust `children.iter().find(|child| child.bbox.relation_to(bbox) == GeoRelation::Surrounding)`（cd_engine.rs:374-376）；C++ `child->bounds.RelationToRustF32(queryBounds) == RectRelation::Surrounding`（Quadtree.cpp:132-134）。C++ 额外暴露 `VirtualRootBounds`/`QueryVirtualRoot` 公共 API（Quadtree.h:52-61）。
  - 一个边缘差异：Rust virtual root 下降依赖 `children` 存在（懒生成），若根无 children 则根即 virtual root；C++ 树总是从根 BuildNode 全量构建，children 存在与否由 maxDepth/edgeThreshold 决定。二者在相同 presence/阈值语义下到达的节点一致（见记录 20 差异 #4 的边界讨论）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 注册/注销：增量 constrict vs 全量 Rebuild | 结果等价/实现不同 | 每次 Register/Deregister O(全量 hazards)；树形态与查询结果一致 | qt_node.rs:39-95 vs Quadtree.cpp:44-81、322-339 |
| 2 | 节点载荷：QTHazardVec（presence+边子集+n_active_edges 缓存）vs Entry{handle,presence,edgeCount} | 结果等价/实现不同 | C++ 查询期每 entry 回 m_Hazards 对 queryBounds 现场重分类（O(顶点数)）；Rust 用缓存边子集 | qt_node.rs:13-25、qt_hazard_vec.rs:13-17 vs Quadtree.h:74-87、Quadtree.cpp:284-294 |
| 3 | cd_threshold 作用时点：查询时（n_active_edges 缓存）vs 建树时（activePartialEdges） | 结果等价/实现不同 | 树深不同、精确检测发生位置不同，结果集合一致 | qt_node.rs:176 vs Quadtree.cpp:252-264 |
| 4 | `collides` 的 Entire 快速路径 + Partial 叶级边测试 | 结果等价/实现不同 | C++ 无节点级短路；靠候选集+调用方精确测试达成同结果 | qt_node.rs:105-143 vs Quadtree.cpp:276-300 + CollisionEngine.cpp:282-294 |
| 5 | `quadrant_order` 启发式（Circle 优先象限） | C++ 缺失 | 仅影响 collides 短路速度，不影响结果 | qt_traits.rs:18-48 vs Quadtree.cpp:295-299 |
| 6 | Edge 专用象限分类（half_intersect + 共享边分摊） | C++ 缺失（退化为包围盒查询） | 结果由调用方 EdgeCollidesWithHazard 兜底保证 | qt_traits.rs:53-129 vs Quadtree.cpp:91-94 |
| 7 | virtual root | 一致 | — | cd_engine.rs:370-383 vs Quadtree.cpp:124-143 |
| 8 | `RegisterHazard(handle, Rect2d)` bounds-only 扩展 | C++ 公开导出、仓内无调用；对角 Edge 仅保存在根节点，子节点范围查询可能漏项 | Rust 无对应入口，不计 parity 覆盖 | Quadtree.h:46-47；Quadtree.cpp:579-598；B5 |
| 9 | 子节点懒生成+收缩（无 Partial 即丢 children）vs 重建时截断 | 结果等价/实现不同 | 同差异 #1 的树形态收敛 | qt_node.rs:61-76、88-90 vs Quadtree.cpp:264 |

## 结论
查询结果层面双侧等价：virtual root 下降、象限相交过滤、presence 语义（Entire 立即命中、Partial 精确几何复核）一一对应。核心架构差异是 Rust 的增量 constrict + 节点内缓存（presence/边子集/n_active_edges）对比 C++ 的全量重建 + 查询期对 queryBounds 现场重分类，后者把 Rust 的三层优化（Entire 短路、边子集、quadrant_order 启发式）全部让位于"宁可重算"的统一路径；cd_threshold/edgeThreshold 同源但作用时点不同（查询时 vs 建树时）。

## 补充审计（2026-09-29，逐行复核 2026-09-29 重写基线：Quadtree.cpp 当前 750 行）

重写后的 C++ 已与本记录"判定"列中标注的旧架构条目全面脱钩：现版本 Quadtree.cpp 是 qt_node/qt_hazard/qt_partial_hazard/qt_hazard_vec 的结构级移植（Node 持 NodeHazard 向量按 presence 降序、PartialHazard 边子集 + ffBbox、activeEdges 缓存、懒扩展、增量 constrict/deregister、VirtualRootFor）。上表差异 #1/#2/#3/#9 描述的"全量重建/不存边子集/建树期阈值"均不再成立。逐行复核发现以下**遗漏/偏差**，并对一条此前疑点作了当前源码复核：

### 补-1【2026-10-17 复核】`RegisterIntoNode` 按各 hazard 自身句柄查找形状
- Rust（qt_node.rs:39-82）：`register_hazard` 收到 `new_qt_haz` 后，懒扩展时对**每个已有 hazard** 调 `constrict_and_register_to_children`，其中 `qt_hazard.constrict(child_bboxes, haz_map)`（qt_node.rs:44）按**该 hazard 自身的 hkey** 从 `haz_map` 取形状（qt_hazard.rs:64 `haz_map[self.hkey].shape`）——每个 hazard 用自己的形状。
- C++（当前 workspace，Quadtree.cpp:469-521）：新条目的 `shapeIt` 按 `hazard.handle` 查表；懒扩展已有条目时另以 `existing.handle` 查 `existingShape`。新旧 hazard 分别使用自己的形状，当前源码与 Rust 的逐条查形状路径一致。
  - 懒扩展分支对已有 hazard 的 constrict（Quadtree.cpp:490-504）：先按 `existing.handle` 查找 `existingShape`，再将该形状传给 `Constrict(existing, ...)`。
  - 新 hazard 下放分支（Quadtree.cpp:506-519）：只有 `shapeIt` 存在且 shape 非空时才将新 hazard constrict 到子节点；bounds-only 重载没有完整 shape，因此不会分发。
- 复核结论：当前 workspace 源码没有使用新条目形状 constrict 旧条目的行为；原疑似差异不成立（静态核对，未运行差分用例）。bounds-only 条目本身没有 shape，因此在后续懒扩展时不会复制到新子节点，但仍保存在原节点；这与其只在根节点登记、虚拟根可能下钻而漏项的限制相同，详见补-3/B5。
- 判定：原疑似实现差异在当前源码中不成立（静态核对；未运行差分用例）。

### 补-2【遗漏】`collect_collisions_until` 的子节点遍历顺序：Rust 固定 0..3，C++ 圆路径按 `quadrantOrder` 启发式
- Rust（qt_node.rs:183-190）：`colliding_quadrants.iter().enumerate().filter(...)` 固定按象限下标 0..3 顺序递归，`quadrant_order` 只用于 `collides`（qt_node.rs:118-124），collect 路径**无**启发式排序（记录正文差异 #5 已正确指出这一点）。
- C++（CollisionEngine.cpp:390-414 `CollectCircleCollisionsUntil`）：子节点遍历使用 `circleQuadrantOrder`（CollisionEngine.cpp:404-405，Quadtree.cpp:209-223 的圆心象限优先序），与 Rust collect 路径的固定序不一致；Edge 版 `CollectEdgeCollisionsUntil`（CollisionEngine.cpp:355-361）固定 0..3 与 Rust 一致。
- 影响：仅早停时收集到的 hazard **集合顺序**（以及 stop 回调触发点）不同——`stop_after_collision` 为 true 时 collector 内残留的 hazard 选取可能与 Rust 不同（树中按序先到者留）。全量收集（回调恒 false）时结果集合一致。注意 C++ 的 `CircleCollides`/`NodeCollidesWithCircle`（detect 路径）用 quadrantOrder 与 Rust collides 一致，属移植者把启发式误用于 collect 路径。
- 判定：差异（早停语义下的顺序分叉；非全量收集的行为差异）。

### 补-3【B5 复核】bounds-only 对角线代理仅驻留根节点
- Rust（qt_hazard.rs:37-44）：`from_root` 恒以**整形状边集**构造根 Partial；Rust 无"无形状 hazard"（Hazard.shape 为 Arc<SPolygon> 必有值）。
- C++ bounds-only 重载（Quadtree.cpp:579-598）把 hazardBounds 保存为登记边界，并以其左下到右上的对角 Edge 建立根 Partial；该 RegisteredShape 的 shape 为空。RegisterIntoNode 只在有完整 shape 时才向子节点分发，所以 bounds-only hazard 留在根节点，不会在子节点中产生 Presence。Query 先调用 VirtualRootFor；当 queryBounds 完全落入一个子节点时会从该子节点开始遍历，不再访问根节点，因此这类查询可能漏掉该 hazard。此为源码路径推导，本轮未运行最小用例。该方法在头文件中带 `JAGUARCORE_API`，但仓内没有此重载调用；按 B5 保留并登记为 C++ 专有公开扩展，Rust 无对应入口，不计 parity 覆盖。
- 判定：C++ 公开扩展；仓内无调用；存在可由当前源码推导的子节点查询漏项，未作运行验证。

### 补-4【浅覆盖→已验证】qt_traits 逐行核验结果（记录正文按旧架构写的"Edge 查询用包围盒/无象限分类"已过时）
重写后 qt_traits.rs 的每个函数在 C++ 中都有逐行移植，记录未逐条登记，现补齐：
- `QTQueryable::collides_with_quadrants`（默认，qt_traits.rs:13-16）↔ `circleCollidesWithQuadrants`（Quadtree.cpp:199-206，Circle2d::IntersectsRustF32 逐象限，同 Rust `qs.map(collides_with(q))`）。判定：一致。
- `impl QTQueryable for Edge::collides_with_quadrants`（qt_traits.rs:54-129）↔ `edgeCollidesWithQuadrants`（Quadtree.cpp:117-194）：bbox 无重叠快判（135-140 ↔ qt_traits.rs:64-69）、端点包含（142 ↔ :70-72）、`std::all_of(has_value)` 早退（146-153 ↔ :80-82）、六次 halfIntersect（left/right/top/bottom/hBisect/vBisect 与象限共享边配对，Quadtree.cpp:171-188 ↔ qt_traits.rs:103-118）、`.value_or(false)` 收尾（Quadtree.cpp:190-193 ↔ :120）。判定：一致。一处细节差异：Rust `q.collides_with(&self.start)` 用 Rect::collides_with(Point) 的 f32 精确比较（rect.rs:300-305），C++ `q.ContainsRustF32`（Quadtree.cpp:142）同为 f32 比较但多一层 `static_cast<float>` 归一——数值同源，等价。
- `half_intersect`（qt_traits.rs:134-164）↔ `halfIntersect`（Quadtree.cpp:80-110）：`anyUndetermined` 短路（86-92 ↔ :140）、方向匹配（FirstHalf/Halfway→firstQuadrants、SecondHalf/Halfway→secondQuadrants，Quadtree.cpp:98-109 ↔ :144-162）。判定：一致。
- `edge_intersection_half`（qt_traits.rs:169-202）↔ `edgeIntersectionHalf`（Quadtree.cpp:40-77）：t/u 参数式（54-62 ↔ :176-187）、平行早退（58 ↔ :180-183）、`[0,1]` 双包含（63 ↔ :187）、按 0.5 三分（65-71 ↔ :188-197）。判定：一致（数值路径 f32 对 f32）。`CollisionHalf` 枚举（Quadtree.cpp:32-37 ↔ qt_traits.rs:204-208）一致。
- `impl QTQueryable for Circle::quadrant_order`（qt_traits.rs:26-48）↔ `circleQuadrantOrder`（Quadtree.cpp:209-223）：rowMask/containingQuadrant/firstAdjacentMask 位运算逐式对应（215-222 ↔ :27-37）；split 点取 `quadrants[0].xMin/yMin`（Quadtree.cpp:212 ↔ :26 `Point(quadrants[0].x_min, quadrants[0].y_min)`）。判定：一致。Rust 的 `debug_assert_eq!` 四象限模式表（qt_traits.rs:38-46）C++ 无对应断言（见记录 21 补充）。
- `Rect::QUADRANT_NEIGHBOR_LAYOUT`（rect.rs:141 `[[1,3],[0,2],[1,3],[0,2]]`）↔ `kQuadrantNeighborLayout`（Quadtree.cpp:15-16 同值）。判定：一致。
- `Rect::sides` 顺序核对：Rust sides = corners[0]→[1]→[2]→[3]（rect.rs:172-193，corners=[TR,TL,BL,BR]），即 [top(TR→TL), left(TL→BL), bottom(BL→BR), right(BR→TR)]。C++ `SidesRustF32() = EdgesRustF32()`（Edge.cpp:86-89），`EdgesRustF32` 由 `CornersRustF32()`（rect.h:164-169 [TR,TL,BL,BR]）连出（Edge.cpp:79-84）——顺序同为 [top,left,bottom,right]。Quadtree.cpp:159-162 按 `sides[0]=top,[1]=left,[2]=bottom,[3]=right` 取用，与 Rust `let [top, left, bottom, right] = r.sides()`（qt_traits.rs:89）一致。判定：一致（记录正文未提供该顺序证据，现补）。

### 补-5【遗漏】`Constrict` 的 ff_bbox 退化回退分支
- Rust（qt_partial_hazard.rs:42-53）：`from_parent` 在边集退化（`x_min < x_max && y_min < y_max` 不成立，即边全轴向对齐）时回退**父 ff_bbox**。
- C++（Quadtree.cpp:399-408）：`if (xMin < xMax && yMin < yMax)` 不成立时 `partial->ffBbox = hazard.partial->ffBbox` 同样回退父 ff_bbox；边数等于父时直接继承（Quadtree.cpp:374-377 ↔ qt_partial_hazard.rs:25-27）。判定：一致（记录 20 未展开此分支，见记录 20 补充）。

### 补-6【浅覆盖】`DeregisterFromNode` 折叠语义
- Rust（qt_node.rs:84-95）：`remove` 返回 Some 才递归/折叠；`no_partial_hazards()`（"无任何 Partial"）→ children=None。
- C++（Quadtree.cpp:523-541）：`RemoveHazard` 返回 false 即 return（525-526）；`HasPartialHazard()`（Quadtree.cpp:265-271）为 false 时 `node.children = {}`（528-531），否则递归。判定：一致（重写后逐行对应，正文差异 #1/#9 的旧"全量 Rebuild"描述过时）。
- 一个边缘差异：Rust `no_partial_hazards` 判定的是**整个 QTHazardVec 无 Partial**；C++ `HasPartialHazard` 同为整个向量无 Partial。子节点折叠后该节点的 None/Entire hazard 仍在向量中（双侧一致），但 C++ 折叠时**直接丢弃子节点中可能残留的同句柄 hazard 吗**？——Rust 语义：折叠发生在"无 Partial"时，子节点里该句柄必然全是 None/Entire；Entire 情形折叠后父节点仍持 Entire 条目（可查询），None 无影响。C++ 同。判定：一致。

### 补-7【遗漏】`Query(Rect2d)` 兼容接口的语义（重写后新增，正文未登记）
- 重写后 `QuadTree::Query`（Quadtree.cpp:605-644）= 从 `VirtualRootFor(m_Root, queryBounds)` 起 DFS，节点 bbox 与 queryBounds 相交才进，收集 presence != None 的 handle 去重排序。该接口与 Rust `collect_collisions_until` 的**候选集语义不同**（不做叶级精确边测试、无 cd_threshold、无 collector/回调），仅作为 C++ 兼容层存在（`QueryVirtualRoot` 直接转发，Quadtree.cpp:651-655）。Rust 侧无对应 API（Rust 查询走 CDEngine 的 collides/collect_collisions_until）。当前 CollisionEngine.cpp 的主检测路径（NodeCollidesWith*/Collect*CollisionsUntil）已不经 `Query`，`Query` 仅测试（TestCollision.cpp:52-54、71-72）使用。判定：C++ 额外（兼容层，非 Rust 对应物；`PresenceAt`（Quadtree.cpp:657-683）/`NodeCount`（685-688）同为测试辅助 API，Rust 无对应）。
