# 10. Simplify（shape_modification.rs 简化部分）逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/shape_modification.rs`（550 行，本记录覆盖 simplify 相关：52-336 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/ShapeModification.h`（53 行）+ `JaguarCore/Source/Geometry/ShapeModification/Simplify.cpp`（350 行）
**判定**: 基本等价（算法逐行等价，含 f32 精度模拟；panic→Result；一处排序稳定性细节需注意）

## 函数逐条比对

### `enum ShapeModifyMode`（Rust 19-25 行）
- C++ 对应：`ShapeModification.h:14-18` `enum class ShapeModifyMode { Inflate, Deflate }`。
- 判定：一致。

### `struct ShapeModifyConfig`（Rust 27-45 行）
- C++ 对应：`ShapeModification.h:21-26`。
- 判定：一致（`simplify_tolerance: Option<f32>` ↔ `std::optional<double> simplifyTolerance`；`offset` ↔ `offset`；`narrow_concavity_cutoff: Option<(f32,f32)>` ↔ `std::optional<std::pair<double,double>> narrowConcavityCutoff`）。存储类型 f32 vs double 属全局约定（RustF32 模拟），使用处均经 `asRustF32` 收敛。

### `simplify_shape`（Rust 52-152 行）
- C++ 对应：`SimplifyShape`（Simplify.cpp:311-349）。
- 判定：基本等价。

逐段对照：

**外层循环与退出条件**（Rust 61-66：`for _ in 0..shape.n_vertices()`，`n_points < 4` break）
- C++ cpp:319-322：`for iteration < shape.VertexCount()`，`points.size() < 4U` break。
- 一致。

**角点生成**（Rust 68-86）
- C++ 对应拆为 `generateCandidates`（cpp:239-277）内部的 corners 构造 cpp:242-250：
```cpp
corners.push_back(Corner{i == 0U ? points.size() - 1U : i - 1U, i, (i + 1U) % points.size()});
```
- Rust 70-71 用 `(i - 1).rem_euclid(n_points)` / `(i + 1).rem_euclid(n_points)`，数学上与 C++ 的条件式/取模一致（`i==0` 前驱是 `n-1`）。
- **Deflate 反转**：Rust 80-86 `corners.reverse()` + 逐个 `Corner::flip()` ↔ C++ cpp:252-257 `std::reverse` + `std::swap(corner.previous, corner.next)`。一致。

**候选生成**（Rust 88-107）
- C++ cpp:259-275：`Concave→Concave 候选`、`Collinear→Collinear 候选`、`Convex 且 prev 为 Convex→ConvexConvex(prev, corner)`。
- 逐条一致：Rust 98-105 的 match 分支 `(Concave, _)`/`(Collinear, _)`/`(Convex, Convex)`/`(_,_)` ↔ C++ cpp:265-271 的 if-else 链（kind==Concave / kind==Collinear / previousKind==Convex——注意 C++ 走到第三个分支时 kind 必为 Convex，因前两个分支已拦截）。
- 初始 `prev_corner = corners.last()`（Rust 90）↔ C++ cpp:260 `corners.back()`。一致。

**候选排序与择优**（Rust 110-115）
```rust
candidates.iter()
    .sorted_by_cached_key(|c| OrderedFloat(calculate_area_delta(&ref_points, c).unwrap_or(f32::INFINITY)))
    .find(|c| candidate_is_valid(&ref_points, c))
```
- C++ cpp:325-335：`std::stable_sort`（按 `candidateAreaDelta` 升序，无效 ConvexConvex 返回 `infinity`，cpp:149）+ `std::find_if(candidateIsValid)`。
- 判定：一致（含 `unwrap_or(INFINITY)` ↔ 无 replacement 时返回 `std::numeric_limits<float>::infinity()` 的 cpp:148-149）。
- 细节：Rust `sorted_by_cached_key` 是稳定排序，C++ 用 `stable_sort`，稳定性语义对齐。Rust `OrderedFloat` 对 NaN 会 panic，C++ `candidateAreaDelta` 不产生 NaN（与 Rust 相同，面积公式在有限输入下有限）。
- 性质差异（不影响选择结果）：Rust 每次迭代重新计算所有 delta（缓存于 sorted_by_cached_key），C++ 在比较器中反复计算（O(n log n) 次调用 vs Rust 的 O(n) 次缓存）。纯性能差，无行为差。

**执行候选与面积守卫**（Rust 118-135）
- Rust：`execute_candidate` → `SPolygon::calculate_area(&new_shape)` → `area_delta = |new - original| / original`，`if area_delta <= max_area_change_ratio` 执行 else break。
- C++ cpp:339-345：`executeCandidate` → `signedAreaRustF32(newPoints)`（cpp:48-61，逐项 f32 模拟 Rust `SPolygon::calculate_area` 的 `(y+nextY)*(x-nextX)` 累加公式）→ `areaDelta = |new - originalArea| / originalArea`，`if (!(areaDelta <= maxAreaChangeRatioF32)) break;`。
- 判定：一致。`!(a<=b)` 写法等价于 `a>b` 并保留 NaN 时 break 的语义（Rust `area_delta <= max` 对 NaN 为 false 同样 break）。`originalArea` 取 `asRustF32(shape.Area())`（cpp:316）↔ Rust `shape.area`（f32 缓存）。

**收尾**（Rust 138-151：`SPolygon::new(ref_points).unwrap()` + info 日志）
- C++ cpp:348：`return SimplePolygon::Create(std::move(points));`——`Create` 是 `Result` 版本，失败向上传（Rust 是 `unwrap()` panic）。
- 判定：差异（panic→Result）。日志（`info!` 两条）在 C++ 中无对应输出，无行为影响。

### `calculate_area_delta`（Rust 154-182 行）
- C++ 对应：`candidateAreaDelta`（cpp:133-152）。
- 判定：一致。
- `Collinear → 0.0`（Rust 157 ↔ cpp:136-137）。
- `Concave → 三角形面积`（Rust 158-167 用鞋带公式取 abs；C++ `triangleArea` cpp:117-131 同公式同 abs，且坐标先 `asRustF32` 再运算，保持 f32 精度）。
- `ConvexConvex →` Rust 168-179：先 `replacing_vertex_convex_convex_candidate(...)?`，失败时返回 `Err(InvalidCandidate)`（上游 `unwrap_or(INFINITY)`）；C++ cpp:146-151：`replacingVertex` 失败返回 `infinity`。一致。三角形顶点（`c1.1`, replacing, `c2.1`）↔（`first.current`, replacement, `second.current`）一致。

### `candidate_is_valid`（Rust 184-216 行）
- C++ 对应：`candidateIsValid`（cpp:166-212）。
- 判定：一致。
- `Collinear → true`（Rust 187 ↔ cpp:169-170）。
- `Concave`：新边 `Edge(shape[c.0], shape[c.2])`，过滤掉端点命中受影响点集 `[c.0, c.1, c.2]` 的边，检查 `!collides_with`（Rust 188-197）↔ C++ cpp:172-188（`affected` 数组前 3 个 + `hasAffectedEndpoint(edge, affected, 3U)` + `IntersectsRustF32`）。一致。
- `ConvexConvex`：replacement 失败 → false；两条新边 `(c1.0, new)`、`(new, c2.2)`；受影响点集 `[c1.1, c1.0, c2.1, c2.2]`（Rust 198-214）↔ C++ cpp:190-211（`affected` 四元素、`IntersectsRustF32` 两条新边）。一致。
- `Edge::try_new(...).unwrap()`（Rust 189, 202-203）在 C++ 中是直接构造 `Edge2d{a,b}`（构造无校验）：C++ 无 panic 路径，行为上等价（Rust 零长边 unwrap 不会触发，因 try_new 仅在 NaN 等情况失败）。

### `edge_iter`（Rust 218-224 行）
- C++ 对应：内联于 `candidateIsValid` 的 for 循环（cpp:179-181, 200-202：`Edge2d{points[i], points[(i+1U)%points.size()]}`）。
- 判定：一致（相同环形边枚举）。

### `execute_candidate`（Rust 226-242 行）
- C++ 对应：`executeCandidate`（cpp:214-237）。
- 判定：一致。
- `Collinear|Concave → remove(c.1)`（Rust 229-231）↔ C++ cpp:218-222（非 ConvexConvex 时 erase `first.current`）。
- `ConvexConvex`（Rust 232-239）：`remove(c1.1)`；`other_index = c1.1 < c2.1 ? c2.1-1 : c2.1`；`remove(other_index)`；`insert(other_index, replacing_vertex)` ↔ C++ cpp:225-235 逐句相同（`first.current < second.current ? second.current - 1U : second.current`）。
- Rust 234 行 `.expect("invalid candidate cannot be executed")` ↔ C++ cpp:225 `*replacingVertex(...)`（解引用 optional）：C++ 依赖调用前已验证 valid（`find_if` 只选 valid 候选），与 Rust 的 expect 语义一致（同样"不应到达"）。

### `replacing_vertex_convex_convex_candidate`（Rust 244-255 行）
- C++ 对应：`replacingVertex`（cpp:81-115）。
- 判定：一致。
- Rust 248-249 `assert_eq!(c1.2, c2.1)` / `assert_eq!(c1.1, c2.0)`（连续角点断言）↔ C++ cpp:85-86 `if (first.next != second.current || first.current != second.previous) return std::nullopt;`——**语义方向不同**：Rust 断言失败 panic，C++ 返回 nullopt 使候选无效。实际调用链中角点序列由生成器保证连续（ConvexConvex 只在相邻角点间产生），该防御路径正常不可达；若可达，Rust 崩溃、C++ 静默拒绝候选。属 panic→Result 约定的变体。
- 边构造：`Edge(shape[c1.0], shape[c1.1])`、`Edge(shape[c2.2], shape[c2.1])`（Rust 251-252）↔ C++ cpp:88-95 坐标一致（`x4,y4` 取 `second.current`，`x3,y3` 取 `second.next`，与 Rust edge_next=(c2.2, c2.1) 的 start/end 对应关系相同）。

### `calculate_intersection_in_front`（Rust 257-287 行）
- C++ 对应：内联于 `replacingVertex` 的 t/u 求交（cpp:97-114）。
- 判定：一致（逐公式对照）。
- `t_nom/t_denom/u_nom/u_denom` 四式（Rust 270-274）↔ cpp:97-104 逐字相同。
- `t_denom == 0.0 → t = 0.0`（Rust 276）↔ cpp:106；`u` 同（Rust 278 ↔ cpp:107）。
- 命中条件 `t < 0.0 && u < 0.0`（Rust 280）↔ cpp:108；交点 `Point(x2 + t*(x1-x2), y2 + t*(y1-y2))`（Rust 282）↔ cpp:110-112。一致。
- 精度：C++ 全程 float（`asRustF32` 后运算），与 Rust f32 对齐；仅返回值提升回 double 存储。

### `enum Candidate` / `struct Corner` / `impl Corner`（Rust 289-311 行）
- C++ 对应：`CandidateKind/Candidate/Cornert`（cpp:15-34）与 `flip`/`to_points` 内联。
- 判定：一致（`Corner(pub usize, usize, usize)` ↔ `Corner{previous,current,next}`；`flip` 交换 0↔2 ↔ cpp:256 `swap(previous, next)`）。

### `enum CornerType` + `from`（Rust 313-336 行）
- C++ 对应：`CornerKind` + `cornerKind`（cpp:36-79）。
- 判定：一致。
- 叉积 `p1p2.0*p1p3.1 - p1p2.1*p1p3.0`（Rust 327）↔ cpp:73 `firstX*secondY - firstY*secondX`（其中 first=second-first、second 向量同 Rust p1p3）。
- 符号映射：`Less→Concave, Equal→Collinear, Greater→Convex`（Rust 330-334）↔ cpp:74-78（`<0→Concave, >0→Convex, 否则 Collinear`）。一致。
- Rust 330 `partial_cmp(&0.0).expect("cross product is NaN")`（NaN panic）在 C++ 无对应（float NaN 比较 `NaN<0`/`NaN>0` 均 false → Collinear）。NaN 输入下行为不同：Rust panic、C++ 归为 Collinear。属边缘输入差异，正常输入不可达。

### `shape_modification_valid`（Rust 521-550 行，虽属本文件但被 OriginalShape 步骤 5 调用，归本记录）
- C++ 对应：`ShapeModificationValid`（Simplify.cpp:280-309）。
- 判定：一致。
- 边容差 `simpl.diameter * 1e-6`（Rust 527）↔ cpp:284-285 `modified.Diameter() * 1.0e-6F`。
- 逐顶点：`on_edge → 通过`；`Inflate: 顶点不在 simpl 内 → 错`；`Deflate: 顶点在 simpl 内 → 错`（Rust 530-547）↔ cpp:286-307（`ContainsRustF32` 判内部）。
- Rust 530 的过滤 `!simpl.vertices.contains(p)`（精确坐标去重免检）在 C++ 中不存在：C++ 对所有原顶点做 on_edge/inside 检查。差异极小——被保留的顶点必然 on_edge（距离 0 < 容差）或 inside，结论相同；仅在顶点恰在 simpl 边上但容差判 0 距离的数值边缘下等价。实际上 C++ 的 `onModifiedEdge` 检查涵盖了 `contains` 的情形，行为等价。
- Rust 539-547 的 `error!` 日志无 C++ 对应（仅返回值），无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 主循环结构/退出条件（n_vertices 轮、<4 break、无候选 break、面积超限 break） | 一致 | 无 | Rust 61-135 vs Simplify.cpp:319-346 |
| 2 | Deflate 模式 corners reverse + flip | 一致 | 无 | Rust 80-86 vs cpp:252-257 |
| 3 | 候选生成三分支 + prev 追踪 | 一致 | 无 | Rust 88-107 vs cpp:259-275 |
| 4 | 候选排序（稳定、无效=∞）+ find first valid | 一致 | 无 | Rust 110-115 vs cpp:325-335 |
| 5 | 面积守卫 `<=` 与 NaN-break 语义 | 一致 | 无 | Rust 122 vs cpp:343 |
| 6 | 收尾 `SPolygon::new().unwrap()` → `SimplePolygon::Create` Result | 差异 | panic→Result | Rust 138 vs cpp:348 |
| 7 | 连续角点断言 assert_eq → nullopt | 差异 | 不可达防御路径：Rust panic / C++ 拒候选 | Rust 248-249 vs cpp:85-86 |
| 8 | 叉积 NaN panic → NaN 归 Collinear | 差异 | 仅 NaN 输入边缘 | Rust 330 vs cpp:74-78 |
| 9 | info!/error! 日志 | C++ 缺失 | 无行为影响 | Rust 123-127, 141-149, 539-545 |
| 10 | f32 精度模拟（asRustF32/triangleArea/signedAreaRustF32/cornerKind/replacingVertex 全 float 运算） | 一致 | 与 Rust f32 逐位对齐 | cpp:43-61, 69-73, 88-113, 121-129 |
| 11 | sorted_by_cached_key 缓存 vs 比较器重算 | 一致（性能差） | 无行为差 | Rust 112-114 vs cpp:325-330 |

## 结论
simplify 算法（角点分类→候选生成→按面积增量稳定排序→取首个有效候选→执行→面积比守卫）在双侧逐行等价，包括 Deflate 的反转/翻转、交点"前方"判定公式、f32 精度路径与执行时的双删除+插入顺序。所有差异均为错误通道（unwrap/assert/panic → Result/nullopt）与日志缺失，正常输入下的输出多边形完全一致。

## 补充审计（2026-09-29）

逐行复核 shape_modification.rs 的 simplify 段（52-336 行：simplify_shape、calculate_area_delta、candidate_is_valid、edge_iter、execute_candidate、replacing_vertex_convex_convex_candidate、calculate_intersection_in_front、InvalidCandidate、Candidate、Corner+flip/to_points、CornerType+from）。与记录 11（338-379）、12（381-519）三段合计覆盖 52-519 行；**三段缝隙核查**：338-379 由 11 覆盖、381-519 由 12 覆盖、521-550（shape_modification_valid）由本记录覆盖；1-51 行（imports + ShapeModifyMode + ShapeModifyConfig）由本记录头部两条覆盖；**550 行全部行段无三段都漏的函数**（文件实际 550 行整）。以下为补充核对项：

### 【遗漏→补齐】ShapeModifyMode 的 derive 集合（Serialize/Deserialize/Eq/Hash）
- shape_modification.rs:19 `#[derive(Serialize, Deserialize, Clone, Copy, Debug, PartialEq, Eq, Hash)]`——该枚举可序列化（JSON 配置导入用）且可哈希。主记录 10 的枚举条只对了值域。C++ 侧：ShapeModification.h:14-18 enum class（天然值语义），**无枚举序列化**——JSON 层的字符串映射（"Inflate"/"Deflate"）属 io 层（34 号记录范围）。Eq/Hash 无 C++ 需求（无以 mode 为键的容器）。
- 判定：【一致】（值域；序列化归 io 层）。
- 证据：shape_modification.rs:19 vs ShapeModification.h:14-18。

### 【遗漏→补齐】ShapeModifyConfig 的 Default derive（simplify/offset/narrow_concavity 全 None）
- shape_modification.rs:27 `#[derive(Serialize, Deserialize, Clone, Copy, Debug, Default, PartialEq)]`——`Default` 使三字段全 None（不修改）。C++ ShapeModification.h:21-26 靠 std::optional 默认空达成同一状态；PartialEq 经 34 号 JSON 层核对。主记录 10 的 config 条对了字段，未提 Default 语义——C++ 聚合默认构造 `ShapeModifyConfig{}` 等价（optional 默认空）。
- 判定：【一致】。
- 证据：shape_modification.rs:27 vs ShapeModification.h:21-26。

### 【浅覆盖→补齐】candidateIsValid 的 affected 判定走 Point PartialEq（而非 Edge PartialEq）
- Rust shape_modification.rs:190/205 `affected_points.contains(&l.start)`——`[Point; 3]/[Point; 4]` 的 contains 用 **Point 的 PartialEq**（±0.0 相等、NaN 不等）。C++ Simplify.cpp:160 `edge.start == affected[i]` 用 Point2d::operator==（point.h:42，同语义）。注意 array 第 4 元素：Rust Concave 分支 `[shape[c.0], shape[c.1], shape[c.2]]` 是**真三元组**；C++ 用定长 array 4 元素第 4 个填 `Point2d{}`（Simplify.cpp:178）且 `hasAffectedEndpoint(edge, affected, 3U)` 只查前 3——**等价**（第 4 元素哨兵不参与）。ConvexConvex 分支两侧都是真 4 元素。
- 判定：【一致】。
- 证据：shape_modification.rs:190, 205 vs Simplify.cpp:160-164, 176-178, 197-204。

### 【浅覆盖→补齐】sorted_by_cached_key 的"缓存"语义与 stable_sort 比较器重算的行为等价性确认
- 主记录差异表 #11 已列性能差。补充一个理论边界：`candidateAreaDelta` 对**同一 Candidate** 在排序期间被 C++ 反复调用，每次返回**相同的纯函数值**（无内部状态；replacingVertex 纯函数）——故缓存与重算结果恒等，比较器一致性成立（违反 strict weak ordering 的风险为零）。
- 判定：【一致】（确认）。
- 证据：Rust 112-114 vs Simplify.cpp:325-330 + candidateAreaDelta 纯函数性。

### 【浅覆盖→补齐】find 首个 valid 与 stable_sort 的联动（同 delta 候选的选取次序）
- Rust `sorted_by_cached_key(...).find(valid)`：稳定排序保相等键的原始序（candidates 生成序），find 取首个 valid。C++ stable_sort + find_if：同。**关键确认**：相等 delta（如多个 Collinear 候选 delta=0.0）的排序稳定性使两侧选取**同一个**候选（生成序由 corners 遍历序决定，两侧同序）。这消除了"多个零面积候选任选其一"的潜在分叉。
- 判定：【一致】。
- 证据：shape_modification.rs:110-115 vs Simplify.cpp:325-335 + generateCandidates 遍历序（Simplify.cpp:242-275 ↔ shape_modification.rs:68-107）。

### 【遗漏→补齐】cfg(test)——shape_modification.rs 无测试模块
- 全文件 550 行无 `#[cfg(test)]`（RustOracle 工具承担 oracle 职责）。判定：不适用。

补充结论：10 记录覆盖完整。三段记录（10/11/12）对 550 行的行段分配核查完毕：**无缝隙遗漏**；ShapeModifyMode/Config 的 derive 契约与 affected 判定的 Point 相等语义、稳定排序联动均已确认一致。

### E1 RustF32调用点与实际验收预算复核（2026-10-22）

本节针对JaguarCore调用点偏差预算，参考Rust源取自`JaguarCore/Tools/RustOracle/Cargo.lock`锁定的jagua-rs revision`f5e2b5ffd14931f17a20ce283a2122e877953ceb`，没有用本地fork推断pinned行为。

**算法内的数值判定并不使用统一的`AlmostEqualRustF32`容差：**

- `cornerKind`先把点收窄为f32，再以叉积`<0`、`>0`分类，等于零才归为Collinear；没有epsilon/ULP带。
- 交点参数`t/u`以f32计算，分母只按`==0`处理，交点资格用严格`<0`判断。
- 候选面积增量、原始面积、简化后面积和面积变化率都在f32域计算；候选按Rust`OrderedFloat`稳定排序，C++按同值稳定的`std::stable_sort`排序，选取首个有效候选。相同面积键不会通过FPA容差折叠成同一键。
- 面积守卫是`area_delta <= max_area_change_ratio`的严格f32比较；`ShapeModificationValid`另以`diameter * 1e-6F`为边界并使用严格距离`<`，边交叉/包含最终输出布尔值。它们是算法阈值和谓词规则，不是最终多边形坐标的容差预算。
- 若中间f32运算产生非有限值，候选排序键及终局面积守卫仍需单独核对；本节的固定输入记录不能证明所有有限坐标的中间运算都保持有限。

**当前测试实际覆盖：**

| 测试入口 | 当前断言 | Rust数值预算判定 |
|---|---|---|
| `CompareGeometry.cpp::runB2ShapeModificationDifferentialTests` | 通过`shape_modification_b2.txt`对比三组`CloseNarrowConcavities`和六组`SimplifyShape`固定输出；Simplify覆盖窄凹口及凸bevel的Inflate/Deflate，以及冗余共线点在零阈值和负最小次正规阈值下的输出 | Simplify向量长度精确相等，顶点数、面积和有序坐标使用`AlmostEqualRustF32`；该六行矩阵是pinned逐输出证据，但不覆盖其他形状、阈值、候选排序并列组合和错误出口。 |
| `CompareGeometry.cpp::runGeometryDifferentialTests` | Inflate/Deflate各检查`HasValue`、顶点数不增加、`ShapeModificationValid`为true | 只有结构和不变式，无Rust期望几何坐标/面积预算。 |
| `TestShapeModification.cpp`冗余点、NaN/Inf/负阈值用例 | 冗余点结果顶点数为4；NaN阈值只断言顶点数不变；Inf阈值只断言成功；负阈值比较原形面积差`<1e-12` | 这些是C++局部契约断言；除源码注释说明Rust不拒绝负值外，没有同revision数值fixture作逐项对比。 |
| `TestShapeModification.cpp`bevel固定期望 | 顶点坐标绝对差`<1e-5`、面积绝对差`<1e-4`，顶点数量精确相等 | 这是当前最明确的Simplify坐标/面积阈值，但测试代码没有标注其pinned Rust revision或fixture生成证据，故只能登记为固定期望阈值，不能视为完整Rust差分预算。 |

**结论**：Simplify的源级f32决策路径已有逐函数映射，且排序/面积门槛并非近似比较。新增的六行pinned Rust fixture覆盖了上述固定形状与阈值，并由B2差分入口消费；它为这些样例建立了`AlmostEqualRustF32`输出预算，不代表其他简化分支、错误出口或全部输入域已核。因此Simplify仍是局部输出预算已建立、完整范围未闭合，E1保持部分状态。
