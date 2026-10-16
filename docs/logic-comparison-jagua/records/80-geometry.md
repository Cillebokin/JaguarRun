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

## 结论边界与后续验收

- 15 个 geometry 实现文件均已登记；`geometry/mod.rs`、`geometry/primitives/mod.rs` 与 `geometry/fail_fast/mod.rs` 的导出边界见 `00-module-exports.md`。
- B1 首轮运行对拍（2026-10-16）：`JaguarCore/Tests/Fixtures/RustReference/geometry_primitives.txt` 使用 pinned `jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`、Rust 1.98.1 生成；原有 circle/edge/rect/point/polygon 输出来自默认 oracle，新扩展用例来自 `--geometry-primitives-parity-only`。Debug 与 Release oracle 的扩展输出一致。
- 本轮 fixture 与 `CompareGeometry.cpp::runB1PrimitiveReferenceTests` 覆盖圆构造/度量/包含/相交/距离/分离/包围圆/变换；边构造/最近点/距离/延伸/缩放/反向/交点/相交/变换；点距离；矩形构造/关系/几乎关系/象限/角点/边/组合/距离/分离；多边形有限输入构造、方向归一、派生指标、POI、包含、距离/分离与变换；凸包顺序/单点；`OriginalShape` 原始指标和默认配置下的预变换转换。有限退化用例覆盖零半径、零长边、f32 收敛为重复端点、矩形零宽/零高、多边形点数不足/重复点/共线/自交/小面积。
- C++ 数值结果通过 `AlmostEqualRustF32` 按 D3 规则比较；布尔值与 `GeoPosition`、顶点顺序精确比较。Rust oracle 的 Debug/Release 命令和 JaguarCore 两个 profile 的完整测试均以退出码 0 结束。
- `DTransformation` 不需要新增同名结构：它保存的旋转和平移可由 `Transform2d::rotationRad/translation` 表达，行为落在 `ApplyRustF32`、`FollowedByRustF32`、`InverseRustF32`；Rust 公开的 `normalize_rotation` 则新增直接对应 `NormalizeRotationRustF32(float)`，按 f32 remainder 及负角修正规则实现，并覆盖 NaN、正负无穷和负零。
- B1 仍为进行中：当前是按操作组的确定性 fixture，不是 9 个 Rust 文件每个公开函数的完整用例矩阵；矩阵形式/分解边界、未覆盖的点接触与比较分支仍需补齐。Surrogate 生成/索引映射归 B3，offset 与简化归 B2。Rust `Edge::try_new` 接受非有限端点而 C++ `Edge2d::Create` 拒绝，先记录为非有限输入域差异，不计入本轮有限输入通过结论。
- 本轮验证命令：Rust `rustup run 1.98.1 cargo run --locked --manifest-path JaguarCore/tools/RustOracle/Cargo.toml -- --geometry-primitives-parity-only` 及带 `--release` 的同命令；JaguarCore Debug/Release `JaguarCoreTests.exe` 全套各运行一次。

## 证据位置

- pinned Rust：`geometry/{convex_hull,d_transformation,geo_enums,geo_traits,original_shape,shape_modification,transformation}.rs`、`geometry/fail_fast/{piers,pole,sp_surrogate}.rs`、`geometry/primitives/{circle,edge,point,rect,simple_polygon}.rs`。
- C++：`JaguarCore/Include/JaguarCore/Geometry/*`、`JaguarCore/Source/Geometry/{ConvexHull,OriginalShape,Primitives,ShapeModification,Transformation}/*`、`JaguarCore/Source/FailFast/Surrogate.cpp`。
- 运行对拍：`JaguarCore/Tests/Differential/CompareGeometry.cpp`、`JaguarCore/Tests/Fixtures/RustReference/geometry_primitives.txt`、`JaguarCore/tools/RustOracle/src/main.rs`。
