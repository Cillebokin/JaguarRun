# 05. simple_polygon.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/primitives/simple_polygon.rs`（368 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/SimplePolygon.h`（103 行）+ `JaguarCore/Source/Geometry/Primitives/SimplePolygon.cpp`（570 行）
**判定**: 基本等价（Create 主链路与 RustF32 判定族逐式复刻；关键差异：重复点查重对 ±0.0 的语义相反、面积零判定（== 0.0 vs 容差）、transform 不重校验 vs 全量重校验、From<Rect> 缺失）

## 方法逐条比对

### SPolygon::new（simple_polygon.rs:41-74）
- C++ 对应：`SimplePolygon::Create`（SimplePolygon.cpp:192-258）。
- 判定：【差异】（多处，逐项列出）
- 证据与说明：
  1. **顶点数下限**：simple_polygon.rs:42-44 `< 3` bail ↔ SimplePolygon.cpp:199-201 `< 3` → InvalidPolygon。一致。
  2. **重复点判定**：Rust simple_polygon.rs:45 `points.iter().unique().count() != points.len()`；锁定的 itertools `unique` 用 `HashMap` 保存已见值，而 jagua-rs `Point` 手写 `Eq`（基于 f32 `PartialEq`）和 `Hash`（按 `to_bits()`）。因此 `( +0.0, y )` 与 `( -0.0, y )` 虽然 `Eq` 相等，却因不同哈希进入不同桶，Rust 查重会保留；C++ SimplePolygon.cpp:219 的逐分量 `==` 直接拒绝这对顶点，属于差异。NaN 与自身不相等，Rust 与 C++ 均不把两个 NaN 顶点判为重复。判定：【±0.0 差异；NaN 一致】。
  3. **非有限值校验位置**：C++ `normalizeRustF32(points)`（SimplePolygon.cpp:22-32）先将double坐标收窄为f32再转回double，随后Create在203-213行拒绝所有残留的非有限顶点并返回 `InvalidInput`；Rust `SPolygon::new` 没有同层finite扫描。锁定RustOracle在Debug/Release下对当前`nan_coordinate` fixture均观测到直径计算的`NotNan::new(...).unwrap()` panic；对`infinite_coordinate` fixture均观测到`Rect::try_new(...).unwrap()` panic。`+Inf`本身可由`NotNan`接受，故这两个定向结果不能推出每种NaN/Inf输入都会以同一方式失败；一般非有限输入的整体接受/失败边界尚未确证。C++ double输入收窄为f32后溢出成infinity也会由入口检查拒绝；Rust外部几何类型本身就是f32。
  4. **面积零判定**：Rust simple_polygon.rs:52-53 `match ... { 0.0 => bail!(...) }`——**严格 == 0.0**（极小非零面积通过）；C++ SimplePolygon.cpp:233 `if (std::abs(rawArea) <= policy.areaTolerance)`——默认 areaTolerance = 0.0（NumericPolicy.h:19-20）时与 `== 0.0`（对绝对值）等价；但 policy 可配非零容差 → 允许被 Rust 拒绝的退化多边形。判【差异】（可配置容差放宽了 Rust 的严格判定；默认值下等价）。
  5. **负面积反转**：simple_polygon.rs:54-58 `area < 0.0 → points.reverse(); -area` ↔ SimplePolygon.cpp:237-240 `rawArea < 0.0F → std::reverse; area = abs(rawArea)`。一致（-area 与 abs 对负数同值）。
  6. **直径/包围盒/POI 的计算次序**：Rust 先 diameter（含凸包）→ bbox → poi（simple_polygon.rs:62-64）；C++ bbox → centroid → diameter → poi（SimplePolygon.cpp:241-256）——计算次序无相互依赖，结果相同。
  7. **POI**：Rust simple_polygon.rs:162-181 构造 dummy SPolygon 后 `compute_pole(&dummy_sp, &[])`；C++ SimplePolygon.cpp:253-256 `ComputePointOfInaccessibility(polygon, {})`——C++ 用已构造的 polygon 本体而非 dummy（dummy 的存在只为满足 Rust 类型系统），语义等价（POI 算法内部差异属 fail_fast 层，不在本文件范围）。

### generate_surrogate（simple_polygon.rs:76-83）
- C++ 对应：`GenerateSurrogate`（SimplePolygon.cpp:376-388）。
- 判定：【一致】
- 证据与说明：Rust：已有且 config 相同则跳过，否则新建（simple_polygon.rs:78-81）；C++ SimplePolygon.cpp:379-380 `if (m_Surrogate && m_Surrogate->Config() == config) return` 否则 `Surrogate::Create`。一致（SPSurrogate 内部属 fail_fast 层）。

### vertex / edge / edge_iter / n_vertices（simple_polygon.rs:85-107）
- C++ 对应：`Vertices()` / `Edges()` / `VertexCount()`（SimplePolygon.cpp:260-306）。
- 判定：【差异】（edge 构造方式：Rust 直接构造不校验；C++ 用 Edge2d::Create 并**静默丢弃失败边**）
- 证据与说明：Rust simple_polygon.rs:91-98 `edge(i)` 直接 `Edge { start, end }`——不校验退化（相邻重复点已在 new 查重中排除，故实际不退化）；C++ SimplePolygon.cpp:301-304 `Edge2d::Create(...)` 失败时 `if (edge.HasValue()) result.push_back(...)`——**失败边被跳过而非报错**。由于 Create 阶段已排除重复点，两侧行为实际一致（都产出 n 条边）；判【一致】（附形态说明）。`vertex(i)` Rust 直接索引（越界 panic）↔ C++ `Vertices()[i]`（越界 UB）——防御性差异，语义同。

### surrogate()（simple_polygon.rs:109-112）
- C++ 对应：`GetSurrogate()`（SimplePolygon.cpp:390-393）。
- 判定：【差异】（失败行为：Rust expect panic vs C++ 返回 nullptr）
- 证据与说明：simple_polygon.rs:111 `.expect("surrogate not generated")`；SimplePolygon.cpp:392 `return m_Surrogate.get();`（可为 nullptr）。调用方需判空。形态差异。

### calculate_diameter（simple_polygon.rs:114-128）
- C++ 对应：匿名空间 `makeDiameterRustF32`（SimplePolygon.cpp:135-150）+ `convexHullRustF32`（SimplePolygon.cpp:100-133）。
- 判定：【差异】（最大值选取：Rust 用 NotNan + max_by_key（NaN 时 unwrap panic）；C++ 用 std::max——凸包输出无 NaN 时等价；另凸包排序稳定性见 07 记录）
- 证据与说明：simple_polygon.rs:120-125 对凸包点做 `array_combinations::<2>()` 全对 `sq_distance_to`，`max_by_key(NotNan::new(*sq_d).unwrap())`——NaN 距离会 panic；C++ SimplePolygon.cpp:139-148 双循环 `maxSquaredDistance = std::max(...)`——NaN 时 std::max 行为不同（依赖比较序）。凸包顶点来自有限输入（Create 已拒非有限），sq_distance 不会 NaN。`sq_diam.sqrt()` ↔ `std::sqrt(maxSquaredDistance)`（SimplePolygon.cpp:149）一致。判【一致】（NaN 理论差异不可达）。注意 Rust 用 `sq_distance_to`（f32 平方和）↔ C++ `dx*dx+dy*dy`（SimplePolygon.cpp:143-145）同式。

### generate_bounding_box（simple_polygon.rs:130-142）
- C++ 对应：匿名空间 `makeBoundingBox`（SimplePolygon.cpp:49-67）。
- 判定：【差异】（无效 bbox 失败行为：Rust unwrap panic vs C++ Create 后续校验错误；另初始值 lowest() 语义）
- 证据与说明：Rust simple_polygon.rs:132-133 初值 `f32::MAX` / `f32::MIN`（f32::MIN = -3.4e38，非 lowest 混淆——Rust 的 MIN 就是最低有限值）；C++ SimplePolygon.cpp:51-54 `std::numeric_limits<float>::max()` / `lowest()`——同为 ±最大有限值。一致。逐点 min/max（f32::min/max ↔ MinRustF32/MaxRustF32）一致。末尾：Rust simple_polygon.rs:141 `Rect::try_new(...).unwrap()`——空点集或退化时 panic；C++ 不在 makeBoundingBox 内校验（Create 阶段已保证 ≥3 点且无重复 → bbox 必有效；TransformedRustF32 中另做 Create 校验，SimplePolygon.cpp:352-355）。空点集在两侧都不可达（Create 已拦 <3）。判【一致】（附说明）。

### calculate_area（simple_polygon.rs:144-160）
- C++ 对应：匿名空间 `signedAreaRustF32`（SimplePolygon.cpp:34-47）。
- 判定：【一致】
- 证据与说明：Shoelace：simple_polygon.rs:148-159 `sigma += (y_i + y_j) * (x_i - x_j)`，`0.5 * sigma`；C++ SimplePolygon.cpp:44-46 `sum += (y + nextY) * (x - nextX); return 0.5F * sum;`。逐式同序（f32 累加，无重结合）。一致。

### calculate_poi（simple_polygon.rs:162-181）
- C++ 对应：`ComputePointOfInaccessibility`（PointOfInaccessibility.h:14-17，实现在 PointOfInaccessibility.cpp，属另层）。
- 判定：【一致】（调用链形态；算法本体在 POI 层另行比对）
- 证据与说明：Rust 构造 dummy（bbox/area/dummy_poi=(f32::MAX,f32::MAX),f32::MAX）后 compute_pole(&dummy_sp, &[])；C++ Create 尾部 SimplePolygon.cpp:253-256 直接传本体 + 空 excluded。dummy 的 poi 字段在 Rust 算法中会被重算覆盖（fail_fast 层语义），不影响等价。

### centroid（simple_polygon.rs:183-203）
- C++ 对应：`makeCentroidRustF32`（SimplePolygon.cpp:69-87）+ `Centroid()` 访问器（SimplePolygon.cpp:270-273）。
- 判定：【一致】
- 证据与说明：多边形质心公式：simple_polygon.rs:191-197 `c_x += (x_i + x_j) * (x_i*y_j - x_j*y_i); c_y += (y_i + y_j) * (同factor)`，末尾 `/= 6.0 * area`；C++ SimplePolygon.cpp:80-84 `factor = pointX*nextY - nextX*pointY; x += (pointX+nextX)*factor; y += (pointY+nextY)*factor; denominator = 6.0F * area; x/denominator`。**注意差异点：Rust 的 area 是字段（取负面积反转后的正值）；C++ 传入的 area 也是 abs 后正值（SimplePolygon.cpp:240-242）**。factor 计算共享同一个子表达式（Rust 内联两次计算 vs C++ 提取变量——数值相同，因 f32 乘法确定性）。`c_x /= 6.0*area` ↔ `x / (6.0F*area)` 同式。**但注意**：Rust 的 centroid 是**按需方法**（每次调用重算，用 self.area）；C++ 是**构造时缓存**（m_Centroid）。TransformedRustF32 中重算（SimplePolygon.cpp:357-358）。数值一致。

### find_self_intersection（simple_polygon.rs:205-216）
- C++ 对应：匿名空间 `hasSelfIntersection`（SimplePolygon.cpp:152-175）。
- 判定：【差异】（邻接边排除逻辑的边界：Rust 用 `i+1==j || (i==0 && j==len-1)`；C++ 用 `i==j || iNext==j || jNext==i`——需核对覆盖是否相同；另有退化边处理差异）
- 证据与说明：Rust simple_polygon.rs:210 `are_neighboring_edges(i,j) = i + 1 == j || (i == 0 && j == points.len() - 1)`——combinations(2) 保证 i<j，故只排除 (i, i+1) 与首尾对 (0, n-1)。C++ SimplePolygon.cpp:162-167 内层 j 从 i+1 起，排除 `i == j（不可达）|| iNext == j || jNext == i`：iNext==j 排除 (i,i+1)；jNext==i 即 (j+1)%n == i，对 i<j 而言仅当 j=n-1, i=0 时成立（排除首尾对）——**覆盖与 Rust 完全一致**。判交用 `first.Value().IntersectsRustF32(second.Value())`（SimplePolygon.cpp:170）↔ Rust `edge(i).collides_with(&edge(j))`（simple_polygon.rs:214，走 edge_intersection）——一致（见 03-edge.md）。**额外差异**：C++ SimplePolygon.cpp:158-160/168-169 对 `Edge2d::Create` 失败（退化边：相邻重复点）直接 `return true`（视为自交）；Rust 侧 edge 直接构造不查退化，退化边会在 edge_intersection 中按普通线段处理（相邻重复点本已被查重排除，不可达）。判【一致】（邻接排除等价；退化边处理差异仅在已被上游排除的不可达输入上）。

### Transformable::transform（simple_polygon.rs:219-248）
- C++ 对应：无对应"就地变换"；`Transformed`（SimplePolygon.cpp:308-336）与 `TransformedRustF32`（SimplePolygon.cpp:338-374）均为返回新对象。
- 判定：【差异】（重校验：Rust transform **不重新校验**（无 new 调用），C++ Transformed **全量重跑 Create 校验**；TransformedRustF32 不重校验但校验 bbox）
- 证据与说明：Rust simple_polygon.rs:220-247：逐点 transform → poi transform → surrogate transform → **重生成 bbox**（simple_polygon.rs:244），area/diameter **不重算**（变换保面积/长度假设：刚体变换下成立；但若 Transformation 含缩放则 area/diameter 失效——Rust 侧矩阵允许非刚体，这是 jagua-rs 的既有限制）。C++ `Transformed`（double 版，SimplePolygon.cpp:308-336）：`SimplePolygon::Create(transformedPoints)` **全量重校验+重算**（面积、直径、质心、POI 全部重算）——与 Rust 语义不同（Rust 保留原 area/diameter）。C++ `TransformedRustF32`（SimplePolygon.cpp:338-374）：逐点 ApplyRustF32 → 重算 bbox（并经 Create 校验 bbox 有效性，SimplePolygon.cpp:352-355）→ 重算 centroid → POI 仅平移 center 保留 radius → surrogate 转发——**area/diameter 保留原值**（与 Rust transform 一致），但多做了 bbox 有效性校验（Rust 无此校验：变换后 bbox 理论上仍有效，除非非刚体/溢出）。验证背景陈述"C++ Transformed(double) 全量重校验 vs Rust transform 不重校验"成立（且 RustF32 版反而更贴近 Rust）。POI 处理差异：Rust `poi.transform(t)`（圆心随变换）↔ C++ RustF32 版同（center 变换 radius 保留，SimplePolygon.cpp:359-362）一致；C++ double 版经 Create 重算 POI（不同）。

### TransformableFrom::transform_from（simple_polygon.rs:250-277）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：trait 形态 API；数值路径同 transform（逐点 transform_from + bbox 重生成）。C++ 无此内存优化形态。

### CollidesWith<Point>::collides_with（simple_polygon.rs:279-315）
- C++ 对应：`ContainsRustF32`（SimplePolygon.cpp:430-474）。
- 判定：【差异】（顶点穿透分支的相等判定：Rust 用 FPA==（epsilon+4ULP），C++ 用 AlmostEqualRustF32 + GreaterRustF32——一致；但初筛 bbox 判据不同：Rust 用 collides_with（含边界 ≤），C++ 用严格 < 排除——边界点行为不同）
- 证据与说明：Rust simple_polygon.rs:282 `self.bbox.collides_with(point)`（rect.rs:300-303：`x >= x_min && x <= x_max && ...`，**闭区间**，边界点进入射线判定）；C++ SimplePolygon.cpp:439 `if (pointX < xMin || pointX > xMax || pointY < yMin || pointY > yMax) return false;`——同为闭区间（<、> 排除）——**一致**。射线端点：Rust simple_polygon.rs:285 `point_outside = Point(bbox.x_max + bbox.width(), point.1)`；C++ SimplePolygon.cpp:442 `pointOutsideX = xMax + (xMax - xMin)`——`bbox.width()` = x_max - x_min（rect.rs:218-219），同式。顶点穿透分支：Rust simple_polygon.rs:296-305 `(s_y == p_y && s_x > p_x) || (e_y == p_y && e_x > p_x)` 用 **FPA 相等**（s_y 等是 FPA 包装，simple_polygon.rs:296-298）+ 严格 `>`（注意：s_x > p_x 是 FPA 偏序的 >，即"大于且不近似相等"）；C++ SimplePolygon.cpp:455-458 `AlmostEqualRustF32(startY, pointY) && GreaterRustF32(startX, pointX)`——GreaterRustF32（RustF32.h:76-79）= `first > second && !AlmostEqual` 与 FPA 的 `>`（partial_cmp：eq 时 Equal，否则原始 >）**等价**。`s_y < p_y || e_y < p_y`（simple_polygon.rs:303，FPA 的 <）↔ SimplePolygon.cpp:460-461 `LessRustF32(startY, pointY) || LessRustF32(endY, pointY)`（RustF32.h:71-74 = `< && !almost`）等价。其余边走 `ray.collides_with(&edge)`（simple_polygon.rs:306）↔ SimplePolygon.cpp:464 `ray.IntersectsRustF32(...)`（走 Edge 判交）一致。奇偶翻转计数（% 2 == 1 ↔ bool 翻转）一致。判【一致】。无后缀 `Contains(point, tolerance)`（SimplePolygon.cpp:395-428，PnPN 算法+边界容差）为【C++额外】，与 Rust 射线法是完全不同的算法（容差版含"点在边附近即 true"的边界带，Rust 无）。

### DistanceTo<Point>::distance_to / sq_distance_to（simple_polygon.rs:317-331）
- C++ 对应：`DistanceToRustF32` / `SquaredDistanceToRustF32`（SimplePolygon.cpp:523-544）。
- 判定：【差异】（空序列防护：Rust `.unwrap()` 对空 edge_iter panic；C++ 初值 f32::max 兜底——不可达差异；最小值比较器差异）
- 证据与说明：Rust simple_polygon.rs:325-328 `min_by(|a,b| a.partial_cmp(b).unwrap())`——NaN 距离 panic；C++ SimplePolygon.cpp:536-543 `std::min` 初值 FLT_MAX。边集非空（≥3 顶点），距离非 NaN（有限输入）。命中时两侧数值同。判【一致】（附不可达说明）。distance = sqrt(sq)（simple_polygon.rs:318-319 ↔ SimplePolygon.cpp:526-527 sqrt((float)sq)）一致。

### SeparationDistance<Point>::separation_distance / sq_separation_distance（simple_polygon.rs:333-352）
- C++ 对应：`SeparationDistanceRustF32` / `SquaredSeparationDistanceRustF32`（SimplePolygon.cpp:546-569）。
- 判定：【一致】
- 证据与说明：sq 版先求各边 sq 距离最小值（`min_by_key(OrderedFloat)` ↔ `std::min`，无 NaN 时等价），再按 `collides_with(point)` 定 Interior/Exterior（simple_polygon.rs:346-350 ↔ SimplePolygon.cpp:565-567 用 ContainsRustF32）。注意 Rust sq_separation 内点返回的是**到最近边的平方距离**（非 0）——C++ 同。separation = sqrt(sq)（simple_polygon.rs:335-336 ↔ SimplePolygon.cpp:550-552）一致。

### From<T: Borrow<Rect>> for SPolygon（simple_polygon.rs:354-368）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：Rust 把矩形转成四顶点多边形（逆时针：(x_min,y_min), (x_max,y_min), (x_max,y_max), (x_min,y_max)，simple_polygon.rs:360-365，注意顶点顺序为逆时针且经 new 全量校验 unwrap）。C++ 无对应工厂（调用方需自行构造 4 点 vector 调 Create）。验证背景陈述"缺 From<Rect>"成立。数值路径无差异，仅缺便捷 API。

### （C++ 额外）Intersects(Edge, tolerance) / IntersectsRustF32(Edge)（SimplePolygon.cpp:476-507）
- Rust 对应：无（Rust 侧多边形与 Edge 的碰撞在 collision_detection 层通过 surrogate/POI 处理，simple_polygon.rs 未提供）。
- 判定：【C++额外】
- 证据与说明：IntersectsRustF32：bbox 预检（SimplePolygon.cpp:494-500，注意用 `(float)` cast 的普通 `<` 而非 MinRustF32 组合——与 Rust bbox 判交（rect.rs:285 max≤min）在 NaN-free 下等价但公式形态不同：`firstBox.xMax < secondBox.xMin || ...` 取反即 max≤min 全部成立的简化，逐分量等价）→ 逐边 IntersectsRustF32 → 端点 ContainsRustF32 兜底。这是 C++ 为补齐"多边形 vs 线段"判交自建的方法，Rust 几何层无对应（属上层功能的下沉）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | 面积零判定：Rust 严格 == 0.0 vs C++ ≤ areaTolerance（默认 0 等价，可配置放宽） | 差异 | 非默认容差下 C++ 接受 Rust 拒绝的近零面积多边形 | simple_polygon.rs:52-53 vs SimplePolygon.cpp:233 + NumericPolicy.h:19-20 |
| 2 | Transformed(double) 全量重校验+重算 area/diameter/POI | 差异 | Rust transform 保留原 area/diameter（刚体假设）；C++ double 版重算（更健壮但行为不同）；RustF32 版与 Rust 一致 | simple_polygon.rs:219-247 vs SimplePolygon.cpp:308-336 与 338-374 |
| 3 | TransformedRustF32 的 bbox 有效性校验（Rust 无） | 差异 | 极端变换（缩放致退化）下 C++ 报错、Rust 静默继续 | SimplePolygon.cpp:352-355 vs simple_polygon.rs:244 |
| 4 | From<Rect>（矩形转多边形工厂） | C++缺失 | 便捷构造缺失，行为可由 4 点 Create 手工复刻 | simple_polygon.rs:354-368 vs SimplePolygon.h 无对应 |
| 5 | TransformableFrom::transform_from | C++缺失 | 同前各记录 | simple_polygon.rs:250-277 |
| 6 | surrogate() 空时 panic vs GetSurrogate 返回 nullptr | 差异（形态） | 失败通道不同 | simple_polygon.rs:111 vs SimplePolygon.cpp:392 |
| 7 | Edges() 静默丢弃退化边（Edge2d::Create 失败跳过） | 差异（不可达） | 上游查重已排除退化，实际不可触发 | SimplePolygon.cpp:301-304 vs simple_polygon.rs:91-98 |
| 8 | 非有限值/超 f32 范围检查位置 | 部分实测；整体契约未证 | Rust 1.98.1 Debug/Release中NaN与+Inf两个固定fixture均panic；2026-10-22当前C++ Debug单独`runIoTests()`、Release全量runner均exit 0，对应`InvalidInput`断言通过。Debug全量runner exit 3已定位到更早的`runCollisionTests()`：注册覆盖根节点的`coveringShape`时触发Quadtree“无Partial子项”断言；该失败不来自Importer，整体非有限输入契约仍未证 | SimplePolygon.cpp:203-213 vs simple_polygon.rs:41-74,120-141 + RustOracle `--import-boundary-parity-only` + TestIo.cpp:249-264 + TestCollision.cpp:61-64 + Quadtree.cpp:423-426 |
| 9 | Contains(point, tolerance)（PnPN 容差版）与 Intersects(Edge, tolerance) | C++额外 | 与 Rust 不同的算法族，含边界带语义 | SimplePolygon.cpp:395-428, 476-487 |
| 10 | 重复点查重的 ±0.0/NaN 语义 | ±0.0 差异；NaN 一致 | Rust `Point` 的 `Eq` 与 `to_bits()` 哈希使不同符号零落入不同桶；C++ `==` 将±0视为相等并拒绝。NaN对两侧均不相等 | simple_polygon.rs:45 + point.rs:57-65 + itertools 0.13.0 `unique_impl.rs:100-117` vs SimplePolygon.cpp:219 + point.h:40-43 |

## 结论

simple_polygon.rs 的构造主链路（查重、自交检测、面积反转、Shoelace、质心、直径经凸包）与 ContainsRustF32 射线法（含 FPA 顶点穿透分支的 AlmostEqual/Greater/Less 复刻）在 C++ 中逐式等价；重复点查重存在一个边界差异：Rust `Point` 的位模式哈希让±0.0对顶点通过 `unique()`，C++ `==`会拒绝，而NaN对顶点两侧都不视为重复。其他实质差异包括：面积零判定引入可配置容差、Transformed 双版本中 double 版全量重算偏离 Rust 的"保留派生量"语义、以及 From<Rect>/TransformableFrom 两项 API 缺失；另有一套 C++ 自有的容差算法族（Contains/Intersects）语义与 Rust 射线法不同。

## 补充审计（2026-09-29）——含一处对主记录结论的修正

逐行复核 simple_polygon.rs 全部 368 行（new/generate_surrogate/vertex/edge/edge_iter/n_vertices/surrogate/calculate_diameter/generate_bounding_box/calculate_area/calculate_poi/centroid/find_self_intersection + 2 个 Transformable + 3 个 trait 实现 + From<Rect>），确认主记录已覆盖全部函数。以下为补充核对项，其中第 1 项**修正主记录的一个结论**：

### 【修正】重复点查重的实现路径：itertools unique 使用 HashMap（Hash+Eq）
- 锁定的 itertools 0.13.0 `unique_impl.rs:100-117` 要求 `Item: Eq + Hash + Clone`，并以 `used.entry(v)` 记录已见值；jagua-rs `Point` 在 `point.rs:57-65` 手写 `Eq` 和 `Hash`，哈希两个坐标的 `to_bits()`。
- `+0.0 == -0.0`，但两者的位模式和哈希不同，因此进入不同 HashMap 桶；Rust `unique()`保留±0.0对顶点，C++逐分量 `==` 则拒绝。NaN不等于自身，两个NaN顶点在两侧都不会被查重移除。
- 影响限于这个重复点边界：Rust后续构造仍可能因其他几何检查失败；此处只确认查重阶段的差异，不推断完整多边形构造结果。
- 证据：`simple_polygon.rs:45`；`itertools-0.13.0/src/unique_impl.rs:100-117`；`point.rs:57-65`；C++ `SimplePolygon.cpp:217-223` 与 `point.h:40-43`。

### 【浅覆盖→补齐】calculate_poi 的 diameter 参数为死参数（未参与计算）
- simple_polygon.rs:162-181 的 `calculate_poi(points, diameter)`：`diameter` 仅被写入 dummy_sp.diameter 字段，而 `compute_pole(&dummy_sp, &[])`（pole.rs:54-79）**从不读取 shape.diameter**（pole.rs 全文仅 bbox.diameter()——Rect 的方法）。即该参数是**无效透传**。C++ Create（SimplePolygon.cpp:241-256）直接传本体调 ComputePointOfInaccessibility，无对应参数——两侧均不消费 diameter，等价。
- 判定：【一致】（死参数；C++ 无此形态但无行为影响）。
- 证据：simple_polygon.rs:162-181；pole.rs:54-137（无 shape.diameter 读取）；SimplePolygon.cpp:253-256。

### 【浅覆盖→补齐】collides_with 射线法中 ray 与顶点穿透分支的判定次序
- 主记录已比对 FPA 分支。补充确认一个次序细节：Rust simple_polygon.rs:300 顶点穿透分支命中后**跳过** ray.collides_with 检查（else-if 结构）；C++ SimplePolygon.cpp:455-471 同为 if/else-if。逐字对齐（主记录隐含但未明写 else-if 互斥关系）。
- 判定：【一致】。
- 证据：simple_polygon.rs:300-308 vs SimplePolygon.cpp:455-471。

### 【浅覆盖→补齐】TransformedRustF32 中 area 的符号语义
- 主记录 centroid 条已核 area 取 abs 正值。补充确认：TransformedRustF32（SimplePolygon.cpp:357-358）传给 makeCentroidRustF32 的是 `static_cast<float>(m_Area)`（存的正面积）；Rust transform 路径中 self.area 同为反转后的正值（simple_polygon.rs:57）。一致。
- 判定：【一致】。
- 证据：simple_polygon.rs:54-59 vs SimplePolygon.cpp:357-358。

### 【遗漏→补齐】cfg(test)——simple_polygon.rs 无测试模块
- simple_polygon.rs 368 行无 `#[cfg(test)]`。判定：不适用。

补充结论：05 记录函数级覆盖完整。重复点查重的±0.0边界两侧不同：Rust `unique()`受`Point`的位模式哈希影响，会保留±0.0对顶点；C++ `==`会拒绝。NaN对两侧均不判为重复。
