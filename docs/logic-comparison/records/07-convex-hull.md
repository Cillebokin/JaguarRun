# 07. convex_hull.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/convex_hull.rs`（66 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/ConvexHull.h`（12 行）+ `JaguarCore/Source/Geometry/ConvexHull/ConvexHull.cpp`（75 行）
**判定**: 基本等价（convex_hull_from_points 算法逐式一致；convex_hull_indices 与 convex_hull_from_surrogate 两个公共 API 缺失）

## 方法逐条比对

### convex_hull_indices(shape: &SPolygon)（convex_hull.rs:7-16）
- C++ 对应：无公共 API；`Surrogate::ConvexHullIndices()`（FailFast/Surrogate.h:68）只暴露 surrogate 内部的索引，不含"从顶点重算索引"的路径。
- 判定：【C++缺失】
- 证据与说明：Rust convex_hull.rs:10-15：调 convex_hull_from_points(vertices.clone()) 得到凸包点集，再对每个凸包点 `vertices.iter().position(|x| x == p).unwrap()` 反查索引（**注意：`==` 是 Point 的 PartialEq——若顶点集中有 +0.0/-0.0 这类 PartialEq 相等但位不同的点，position 返回第一个匹配；unwrap 在不匹配时 panic——理论上凸包点都来自原集合，不会 panic**）。验证背景陈述"缺 convexex_hull_indices 公共 API"成立。C++ 侧 `ConvexHull(points)` 只返回点集（ConvexHull.h:11），调用方若需索引须自行匹配。注意 C++ 还有一个语义近似物：SimplePolygon.cpp:100-133 的匿名空间 `convexHullRustF32`（直径计算内部使用，同样不返回索引）。

### convex_hull_from_surrogate(s: &SPolygon)（convex_hull.rs:18-29）
- C++ 对应：无直接 API；`Surrogate` 持有 `m_ConvexHullIndices`（Surrogate.cpp:569, 664-666 暴露 `ConvexHullIndices()`），但"由 surrogate 索引取回顶点"的组合操作未在几何层提供。
- 判定：【C++缺失】
- 证据与说明：Rust convex_hull.rs:20-28：有 surrogate 则 `surr.convex_hull_indices.iter().map(|&i| s.vertices[i]).collect()`，否则 `bail!("no surrogate present")`。C++ 中同等数据可达（`SimplePolygon::GetSurrogate()->ConvexHullIndices()` + `Vertices()`），但组合便捷 API 与错误通道（anyhow bail）缺失，由上层自行拼装。

### convex_hull_from_points(points: Vec<Point>)（convex_hull.rs:31-53）
- C++ 对应：`ConvexHull(std::vector<Point2d>)`（ConvexHull.cpp:36-74）。
- 判定：【差异】（排序细节：Rust `sort_by_key(OrderedFloat(p.0))` 是**稳定排序**且用全序比较器（NaN 排最后）；C++ `stable_sort` + `orderedFloatLess` 手工 NaN 处理——NaN 语义等价性需核对；另 C++ 预先将输入量化到 f32）
- 证据与说明：Monotone chain 算法主体逐式一致：
  1. **x 排序**：Rust convex_hull.rs:37 `points.sort_by_key(|p| OrderedFloat(p.0))`——`sort_by_key` 是稳定排序（Rust 文档：equal 元素保持原序），OrderedFloat 全序（NaN 视为最大）。C++ ConvexHull.cpp:47-50 `std::stable_sort` + `orderedFloatLess`（ConvexHull.cpp:24-33）：`leftNan → false; rightNan → true; left < right`——NaN 视为最大且稳定。**与 OrderedFloat 的 NaN 排序一致**（ordered-float 库中 OrderedFloat 的 Ord 将 NaN 排在最后）。稳定 + NaN 置尾两侧一致。判该子项【一致】。
  2. **f32 量化前置**：C++ ConvexHull.cpp:38-42 把每个输入 cast f32 再存回 double——Rust 天然 f32。对合法输入无差。【一致】。
  3. **下/上壳构建**：Rust convex_hull.rs:39-45 fold + grow_convex_hull；C++ ConvexHull.cpp:52-68 双循环内联 `while (lower.size() >= 2 && cross(...) <= 0.0F) pop_back`——`h.len() >= 2 && cross(...) <= 0.0`（convex_hull.rs:57）逐字对应（**`<= 0.0` 含共线剔除**：共线点被弹出，两侧一致）。上壳用反向迭代（`points.iter().rev()` ↔ `points.rbegin()`）。【一致】。
  4. **首尾弹出**：`upper_hull.pop(); lower_hull.pop();`（convex_hull.rs:48-49）↔ ConvexHull.cpp:70-71 `lower.pop_back(); upper.pop_back();`——**注意顺序颠倒**：Rust 先 pop upper 再 pop lower，C++ 先 pop lower 再 pop upper。两者互不依赖（各自弹出各自向量的末元素），结果相同。【一致】。
  5. **拼接**：`lower_hull.append(&mut upper_hull)`（convex_hull.rs:51）↔ ConvexHull.cpp:72 `lower.insert(lower.end(), upper.begin(), upper.end())`。**结果顺序核对**：Rust append 后 = [lower…, upper…]；C++ 同。下壳起点是最小 x 点、上壳起点是最大 x 点（反向遍历）——两侧构造同向。【一致】。
  6. **空集**：Rust 空 points → 两个空 hull，pop 在空 vec 上返回 None（Vec::pop 对空返回 None，不 panic）→ 结果空 vec；C++ ConvexHull.cpp:44-45 `if (points.empty()) return points;` 显式早退（**若无此早退，空 vector 上 pop_back 是 UB**——C++ 加了防护）。单点集：Rust lower=[p], upper=[p]，pop 后均空 → 结果 [p]；C++ 同（size 1 时不进 while，pop 后为空）。【一致】。
  7. **cross 函数**：convex_hull.rs:64-66 `(b.0-a.0)*(c.1-a.1) - (b.1-a.1)*(c.0-a.0)` ↔ ConvexHull.cpp:15-22 `rustF32(first.x)-rustF32(origin.x)) * (rustF32(second.y)-rustF32(origin.y)) - ...`——逐操作同序（f32 减、乘、减）。【一致】。

### grow_convex_hull（私有，convex_hull.rs:55-62）
- C++ 对应：ConvexHull.cpp:55-59 / 63-67 内联 while。
- 判定：【一致】
- 证据与说明：见上条第 3 点——`h.len() >= 2 && cross(h[h.len()-2], h[h.len()-1], next) <= 0.0` ↔ `lower.size() >= 2 && cross(lower[lower.size()-2], lower.back(), point) <= 0.0F`，含 `<= 0.0` 的共线剔除语义与 pop-then-push 顺序。

### cross（私有，convex_hull.rs:64-66）
- C++ 对应：匿名空间 `cross`（ConvexHull.cpp:15-22）。
- 判定：【一致】
- 证据与说明：见上条第 7 点。

### （C++ 额外）SimplePolygon.cpp:100-133 的 convexHullRustF32
- Rust 对应：convex_hull_from_points（经 SPolygon::calculate_diameter 调用）。
- 判定：【C++额外】（重复实现）
- 证据与说明：SimplePolygon.cpp:100-133 在直径计算中**内联复制**了一份凸包实现（含 stable_sort + 同样的 grow lambda），与 ConvexHull.cpp:36-74 的公共函数逻辑相同。差异细节：SimplePolygon.cpp:105-108 的排序比较器是 `rustF32(left.x) < rustF32(right.x)` **无 NaN 处理**（ConvexHull.cpp:24-33 的 orderedFloatLess 有）——SimplePolygon 输入经 Create 校验必无 NaN，故无实际差异。属代码重复而非行为差异（两份实现的输出在无 NaN 输入下一致）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | convex_hull_indices 公共 API | C++缺失 | "多边形→凸包顶点索引"公共入口缺失；surrogate 内部索引 API 不等价（不重算） | convex_hull.rs:7-16 vs ConvexHull.h:11（仅点集） |
| 2 | convex_hull_from_surrogate | C++缺失 | surrogate 索引取回顶点的便捷 API + bail 错误通道缺失，数据可达需上层拼装 | convex_hull.rs:18-29 vs Surrogate.h:68（仅索引 getter） |
| 3 | 空集防护：Rust Vec::pop 天然安全 vs C++ 显式早退 | 一致（防护形态差异） | 两侧空集均返回空，无 UB | convex_hull.rs:48-49 vs ConvexHull.cpp:44-45 |
| 4 | SimplePolygon.cpp 内联凸包重复实现（比较器无 NaN 处理） | C++额外 | 代码重复；NaN 不可达故行为一致 | SimplePolygon.cpp:100-133 vs ConvexHull.cpp:36-74 |
| 5 | 输入 f32 量化前置 | C++额外（防御） | 对合法输入无损 | ConvexHull.cpp:38-42 |

## 结论

凸包核心算法（Monotone chain：稳定 x 排序含 NaN 置尾、`cross <= 0` 共线剔除、上下壳构建与拼接顺序）在 C++ 中逐式复刻，行为等价，包括稳定排序语义与空/单点集的边界。缺的是两个上层便捷 API（convex_hull_indices、convex_hull_from_surrogate），其底层数据在 C++ 中均可达但需调用方自行组合；另有一份为直径计算内联的重复实现（行为一致、无 NaN 防护差异不可达）。
