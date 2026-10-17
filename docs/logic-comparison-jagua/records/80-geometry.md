# Geometry 逐文件静态映射

基线：pinned `jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。本记录覆盖清单剩余的 15 个 geometry 实现文件。Rust 几何值以 f32 为主；JaguarCore 使用 double 存储，并为若干对拍路径提供 `RustF32` 方法。这里的静态对应不代表所有调用路径都选用了这些方法，也不代表数值等价已经运行验证。

## 类型、变换与通用算法

### `jagua-rs/src/geometry/geo_enums.rs`

| Rust 声明/行 | 逻辑与边界 | JaguarCore 对应与差异 |
|---|---|---|
| `GeoPosition`、`GeoRelation`、`RotationRange` (1–29) | 位置为 Interior/Exterior；关系按 A 对 B 区分 Intersecting、Enclosed、Surrounding、Disjoint；旋转范围为 None、Continuous 或离散 f32 列表 | `GeoPosition`、`RectRelation` 在 `Geometry/Rect.h`；旋转策略对应 `Entities/Item.h::RotationRange`。C++ 没有完全相同的通用 `GeoRelation` 类型，矩形关系枚举是具体落点 |

### `jagua-rs/src/geometry/geo_traits.rs`

| Rust trait/行 | 逻辑与边界 | JaguarCore 对应与差异 |
|---|---|---|
| `CollidesWith`、`AlmostCollidesWith`、`DistanceTo`、`SeparationDistance` (4–35) | 定义按类型参数分发的相交、近似相交、距离、带内外位置的分离距离合同；近似相交倾向容忍度引发的 false positive | C++ 没有同构泛型 trait；落在 `Point2d/Edge2d/Circle2d/Rect2d/SimplePolygon` 的重载与 `RustF32`/almost 方法。必须逐类型对照调用点，不能凭 trait 同名推定覆盖一致 |
| `Transformable`、`TransformableFrom` (37–57) | 原地变换需 `Clone`；默认 `transform_clone` 先复制再变换；`transform_from` 把 reference 变换写入已有对象以复用分配 | C++ 按类型提供 `TransformRustF32`、`TransformedRustF32` 等接口，没有统一 trait 默认实现；有状态缓存的类型需分别审计 |

### `jagua-rs/src/geometry/d_transformation.rs`

| Rust 声明/函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `DTransformation/new`、访问器与 `compose` (11–53) | 保存弧度与 x/y 平移，三个值用 `NotNan<f32>`；`new` 遇 NaN 时 `expect` panic，但不拒绝 infinity；可转换为矩阵 `Transformation` | C++ `Transform2d`（`Geometry/Transformation.h`）直接保存分解式 rotation/translation，double 存储；通过 `ApplyRustF32`、`InverseRustF32`、`FollowedByRustF32`显式走 f32 算术 |
| `From<Transformation>`、`Display`、`normalize_rotation` (55–85) | 从矩阵分解；文本输出角度/位移；rotation 以 `% 2π` 归一化，负余数加 `2π`，NaN/infinity 不在此函数拒绝 | `Transform2d` 的旋转/平移字段承接分解数据；`NormalizeRotationRustF32` 对应公开角度 helper，B1 首轮已比较有限角、NaN/infinity 与 signed zero。Rust `Display` 暂无同构格式化 API |

### `jagua-rs/src/geometry/transformation.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| 构造与组合 (13–79) | 3×3 仿射矩阵元素为 `NotNan<f32>`；提供 empty、纯平移/旋转、rotate、translate、rotate_translate、translate_rotate 和左乘式 transform。输入 NaN 在 `NotNan::new(...).expect` 处 panic | C++ `Transform2d` 用分解参数表达刚体变换；`FollowedByRustF32` 做 f32 组合，操作顺序需按调用链区分，不能把矩阵存储形式当成一致 |
| `inverse`、`decompose` 与矩阵 helper (80–223) | 逆矩阵按通用 3×3 行列式公式计算，无 singular guard；`decompose` 用 atan2 取旋转、读取平移项，只对刚体矩阵有预期意义；乘法/除法可能产生 inf，NaN 包装处可 panic | `Transform2d::InverseRustF32/FollowedByRustF32`（`Transformation.cpp:49–91`）提供刚体公式并将中间量转 f32；输入受 C++ double API 与 Rust `NotNan` 构造路径约束不同 |

### `jagua-rs/src/geometry/convex_hull.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `convex_hull_indices`、`convex_hull_from_surrogate` (7–29) | 先重建凸包点，再对原顶点做精确相等 `position().unwrap()` 得索引；surrogate 缺失返回 anyhow error，索引来自 surrogate 时直接索引顶点 | `ConvexHull`（`Geometry/ConvexHull/ConvexHull.cpp`）返回点；Surrogate 创建时自行映射索引，找不到点返回 `InvalidPolygon`，不是 Rust 的 unwrap 路径 |
| `convex_hull_from_points`、`grow_convex_hull` (31–66) | 按 x 的 `OrderedFloat` 稳定排序，构造 monotone chain；叉积 `<=0` 会移除共线/右转点；空输入返回空向量，无显式错误 | C++ `ConvexHull` 对点先量化 f32、稳定按 x 排序并以 f32 叉积运行（`ConvexHull.cpp:10–74`）；算法结构相同，非有限点排序、重复点与 hull 索引映射仍需边界 fixture |

### `jagua-rs/src/geometry/original_shape.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `OriginalShape`、`convert_to_internal` (15–52) | 先应用 pre_transform；非零 offset 调用 `offset_shape` 并传播 Result；有 simplify tolerance 时简化，若同时配置 narrow cutoff 则先闭合再以 tolerance/10 二次简化；只在简化分支 Debug 检查修改方向 | `OriginalShape::ConvertToInternal`（`OriginalShape.cpp:5–45`）同序调用并传播 transform/offset/simplify 错误；C++ 无论 Debug/Release 都运行 `ShapeModificationValid`，失败返回 `InvalidPolygon`，Rust 只在 Debug `debug_assert!` |
| metadata getters (54–72) | centroid/area/bbox/diameter 直接读取原始 `shape`，不代表转换后的 internal shape | C++ `OriginalShape` 保存相同来源 shape 与 preTransform；应区分 DTO 原始指标和内部多边形缓存值 |

## 基本几何原语

### `jagua-rs/src/geometry/primitives/point.rs`

| Rust 类型/函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `Point` 与变换 (6–43) | tuple f32；用矩阵乘列向量公式变换；`transform_from` 对 reference 点计算后写入自身 | `Point2d`（`Geometry/Point.h`）以 double 存储，`ApplyRustF32`/各原语的 `TransformRustF32` 对齐 f32 算术 |
| 距离、Eq/Hash 与转换 (45–87) | 欧氏距离及平方距离；手写 Eq 建立在 f32 `PartialEq` 上，但 Hash 使用 `to_bits`，所以 `+0.0 == -0.0` 而 hash 不同；从 tuple 双向转换；反向委托 `CollidesWith` | C++ `Point2d::DistanceToRustF32/SquaredDistanceToRustF32` 对应距离；未见与 Rust 手写 Hash/Eq 同构的通用键合同，signed-zero 语义要在使用哈希/去重的路径验收 |

### `jagua-rs/src/geometry/primitives/edge.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| 构造、扩展、缩放、反向 (15–65) | `try_new` 只拒绝两端点完全相等；端点字段可直接构造。extend/scale 按线段方向改端点，零长边直接调用可能除零；`collides_at` 需要交点但若底层返回 `Yes(None)` 会 panic | `Edge2d::Create`、`ExtendedAtFrontRustF32`、`ExtendedAtBackRustF32`、`ScaledRustF32`、`ReversedRustF32`、`IntersectionPointRustF32`（`Edge.h/.cpp`）对应；C++ 构造经 Result，并在存入前 f32 量化 |
| 最近点、边界指标、距离与相交 (67–248) | 最近点投影参数截断到端点/线段；边边相交先 bbox 拒绝，再求 t/u，平行或共线分母为 0 一律返回不相交，端点区间闭合；距离用最近点 | `ClosestPointRustF32/IntersectsRustF32` 与 Rust 对应算法；double tolerant `Intersects` 是另一合同。共线重叠与零长直接构造是重点差分边界 |

### `jagua-rs/src/geometry/primitives/circle.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| 构造、包围圆、度量 (21–80) | `try_new` 要求 radius 非负有限且中心有限；`bounding_circle` 从第一圆起逐一扩张，空输入 `expect` panic；面积、bbox、直径按 f32 直接算 | `Circle2d::Create/BoundingCircleRustF32/AreaRustF32/BoundingBoxRustF32`（`Circle.cpp`）对应；C++ 包围圆函数对空输入返回 Result 错误，错误合同不同 |
| 变换、相交、距离/分离 (82–233) | 变换只移动中心；相交比较平方距离，边界视作相交；Circle–Point 的 separation 在等半径边界归 Interior，Circle–Edge 边界因严格 `<` 归 Exterior；非有限比较在 point separation 的 `partial_cmp().unwrap()` 可 panic | `Circle2d` 的 `IntersectsRustF32`、`DistanceToRustF32`、`SeparationDistanceRustF32` 重载对应；存储 double 但 RustF32 分支量化输入；边界内外分类与 NaN 路径需要按各重载对拍 |

### `jagua-rs/src/geometry/primitives/rect.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| 构造、关系与缩放 (21–138) | `try_new` 只要求 min<max，不显式检查有限性；relation 先判相交，再判包含；几乎关系用 FPA，边界更偏向包含；scale 调用 resize，生成非法矩形时 `expect` panic，resize 本身用 Option 返回 | `Rect2d::Create/RelationToRustF32/AlmostRelationToRustF32/ScaledRustF32/ResizeByRustF32`（`Rect.h`）对应；Rust Result/Option/panic 与 C++ Result/optional 分别记录 |
| 象限、边角与度量 (140–280) | quadrant 顺序为 `[右上、左上、左下、右下]`；子矩形构造以 unwrap 结束；`sides` 与 `edges` 当前生成同一组边；intersection 要求正面积，单纯边/点接触返回 None，bounding rect 为包围两者 | `Rect2d::QuadrantsRustF32/CornersRustF32/EdgesRustF32/IntersectionRustF32` 对应 Rust 顺序；普通 `Quadrants()` 是另一顺序，POI 必须走 RustF32 版本 |
| 相交、距离与分离 (282–413) | 矩形相交和点包含都用闭边界；因此仅边界接触时 `collides_with=true`，但 `intersection=None`。edge 相交先 bbox、端点，再看角相对线段同侧；点在边界算 Interior，最近边距为 0 | `IntersectsRustF32/ContainsRustF32/DistanceToRustF32/SeparationDistanceRustF32` 在 `Rect.h`；普通 double 与 F32 API 是不同数值合同 |

### `jagua-rs/src/geometry/primitives/simple_polygon.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `SPolygon/new`、surrogate、顶点访问 (24–113) | 公共字段保存 vertices/bbox/area/diameter/POI/可选 surrogate；new 检查至少 3 点、精确重复、自相交、非零鞋带面积，并把顺时针反转为逆时针；不单独检查 finite。POI 失败返回 Result；`vertex` 越界 panic，`edge` 显式 assert；读取未生成 surrogate 会 `expect` panic | `SimplePolygon::Create/GenerateSurrogate/GetSurrogate`（`SimplePolygon.cpp`、`SimplePolygon.h`）校验并返回 Result；C++ 多边形内部字段私有，缺失 surrogate 以空指针表示。检查顺序和缺失代理合同是差分项 |
| 派生量与自交检测 (115–217) | diameter 在 convex hull 点对中找最大距离；bbox 逐点 min/max 后 unwrap；area 用 f32 shoelace；POI 以 dummy polygon 调 `compute_pole`；centroid 每次按公式重算；自交检查所有非相邻边对 | C++ `SimplePolygon::Create` 也生成 bbox/area/diameter/POI，并缓存 centroid；它有额外 finite/数值归一化校验，需区分缓存与 Rust 按需计算 |
| 变换、包含、距离与分离 (219–368) | rigid transform 更新 vertices、POI、surrogate、bbox，但不重算 area/diameter；`transform_from` 只在目标有 surrogate 时调用 `reference.surrogate()`，reference 缺失代理可 panic，长度仅 Debug 检查。包含使用 bbox 预筛与射线法/FPA 顶点退化处理；距离对空边集合 unwrap；Rect 转 polygon 会 unwrap 构造结果 | `TransformedRustF32/ContainsRustF32/IntersectsRustF32/DistanceToRustF32` 对应；C++ transform 以 Result 返回并保存 centroid；Rust `transform_from` 的代理配对与缺代理路径需单独测 |

## 修改算法与 fail-fast 代理

### `jagua-rs/src/geometry/shape_modification.rs`

| Rust 函数/范围 | 逻辑、分支与失败出口 | JaguarCore 对应与静态差异 |
|---|---|---|
| `ShapeModifyMode/ShapeModifyConfig` (19–47) | Inflate/Deflate 决定输出应为原形的超集/子集；config 含简化面积变化比例、offset、窄凹陷距离/面积比例 | `ShapeModifyMode/ShapeModifyConfig`（`Geometry/ShapeModification.h`）字段对应，C++ numeric fields 为 double |
| `simplify_shape` 与 candidate helpers (49–338) | 按角点类型产生 collinear/concave/convex-convex 候选，按面积差排序，跳过会自交候选；单次面积变化超阈或无候选即停止；最终 `SPolygon::new(...).unwrap()`，候选中的假设失败可触发 expect/assert | `SimplifyShape`、`ShapeModificationValid`（`Simplify.cpp:280–349`）同类候选算法；C++ 最终建 polygon 以 Result 返回。Rust 直接返回 SPolygon，内部不变量失败会 panic |
| `offset_shape` (340–383) | Inflate/Deflate 转正负距离；用 `geo::Buffer`、Round join 0.1 rad；空结果返回 error，多结果只取第一个并 warn；f64 坐标转回 f32 时 unwrap，再走通用 importer | `OffsetShape/OffsetShapes`（`Offset.cpp:713–830`）使用 RustF32 归一化、straight-skeleton/特定 convex/fallback 路径，并由兼容单结果 API 取第一个轮廓。算法和退化回退路线与 pinned Rust buffer 不同，是几何对拍重点 |
| `close_narrow_concavities` (385–523) | O(n²) 枚举非相邻端点；候选边缩为 0.9999，受距离/面积比、内外模式和边相交限制；优先消除最多顶点，重建失败以 `expect` panic；无候选停止 | `CloseNarrowConcavities`（`NarrowConcavity.cpp:76–166`）实现相同候选目标并用 RustF32 helper；C++ 重建失败时 `break` 并返回当前形状，Rust 对应路径 panic |
| `shape_modification_valid` (525–554) | 对原顶点逐项检查：Inflate 须落在新形状内或近边，Deflate 须在新形状外或近边；边容差为 `diameter * 1e-6`；失败打日志并返回 false | `ShapeModificationValid`（`Simplify.cpp:280–309`）对应；不同之处是 `OriginalShape::convert_to_internal` 只 Debug 断言，而 C++ ConvertToInternal 总会校验并将 false 转 `InvalidPolygon` |

### `jagua-rs/src/geometry/fail_fast/piers.rs`

| Rust 函数/行 | 逻辑、profile 与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `generate_piers` (16–94) | n=0 立即空结果；否则从 bbox/质心造基准竖线，生成多角度平移射线并裁剪，创建未被 poles 代表的点网格，迭代挑选 n 条 loss 最低的 piers；无 clipped ray 返回 error；NotNan、Edge 构造和 ray 端点前置条件可能 panic/assert | `generatePiers`（`FailFast/Surrogate.cpp:440–489`）返回 Result，覆盖相同候选/损失筛选；Rust ray/grid 参数按 Debug 与 Release 分别是 10/200、4/90、10/100，C++ 对应分支受 `NDEBUG` 控制，结果不能跨 profile 混比 |
| ray、clip、grid、loss helpers (96–228) | 按 bbox 等分 x 位置，角度取 `[0,π)`；clip 对边界交点按起点距离排序、成对裁剪并 trim 0.999；loss 是代表点至现有 poles/piers 的平方距离和 | C++ `generateCandidatePiers/clipRayRustF32/loss` 系 helper 在同一 `Surrogate.cpp`；端点/共线交点、空网格与并列 loss 应进入 B3 fixture |

### `jagua-rs/src/geometry/fail_fast/pole.rs`

| Rust 函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `generate_surrogate_poles` (10–49) | 先包含 shape.poi，再循环计算下一个 pole；累计圆面积/shape.area，选择当前 coverage 下满足严格 `coverage > threshold` 的最小 pole 数限制；超过 1000 时 assert panic | `generatePoles`（`Surrogate.cpp:49–83`）同样逐个生成和比较阈值；达到 1000 时返回 Result error，而非 panic |
| `compute_pole`、`POINode` (51–138) | bbox 扩成正方形，FIFO 四叉树搜索深度 10；node 质心须在 shape 内且排除所有已有 pole，distance 到边/pole 最小值取正负；以 radius+distance upper bound 裁枝；无有效 best 返回 anyhow error，候选 Circle 创建处仍 unwrap | `ComputePointOfInaccessibility`（`PointOfInaccessibility.cpp:91–148`）实现相同深度与 FIFO 象限顺序，失败用 Result 返回；C++ 注释明确保持 Rust quadrant 次序 |

### `jagua-rs/src/geometry/fail_fast/sp_surrogate.rs`

| Rust 类型/函数/行 | 逻辑、分支与失败出口 | JaguarCore 对应与差异 |
|---|---|---|
| `SPSurrogate/new` (13–84) | ratio 必须在 [0,1]，否则 ensure error；依序生成凸包索引/面积、所有 poles、达到 fail-fast 覆盖所需的最短 pole 前缀，再用该前缀生成 piers；任一步 Result 失败即返回；`ff_pole_area_ratio=0` 时前缀为 0，piers 仍按 n 配置生成 | `Surrogate::Create`（`Surrogate.cpp:575–641`）验证 ratio、生成 poles、累计 f32 圆面积并求 fail-fast 数，再建 piers/hull；同层错误返回 Result，C++ 配置会先转成 RustF32 |
| 变换与 `SPSurrogateConfig` (86–170) | 普通 transform 只移动 poles/piers，凸包索引/面积/config/前缀长度不变；transform_from 在 Debug 断言长度相同后 zip，长度不一致在 Release 静默截断；config 固定 3 档 pole limits，`none()` 设全零且 piers=0 | C++ `Surrogate::TransformedRustF32` 返回 Result，保存 `m_FailFastPoleCount`、凸包缓存和 config；目前没有与 Rust 变换 trait 同构的可变接口，缓存保持规则要按消费者核实 |

## B1 第三轮：逐公开函数覆盖矩阵

### 盘点口径

- 范围限定为 B1 列出的 9 个 pinned Rust 实现文件：`circle.rs`、`edge.rs`、`point.rs`、`rect.rs`、`simple_polygon.rs`、`transformation.rs`、`d_transformation.rs`、`convex_hull.rs`、`original_shape.rs`。
- 对照 revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb` 的源码，共盘点到 **78 个显式 `pub fn` / `pub const fn`**。该数不含公开字段/常量，也不含 trait impl 中没有 `pub` 修饰的实现方法；后者另按 33 个实现块登记。
- “直接对拍”表示 Rust oracle 的结果进入当前 fixture，且 `CompareGeometry.cpp` 对对应 C++ 路径作断言；“间接”表示值由构造或其他公开函数产出，未独立调用该函数；“未对拍”表示当前没有被 C++ 比较消费的对应输出；“B3”表示按工作项归入 surrogate 生成/索引映射。

### 78 个 inherent/free `pub fn` 覆盖矩阵

| Rust 文件 | Rust 公开函数 | C++ 对应路径与当前证据 | 覆盖状态及边界 |
|---|---|---|---|
| `primitives/circle.rs`（5） | `try_new` | `Circle2d::Create`；`geometry.circle_construction`、`geometry.circle_basics` | 直接对拍：零半径、负半径及非有限构造分支有记录；非有限中心/半径拒绝行为已比较 |
| | `bounding_circle` | `Circle2d::BoundingCircleRustF32`；`circle.bounding_circle`、`geometry.circle_bounding_circle_*` | 两相离圆既有值继续对拍；新增空集、单圆、前圆包含后圆、后圆包含前圆、同心嵌套。空集 Rust panic/C++ Result 错误；单圆和前包含后相等；后圆包含前圆时两侧都返回圆心 `(5,0)`、半径 `6`，未包住半径 10 的输入圆；同心嵌套输入产生 NaN 圆心/半径，输入有限但输出非有限 |
| | `area`、`bbox`、`diameter` | `AreaRustF32`、`BoundingBoxRustF32`、`DiameterRustF32`；`geometry.circle_basics` | 直接对拍；`diameter` 另出现在 `circle.distance` |
| `primitives/edge.rs`（14） | `try_new` | `Edge2d::Create`；`geometry.edge_construction`、`geometry.edge_nonfinite_bbox_contract` | 直接对拍有限有效、零长、f32 收敛端点和所测 NaN/Inf 端点；两侧均接受非有限端点 |
| | `extend_at_front`、`extend_at_back`、`scale`、`reverse` | `ExtendedAtFrontRustF32`、`ExtendedAtBackRustF32`、`ScaledRustF32`、`ReversedRustF32`；`edge.operations`、`geometry.edge_zero_length_*` | 普通及原始零长输入已对拍：零长 extend 的被延长端产生 NaN；scale/reverse 保持同一点；与穿过该点的线段既不相交也不返回交点。负/非有限缩放仍未覆盖 |
| | `collides_at` | `IntersectionPointRustF32`；`edge.intersection`、`geometry.edge_collides_at`、`geometry.edge_collides_at_points` | 直接对拍：内部相交、端点、T 形、共线、平行和线段外交点分支 |
| | `closest_point_on_edge` | `ClosestPointRustF32`；`geometry.edge_closest`、`geometry.edge_zero_length_operations` | 投影样例与手工构造的零长 Edge 最近点均直接对拍；Rust 构造器仍拒绝零长端点 |
| | `x_min`、`y_min`、`x_max`、`y_max`、`length`、`centroid` | 对应 RustF32 查询；`edge.operations`、`geometry.edge_zero_length_operations` | 有限普通样例及原始零长 Edge 的 bbox/长度/质心直接对拍 |
| | `bbox` | `Edge2d::BoundingBox()`；`geometry.independent_accessors`、`geometry.edge_nan_bbox_values` | 有限边与 NaN 起点 bbox 数值已对拍；NaN 结果的 raw-bit 表示尚未单独固定 |
| `primitives/point.rs`（2） | `x`、`y` | C++ `Point2d` 公共坐标字段；`geometry.independent_accessors` | 独立读取并对拍普通有限坐标；非有限/signed-zero accessor 值未单列 |
| `primitives/rect.rs`（18） | `try_new` | `Rect2d::Create`；`geometry.rect_construction` | 直接对拍：有效、零宽/高、NaN 与正无穷上界 |
| | `from_diagonal_corners`、`inflate_to_square`、`scale`、`resize_by` | 对应 `Rect2d` helper；`rect.operations`、`rect.distance`、`geometry.rect_boundary_resizes`、`geometry.rect_zero_scale_rejected`、`geometry.rect_degenerate_*` | 直接对拍有效分支、收缩至零、负比例、零宽/零高缩放及正向 resize 恢复。无效缩放 Rust panic、C++ Result 错误；有效退化组合两侧数值一致 |
| | `relation_to` | `RelationToRustF32`；`geometry.rect_relations`、`geometry.rect_relation_ulp_boundary` | 直接对拍 Surrounding、Enclosed、Intersecting、Disjoint 与 ULP 邻界 |
| | `almost_relation_to` | `AlmostRelationToRustF32`；`rect.almost_relation_cases`、`geometry.rect_relation_ulp_boundary` | 直接对拍关系枚举和 ULP 邻界；`AlmostCollidesWith` bool trait 另由 `geometry.trait_dispatch` 单独对拍 |
| | `quadrants`、`centroid`、`width`、`height` | `QuadrantsRustF32`、`CentroidRustF32`、`Width/HeightRustF32`；`geometry.rect_basics`、`rect.operations`、`geometry.rect_quadrant_panic_contract`、`geometry.rect_degenerate_metrics` | 普通象限顺序、零宽/零高度量，以及 Rust 在 1-ULP/零宽矩形上的 panic 与 C++ 退化象限均已刻画；错误出口仍不等价，极端尺寸未覆盖 |
| | `corners`、`intersection`、`bounding_rect` | `CornersRustF32`、`IntersectionRustF32`、`BoundingRectRustF32`；`rect.combination`、`geometry.rect_boundary_intersections`、`geometry.rect_degenerate_boundary_shapes`、`geometry.rect_degenerate_intersections` | 退化 corners 与 intersection 已对拍；普通 `bounding_rect` 对拍仍未扩展到退化输入 |
| | `edges` | `EdgesRustF32`；`rect.edges`、`geometry.rect_degenerate_boundary_shapes` | 普通与零宽/零高矩形的四条边坐标和顺序均直接对拍 |
| | `sides` | `SidesRustF32()`；`geometry.independent_accessors` | 普通矩形四边顺序已独立对拍；零宽/零高输入尚未直接调用 `sides()` |
| | `area`、`diameter` | `AreaRustF32`、`DiameterRustF32`；`rect.operations`、`geometry.rect_degenerate_metrics` | 正常、零宽和零高矩形度量直接对拍 |
| `primitives/simple_polygon.rs`（12） | `new` | `SimplePolygon::Create`；`geometry.simple_polygon_construction`、`geometry.simple_polygon_basics`、`geometry.simple_polygon_clockwise_vertices` | 直接对拍有限有效、点数不足、重复、共线、自交、小面积及方向归一 |
| | `vertex`、`edge`、`edge_iter`、`n_vertices` | `Vertices`、`Edges`、`VertexCount`、`Centroid`；`geometry.simple_polygon_basics`、`geometry.simple_polygon_clockwise_vertices` | `edge_iter`/`n_vertices` 有直接输出；`edge` 经迭代器、`vertex` 经质心计算间接执行；索引越界/panic 未覆盖 |
| | `calculate_diameter`、`generate_bounding_box`、`calculate_area`、`calculate_poi` | Rust 直接调用并输出 `geometry.simple_polygon_helpers`、`geometry.simple_polygon_helper_failures`、`geometry.simple_polygon_helper_rejections`、`geometry.simple_polygon_degenerate_*`；C++ 对照 `SimplePolygon::Create` 的正常缓存值和退化构造拒绝 | 新增重复点与共线 helper 的 area/diameter/bbox 数值及 panic/Err 状态；C++ 无同构静态 helper API，退化 helper 数值只有 Rust reference，不能声称已完成 C++ 值对拍 |
| | `centroid` | `Centroid`；`geometry.simple_polygon_basics`、`geometry.original_shape` | 直接对拍普通多边形；数值退化边界另有 oracle 输出但未全部进入当前 comparator |
| | `generate_surrogate`、`surrogate` | `Surrogate::Create`、surrogate accessor | 归 B3；B1 不把 surrogate 生成/索引行为计作已覆盖 |
| `transformation.rs`（13） | `empty`、`from_translation`、`from_rotation`、`rotate`、`translate`、`rotate_translate`、`translate_rotate`、`transform`、`transform_from_decomposed`、`inverse` | `Transform2d::{Identity,FromRotationTranslation,FollowedByRustF32,InverseRustF32,ApplyRustF32}`；`geometry.transformation_apply`、`geometry.transformation_order`、`geometry.transformation_matrix_probes`、`geometry.transformation_decompositions`、`geometry.transform_followed_by`、`geometry.transform_inverse` | 直接对拍有限刚体变换路径和基向量作用；非刚体矩阵不是 C++ 类型可表达域 |
| | `is_empty`、`decompose` | `Transform2d` 零字段判定及分解字段；`geometry.transformation_is_empty`、`geometry.transformation_decompositions` | 直接对拍字段/行为映射 |
| | `matrix` | Rust 3×3 matrix accessor；C++ 无同形 accessor | 仅以基向量作用结果间接核对；不构成原始矩阵字段逐项相等证明 |
| `d_transformation.rs`（6） | `new`、`empty`、`rotation`、`translation`、`compose` | `Transform2d` 字段与组合 helper；`geometry.dtransformation`、`geometry.transformation_decompositions` | 直接对拍有限值；`NotNan` 输入 NaN 的 panic 行为和 infinity 域未独立覆盖 |
| | `normalize_rotation` | `NormalizeRotationRustF32`；`geometry.normalize_rotation`、`geometry.normalize_rotation_nonfinite`、`geometry.normalize_rotation_negative_zero` | 直接对拍有限角、NaN、正负无穷和负零 |
| `convex_hull.rs`（3） | `convex_hull_from_points` | `ConvexHull`；`geometry.convex_hull_tied_x`、`geometry.convex_hull_singleton_count`、`geometry.convex_hull_finite_degenerates_*` | 直接对拍 empty、singleton、重复点、重复顶点云和水平/斜向共线输入的顶点数与顺序；六组结果相等 |
| | `convex_hull_indices`、`convex_hull_from_surrogate` | `Surrogate` 凸包索引/点映射 | 归 B3；当前 B1 fixture 未覆盖 surrogate 依赖路径 |
| `original_shape.rs`（5） | `convert_to_internal`、`centroid`、`area`、`bbox`、`diameter` | `geometry.original_shape`；`convert_to_internal` 与 C++ `OriginalShape::ConvertToInternal` 比较，Rust getter 与 C++ 保存的原始 `SimplePolygon` getter 比较 | `convert_to_internal` 直接对拍；4 个 getter 的值有对拍，但 C++ 侧读取的是底层原始 shape getter，并非同构的 `OriginalShape` getter API。仅一个 pre-transform、默认修改配置样例，offset/simplify 等修改模式归 B2 |

### 33 个公开 geometry trait 实现块

trait 实现方法在 Rust impl 中没有 `pub` 修饰，不计入上面的 78 个声明；但它们是可达泛型行为，单独纳入本矩阵。

| Trait 实现 | 当前证据 | 覆盖判断 |
|---|---|---|
| `Transformable` for `Point`、`Circle`、`Edge`、`SPolygon`（4） | `geometry.transformation_apply`、`circle.transform`、`edge.transform`、`geometry.simple_polygon_transform` | 数值变换结果均有对拍；oracle 对 Point 与 SPolygon 调用 `transform_clone` 并比较数值，Circle/Edge 覆盖原地 `transform`。clone 与原地对象状态、独立 clone 语义未逐类型单独验证 |
| `TransformableFrom` for `Point`、`Circle`、`Edge`、`SPolygon`（4） | `geometry.transform_from`；Rust 对四种类型直接调用 | 四种类型均有有限平移对拍；SPolygon 另有含 surrogate 的结果对拍，C++ 以 `TransformedRustF32` 作结果级映射。Rust Debug/Release surrogate 生成值不同，按 profile 分开记录。缺少 reference surrogate 时的 Rust panic 已单独刻画；生成与索引算法归 B3 |
| `CollidesWith` for `Circle` × `Circle/Edge/Rect/Point`（4） | `circle.collision`、`geometry.circle_contains` | 直接对拍 |
| `CollidesWith` for `Edge` × `Edge/Rect`（2） | `edge.intersection`、`geometry.edge_intersects`、`geometry.rect_edge_intersects`、`geometry.trait_dispatch` | Edge×Edge 有对拍；Edge×Rect 与 Rect×Edge 的 Rust 入口均直接调用并比较布尔结果；C++ 以 `Edge2d::IntersectsRustF32(Rect2d)` 对照两种分发方向 |
| `CollidesWith` for `Rect` × `Rect/Point/Edge`（3） | `geometry.trait_dispatch`；既有 relation/separation keys | 三个 Rust bool trait 入口均已直接调用；C++ 分别以 `RelationToRustF32 != Disjoint`、`ContainsRustF32`、`Edge2d::IntersectsRustF32(Rect2d)` 对照 |
| `CollidesWith` for `SPolygon` × `Point`（1） | `geometry.simple_polygon_contains` | 直接对拍 inside/boundary/outside |
| `CollidesWith` blanket impl for `Point`（1） | `geometry.trait_dispatch`：Point-left Circle/Rect/SPolygon 的 inside/outside | 三类可达 blanket dispatch 均已从 Point 左侧直接调用；C++ 以对应 Circle/Rect/SPolygon 查询对照。仅代表性内部/外部点 |
| `AlmostCollidesWith` for `Rect` × `Rect/Point`（2） | `geometry.trait_dispatch`；原有 relation 边界键 | 两个 Rust bool trait 入口均直接调用；C++ 以 `AlmostRelationToRustF32 != Disjoint` 与 `AlmostContainsRustF32` 对照，覆盖近边界和更远外部点/矩形 |
| `DistanceTo`（7）：`Circle` × `Point/Circle/Edge`、`Edge` × `Point`、`Point` × `Point`、`Rect` × `Point`、`SPolygon` × `Point` | `circle.distance`、`geometry.edge_closest`、`point.distance`、`rect.distance`、`polygon.distance` | 线性与平方距离均有对应输出；样本边界仍有限 |
| `SeparationDistance`（5）：`Circle` × `Point/Circle/Edge`、`Rect` × `Point`、`SPolygon` × `Point` | `circle.separation`、`circle.squared_separation`、`rect.distance`、`polygon.distance` | 位置与距离（含平方）均有样例；非有限与完整边界矩阵未覆盖 |

### 第三轮结论与当轮发现

- 逐文件静态矩阵已覆盖 **78/78 个显式 `pub fn` / `pub const fn` 声明**，并把 **33 个 trait 实现块**独立登记；“列入矩阵”不等于“已直接对拍”。
- 第三轮识别的 `Edge::bbox`、`Point::x/y`、`Rect::sides` 和四个 `SPolygon` helper 缺口已在第四轮补入独立 Rust 调用；第五轮再补 helper 的 panic/`Err` 出口。helper 的 C++ 对照仍以 `SimplePolygon::Create` 缓存字段完成，退化数值没有同构 API 对照。
- 第三轮识别的 `TransformableFrom`、Point-left blanket dispatch、矩形 bool collision/near-collision 及 Edge×Rect 分发入口已在第四轮补入；第五轮补入含 surrogate 路径和缺 reference surrogate 的 Rust panic 刻画。
- `SPolygon::generate_surrogate/surrogate`、`convex_hull_indices/from_surrogate` 继续归 B3；`OriginalShape::convert_to_internal` 的 shape modification 分支归 B2。已复核 Rust `Edge::try_new` 与 C++ `Edge2d::Create` 都接受本轮探测的 NaN/Inf 端点；旧记录所称“C++ 拒绝”不成立。
- 本轮只做 pinned 源码、Rust oracle、fixture 与 C++ comparator 的静态映射，没有新增或运行测试，也没有更新任何运行通过结论。B1 继续标为“部分”。

## B1 第四轮：独立 accessor 与 trait 分发对拍

- 新增四个固定键：`geometry.independent_accessors`（Point x/y、Edge bbox、Rect sides）、`geometry.transform_from`（Point/Circle/Edge/SPolygon）、`geometry.trait_dispatch`（Point-left、矩形严格/近似碰撞及 Edge/Rect 双向调用）、`geometry.simple_polygon_helpers`（四个静态派生 helper）。fixture 从 48 键增至 52 键。
- `TransformableFrom` 的 C++ 对照是结果级映射：Point 用 `ApplyRustF32`，Circle/Edge 用 `TransformRustF32`，SPolygon 用 `TransformedRustF32`。SPolygon 样例使用无 surrogate、相同 area/diameter 的平移副本，不证明 surrogate 配对或不匹配缓存语义。
- 四个 `SPolygon` helper 在 Rust oracle 中分别直接调用；C++ 端没有同构 helper API，以 `SimplePolygon::Create` 的缓存值对照。因此该证据确认派生数值对齐，不等同于 C++ 独立 helper 调用。
- Rust 1.98.1 Debug/Release oracle 的 52 个 fixture 键逐值匹配；C++ Debug 与 Release 全套 `JaguarCoreTests.exe` 均退出码 0。本轮先加入 comparator 断言并观察到 fixture 缺键失败，再补 oracle 输出与 fixture 后通过。
- 第五轮处理了上述 helper 空/退化输入和 `calculate_poi` 错误出口、`Edge::bbox` 非有限输出、SPolygon surrogate 变换结果，以及矩形交集/收缩/零缩放/象限边界；仍需按剩余函数清单扩展边界矩阵，不能据此认定 B1 完成。

## B1 第五轮：有限输入域退化边界与错误出口

- fixture 从 52 个键增至 65 个键。新增 `geometry.edge_zero_length_operations`、`geometry.edge_underflow_length_contract`、`geometry.edge_nonfinite_bbox_contract`、`geometry.edge_nan_bbox_values`，覆盖原始零长 Edge、有限次正规长度平方下溢及 NaN/Inf bbox；本轮确认 Rust 与 C++ 的 Edge 构造均接受所测非有限端点，NaN bbox 为 `1,0,1,1`。
- 新增 `geometry.rect_boundary_intersections`、`geometry.rect_boundary_resizes`、`geometry.rect_zero_scale_rejected`、`geometry.rect_quadrant_panic_contract`。重叠交集成功，边/角接触与分离返回空；收缩为零宽/高返回空，零缩放在 Rust panic、C++ 返回错误。Rust 对 1-ULP 窄矩形和零宽矩形的 `quadrants()` panic，C++ 则返回退化象限，属于已确认的错误出口差异。
- 新增 `geometry.simple_polygon_helper_failures` 和 `geometry.simple_polygon_helper_rejections`：构造器对空、单点、两点、共线输入均拒绝；直接 Rust helper 对空/单点的 diameter、bbox、POI 会 panic，两点与共线 POI 返回 `Err` 而不 panic。C++ 无同构静态 helper API，故这组仅刻画 Rust 出口；多边形可用性由 `SimplePolygon::Create` 的拒绝结果对照。
- 新增 profile-specific `geometry.transform_from_surrogate_debug` / `_release` 和 `geometry.transform_from_surrogate_state`。Rust surrogate 生成结果的 pier 在 Debug/Release 间不同，C++ 两个 profile 各自与对应 Rust fixture 对齐；目标无 surrogate 时 Rust 保持无 surrogate，reference 缺 surrogate 时 Rust panic。该行为通过 `TransformedRustF32` 对照结果级变换，生成算法仍归 B3。
- Rust 1.98.1 Debug/Release oracle 均退出码 0；本轮新增输出除上述 profile-specific surrogate 键外逐项一致。C++ `runGeometryDifferentialTests` 的定向 Debug/Release 入口均退出码 0，`cargo fmt --check` 通过。完整 `JaguarCoreTests.exe` 本轮未完成：入口启动数分钟仍无输出后被停止，因此不记录全套测试结果。
- 本轮先以缺 fixture 键确认比较器红测；排查 Release 首次失败后确认是把 Rust Debug pier 值用于 Release 对拍，改为 profile-specific fixture 后两侧通过。B1 仍为“部分”，剩余边界矩阵和已确认的 panic/退化象限差异仍待决策或逐项验收。

## B1 第六轮：收口剩余有限退化边界

- fixture 从 65 个键增至 80 个键。新增 15 个键，覆盖原始零长 Edge 的 extend/scale/reverse/intersection，零宽/零高 Rect 的度量、corners、edges、交集、正向 resize 恢复和负比例/零尺寸 scale，凸包 empty/singleton/重复/共线，以及 SPolygon helper 的重复点/共线数值与失败出口，Circle 包围圆的 empty/single/包含输入。
- Edge：Rust 与 C++ 对零长边前/后延伸均产生 NaN；缩放和反向保持同一点；零长边即使落在线段上，也不被 Rust `collides_with` / `collides_at` 视为相交。Debug/Release comparator 均匹配。
- Rect：零宽、零高的宽高/面积/直径/质心、四角、四边和交集结果逐值一致；`resize_by` 对称扩张可以恢复为正面积矩形。无效缩放时 Rust `scale` panic，C++ `ScaledRustF32` 返回错误；前轮已确认的 `quadrants()` 零退化 panic 与 C++ 返回退化象限仍不相同。
- 凸包：六组输入 `[]`、单点、重复单点、重复顶点点集、斜向共线、水平共线的输出点数和坐标顺序在 C++ `ConvexHull` 中一致。SPolygon helper 新增直接 Rust 数值输出：两重合点直径为 0、面积为 0；水平共线 bbox panic；重复顶点点集的面积为 2、直径为 `sqrt(8)`、bbox 为 `[0,0,2,2]`。C++ 没有同构静态 helper API，因此这些 helper 数值作为 Rust reference 留档，C++ 侧只对照构造拒绝和可用的 `ConvexHull` API。
- Circle：空输入 Rust panic/C++ Result 错误；单圆和首圆包住后续圆均保持首圆。后续大圆包住首圆时，两侧同得圆心 `(5,0)`、半径 `6`，但结果没有包住半径 10 的输入圆；同心嵌套则两侧都返回 NaN 圆心/半径。它们是相同的既有算法行为，不代表包围圆算法满足“最小包围圆”的几何定义。
- TDD 红测先由缺失的新 fixture key 触发；补齐 reference 后，锁定 Rust 1.98.1 Debug/Release oracle 的 15 个新增值逐字匹配 fixture，C++ 定向 geometry differential Debug/Release 均退出码 0，`cargo fmt --check` 通过。fixture 共 80 个唯一 key；本轮没有运行全套 `JaguarCoreTests.exe`。
- 本轮收口的是上述有限退化输入矩阵，不等于 B1 完成。仍需处理/确认 Rect `quadrants()` 与缩放的 panic→Result 合同、SPolygon helper 的 C++ API 映射；`Rect::sides()` 与退化 `bounding_rect` 也尚未用这轮输入直接对拍。B1 继续为“部分”。

## 结论边界与后续验收

- 15 个 geometry 实现文件均已登记；`geometry/mod.rs`、`geometry/primitives/mod.rs` 与 `geometry/fail_fast/mod.rs` 的导出边界见 `00-module-exports.md`。
- B1 首轮运行对拍（2026-10-16）：`JaguarCore/Tests/Fixtures/RustReference/geometry_primitives.txt` 使用 pinned `jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`、Rust 1.98.1 生成；原有 circle/edge/rect/point/polygon 输出来自默认 oracle，新扩展用例来自 `--geometry-primitives-parity-only`。Debug 与 Release oracle 的扩展输出一致。
- 首轮 fixture 与 `CompareGeometry.cpp::runB1PrimitiveReferenceTests` 覆盖圆构造/度量/包含/相交/距离/分离/包围圆/变换；边构造/最近点/距离/延伸/缩放/反向/交点/相交/变换；点距离；矩形构造/关系/几乎关系/象限/角点/边/组合/距离/分离；多边形有限输入构造、方向归一、派生指标、POI、包含、距离/分离与变换；凸包顺序/单点；`OriginalShape` 原始指标和默认配置下的预变换转换。有限退化用例覆盖零半径、零长边、f32 收敛为重复端点、矩形零宽/零高、多边形点数不足/重复点/共线/自交/小面积。
- 首轮 C++ 数值结果通过 `AlmostEqualRustF32` 按 D3 规则比较；布尔值与 `GeoPosition`、顶点顺序精确比较。Rust oracle 的 Debug/Release 命令和 JaguarCore 两个 profile 的完整测试均以退出码 0 结束。
- `DTransformation` 不需要新增同名结构：它保存的旋转和平移可由 `Transform2d::rotationRad/translation` 表达，行为落在 `ApplyRustF32`、`FollowedByRustF32`、`InverseRustF32`；Rust 公开的 `normalize_rotation` 则新增直接对应 `NormalizeRotationRustF32(float)`，按 f32 remainder 及负角修正规则实现，并覆盖 NaN、正负无穷和负零。
- B1 第二轮运行对拍（2026-10-16）：新增 `geometry.transformation_matrix_probes`，按 `empty/from_translation/from_rotation/rotate/translate/rotate_translate/translate_rotate/transform/transform_from_decomposed/inverse` 路径，对原点与两个单位基向量逐项比较；同时比较每条路径的分解字段与 `is_empty` 边界。C++ 没有原始 3×3 matrix accessor 或同名 `is_empty` 方法；矩阵作用映射到 `Transform2d::ApplyRustF32`，空变换判定映射到精确零旋转/零平移字段。Rust matrix 与 C++ 表示差异保留在 API 映射记录。
- 第二轮还补了 `Edge::collides_at` 的 7 组用例：线段内部相交、端点接触、T 形接触、共线重叠、共线端点接触、平行分离及交点落在线段范围外；对应交点与存在性均和 `IntersectionPointRustF32` 比较。矩形 relation/`almost_relation_to` 的阈值矩阵覆盖距 `1.0f` 下方 1、2、3、4、8、16 ULP；严格 relation 均为 `Intersecting`，almost relation 在 1–4 ULP 归为 `Surrounding`，8/16 ULP 归为 `Intersecting`。
- 当前 B1 仍为进行中：78 个显式公开函数和 33 个 trait 实现块已有静态覆盖矩阵；第四至第六轮扩展了对拍，但剩余边界见本记录“B1 第六轮”。Surrogate 生成/索引映射归 B3，offset 与简化归 B2。Rust 与 C++ 的 Edge 构造都接受第五轮探测的非有限端点，不再记录为构造输入域差异；Rust 与 C++ 的 Rect 象限退化行为及无效缩放失败形态仍有已确认差异，SPolygon 的 C++ helper API 也未补齐。
- 第三轮之前的既有验证：Rust 1.98.1 Debug/Release geometry-only oracle 新增 6 个键逐值一致；当时 fixture 共 48 个键，默认 oracle 与 geometry-only oracle 的对应输出逐值一致；JaguarCore Debug/Release 全套 `JaguarCoreTests.exe` 均以退出码 0 结束。第四轮更新后的 52 键和双 profile 验证见本记录“B1 第四轮”。

## 证据位置

- pinned Rust：`geometry/{convex_hull,d_transformation,geo_enums,geo_traits,original_shape,shape_modification,transformation}.rs`、`geometry/fail_fast/{piers,pole,sp_surrogate}.rs`、`geometry/primitives/{circle,edge,point,rect,simple_polygon}.rs`。
- C++：`JaguarCore/Include/JaguarCore/Geometry/*`、`JaguarCore/Source/Geometry/{ConvexHull,OriginalShape,Primitives,ShapeModification,Transformation}/*`、`JaguarCore/Source/FailFast/Surrogate.cpp`。
- 运行对拍：`JaguarCore/Tests/Differential/CompareGeometry.cpp`、`JaguarCore/Tests/Fixtures/RustReference/geometry_primitives.txt`、`JaguarCore/tools/RustOracle/src/main.rs`。
