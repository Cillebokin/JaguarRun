# 21. quadtree-assertions 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/quadtree/assertions.rs`（48 行）+ 引用的 `util/assertions.rs`（280 行，quadtree 相关部分）
**C++**: `JaguarCore/Source/Support/Assertions.cpp`（413 行）中 quadtree 相关校验
**判定**: 不等价（C++ 缺全部 quadtree 专项自检；两侧断言机制本身也不同——Rust debug_assert 内联 vs C++ 正式校验 API）

## 函数逐条比对

### `edge_quadrant_classification_is_conservative`（quadtree/assertions.rs:3-12）
- C++ 对应：无
- 判定：C++ 缺失
- 证据与说明：Rust 在 `Edge::collides_with_quadrants`（qt_traits.rs:121-126）以 debug_assert 验证 f32 快速分类是 f64 精确解的保守超集（`!collides || classified`，即 oracle 说相交则分类必须说相交）。C++ 侧没有边-象限分类实现（见记录 19 差异 #6），自然无此断言；C++ 的边查询走包围盒 + 调用方 `IntersectsRustF32` 精确测试，不需要保守性证明。

### `edge_collisions_with_rects_f64` / `edge_collides_with_rect_f64`（quadtree/assertions.rs:14-48）
- C++ 对应：无
- 判定：C++ 缺失
- 证据与说明：f64 oracle：坐标区间重叠快筛（assertions.rs:27-31）+ 端点在矩形内（33-35）+ 四角符号法判跨立（40-47）。这是上面保守性断言的基准实现。C++ 无对应（其 Edge 相交判定 `IntersectsRustF32` 是生产代码非 oracle，记录 03）。

### `qt_contains_no_dangling_hazards`（util/assertions.rs:58-95）
- C++ 对应：无
- 判定：C++ 缺失
- 证据与说明：Rust 在 CDEngine 每次 register/deregister 后 debug_assert（cd_engine.rs:68、82、93）：子节点 hazards 的 entity 必须出现在父节点 hazards 中（util/assertions.rs:69-95，逐层递归）。C++ 的树由全量 Rebuild 从 `m_Hazards` 单一来源构建（Quadtree.cpp:336-339），结构上不存在增量维护引入悬挂引用的可能，故无此自检——**由架构差异天然豁免**，而非功能缺失。

### `layout_qt_matches_fresh_qt`（util/assertions.rs:98-111）+ `qt_nodes_match`（113-216）+ `hazards_match`（218-231）
- C++ 对应：无（`Assertions.cpp` 无任何 quadtree 内容）
- 判定：C++ 缺失
- 证据与说明：Rust 在 Layout 的每次状态变更后 debug_assert（entities/layout.rs:86、102、117）：克隆 base_cde、重放全部 placed_items 注册，与现役 quadtree 逐节点比对 hazards 的 (entity, presence) 集合与子树存在性。这是对增量 constrict 树正确性的端到端验证。C++ `Assertions.cpp` 的校验对象是快照一致性（`sameLayoutSnapshot`：placements/slotGenerations/freeSlotIndices/dynamicHazards，Assertions.cpp:129-169）与问题级需求（demand/bin stock），**不含四叉树内容比对**；C++ 的树由 RegisterHazard/Restore 全量重建（记录 19、22），无需"现役树 vs 新鲜树"对账。

### `collision_hazards_sorted_correctly`（util/assertions.rs:37-55）
- C++ 对应：无
- 判定：C++ 缺失（架构豁免）
- 证据与说明：验证 QTHazardVec 的 Entire-before-Partial 排序不变式（支撑 `strongest()`）。C++ 无排序 hazard 向量（记录 20 差异 #5），不适用。

### `quadrants_have_valid_layout`（util/assertions.rs:233-260）
- C++ 对应：无
- 判定：C++ 缺失（架构豁免）
- 证据与说明：验证象限数组符合 `QUADRANT_NEIGHBOR_LAYOUT`（相邻象限共享 2 个角点）。Rust 在 `constrict` 时 debug_assert（qt_hazard.rs:56）。C++ 象限由 `bounds.QuadrantsRustF32()` 一次性生成（Quadtree.cpp:267），布局恒定，无验证需求。

### C++ 侧对应物范围核查
- 判定：C++ 额外（方向相反：正式校验 vs 调试断言）
- 证据与说明：`Assertions.cpp` 提供的是 **release 也可用的公开校验 API**：`ValidateLayoutSnapshot`/`ValidateLayout`（210-223）、`ValidateSPSolution`（225-253）、`ValidateBPSolution`（255-319）、`ValidateMSPSolution`（321-376）及 DebugAssert* 包装（378-412）。其中与 CDE 相关的仅有快照里的 dynamicHazards 集合比对（`sameHazard`，Assertions.cpp:123-127、152-167）。**没有任何函数检查 QuadTree 节点内容、presence 或分类结果**。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `edge_quadrant_classification_is_conservative` + f64 oracle | C++ 缺失 | C++ 无边级象限分类，断言对象不存在（架构豁免） | assertions.rs:3-48 vs — |
| 2 | `qt_contains_no_dangling_hazards` | C++ 缺失（架构豁免） | 全量重建无悬挂引用可能 | util/assertions.rs:58-95 vs Quadtree.cpp:336-339 |
| 3 | `layout_qt_matches_fresh_qt` + `qt_nodes_match` + `hazards_match` | C++ 缺失 | 无"现役树 vs 新鲜树"对账；全量重建架构下需求消失 | util/assertions.rs:98-231 vs Assertions.cpp（全文无 quadtree 校验） |
| 4 | `collision_hazards_sorted_correctly` | C++ 缺失（架构豁免） | C++ 无排序 hazard 向量 | util/assertions.rs:37-55 vs — |
| 5 | `quadrants_have_valid_layout` | C++ 缺失（架构豁免） | C++ 象限布局恒定 | util/assertions.rs:233-260 vs Quadtree.cpp:267 |
| 6 | 正式校验 API（Validate*）| C++ 额外 | 断言→release 校验的机制迁移（记录 39） | — vs Assertions.cpp:210-412 |

## 结论
Rust 的 4 项 quadtree 专项自检（保守性 oracle、无悬挂 hazard、树 vs 新鲜树对账、排序/布局不变式）在 C++ 中全部没有直接对应；但这属架构差异的必然结果——C++ 树由单一数据源全量重建、无排序向量、无边级象限分类，这些断言保护的不变式要么不可能被破坏、要么断言对象不存在。C++ 的 Assertions.cpp 走的是另一条路：release 可用的快照/问题级正式校验，与 Rust 的 debug_assert 体系在机制与覆盖面上均不同（详见记录 39）。

## 补充审计（2026-09-29，逐行复核 2026-09-29 重写基线）

**前提变化**：正文多处以"架构豁免"为由豁免 C++ 缺失——但 2026-09-29 重写后 C++ 四叉树**已是增量 constrict 树**（懒扩展、QTHazardVec 排序向量、PartialHazard 边子集，见记录 19/20 补充），这些断言保护的旧前提已不存在，豁免理由部分失效。逐条复核：

### 补-1【浅覆盖→判定反转】`edge_quadrant_classification_is_conservative` + f64 oracle 的"架构豁免"已失效
- 正文差异 #1 称"C++ 无边级象限分类，断言对象不存在（架构豁免）"。重写后 `edgeCollidesWithQuadrants`（Quadtree.cpp:117-194）正是 Rust `Edge::collides_with_quadrants` 的 f32 生产实现，被 `NodeCollidesWithEdge`/`CollectEdgeCollisionsUntil`（CollisionEngine.cpp:260、353）调用——**保守性断言的对象在 C++ 中现在存在**，但既无 debug 断言也无 oracle。Rust 在每次调用后用 f64 oracle 验证 f32 分类是保守超集（qt_traits.rs:121-126）；C++ 的同类保护完全缺失。若 f32 分类出现**假阴性**（漏报相交象限），C++ 无任何防线（Rust debug 构建会 panic）。风险与 Rust 引入该断言的动机同源（tests.rs 锁定的正是 f32 假阳性/假阴性边界行为）。
- 判定：C++ 缺失（豁免理由失效，真实测试缺口）。

### 补-2【浅覆盖→判定变化】`qt_contains_no_dangling_hazards` 的"架构豁免"已失效
- 正文差异 #2 称"C++ 全量重建无悬挂引用可能"。重写后 C++ 树为增量维护（RegisterIntoNode/DeregisterFromNode，Quadtree.cpp:469-541），与 Rust 相同的悬挂引用风险重新出现——尤其是记录 19 补-1 发现的 shapeIt 传错形状 bug 正属于这类增量维护错误的范畴，若有此断言体系（或对应测试）更易暴露。C++ 仍无对应自检。
- 判定：C++ 缺失（豁免理由失效；与补-4 的测试缺口叠加）。

### 补-3【验证】`collision_hazards_sorted_correctly` / `quadrants_have_valid_layout` 缺失现状
- 重写后 C++ `Node::AddHazard` 维护 presence 降序向量（Quadtree.cpp:226-249），排序不变式真实存在，但无断言复核（Rust `assert_caches_correct`，qt_hazard_vec.rs:103-121，同时复核排序 + activeEdges 对账）。判定：C++ 缺失（豁免理由失效）。
- `quadrants_have_valid_layout`（util/assertions.rs:233-260）：C++ 象限由 `QuadrantsRustF32()`（rect.h:300-319）固定生成，布局恒定——**此条豁免仍成立**（Quadtree.cpp 的 kQuadrantNeighborLayout 为编译期常量）。判定：维持 C++ 缺失（豁免有效）。

### 补-4【遗漏】tests.rs 行为契约（36 行）的 C++ 对应测试缺口
本记录正文完全未覆盖 `tests.rs`（审计任务指明需检查）。`edge_quadrants_allow_f32_false_positive_at_bisector`（tests.rs:6-36）锁定的行为契约：
- 构造 rect=(0,6.1875,100,106.1875)（centroid.y 恰为非整 f32），edge=(-29.439_144,-32.354_836)→(51.902_332,212.291_02) 穿过水平 bisector 且穿越点在 f64 下高于 bisector 但差值小于 1 ULP；
- 断言三件事：① `edge.collides_with(&quadrant)`（原始 Rect-Edge 测试）对 4 象限为 [false,true,true,false]（象限 1/2 出现 f32 假阳性）；② f64 oracle 为 [false,true,false,false]；③ **`edge.collides_with_quadrants(&rect, quadrants)`（快速分类）必须与 f64 oracle 一致 = [false,true,false,false]**——即快速分类在 bisector 附近**不允许**继承原始判交的假阳性，它必须更精确。
- C++ 侧核验：全仓（tests/ 17 个文件）搜索无 6.1875/106.1875/bisector/该坐标常量；`edgeCollidesWithQuadrants`/`halfIntersect`/`CollisionHalf` 无任何测试引用（仅 Quadtree.cpp 与 CollisionEngine.cpp 两个生产文件使用）。TestCollision.cpp 有 notch/pier oracle 场景（TestCollision.cpp:543-648，notchContainer 高低阈值下 detect/collect 分歧的锁定测试），但**无 bisector f32 边界契约测试**。
- 影响：C++ 的 `edgeCollidesWithQuadrants` 是 Rust 快速分类的移植（记录 19 补-4 已确认逐行一致），当前行为一致，但"bisector 附近 f32 假阳性被快速分类修正"这一微妙契约**无 C++ 测试锁定**——后续 C++ 侧重构（如改用 double 中间量、调整 halfIntersect 顺序）可能无声破坏该行为，而测试全绿。
- 判定：C++ 缺失（测试契约缺口，建议补测试）。
