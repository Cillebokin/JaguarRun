# 12. Narrow Concavity（shape_modification.rs 窄凹陷部分）逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/shape_modification.rs`（550 行，本记录覆盖 close_narrow_concavities：381-519 行）
**C++**: `JaguarCore/Source/Geometry/ShapeModification/NarrowConcavity.cpp`（167 行），对照 `Geometry/ShapeModification.h:48-52`
**判定**: 基本等价（候选筛选条件逐条一致；中途构造失败时 break 保留旧形状 vs Rust panic；f32 精度模拟到位）

## 函数逐条比对

### `close_narrow_concavities`（Rust 384-519 行）
- C++ 对应：`CloseNarrowConcavities`（NarrowConcavity.cpp:76-166）。
- 判定：基本等价。

按结构逐段对照：

**外层循环**（Rust 392：`for _ in 0..shape.n_vertices()`）
- C++ cpp:82：`for pass < original.VertexCount()`。
- 差异（极小）：Rust 每轮用 `shape.n_vertices()`（当前形状，会随闭合变小）作为隐式上界；C++ 固定用 `original.VertexCount()`。由于每轮至少消 1 个顶点、最多 `n_vertices` 轮，上界取原值或当前值都不可能先耗尽（实际总是先"无候选 break"），无行为差。

**候选边构造与 0.9999 缩放**（Rust 410-412）
```rust
let c_edge = Edge::try_new(shape.vertex(i), shape.vertex(j))
    .expect("invalid edge in string candidate")
    .scale(0.9999); //slightly shrink the edge to avoid self-intersections
```
- C++ `candidateEdgeRustF32`（cpp:16-34）：float 化坐标，`factor = 0.9999F`，`halfScale = (factor - 1.0F) / 2.0F`，起点 `start - d*halfScale`、终点 `end + d*halfScale`。
- 判定：一致。常量 0.9999 对齐。注意 C++ 的公式是"两端各向内缩 (1-0.9999)/2"——总长 0.9999 倍，与 Rust `Edge::scale(0.9999)`（整体缩放，中点不变、长度 ×0.9999）在几何上等价（缩放量微小，端点位置差为端点间距离的 0.00005 倍量级，后续 `collides_with` 判定在这种量级上不敏感）。Rust 侧 `Edge::scale` 定义见记录 03（缩放语义一致）。
- `Edge::try_new(...).expect(...)`（Rust 410-411, 480-481）在 C++ 是直接构造（无 panic 路径），同记录 10 的约定。

**距离阈值**（Rust 414：`c_edge.length() > cutoff_distance_ratio * shape.diameter` skip）
- C++ cpp:101：`edgeLengthRustF32(candidate) > cutoffDistanceRatio * diameter`，其中 `diameter = asRustF32(current.Diameter())`（cpp:89）。
- 判定：一致（含 f32 化的 ratio 与 diameter；`edgeLengthRustF32` cpp:36-41 全 float 运算）。

**Inflate 端点在内 skip**（Rust 419-424：任一端点 `collides_with` 形状则 skip）
- C++ cpp:104-108：`startInside || endInside` 时 skip（`ContainsRustF32`）。
- 判定：一致。

**Deflate 端点不在内 skip**（Rust 426-431：两端点都须 `collides_with`）
- C++ cpp:109-111：`!(startInside && endInside)` 时 skip。
- 判定：一致。

**与形状任一边相交 skip**（Rust 433-436）
- C++ cpp:113-126：遍历 `points.size()` 条环形边，`IntersectsRustF32(candidate)` 命中即 skip。
- 判定：一致。

**子形状负面积判定**（Rust 438-449）
```rust
let sub_shape_points = if j > i { shape.vertices[i..j].to_vec() }
                       else { [&shape.vertices[i..], &shape.vertices[..j]].concat() };
SPolygon::calculate_area(&sub_shape_points)
```
- C++ `subAreaRustF32`（cpp:43-73）：`end > start` 时取 `[start, end)`，否则 `[start, n) ++ [0, end)`；面积公式 `(y + nextY) * (x - nextX)` 累加 ×0.5，全 float。
- 判定：一致（切片边界与环绕拼接与 Rust 完全相同；面积公式即 Rust `SPolygon::calculate_area`）。

**面积双重守卫**（Rust 446-453：`sub_shape_area >= 0.0` skip；`|area| > cutoff_area_ratio * shape.area` skip）
- C++ cpp:128-131：`subArea >= 0.0F || std::abs(subArea) > cutoffAreaRatio * area` skip。
- 判定：一致。

**相邻点跳过**（Rust 406：`i == j || (i+1)%n == j || (j+1)%n == i`）
- C++ cpp:96-98：同三条件。
- 判定：一致。

**最优候选选择**（Rust 456-468：首个候选直接取；之后 `calc_vert_elim(i,j) > calc_vert_elim(best)` 才替换）
- C++ cpp:133-141：`eliminated = end > start ? end-start-1 : n-start+end-1`（cpp:133-135，与 Rust `calc_vert_elim` 395-401 相同）；`!best.has_value() || eliminated > bestVertexElimination` 时替换。
- 判定：一致（同为 `>` 严格大于，即并列时保留先出现的候选；遍历顺序同为 i 外层 j 内层升序，cpp:92-94 ↔ Rust 404-405）。

**执行闭合（删除区间顶点）**（Rust 470-499）
- Rust：`start = i+1`，`end = j-1`；`start <= end` 时 `drain(start..=end)`；否则环绕两段 `drain(start..)` + `drain(0..=end)`（含 `start < n_points` / `end >= 0` 防御）；`SPolygon::new(ref_points).expect("invalid shape after closing concavity")`。
- C++ cpp:148-162：按 `eliminated` 布尔标记重建——`best->second > best->first ? (i > first && i < second) : (i > first || i < second)`，即删除开区间 `(first, second)` 或环绕并集，与 Rust 的 drain 区间 `[i+1, j-1]`（闭）等价（Rust drain `start..=end` = `[i+1, j-1]`，C++ 条件 `i > first && i < second` = 开区间 `(i, j)`，相同）。
- 判定：一致。
- **失败语义差异**：Rust 499 行 `SPolygon::new(...).expect(...)` 在结果非法时 panic；C++ cpp:159-161 `if (!next.HasValue()) break;`——**中途失败时 break 并保留上一轮的 `current` 形状**继续返回。即 Rust 崩溃的输入在 C++ 返回"闭合到失败前为止"的结果。属 panic→break 保留的差异，方向上 C++ 更宽容。

**循环退出与收尾**（Rust 501-504 无候选 break；507-516 日志）
- C++ cpp:145-146 无候选 break。
- 判定：一致（退出条件）。`info!` 统计日志（Rust 507-516）与 `debug!`（474-484）无 C++ 对应，无行为影响；C++ 不统计 `n_concav_closed`（仅用于日志，无逻辑作用）。

### 返回值语义
- Rust 519 返回 `shape`（克隆演化后的新形状，原 `orig_shape` 不变——Rust 390 `let mut shape = orig_shape.clone()`）。
- C++ cpp:81 `SimplePolygon current = original;`（值拷贝演化），cpp:165 返回 `current`。
- 判定：一致（均不修改入参）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 相邻点跳过三条件 | 一致 | 无 | Rust 406 vs cpp:96-98 |
| 2 | 候选边 0.9999 缩放 | 一致 | 缩放实现（整体 vs 两端各半）几何等价 | Rust 412 vs cpp:26-33 |
| 3 | 距离阈值 `cutoff_distance_ratio * diameter` | 一致 | 无 | Rust 414 vs cpp:101 |
| 4 | Inflate/Deflate 端点包含性守卫 | 一致 | 无 | Rust 419-431 vs cpp:104-111 |
| 5 | 与形状边相交拒绝 | 一致 | 无 | Rust 433-436 vs cpp:113-126 |
| 6 | 子形状负面积 + 面积比守卫 | 一致 | 无 | Rust 438-453 vs cpp:43-73, 128-131 |
| 7 | 最优候选：最大消除顶点数、并列保先 | 一致 | 无 | Rust 395-401, 456-468 vs cpp:133-141 |
| 8 | 删除区间（含环绕拼接） | 一致 | 无 | Rust 470-498 vs cpp:148-157 |
| 9 | 闭合后 `SPolygon::new().expect` panic → `Create` 失败 break 保留旧形状 | 差异 | Rust 崩溃 / C++ 返回失败前结果 | Rust 499 vs cpp:159-161 |
| 10 | 外层循环上界：当前 n_vertices vs 原始 VertexCount | 一致 | 不可触发差异（总会先无候选 break） | Rust 392 vs cpp:82 |
| 11 | info!/debug! 日志与 n_concav_closed 统计 | C++ 缺失 | 无行为影响 | Rust 389, 474-484, 507-516 |
| 12 | `Edge::try_new().expect` panic → 直接构造 | 一致 | panic 路径不可达 | Rust 410-411, 480-481 vs cpp:16-34 |

## 结论
窄凹陷闭合的全部筛选条件（相邻跳过、0.9999 缩边、距离比、双模式端点包含性、形状边相交、子形状负面积与面积比）与择优规则（最大消除顶点数、并列保先）、删除区间语义（含环绕）在双侧逐条一致，且 C++ 对 ratio/diameter/area/边长/面积全部做了 f32 化模拟。唯一实质差异是失败通道：Rust 在闭合结果非法时 panic，C++ break 并保留上一轮形状返回——同方向（更宽容）的 panic→Result 约定变体。正常输入下输出完全等价。

## 补充审计（2026-09-29）

逐行复核 shape_modification.rs 的 narrow concavity 段（381-519 行：close_narrow_concavities 单函数 + calc_vert_elim 闭包）。行段归属确认：382-383 为 doc 注释与 allow 属性；本记录覆盖 384-519。与 10（52-336）、11（338-379）三段对 550 行的分配**无重叠、无三段都漏的缝隙**。以下为补充核对项：

### 【浅覆盖→补齐】debug! 日志中的 Edge::try_new(...).length() 的副作用核对
- Rust shape_modification.rs:480-482：debug! 宏参数中**重复构造** `Edge::try_new(shape.vertex(i), shape.vertex(j)).expect(...).length()`——这是一次额外的 try_new+expect（与 410 行的候选边构造独立）。在 debug! 被编译（debug 构建或日志级别开启）时执行；**release 且日志关闭时参数不求值**（log 宏惰性）。若 410 的 expect 通过则此处必通过（相同输入），无新 panic 路径。C++ 无此重复（无日志）。行为等价性不受影响。
- 判定：【一致】（日志惰性求值；无新失败路径）。
- 证据：shape_modification.rs:474-484。

### 【浅覆盖→补齐】候选边 scale(0.9999) 与 410 行构造的差异注意点
- Rust 410-412：`Edge::try_new(...).expect(...).scale(0.9999)`——先验证原始边非退化，再缩放。**缩放后的边不再验证**（缩放 0.9999 不会产生退化，仅缩短）。C++ candidateEdgeRustF32（NarrowConcavity.cpp:16-34）：直接构造缩放后边，无验证。主记录差异表 #12 已判 try_new "不可达"，此处确认缩放后边也不引入退化可能（scale 保持方向、长度 ×0.9999 > 0）。一致。
- 判定：【一致】。
- 证据：shape_modification.rs:410-412 vs NarrowConcavity.cpp:16-34。

### 【浅覆盖→补齐】Deflate 模式端点包含守卫的边界值（端点恰在边上）
- Rust 419-431 端点包含守卫用 `shape.collides_with(point)`（射线法）：对**恰在边上的点**的返回值依赖射线法顶点穿透分支（FPA 近似相等）——边界点可能 true/false 摆动。C++ ContainsRustF32 同一算法（记录 05 已比对 FPA 谓词族位级一致）→ **边界情形两侧同摆动**（同算法同谓词）。主记录 #4 判一致，此处确认边界点的判定由同一 FPA 谓词族承担，无单侧偏差。
- 判定：【一致】。
- 证据：shape_modification.rs:419-431 vs NarrowConcavity.cpp:104-111 + 记录 05 ContainsRustF32 比对。

### 【浅覆盖→补齐】drain 环绕分支的 j=0 边界（end = -1 空区间）
- Rust 488-497 的环绕 drain 分支：`end = j - 1`；**j=0 时 end=-1** → `drain(0..=end)`（`0..=(-1)` 为空 RangeInclusive）不执行——即 j=0 时只删后段。`if end >= 0` 守卫在 j=0 时为 false，显式跳过。核对 C++ 对应：`eliminated = ... : i > first || i < second`（NarrowConcavity.cpp:152-154，second=0 时 `i < 0` 恒 false）——**同语义**（j=0 时前段无删除）。主记录 #8 判一致，此处补 j=0 边界的显式核对。
- 判定：【一致】。
- 证据：shape_modification.rs:485-498 vs NarrowConcavity.cpp:150-157。

### 【遗漏→补齐】cfg(test)——本段无测试模块
- 381-519 无 `#[cfg(test)]`。判定：不适用。

补充结论：12 记录覆盖完整。四项补充核对（debug! 惰性求值、缩放后边的退化不可能性、Deflate 边界点的同谓词摆动、j=0 环绕分支）全部确认一致，不改变"基本等价"判定。三段记录（10/11/12）对 550 行的覆盖经本次审计确认**无遗漏函数**。
