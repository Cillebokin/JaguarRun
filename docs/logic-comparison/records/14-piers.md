# 14. Piers（fail_fast/piers.rs）逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/fail_fast/piers.rs`（228 行）
**C++**: `JaguarCore/Source/FailFast/Surrogate.cpp`（piers 部分：cpp:86-490，含 generateCandidatePiers/clipRayRustF32/generateUnrepresentedGrid/pierLoss/generatePiers 等）
**判定**: 基本等价（常量、射线变换、裁剪、损失函数、择优一致；eef0830 后空候选已对齐为报错；缺"射线端点在形外"断言）

## 函数逐条比对

### 常量（Rust 16-20 行）
```rust
static RAYS_PER_ANGLE: usize = if cfg!(debug_assertions) { 10 } else { 200 };
static N_ANGLES: usize = if cfg!(debug_assertions) { 4 } else { 90 };
static N_POINTS_PER_DIMENSION: usize = if cfg!(debug_assertions) { 10 } else { 100 };
static CLIPPING_TRIM: f32 = 0.999;
static ACTION_RADIUS_RATIO: f32 = 0.10;
```
- C++ 对应：`generateCandidatePiers` 内 `#ifdef NDEBUG`（Surrogate.cpp:186-194）：release `raysPerAngle=200, angleCount=90`，debug `10, 4`；`generateUnrepresentedGrid` 内（cpp:333-337）：release `pointsPerDimension=100`，debug `10`；`trimEdgeRustF32(edge, 0.999F)`（cpp:181）；`radiusOfInfluence = 0.10F * expandedWidth`（cpp:463）。
- 判定：一致（5 组常量全部对齐：200/10、90/4、100/10、0.999、0.10；debug/release 门控语义对应 cfg!(debug_assertions) ↔ 非 NDEBUG）。

### `generate_piers`（Rust 25-94 行）
- C++ 对应：`generatePiers`（Surrogate.cpp:440-490）。
- 判定：基本等价。

逐段对照：

**n==0 短路**（Rust 26-28：返回空）
- C++ cpp:445-446：`FromValue({})`。一致。

**bbox 与基准射线**（Rust 31-39：`expanded_bbox = bbox.inflate_to_square()`；质心起、`±2 * expanded_bbox.height()` 的竖直射线）
- C++ cpp:197-208：`squareSearchBoxRustF32(shape.BoundingBox())`（cpp:33-47，f32 扩方）；`halfRayLength = 2.0F * expandedBox.Height()`；`baseStart/baseEnd` 为质心 x、`centerY ∓ halfRayLength`。
- 判定：一致。

**候选生成与裁剪**（Rust 41-50）
- C++：`generateCandidatePiers(shape)`（cpp:186-241，一次生成所有角度×平移的裁剪射线并拼接）。
- 判定：一致（生成顺序与拼接语义相同：角度外层、平移内层，flat_map ↔ 双层 for + insert）。

**未表示点网格**（Rust 51-52）
- C++ cpp:460：`generateUnrepresentedGrid(shape, poles)`。一致。

**影响半径与 forfeit 距离**（Rust 56-57）
```rust
let radius_of_ray_influence = ACTION_RADIUS_RATIO * expanded_bbox.width();
let forfeit_distance = f32::sqrt(bbox.width().powi(2) * bbox.height().powi(2));
```
- C++ cpp:455-463：`forfeitDistance = sqrt(boxWidth² * (boxHeight²))`（cpp:458-459，数学上即 w*h 的 sqrt）；`radiusOfInfluence = 0.10F * squareSearchBoxRustF32(box).Width()`。
- 判定：一致（注意 C++ 的 expandedWidth 以 `box`（原始 bbox）再扩方计算，与 Rust 用 `expanded_bbox.width()` 相同输入相同结果）。

**择优循环**（Rust 59-92：每轮重算 min_distances，对每条裁剪射线算 loss，取 `min_by_key` 的 NotNan loss）
- C++ cpp:466-488：每轮 `distancesToPiers/distancesToPoles` 重算，线性扫描取 `loss < bestLoss` 的首个最小值。
- 判定：一致。Rust `min_by_key` 返回**第一个**最小元素；C++ `loss < bestLoss` 严格小于更新（相等不换），同为保首个最小。Rust `NotNan::new(x).unwrap()`（79 行）在 NaN 时 panic；C++ 无 NaN 防御（float 运算产生 NaN 时比较全 false，行为退化为保首个）。正常输入下 loss 有限，无差异。

**空候选报错**（Rust 88-91：`None => bail!("no ray found")`）
- C++ cpp:448-452（eef0830 后）：`candidates.empty()` → `FromError({ErrorCode::Infeasible, "no ray found"})`。
- 判定：一致（**eef0830 已对齐**：此前 C++ 静默返回空，现为 Infeasible 错误；错误文本对齐 "no ray found"）。
- 细节：Rust 的 `None` 出现在 `clipped_rays` 为空时（`min_by_key` 对空迭代器返回 None），与 C++ 的 `candidates.empty()` 同一触发条件。但注意 Rust 在**每轮循环**内检查（若首轮有候选而后续轮次不会变空——候选集固定，故等价）；C++ 在循环外一次性检查。等价。

### `generate_ray_transformations`（Rust 97-122 行）
- C++ 对应：内联于 `generateCandidatePiers`（cpp:209-239）。
- 判定：基本等价。

**平移集**（Rust 103-107：`dx = bbox.width()/rays_per_angle`，`x = x_min + dx*i`）
- C++ cpp:199, 222-223：`translationStep = side / raysPerAngle`；`x = expandedBox.xMin + translationStep * i`。一致（side 即 expandedBox.Width()）。

**角度集**（Rust 109-110：`Array::linspace(0.0, PI, n_angles+1)` 取前 n_angles 个）
- C++ cpp:200, 213：`angleStep = pi / angleCount`；`angle = angleStep * angleIndex`（0..angleCount）。
- 判定：一致（linspace(0, π, n+1) 的前 n 个 = {0, π/n, 2π/n, …, (n-1)π/n} = angleStep*i；C++ 直接用步进式，数值上 linspace 的 i*(π/n) 与 C++ 相同；`pi` 常量 cpp:196 = 3.14159265358979323846F）。

**组合变换 R*T**（Rust 113-121：`translation.rotate(*angle)`——平移先施加、再绕原点旋转）
- C++ cpp:217-229：构造 `Transform2d{angle, Point2d{cos*x, sin*x}}`（旋转 + 旋转后的平移分量），注释明确"R * T, so the translation component is also rotated"。
- 判定：一致（Rust `Transformation::rotate` 把变换矩阵左乘旋转，平移分量被旋转；C++ 直接给出等价矩阵）。基准射线两端点经 `rayTransform.ApplyRustF32`（cpp:230-231）↔ Rust `base_ray.transform_clone(&t)`（f32 精度路径对齐）。

### `clip`（Rust 125-152 行）
- C++ 对应：`clipRayRustF32`（Surrogate.cpp:149-184）。
- 判定：基本等价。

**端点在形外断言**（Rust 127）
```rust
assert!(!shape.collides_with(&ray.start) && !shape.collides_with(&ray.end));
```
- C++ 对应：**缺失**（clipRayRustF32 无此检查）。基准射线长为 4 倍 bbox 高、两端在扩方 bbox 之外，正常输入下断言恒真；C++ 信任这一不变量。判定：C++ 缺失（debug 断言，release 下 Rust 也不检查；不可达防御）。

**求交与排序**（Rust 130-134：`edge.collides_at(ray)` 收集交点，按到 ray.start 距离排序）
- C++ cpp:158-173：`intersectRustF32(edge, ray)` 收集（cpp:92-129，含 bbox 预检 + t/u 公式，t/u ∈ [0,1] 判相交，与 Rust `Edge::collides_at` 的求交语义对齐，见记录 03），按 `distance <` **stable_sort**。
- 判定：一致。Rust `sorted_by_key(NotNan)` 为稳定排序 ↔ C++ `stable_sort`。距离计算 f32（cpp:164-167）。

**成对裁剪 + trim**（Rust 137-151：chunks(2)，单交点丢弃；`start == end` 丢弃；`Edge::try_new(start, end).scale(CLIPPING_TRIM)`）
- C++ cpp:175-183：`for i=1; i<size; i+=2` 取 `(i-1, i)` 对；`Edge2d::Create` 失败跳过；`trimEdgeRustF32(edge, 0.999F)`（cpp:131-147：两端各延伸 (factor-1)/2 倍 d——即向内收缩，与记录 12 相同的等价缩放）。
- 判定：一致（chunks(2) 丢弃奇数尾部 ↔ i+=2 不含尾单；`start == end` 时 Rust 返回 None 丢弃，C++ `Edge2d::Create` 对零长边的行为是失败跳过（见记录 03 的 try_new 语义）——等价）。奇数个交点（射线端点恰在边上）时 Rust 丢尾对、C++ 同样不处理尾单，一致。

### `generate_unrepresented_point_grid`（Rust 154-173 行）
- C++ 对应：`generateUnrepresentedGrid`（Surrogate.cpp:329-373）。
- 判定：基本等价。
- 网格：Rust `Array::linspace(x_min, x_max, n)` × `linspace(y_min, y_max, n)`，先 x 外层 y 内层（163-171）；C++ cpp:344-359：`xStep = (xMax-xMin)/(n-1)`、`yStep` 同，x 外层 y 内层。linspace(n) 与步进式在 n≥2 时数值一致（linspace 的 i*(max-min)/(n-1)）。
- 过滤：`shape.collides_with(p)`（在形内）且 `poles.iter().all(|c| !c.collides_with(p))`（不在任一 pole 内）↔ cpp:360-367 `rustF32PointInside` + `any_of(rustF32CircleContains)` 取反。
- 差异（实现载体）：C++ 的 `rustF32PointInside`（cpp:282-320）是为避免 SimplePolygon 依赖而内联的射线法（bbox 预检 + 水平射线计数，含 AlmostEqualRustF32/GreaterRustF32/LessRustF32 的 fpa 近似），语义对应 Rust `SPolygon::collides_with(point)`（见记录 05 的射线法比对）；`rustF32CircleContains` ↔ `Circle::collides_with(point)`（记录 04）。这两处为等价重实现而非调用同一基元，边缘判定（±0.0/NaN）按记录 05 的已知差异。

### `loss_function`（Rust 175-205 行）
- C++ 对应：`pierLoss`（Surrogate.cpp:418-438）。
- 判定：一致。
```rust
let min_distance_to_ray = f32::min(*min_distance_to_existing_ray, distance_to_new_ray);
if min_distance_to_ray < radius_of_ray_influence {
    f32::min(*min_distance_to_pole, min_distance_to_ray)
} else { *min_distance_to_pole }
```
- C++ cpp:427-434：`distanceToPier = min(distancesToSelectedPiers[i], distanceToCandidate)`；`distanceToPier < radiusOfInfluence ? min(distancesToPolesForPoints[i], distanceToPier) : distancesToPolesForPoints[i]`；cpp:435 `loss += d*d`（`powi(2)` ↔ 乘法，f32 下 powi(2) 即 x*x，一致）。
- 逐字对齐（`<` 严格比较、min 链、平方求和）。

### `min_distances_to_rays`（Rust 207-216 行）
- C++ 对应：`distancesToPiers`（Surrogate.cpp:375-389）。
- 判定：一致（fold forfeit 起始 + min；`pointToEdgeDistanceRustF32` = `Edge2d::DistanceToRustF32`，f32 化）。

### `min_distances_to_poles`（Rust 218-228 行）
- C++ 对应：`distancesToPoles`（Surrogate.cpp:391-416）。
- 判定：一致。Rust `Circle::distance_to(p)`（点在圆内为 0，否则 dist-r，见记录 04）↔ C++ cpp:402-411：`squaredDistance < radius² ? 0.0F : sqrt - radius`。注意 C++ 用 `<`（边界恰好等于半径时给 sqrt-r = 0，与 Rust `distance_to` 的行为一致——Rust 的 `distance_to` 对边界点返回 0，两侧等价）。fold 初值 forfeit_distance 一致。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | RAYS_PER_ANGLE 200/10、N_ANGLES 90/4、N_POINTS 100/10 | 一致 | 无 | Rust 16-18 vs Surrogate.cpp:188-194, 333-337 |
| 2 | CLIPPING_TRIM 0.999 | 一致 | 无 | Rust 19 vs cpp:181 |
| 3 | ACTION_RADIUS_RATIO 0.10 × expanded width | 一致 | 无 | Rust 20, 56 vs cpp:463 |
| 4 | 基准射线（质心、±2×扩方高） | 一致 | 无 | Rust 31-39 vs cpp:197-208 |
| 5 | 角度集 linspace(0,π,n+1) 前缀 vs step*i | 一致 | 无 | Rust 109-110 vs cpp:200, 213 |
| 6 | 变换组合 R*T（平移分量随旋转） | 一致 | 无 | Rust 113-121 vs cpp:217-229 |
| 7 | forfeit_distance = sqrt(w²h²) | 一致 | 无 | Rust 57 vs cpp:458-459 |
| 8 | 裁剪：稳定排序 + 成对 + trim + 奇尾丢弃 | 一致 | 无 | Rust 130-151 vs cpp:158-183 |
| 9 | 空候选报错（"no ray found"） | 一致 | **eef0830 已对齐**（Infeasible） | Rust 88-91 vs cpp:448-452 |
| 10 | 损失函数（min 链、影响半径判定、平方和） | 一致 | 无 | Rust 175-205 vs cpp:418-438 |
| 11 | 择优保首个最小（min_by_key ↔ `<` 严格更新） | 一致 | 无 | Rust 82-86 vs cpp:473-487 |
| 12 | 射线端点在形外断言（assert） | C++ 缺失 | 不可达防御；release 下 Rust 同样无检查 | Rust 127 vs cpp:149-184（无） |
| 13 | 点在形内/圆内判定为内联重实现（rustF32PointInside/rustF32CircleContains） | 一致 | 边缘 ±0.0/NaN 判定按记录 05 已知差异 | cpp:282-320, 322-327 vs Rust 169-170 |
| 14 | NotNan unwrap（NaN panic）→ 无 NaN 防御 | 差异 | 仅 NaN 输入；正常 loss 有限 | Rust 79 vs cpp:473-487 |
| 15 | 每轮重算 min_distances（非增量） | 一致 | 无（性能特征相同） | Rust 60-66 vs cpp:468-471 |

## 结论
piers 生成的全链路（常量 200/10、90/4、100/10、0.999、0.10；基准射线与 R*T 变换组合；稳定排序成对裁剪；linspace 网格与双过滤；损失函数的 min 链与平方和；保首最小择优）在双侧逐条等价，f32 精度模拟贯穿。2026-09-28 eef0830 提交已把空候选从静默返回改为 `Infeasible "no ray found"`，与 Rust 的 bail 对齐。剩余差异：C++ 缺"射线端点在形外"的 debug 断言（不可达不变量）、点/圆包含判定为等价内联重实现（边缘数值行为按记录 05）、NotNan panic 无对应。正常输入下选出的 pier 序列完全一致。
