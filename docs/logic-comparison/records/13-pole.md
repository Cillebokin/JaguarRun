# 13. Pole（fail_fast/pole.rs）逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/fail_fast/pole.rs`（138 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/PointOfInaccessibility.h`（17 行）+ `JaguarCore/Source/Geometry/Primitives/PointOfInaccessibility.cpp`（149 行）+ `JaguarCore/Source/FailFast/Surrogate.cpp`（generatePoles，cpp:49-84）
**判定**: 基本等价（BFS 队列/深度/象限顺序/prune 条件一致；1000 上限 panic→Result；错误消息与细节不同）

## 函数逐条比对

### `generate_surrogate_poles`（Rust 14-49 行）
- C++ 对应：`generatePoles`（Surrogate.cpp:49-84）。
- 判定：基本等价。

逐段对照：

**初始 pole**（Rust 18：`let mut all_poles = vec![shape.poi]`）
- C++ cpp:53：`std::vector<Circle2d> poles{shape.PointOfInaccessibility()}`。
- 差异（载体）：Rust `shape.poi` 是 `SPolygon` 构造时缓存的 POI（simple_polygon.rs 中预计算）；C++ `shape.PointOfInaccessibility()` 按需计算（`ComputePointOfInaccessibility(shape, {})` 的等价封装，见记录 05）。算法一致，仅缓存/按需差异。`total_pole_area = shape.poi.area()`（Rust 19）↔ cpp:54 `coveredArea = circleAreaRustF32(poles.front())`（f32 化的 πr²）。

**迭代生成**（Rust 22-26：`loop { compute_pole(shape, &all_poles)?; push; area 累加 }`）
- C++ cpp:56-63：`while(true)` 中 `ComputePointOfInaccessibility(shape, poles)`，失败透传错误（cpp:59-60），push 后 `coveredArea += circleAreaRustF32(...)`。
- 判定：一致（`?` ↔ Result 透传）。

**覆盖率与 pole 数上限**（Rust 28-42）
```rust
let current_coverage = total_pole_area / shape.area;
let active_pole_limit = n_pole_limits.iter()
    .filter(|(_, coverage_threshold)| current_coverage > *coverage_threshold)
    .min_by_key(|(n_poles, _)| *n_poles)
    .map(|(n_poles, _)| n_poles);
if let Some(active_pole_limit) = active_pole_limit
    && all_poles.len() >= *active_pole_limit { break; }
```
- C++ cpp:65-74：
```cpp
std::optional<std::size_t> activeLimit;
for (const auto& [limit, threshold] : config.poleLimits)
    if (coveredArea / static_cast<float>(shape.Area()) > static_cast<float>(threshold) &&
        (!activeLimit.has_value() || limit < *activeLimit))
        activeLimit = limit;
if (activeLimit.has_value() && poles.size() >= *activeLimit) break;
```
- 判定：一致（过滤 `coverage > threshold` 后取最小 n_poles；`>` 严格大于、`>=` 达限即停，逐字对齐）。coverage 计算处 C++ 将 `shape.Area()` 与 threshold 均 f32 化后比较，与 Rust f32 语义对齐。

**1000 上限**（Rust 43-46）
```rust
assert!(all_poles.len() < 1000, "More than 1000 poles were generated, please check the SPSurrogateConfig");
```
- C++ cpp:76-81：`if (poles.size() >= 1000U) return FromError({ErrorCode::InvalidInput, "more than 1000 poles were generated; check the surrogate configuration"})`。
- 判定：差异（panic→Result）。常量 1000 对齐；触发条件 `len < 1000` 断言 ↔ `>= 1000` 报错等价。assert 在 release 构建被移除（无限循环风险由 n_pole_limits 兜底），C++ 全构建生效。

**返回**：Rust 48 `Ok(all_poles)` ↔ cpp:83 `FromValue(std::move(poles))`。一致。

### `compute_pole`（Rust 54-79 行）
- C++ 对应：`ComputePointOfInaccessibility`（PointOfInaccessibility.cpp:91-148）。
- 判定：基本等价。

逐段对照：

**根节点**（Rust 55-57：`bbox.inflate_to_square()` → `POINode::new(bbox, MAX_POI_TREE_DEPTH, shape, poles)` 入队）
- C++ cpp:95-102：`InflateToSquareRustF32(shape.BoundingBox())`（cpp:24-42，f32 化的扩方，与 Rust `Rect::inflate_to_square` 对齐）→ `Node{root, ClearanceRustF32(...), 10U}`。
- 判定：一致（`MAX_POI_TREE_DEPTH = 10`，Rust 81 ↔ C++ 硬编码 `10U` cpp:102, 120-122）。

**BFS 主循环**（Rust 61-73：`while let Some(node) = queue.pop_front()`）
- C++ cpp:105-139：`while (!queue.empty())` + `pop_front()`。
- 判定：一致（同为 FIFO 队列；Rust VecDeque ↔ std::deque）。

**best 更新**（Rust 63-65：`node.distance > distance(&best)` 时 `best = Circle(bbox.centroid(), node.distance)`）
- C++ cpp:109-118：`current.distance > bestRadius` 时 `Circle2d::Create(center, current.distance)`，Create 失败返回错误（cpp:115-116）。
- 判定：一致。`distance(&best)` 的 None→0.0（Rust 59）↔ cpp:109 `best.has_value() ? best->radius : 0.0`。Rust 64 `Circle::try_new(...).unwrap()` ↔ C++ `Create` 失败走 Result（panic→Result，负半径防御路径；正常 node.distance ≥ 0 时不可达）。

**prune 与分裂条件**（Rust 68-72）
```rust
if node.distance_upperbound() > distance(&best) && let Some(children) = node.split(shape, poles) {
    queue.extend(children);
}
```
- C++ cpp:120-127：
```cpp
const float radius = current.level == 10U ? rootRadius : DiameterRustF32(current.box) / 2.0F;
if (static_cast<double>(current.distance + radius) <= bestRadius || current.level == 0U)
    continue;
```
- 判定：一致。`distance_upperbound() = radius + distance`（Rust 135-137）↔ `current.distance + radius`；`split` 在 level==0 时 None（Rust 125-126）↔ `current.level == 0U` 跳过。C++ 根节点（level==10）radius 用 rootRadius（等价于自身 bbox 直径/2，写法特殊但值相同）。
- 细节：Rust `>` 严格大于才分裂 ↔ C++ `<=` 则 continue（即 `>` 才分裂），对齐。

**子节点入队顺序**（Rust 129-131：`self.bbox.quadrants().map(POINode::new)`）
- C++ cpp:129-138：`current.box.QuadrantsRustF32()` 逐个 push，注释明确"jagua-rs Rect::quadrants() is ordered [top-right, top-left, bottom-left, bottom-right]. Preserve that FIFO order."。
- 判定：一致（象限顺序显式对齐；顺序只影响 best 的 tie-break，此处两侧相同）。

**无解返回**（Rust 74-78：`best.ok_or(anyhow!("no pole found with {} levels of recursion. Please check the input shape: {:?}", ...))`）
- C++ cpp:141-146：`FromError({ErrorCode::InvalidPolygon, "point of inaccessibility could not be found"})`。
- 判定：差异（错误消息不同；Rust 带深度与顶点信息，C++ 固定文本）。行为等价（同是失败返回）。注意：best 初值 None 且首节点 distance 可能为负（质心在形外）时仍可能找到负 distance 圆——`distance > 0.0`（bestRadius 初值）不成立则 best 保持 None。双侧同此逻辑（Rust `distance(&best)` None→0.0），一致。

### `const MAX_POI_TREE_DEPTH: usize = 10`（Rust 81 行）
- C++ 对应：`10U` 硬编码（PointOfInaccessibility.cpp:102, 120）。
- 判定：一致（常量值 10 对齐；C++ 无命名常量）。

### `struct POINode` + `new`（Rust 83-122 行）
- C++ 对应：`struct Node` + `ClearanceRustF32`（PointOfInaccessibility.cpp:17-88）。
- 判定：一致。
- `radius = bbox.diameter() / 2.0`（Rust 92）↔ `DiameterRustF32(box) / 2.0F`（cpp:44-51，f32 化对角线）。
- `centroid_inside = poly.collides_with(centroid) && poles 无一碰撞`（Rust 94-95）↔ cpp:78-87：`shape.ContainsRustF32(center)` 且任一 excluded 圆 `ContainsRustF32` 即 false。
- `distance_to_edges` 取 min（Rust 98）↔ cpp:58-63 逐边 `DistanceToRustF32` 取 min。
- `distance_to_poles` 用 `separation_distance(&centroid).1`（Rust 100-102，点-圆分离距离 = |dist(c,p)| - r）↔ cpp:65-76 `|sqrt(dx²+dy²) - radius|`。一致。
- `fold(f32::MAX, f32::min)`（Rust 106）↔ `std::numeric_limits<float>::max()` 初值（cpp:57）。
- 质心在形外取负（Rust 109-113）↔ cpp:87 `centroidInside ? distanceToBorder : -distanceToBorder`。一致。
- 差异（组织）：Rust `POINode::new` 在 split 时对每个象限重算；C++ 在入队时算好存入 Node——时机不同（C++ 更早一步），值与个数一致（每节点恰好算一次 clearance）。

### `split`（Rust 124-133 行）
- C++ 对应：内联于主循环（cpp:120-127 的 level==0 检查 + cpp:129-138 的象限展开）。
- 判定：一致。

### `distance_upperbound`（Rust 135-137 行）
- C++ 对应：内联 `current.distance + radius`（cpp:123）。
- 判定：一致。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 初始 pole = shape POI + 面积累加 | 一致 | 无（缓存 vs 按需计算，见记录 05） | Rust 18-19 vs Surrogate.cpp:53-54 |
| 2 | 覆盖率 `total_pole_area / shape.area`（f32） | 一致 | 无 | Rust 28 vs Surrogate.cpp:68 |
| 3 | limit 筛选：`coverage > threshold` 取最小 n_poles，`len >= limit` 停 | 一致 | 无 | Rust 31-42 vs Surrogate.cpp:65-74 |
| 4 | 1000 上限：assert panic → InvalidInput Result | 差异 | release 下 Rust 不设限（依赖 limits 兜底），C++ 全构建报错 | Rust 43-46 vs Surrogate.cpp:76-81 |
| 5 | BFS FIFO + best 更新（None→0.0、`>` 严格） | 一致 | 无 | Rust 59-65 vs POI.cpp:105-118 |
| 6 | prune `distance + radius <= best` 不分裂 / level 0 不分裂 | 一致 | 无 | Rust 68-72, 125-126 vs POI.cpp:120-127 |
| 7 | 象限顺序 [TR, TL, BL, BR] FIFO | 一致 | 无（C++ 注释显式对齐） | Rust 129-131 vs POI.cpp:129-138 |
| 8 | MAX_POI_TREE_DEPTH 10 | 一致 | 无（C++ 硬编码 10U） | Rust 81 vs POI.cpp:102, 120 |
| 9 | clearance：边距 min + 圆分离距离 min，形外取负 | 一致 | 无 | Rust 94-114 vs POI.cpp:53-88 |
| 10 | 无解错误消息（含深度/顶点 vs 固定文本） | 差异 | 仅诊断信息 | Rust 74-78 vs POI.cpp:141-146 |
| 11 | `Circle::try_new().unwrap()` → `Circle2d::Create` Result | 差异 | panic→Result（负半径不可达路径） | Rust 64 vs POI.cpp:113-116 |
| 12 | Node::new 时机（split 时 vs 入队时） | 一致 | 计算次数相同，无行为差 | Rust 91-122, 130 vs POI.cpp:100-102, 134-137 |

## 结论
pole 生成（初始 POI + 迭代最大内切圆）与 compute_pole 的 BFS 求解（正方形根、深度 10、象限 FIFO 顺序、`distance+radius` prune、形外取负 clearance）在双侧逐行等价，覆盖率/上限筛选条件与 f32 精度模拟完全对齐。差异集中在错误通道：1000 上限由 assert panic（release 移除）变为全构建的 `InvalidInput` 错误，无解时的错误消息内容不同，`Circle::try_new().unwrap()` 变为 `Result`。正常输入下生成的 pole 序列（含顺序）完全一致。
