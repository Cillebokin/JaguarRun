# 11. Offset（shape_modification.rs 偏移部分）逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/shape_modification.rs`（550 行，本记录覆盖 offset 相关：338-379 行）
**C++**: `JaguarCore/Source/Geometry/ShapeModification/Offset.cpp`（343 行）+ `StraightSkeleton.cpp`（910 行）+ `StraightSkeleton.h`（15 行），对照 `Geometry/ShapeModification.h`（53 行）
**判定**: 不等价（**算法本体替换**：Rust 调 geo-buffer 0.2.0 crate，C++ 是对 geo-buffer 事件模型的私有重实现 + 保模校验 + 简易 fallback + 双 API；失败边界不同）

**geo-buffer 版本确认**（以当前工作区为准）：`CodeResource/jagua-rs/Cargo.lock` 中 `geo-buffer 0.2.0`（checksum 267bf037…）。C++ 侧 StraightSkeleton.cpp:1-9 头注释自述为"event model used by geo-buffer 0.2.0"的独立 C++17 重实现，不链接 Rust。

## 函数逐条比对

### `offset_shape`（Rust 340-379 行）
- C++ 对应：`OffsetShape`（Offset.cpp:261-287，单轮廓 API）+ `OffsetShapes`（Offset.cpp:289-342，全轮廓 API）。
- 判定：差异（结构性）。

逐段对照：

**符号化距离**（Rust 341-344：`Deflate→-distance, Inflate→distance`）
- C++ `normalizeOffsetRustF32`（Offset.cpp:26-36）：先 `f32` 化距离（cpp:29-30，模拟 Rust `distance: f32` 参数），再按 mode 取符号，然后**重新归一化 mode**（`signedDistance < 0 → Deflate, 否则 Inflate`）并取绝对值（cpp:31-35）。
- 差异：C++ 允许传入负 `distance`（如 Inflate -5 → 实际按 Deflate 5 处理）；Rust 的 `offset_shape` 直接把负 distance 传给 geo-buffer（`buffer_polygon_rounded(&geo_poly, f64::from(offset))`，负值即向内缩）。语义上 Rust 用 f64 传入负偏移也会向内缩，C++ 的归一化与之殊途同归，但 **C++ 额外做了 f32 化 + mode 重判**，且 `OffsetShapes` 内部再次以 `Inflate?+d:-d` 传给 skeleton（cpp:301-305），行为等价于 Rust 的单一符号路径。

**核心算法调用**（Rust 356：`geo_buffer::buffer_polygon_rounded(&geo_poly, f64::from(offset)).0`）
- Rust 语义（geo-buffer 0.2.0）：输入多边形 + 有符号距离，返回 `(Vec<Polygon>, ..)`；`.0` 取外轮廓结果列表。内部是圆角 straight-skeleton 事件模型。
- C++ `OffsetShape`（cpp:274-275）委托 `OffsetShapes` → `detail::roundedStraightSkeletonOffsets`（StraightSkeleton.cpp:888-909）：
  - `Skeleton` 构造（cpp:516-539）：对每个顶点建 `makeInitialVertex`（左/右射线 + 内角平分线轴，cpp:425-435），初始化环形 `VertexQueue`，为每个活动顶点生成 shrink 事件（相邻射线相交，cpp:607-632）与 split 事件（角点反向射线与其它边前向平分相交，cpp:634-734）。
  - `buildEvents`（cpp:736-818）：按 `timelineLess`（cpp:471-494，时间→种类 Split 优先→坐标→索引的字典序）取最小事件，应用 shrink（合并两顶点为新树节点，cpp:744-778）或 split（一拆二，cpp:781-816），直至事件耗尽。
  - `loops(offsetDistance)`（cpp:541-599）：以初始队列重放所有 `time <= offsetDistance` 的事件，每个幸存顶点输出 `axis.point(remaining)`（直边中点情形）或按 `ROUNDED_STEP = 0.1` 弧度步进在左右法线间插圆弧点（cpp:586-595）——对应 geo-buffer 的 "rounded" 语义。常量 `ROUNDED_STEP 0.1`（StraightSkeleton.cpp:28）。
  - `cleanLoop`（cpp:859-872，EPSILON=1e-9 去重点）+ `signedArea > EPSILON` 正面积过滤（cpp:902）+ `createRustImportedOffsetPolygon`（先 `eliminateDegenerateOffsetVertices` cpp:56-78 用 `rustApproxEqual`（f32 相等或 ULP≤4，cpp:39-54）去重复点，再 `SimplePolygon::Create`）。
- 判定：差异（算法本体替换）。C++ 是对 geo-buffer 0.2.0 事件模型（顶点队列 + shrink/split 时间线 + 圆角步进）的语义级重实现，而非逐行翻译；两实现的浮点细节（求交、epsilon、事件排序 tie-break）不可能逐位一致，**相同输入可产生顶点级不同（拓扑同构）的偏移多边形**。

**多轮廓处理**（Rust 358-367）
```rust
match geo_poly_offsets.len() {
    0 => bail!("Offset resulted in an empty polygon"),
    1 => &geo_poly_offsets[0],
    _ => { warn!("Offset resulted in multiple polygons, taking the first one."); &geo_poly_offsets[0] }
}
```
- C++ `OffsetShape`（cpp:274-283）：`OffsetShapes` 成功且非空 → 取 `.front()`（取第一个，一致）；空 → 返回 `OffsetFailed "Offset resulted in an empty polygon"`，与 Rust 的失败语义和文本一致（C++ 保留 `Result` 错误传递）。
- 判定：取首组件逻辑一致；**差异**在于：
  1. Rust 的 `warn!` 多分支仅日志不同，行为与单轮廓相同（都是取 [0]）；C++ 无日志。
  2. Rust `offset_shape` 在导入前就选首组件；C++ `OffsetShape` 经 `OffsetShapes` 选首个有效 `SimplePolygon`。C++ 的 `OffsetShapes` 是额外 API，逐轮廓校验/过滤只影响该额外 API；需验证其首组件仍与 Rust 结果相同。
  3. C++ `OffsetShape` 在骨架结果为空时**不再直接报错**，而是落入 `fallbackOffsetShape`（cpp:286）——但注意 `OffsetShape` 路径中 `OffsetShapes` 已在内部先行处理空结果（可能返回空 vector、fallback 单元素或错误，见下），到 cpp:278 的空判断时实际值取决于 `OffsetShapes` 的内部分支。

**多组件 oracle 边界：** `print_buffer_components` 直接探测 geo-buffer 的原始多组件结果，并将距离先按 JaguarCore/Rust 核心 API 的 `f32` 输入量化。它不能直接当作 Rust `offset_shape` 的公开返回值：Rust 只选第一个组件后导入；C++ `OffsetShapes` 则只暴露可构造成 `SimplePolygon` 的组件。梳形 `Deflate 2.1` fixture 的首组件在两侧一致；原始列表中的第二环不属于 Rust 核心 API 返回值，不能据此判定 `OffsetShape` 分叉。

**回转内部表示**（Rust 370-378：取 exterior 点集 → `ExtSPolygon` → `import::import_simple_polygon`）
- C++：`reimportOffsetPolygon`（Offset.cpp:38-41）→ `createRustImportedOffsetPolygon`（StraightSkeleton.cpp:875-886）——先去退化点再 `SimplePolygon::Create`（即 Rust import 的多边形构造校验：简单性、定向、最小顶点数等，见记录 34/05）。
- 判定：一致（语义上同为"重建为内部多边形并走导入校验"；Rust 走 f64→f32 收缩，C++ 直接用 double 顶点但构造前有 f32 近似去重）。

### `preservesModificationMode`（C++ Offset.cpp:148-178）—— Rust 无对应
- 判定：C++ 额外。
- 内容：面积相等时按 `samePolygonCycle`（cpp:121-146，环形同序列比较）判等；Inflate 要求新面积 ≥ 原面积且原顶点全在新形内（容差 1e-8，`Contains(point, tolerance)`）；Deflate 要求新面积 ≤ 原且新顶点全在原形内。
- 影响：Rust 只 `warn!` 取首组件、完全信任 geo-buffer 的保模性；C++ 会因骨架输出轻微违反包含关系（浮点误差量级）而**硬失败**（`OffsetFailed "straight-skeleton offset does not preserve the requested relation"`，cpp:336-338）。这是 C++ 的强校验机制，与 Rust "只 warn 取首组件" 的宽容策略相反。

### `fallbackOffsetShape`（C++ Offset.cpp:180-257）—— Rust 无对应
- 判定：C++ 额外。
- 内容：逐顶点法线平移 + 交点/圆角连接（`appendRoundedJoin` 步长 0.1 弧度，cpp:87；deflate 时法线取反，cpp:219）+ `preservesModificationMode` 校验失败报错（cpp:250-255）。仅在骨架路径失败/为空的分支被调用（Offset.cpp:286、317-323）。已有 L 形 deflate fixture 的状态、组件数、面积和边界框与 Rust oracle 一致；其他 fallback 输入仍需对照。
- 调用点分析（OffsetShapes cpp:309-330）：骨架为空时——若 Deflate 且 `distance >= shape.PointOfInaccessibility().radius`（收缩到 POI 半径及以内），返回空 vector（**有意接受"收缩到消失"**）；否则尝试 fallback；fallback 也失败时 Inflate 报错、Deflate 返回骨架的空结果。
- **Deflate 收缩到消失的半径判定**：Rust 侧 geo-buffer 对过度 deflate 的表现由 crate 内部决定（通常空结果），Rust `offset_shape` 对空结果一律 `bail!`（358-359）。C++ 用 `PointOfInaccessibility().radius` 作为阈值把"合理消失"与"异常失败"区分开——**与 Rust 失败边界不同**：Rust 会报错的输入（deflate 超过 POI 半径）在 C++ `OffsetShapes` 返回空成功、`OffsetShape` 报 "empty polygon" 错误，而**未超阈值**的空结果 Rust 报错、C++ 走 fallback 可能成功返回。

### `OffsetShapes` 双 API（C++ ShapeModification.h:45-46, Offset.cpp:289-342）—— Rust 无对应
- 判定：C++ 额外。Rust 单 API `offset_shape` 只暴露首组件；C++ 增加返回全部有效轮廓的 `OffsetShapes`，`OffsetShape` 保留兼容性只取第一个（h:43-44 注释自述）。头部注释（StraightSkeleton.cpp:5-8）说明私有原因：skeleton 天然产生零或多个分量，而公开 API 是单多边形。

### StraightSkeleton 内部构件与 Rust/geo-buffer 的对应关系（抽样式核对）
| Rust（geo-buffer 0.2 语义） | C++ | 判定 |
|---|---|---|
| 圆角偏移的弧步进（buffer "rounded"） | `ROUNDED_STEP = 0.1`（StraightSkeleton.cpp:28, 589-590） | 常量需与 crate 源核对；C++ 注释声明按 0.2.0 事件模型，无 crate 内常量可逐位对照（闭源重实现） |
| 顶点事件（edge collapse/shrink） | `makeShrinkEvents` + Timeline::Kind::Shrink（cpp:607-632, 744-778） | 结构对应 |
| 分裂事件（split event） | `findSplit`/`makeSplitEvents`（cpp:634-734） | 结构对应；含 initial/non-initial 两套方向过滤（cpp:683-702） |
| 事件排序 | `timelineLess`：time→Split 优先→location→real index（cpp:471-494） | 对应 crate 的 BinaryHeap+Reverse 语义（tie-break 无法逐位验证） |
| 队列重放生成 offset 环 | `loops(distance)` 重放 `time <= d` 事件（cpp:541-551） | 结构对应 |
| 输出环清理 | `cleanLoop`（EPSILON 1e-9）+ `eliminateDegenerateOffsetVertices`（rustApproxEqual: f32 相等或 ULP≤4） | 对应 crate 输出清理语义；阈值选取为 C++ 自定 |

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 算法本体：geo-buffer 0.2.0 crate → 私有 straight-skeleton 重实现 | 差异 | 顶点级输出可不同（拓扑同构）；tie-break/epsilon 细节不可逐位对齐 | Rust 356 vs StraightSkeleton.cpp:1-9, 516-599, 736-818 |
| 2 | `offset_shape` 单 API vs `OffsetShape`+`OffsetShapes` 双 API | C++ 额外 | 接口面差异；OffsetShape 取 `.front()` 与 Rust 取 `[0]` 一致 | Rust 340-379 vs ShapeModification.h:39-46, Offset.cpp:261-342 |
| 3 | 多轮廓：Rust 核心 API 取首 / C++ 额外 `OffsetShapes` 暴露有效组件 | 接口面额外 | Rust `OffsetShape` 与 C++ `OffsetShape` 都取首个公开结果；原始 raw 组件数不等于 C++ 可构造的有效 `SimplePolygon` 数 | Rust 361-366 vs Offset.cpp:274-342；comb fixture |
| 4 | `preservesModificationMode`（面积+双向顶点包含，容差 1e-8） | C++ 额外，潜在分叉未复现 | 目前覆盖 fixture 未发现其改变核心 `OffsetShape` 的成功/失败边界；仍需专门覆盖校验失败路径 | Offset.cpp:148-178 |
| 5 | `fallbackOffsetShape`（法线平移+圆角连接的简易后备） | C++ 额外，潜在分叉未复现 | L 形 deflate fixture 与 Rust 结果一致；其他 fallback 失败边界仍待 fixture | Offset.cpp:180-257, 286, 317-323 |
| 6 | Deflate 收缩到消失：`OffsetShapes` 可返回空 vector | 额外 API 差异 | Rust 核心 `offset_shape` 把空结果报错；C++ `OffsetShape` 也映射为空多边形错误，只有额外 `OffsetShapes` 暴露空 vector | Rust 358-359 vs Offset.cpp:274-281, 311-314 |
| 7 | 空结果错误消息 | 一致 | 错误类别映射为 `OffsetFailed`，消息与 Rust 对齐为 "Offset resulted in an empty polygon" | Rust 359 vs Offset.cpp:281 |
| 8 | 回转内部表示（exterior→导入校验） | 一致 | 语义对齐（去退化点+Create 校验） | Rust 370-378 vs StraightSkeleton.cpp:875-886 |
| 9 | 距离 f32 化（normalizeOffsetRustF32）+ mode 归一 | 一致（模拟）/ C++ 额外（负距离重判） | Rust 参数即 f32；负距离路径 C++ 更宽容 | Rust 340-344 vs Offset.cpp:26-36 |
| 10 | 非有限距离（NaN、±Infinity） | 行为边界一致 | Rust Debug/Release oracle 均返回空多边形错误；C++ `OffsetShape`/`OffsetShapes` 映射为 `OffsetFailed` 和相同消息，保留约定的 `Result` 传递 | Rust oracle `--offset-boundary-parity-only`；Offset.cpp:266-270, 294-298；TestShapeModification |
| 11 | 圆角采样方向 | 已对齐 | C++ 现按首个骨架顶点复现 Rust `get_orientation()`；不再把 offset 的 inward/outward 标志误当作环方向 | Rust `get_orientation` vs StraightSkeleton.cpp:541-600 |
| 12 | ROUNDED_STEP 0.1 / EPSILON 1e-9 / ULP≤4 等内部常量 | 源码可核对，浮点输出仍需 fixture | geo-buffer 0.2.0 源码与 C++ 实现可直接对照；数值容差与 tie-break 仍通过 Rust oracle 验收 | geo-buffer 0.2.0 source vs StraightSkeleton.cpp:27-28, 39-54 |

## 结论
这是全部比对组中唯一的**算法本体替换**：Rust 调用 geo-buffer 0.2.0，C++ 用 straight-skeleton 事件模型重实现。已覆盖 fixture 的核心结果在 Debug/Release 对照中一致；圆角采样方向按 Rust `get_orientation()` 修正，非有限距离边界也已对齐。C++ 的保模校验、fallback 和 `OffsetShapes` 属额外实现；L 形 fallback 已有匹配 fixture，其余分支仍未完整验收。由于 Offset 仍缺少对这些分支的完整差分证据，本项暂不宣称等价。
