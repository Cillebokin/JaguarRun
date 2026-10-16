# JaguarCore 几何与实体部分代码逻辑说明

本文覆盖 `Source/Geometry/Primitives`、`Transformation`、`ConvexHull`、`OriginalShape`，以及 `Source/Entities` 中的 `Container`、`Item`、`PlacedItem`、`Layout`，并覆盖 `Source/Support/Assertions.cpp` 与 `result.h`、`NumericPolicy.h`、`RustF32.h` 的数值与错误策略。Rust 对照对象为 `CodeResource/jagua-rs/jagua-rs/src` 下的对应文件。路径中的行号均指 C++ 源文件，除非特别注明。`Source/Geometry/ShapeModification` 下的 Offset、Simplify、StraightSkeleton、NarrowConcavity 未在本文展开，仅说明其在 `OriginalShape` 中的调用位置。

## 一、数值策略：模拟 Rust f32 的基础设施

整个几何层并存 double 实现与 RustF32 实现。后者逐步模拟 Rust f32 运算，用于提高指定数值路径的行为对应度；这不意味着业务链路在所有函数、平台和输入上逐位相同。实际等价范围应以字段容差、FloatBits 用例和差分记录为准。阅读代码时需确认调用链选用了哪种精度，避免无意混用。

`include/JaguarCore/geometry/RustF32.h` 提供了四类工具。第一是 `MinRustF32`/`MaxRustF32`，直接包装 `std::fmin`/`std::fmax`，与 Rust `f32::min`/`max` 的 NaN 处理一致。第二是 `MidpointRustF32`（第 15-24 行），对两端同号且绝对值超过 `max/2` 的情况改用 `first*0.5 + second*0.5`，以避免溢出，这是对 Rust 实现的精确复刻。第三是 `AlmostEqualRustF32`（第 32-50 行），判定顺序为：完全相等；绝对差不超过 `FLT_EPSILON`；有限性检查；最后是按有序位模式计算的 ULP 差不超过 4。注意前两步是绝对容差，对绝对值很小的坐标会把不同数值判为相等，这一点在做微小量比较时需要留意。`LessRustF32`、`GreaterRustF32`、`LessEqualRustF32`（第 56-65 行）分别基于它定义，因此与 Rust 中 `FPA` 包装类型的 `PartialEq`/`PartialOrd` 语义一致（Rust 侧 `util/fpa.rs` 使用 `float_cmp::approx_eq!`）。

`include/JaguarCore/NumericPolicy.h` 定义了四个容差：`equalityTolerance`（1e-9，绝对值）、`geometryTolerance`（1e-7）、`areaTolerance`（默认 0.0）、`arcChordTolerance`（1e-3）。`IsValid()` 检查它们是否为有限非负数。就本次阅读范围而言，`SimplePolygon::Create` 只用到 `policy.areaTolerance`（`SimplePolygon.cpp:233`），而 `Layout.cpp`、`Assertions.cpp` 中硬编码使用了 `DefaultEqualityTolerance()`。`geometryTolerance` 与 `arcChordTolerance` 在本范围内未见使用，需要在全库检索确认是否属于死配置。另外需要指出，`Edge2d::Intersects` 与 `SimplePolygon::Contains` 的容差是绝对距离，但 `Edge2d::Intersects` 中判断的是叉积，叉积量纲是长度平方，因此 1e-9 这一阈值对不同尺度的坐标具有不同的实际含义，这是一个隐含假设。

## 二、几何原语

`Edge2d`（`Edge.cpp`，`edge.h`）是最基础的原语。`Edge2d::Create`（第 91-104 行）先转换到 f32 再判断起终点是否相同，拒绝退化边。`LengthRustF32`、`ExtendedAtFrontRustF32`、`ExtendedAtBackRustF32` 等函数都先转 f32 再计算。其中延伸函数直接除以边长，调用方必须保证边非退化，否则会产生 NaN，这一前置条件没有在函数内检查。最近点计算 `ClosestPointRustF32`（第 246-271 行）的结构与 Rust `Edge::closest_point_on_edge` 一致：退化边时参数取 -1，从而返回起点。线段相交有两套实现，需要区分。第一套是 `IntersectsRustF32(Edge2d)`，经 `intersectionPointRustF32`（第 32-76 行）实现：先做 x、y 轴的包围盒分离判断（使用严格大于，与 Rust `edge_intersection` 相同），再按参数化公式求 t、u，分母为零直接判为不相交，t、u 均在闭区间 [0,1] 内才视为相交。该实现与 Rust `primitives/edge.rs` 第 218-258 行的 `edge_intersection` 在分子分母的表达式上完全一致，因此是模拟 Rust 语义的首选路径。第二套是 `Edge2d::Intersects(other, tolerance)`（第 292-329 行），使用叉积符号判定加容差，并包含端点落在线段上的分支。文件中第 309-311 行的注释声称与 `Edge::collides_with` 对齐，但两者算法并不相同：C++ 版本的容差作用于叉积，端点接触被容差放宽，而 Rust 版本对端点接触的判断是精确的参数区间判断。因此不要把 `Intersects` 当成 Rust 的等价物，业务上应使用 `IntersectsRustF32`。

`Rect2d`（完全在 `include/JaguarCore/geometry/rect.h` 中，共 461 行）的 `Create`（第 45-61 行）要求 f32 下 `xMin < xMax` 且 `yMin < yMax`，因此零宽或零高矩形会被拒绝。`QuadrantsRustF32`（第 301-318 行）按 `[右上, 左上, 左下, 右下]` 顺序返回四个子矩形，与 jagua-rs `Rect::quadrants` 一致，POI 计算依赖这一顺序（`PointOfInaccessibility.cpp:129-138` 的注释也指出了这一点）。需要特别警惕的是，同一个类中的 double 版本 `Quadrants()`（第 278-284 行）返回的顺序是 `[左下, 右下, 右上, 左上]`，与 RustF32 版本不同。两者混用会改变 POI 的 FIFO 遍历顺序，从而改变结果的并列选择。`Rect2d::InflateToSquareRustF32` 在三处重复实现（`rect.h`、`PointOfInaccessibility.cpp:24-42`、`Container.cpp:13-31`），后续若修改应统一。

`Circle2d`（`Circle.cpp`，`circle.h`）的 `Create`（第 15-29 行）拒绝负半径和非有限值，允许零半径。与圆相关的 `*RustF32` 函数基本都直接展开 Rust `circle.rs` 的公式，例如圆与圆相交用平方比较 `d² <= (r1+r2)²`（第 134-144 行），圆与矩形相交用最近点法（第 155-172 行），这些都与 Rust 逐行对应。`SeparationDistanceRustF32` 系列返回的是穿透深度或间隙，其中内部判定在点版本用 `<=`，在线段版本用 `<`（第 181 与第 215 行），边界处理不一致，但对实际结果影响很小。需要注意的是，`SquaredDistanceToRustF32` 的语义实际上是“到圆周距离的平方”，并非到圆心距离的平方，这与 Rust `sq_distance_to` 的定义相同，属于沿用而非新问题。

`Circle2d::BoundingCircleRustF32`（第 31-81 行）是本文件中最值得注意的疑点。头文件 `circle.h` 第 236 行的注释写的是“Smallest circle that contains all inputs”，但实现是顺序合并：每次把当前圆与新圆合并为二者的最小包围圆，这是一种增量近似，结果依赖输入顺序，并不保证最小。另外，当当前圆与新圆同心（`centerDistance == 0`）且当前半径小于新圆半径时，代码执行 `dx * (startRadius / edgeLength)`，此时 `edgeLength` 为 0，`0 * inf` 得到 NaN，结果圆的圆心和半径都会变成 NaN。这一路径没有任何保护，应补充同心分支。

`SimplePolygon`（`SimplePolygon.cpp`，`SimplePolygon.h`）是最重要的原语。`Create`（第 192-258 行）的校验顺序为：策略有效性、顶点数不少于 3、有限性、f32 归一化（归一化后仍须有限）、去重（O(n²) 精确比较，第 215-223 行）、自相交（第 225 行，调用 `hasSelfIntersection`，第 152-175 行，跳过相邻边对）、面积（第 229-235 行，用 f32 鞋带公式，且 `signedAreaRustF32` 的符号约定是逆时针为正，第 237-238 行在面积为负时反转顶点，保证输出为逆时针）。随后计算包围盒、质心（`makeCentroidRustF32`，第 69-87 行，按 f32 累加后除以 `6*area`）、直径（凸包上 O(h²) 的最大距离，第 135-150 行）以及极点（调用 `ComputePointOfInaccessibility`）。整个流程与 Rust `SPolygon::new` 的顺序一致，区别在于 C++ 额外保存了 `m_Centroid`，而 Rust 的 `SPolygon` 结构体并无此字段，质心是按需由顶点推导的。

需要注意的一个缺口是，`hasSelfIntersection` 依赖 `IntersectsRustF32`，而后者对平行或共线的线段返回“不相交”。因此两条非相邻边共线重叠而无端点穿越的情况不会被识别为自相交。Rust 侧的 `find_self_intersection` 通过相同的 `collides_with` 实现，因此这是与 Rust 一致的既有缺口，而非移植错误。另一个潜在问题是 `signedAreaRustF32` 与 `makeCentroidRustF32` 都在 f32 下做大量累加，坐标量级较大（例如 1e5 以上）时存在明显的抵消误差，面积为小量时尤其严重。

`SimplePolygon::Contains`（第 395-428 行）是 double 版本，先做包围盒容差检查，再检查点到各边的距离是否在容差内，最后做标准的射线法。`ContainsRustF32`（第 430-474 行）则逐行复刻 Rust `SPolygon::collides_with(Point)`（`primitives/simple_polygon.rs` 第 280-310 行）：当射线经过顶点且该顶点与点同高时，只在另一端点位于射线下方时计数，这种处理与 Rust 完全一致，是解决顶点退化问题的关键。`IntersectsRustF32`（第 489-507 行）先做包围盒判断，再判断边与多边形边的相交，最后判断两端点是否在多边形内。

`TransformedRustF32`（第 338-374 行）与 `Transformed`（第 308-336 行）的区别非常大。前者复制一份多边形，原地对顶点做 `ApplyRustF32`，重算包围盒（经 `Rect2d::Create` 校验）和质心，极点中心变换、半径保持，面积和直径保持不变（刚体变换下成立，与 Rust `SPolygon::transform` 一致，Rust 第 219-260 行的 `transform` 同样不重算 `area` 与 `diameter`）。后者则调用 `Transform2d::Apply`（double 精度），然后重新执行完整的 `Create`，包括自相交检测和 POI 计算，代价高出一个数量级，而且 f32 归一化后可能因为精度原因返回错误。业务路径应使用 `TransformedRustF32`。

`ComputePointOfInaccessibility`（`PointOfInaccessibility.cpp`，第 91-148 行）是 polylabel 风格的四叉树搜索，使用 FIFO 队列（`std::deque`，第 99 行、第 107 行），与 Rust `geometry/fail_fast/pole.rs` 中 `compute_pole` 的 `VecDeque` 遍历一致。根节点为正方形化后的包围盒，深度标记为 10（第 102 行），剪枝条件为 `current.distance + radius <= bestRadius`（第 123 行），其中 `radius` 为半对角线，这是有效的上界，因为有符号距离是 1-Lipschitz 函数。`ClearanceRustF32`（第 53-88 行）把多边形边距离与排除圆距离合并，并以质心位置是否在形状内且不在任何排除圆内决定符号。注意每次调用都会通过 `shape.Edges()` 重新构造一个 `std::vector<Edge2d>`（`SimplePolygon.cpp:295-306`），属于明显的性能浪费。

`ConvexHull`（`ConvexHull.cpp`，第 185-223 行）是 Andrew 单调链算法，输入先转为 f32，再按 x 坐标做稳定排序，这与 Rust `convex_hull_from_points` 的 `sort_by_key(OrderedFloat(x))` 一致。需要注意两点。其一，单点输入会返回空向量，因为下凸壳和上凸壳各弹出一个元素后都为空；而 `SimplePolygon.cpp:102-103` 中的内部副本对单点直接返回原输入，两者行为不一致。其二，排序只看 x，同 x 点的相对顺序依赖输入，经验上对凸包顶点集合通常正确，但我没有给出完整的正确性证明，因此在依赖退化输入的场景下应补充测试。`SimplePolygon.cpp` 中的 `convexHullRustF32` 是这份代码的第二份拷贝，并且没有 NaN 排序处理，建议合并。

## 三、Transformation 的组合顺序

C++ 的 `Transform2d`（`transformation.h`，`Transformation.cpp`）只保存旋转角与平移量，对应 Rust `Transformation` 的 3×3 齐次矩阵。两边的组合语义需要明确区分。`Apply`（`Transformation.cpp:310-316`）先旋转后平移，与 Rust `rot_transl_m`（`geometry/transformation.rs` 中 `rot_transl_m`）一致。`FollowedBy(next)`（第 361-364 行）的语义是“先执行本变换，再执行 next”，其结果为角度相加、平移量为 `next.Apply(translation)`。这与 Rust `transform(other)` 中的 `dot_prod(&other.matrix, &self.matrix)`（即 other·self）是同一语义，所以两者的组合顺序在纯数学层面是一致的。`FollowedByRustF32`（第 366-386 行）按 f32 展开矩阵乘法，并通过 `atan2` 从合成矩阵恢复角度，这与 Rust 的 `decompose` 相同，但经过一次 sin/cos 与 atan2 的往返，并非逐位相同。`Inverse`（第 333-342 行）与 `InverseRustF32`（第 344-359 行）分别用 double 与 f32 实现，后者利用 `det = cos²+sin²` 的显式公式，与 Rust 通用 3×3 求逆在数值上等价但不逐位一致。

这里有一个容易忽略的疑点。角度在 `FollowedBy` 中直接相加，不做 `(-π, π]` 的归一化，因此多次组合后 `rotationRad` 可能超出常规范围。`Assertions.cpp:71-76` 的 `sameTransform` 直接比较原始角度，两个等价但角度相差 2π 的变换会被判为不同。Rust 侧 `Transformation` 不保存角度，因此不存在这一问题，这是 C++ 特有的隐患。

## 四、OriginalShape 预处理链

`OriginalShape::ConvertToInternal`（`OriginalShape.cpp:229-270`）是原始形状到碰撞形状的唯一入口，被 `Item::Create` 与 `Container::Create` 调用。流程依次为：先用 `preTransform` 做一次 `TransformedRustF32`（第 231 行），再在 `offset` 非零时调用 `OffsetShape`（第 236-242 行），然后在存在 `simplifyTolerance` 时做化简（第 243-250 行），若设置了 `narrowConcavityCutoff` 则闭合窄凹口并以 `simplifyTolerance / 10.0` 再次化简（第 251-260 行，其中的 10 是硬编码常数），最后做 `ShapeModificationValid` 检查（第 262-267 行）。这里有一个值得注意的行为：膨胀或收缩关系的有效性检查只在化简分支内执行，如果只设置了 `offset` 而没有 `simplifyTolerance`，则不会做这一检查。这一条件分支是否符合设计意图，需要与 `ShapeModification.h` 的调用约定一起确认。

此外，`Item::Area()`（`Item.cpp:164-167`）和 `Container::Area()`（`Container.cpp:107-110`）都使用原始形状的面积，不受 `preTransform` 与膨胀化简影响，这与 Rust `Item::area`、`Container::area` 的定义一致。

## 五、实体层：Container、Item、PlacedItem

`Container`（`Container.cpp`，`container.h`）是一个以 `std::shared_ptr<const Data>` 为核心的值语义句柄。`Data` 结构（第 34-43 行）包含 id、原始外形、碰撞外形、质量区、CDE 配置、包围正方形 `bounds` 以及静态障碍物列表 `staticHazards`。`Create`（第 50-100 行）首先通过 `ConvertToInternal` 得到碰撞外形，然后检查质量等级唯一且小于 10（`MAX_QUALITIES`，第 11 行，与 Rust `N_QUALITIES` 一致）。静态障碍物包括一个 `Exterior` 外部障碍物，以及按质量区域生成的障碍物，其中质量 0 的区域被标记为 `Hole`，其余为 `InferiorQualityZone`（第 85-97 行），这与 Rust `container.rs` 第 118 行的映射一致。`SameBase`（第 132-135 行）通过比较 `Data` 指针判断两个容器是否来自同一基础容器，这是快照恢复的前提。`Area()` 返回的是原始外形面积，即“毛面积”，不扣除孔洞和质量区，与 Rust 文档注释一致。`CreateCollisionEngine`（第 137-147 行）构造 `CollisionEngine` 并注册所有静态障碍物，但任何注册失败都直接返回 `nullptr`，错误信息被丢弃。

`Item`（`Item.cpp`，`item.h`）保存索引、外部 ID、原始形状、碰撞形状、代理配置、最低质量要求和旋转范围。`Item::Create`（第 98-127 行）先校验旋转范围（`rotationRangeRepresentable`，第 66-78 行：`None` 与 `Continuous` 不允许携带离散角度，`Discrete` 的角度数量则不做校验，注释说明 1 到 65536 的规则由导入层执行），再把所有离散角度舍入到 f32（第 111-112 行），然后调用 `ConvertToInternal` 与 `GenerateSurrogate`。`minQuality` 没有与容器的质量区做任何一致性校验。

`PlacedItem`（`PlacedItem.cpp`，`PlacedItem.h`）的 `Create`（第 16-31 行）是放置变换进入系统的入口。它先把旋转角和平移量舍入到 f32，并以舍入后的变换保存（`Transform()` 因此返回的不是调用者传入的原值，这与 Rust `DTransformation::new` 中的 `NotNan<f32>` 语义一致）。然后调用 `TransformedRustF32` 生成碰撞形状。注意第 28 行 `std::make_shared<const Item>(item)` 对整个 `Item` 做了深拷贝，包括原始形状与碰撞形状的顶点数组，而 Rust 侧使用的是 `Arc::clone`，只增加引用计数。这一差异在放置数量大时会带来可观的内存与拷贝开销。`GetItem()` 返回的引用指向这份拷贝，生命周期与 `PlacedItem` 绑定。

## 六、Layout：PlacedItem 与 hazard 的管理

`Layout`（`Layout.cpp`，`layout.h`）持有 `m_Container`、`m_Cde`（`CollisionEnginePtr`，即 `unique_ptr`）、`m_Slots`（`std::vector<Slot>`）以及 `m_FreeSlotIndices`（LIFO 栈）。每个 `Slot` 含 `optional<PlacedItem>`、碰撞引擎句柄 `cdeHandle` 与代数 `generation`。第 0 号槽位是哨兵，代数恒为 0，在构造函数第 58-60 行中设置。

句柄 `PlacedItemHandle` 是 `HazardHandle` 的别名（`layout.h:12`），即一个 `(index, generation)` 二元组。代数的奇偶性承载了占用状态：占用时为奇数，空闲时为偶数。`AllocateHandle`（第 83-96 行）在复用空闲槽时执行 `generation |= 1`，新槽位则以代数 1 进入；`RemoveItem`（第 153-154 行）与 `PlaceItem` 的失败回滚（第 130-131 行）通过 `++generation` 使其变为偶数。`Get`（第 63-81 行）同时检查索引越界、代数匹配以及是否占用，从而防止已删除句柄被误用（ABA 问题）。代数为 `uint32`，没有溢出检查，在同一槽位经历约 2³¹ 次复用后会回绕，需要注意这一边界，但在常规规模下不构成问题。

放置流程（`PlaceItem`，第 108-137 行）的顺序很讲究。先调用 `PlacedItem::Create`，失败时没有任何状态变化；然后分配句柄，写入槽位；再向碰撞引擎注册 `MakeHazard` 生成的障碍物（第 98-106 行，`dynamic=true`，实体包含 `PlacedItem` 类型、物品索引、句柄与变换）。若注册失败，则回滚槽位并把句柄放回空闲栈。删除流程（`RemoveItem`，第 139-158 行）先校验句柄，再注销碰撞引擎中的障碍物，只有注销成功才会修改槽位，因此是原子的。Layout 本身并不判断放置是否可行，可行性由 `IsFeasible`（第 203-224 行）事后检查：对每个已放置物品调用 `DetectPolygonCollision`，并用 `irrelevantHandle` 过滤掉自身的障碍物。这里的碰撞引擎内部实现不展开，只需注意 `IsFeasible` 的复杂度是 O(n) 次查询。

`SwapContainer`（第 160-201 行）的设计是先在新容器上建立新引擎，再把旧引擎快照中的所有动态障碍物重新注册，并记录新句柄，任何一步失败都不修改旧状态，因此是强异常安全的。但它没有检查每一个已放置槽位是否都在旧快照中找到了对应障碍物。如果存在遗漏，对应槽位的 `cdeHandle` 会保持为默认值 `{0,0}` 而不报错，这与 `Restore` 中的完整性检查（第 348-364 行）不对称。

快照 `Save`（第 243-260 行）记录容器（浅拷贝，共享数据）、所有已占用的 `PlacedItem`（深拷贝）、碰撞引擎快照、全部槽位代数以及空闲栈。`LayoutSnapshot::PlacedItemArea`（第 35-53 行）按句柄索引和代数排序后求和，`Layout::PlacedItemArea`（第 226-235 行）则直接按槽位顺序求和，两者顺序一致，保证浮点求和结果确定。二者都使用 `float` 累加，对大量物品会有累积误差，`Density()`（第 237-241 行）也是 f32 相除后转 double，这与 Rust `Layout::density` 返回 `f32` 的设计一致，但精度特征需要知道。

`Restore`（第 262-443 行）是这一文件中最复杂的函数。它的校验分为五步。第一步检查容器是否同源（`SameBase`）。第二步检查槽位元数据，包括哨兵代数为 0、每个放置句柄的索引不为 0、代数非零且为奇数、索引在范围内且不重复、与元数据中的代数一致（第 310-324 行）。第三步检查空闲栈，要求索引有效、不与放置冲突、代数为偶数，并且所有非哨兵槽位恰好属于“放置”或“空闲”之一（第 332-366 行）。第四步检查动态障碍物的形状非空。第五步才真正修改状态：先赋值 `m_Container`、`m_Slots`、`m_FreeSlotIndices`（第 391-393 行），然后调用 `m_Cde->Restore`（第 395 行），注释说明这是为了与 Rust 的执行顺序保持一致。这意味着如果 `m_Cde->Restore` 失败，`Layout` 会处于“槽位已换、引擎未换”的部分恢复状态，函数返回错误但并不回滚。最后一步，函数重新比对引擎中的动态障碍物与期望值（第 400-420 行），并为每个槽位重新绑定句柄（第 422-436 行）。不一致时，只有 Debug 构建会抛出 `std::logic_error`（第 438-441 行），Release 构建中 `dynamicHazardsMismatch` 只被计算却未被使用，错误被静默吞掉，且第 432 行 `continue` 之后槽位的 `cdeHandle` 会保留旧值。这是本文件最需要团队注意的风险点之一。

此外，`Layout` 的构造函数（第 55-61 行）并不检查 `CreateCollisionEngine()` 是否返回空指针，这时 `m_Cde` 为空。`PlaceItem` 与 `IsFeasible` 对此有防护，但 `Save()`（第 245 行）、`SwapContainer()`（第 171 行）与 `Restore()`（第 395 行）直接解引用 `m_Cde`，会在空指针下崩溃。同样，`Layout` 的移动构造是默认的，被移走的对象 `m_Cde` 为空，之后调用上述三个函数同样会崩溃。

## 七、错误传播与断言策略

`result.h` 定义了 `Result<T>`，内部是 `std::variant<T, Error>`，`Error` 包含 `ErrorCode` 与字符串消息。`Result<void>` 使用 `bool` 加错误对象实现。`Value()` 与 `GetError()` 在错误的一侧调用时直接抛出 `std::bad_variant_access`（若是 `Result<void>` 则只能通过 `HasValue` 判断），没有任何更友好的诊断信息，因此调用方必须先检查 `HasValue()`。`ErrorCode` 的枚举覆盖输入无效、多边形无效、自相交、零面积、偏移失败、碰撞引擎失败、快照不匹配、不可行、取消、超时、序列化失败与未实现等类别。业务代码中的错误传播基本采用“检查后直接返回 `FromError(error)`”的模式，例如 `SimplePolygon::Create` 把 POI 的错误原样透传（第 253-255 行）。

几何层的失败基本都是可恢复的，并以 `Result` 返回，例如非有限输入、零面积、自相交、非法矩形与非法圆。对于 Rust 中的 `expect` 与 `panic` 语义，C++ 做了有意的降级处理。例如 Rust `Layout::remove_item` 对无效句柄直接 `expect` 失败，而 C++ 版本（`Layout.cpp:139-146`）返回 `InvalidInput`。这种 fail-soft 是合理的，但它改变了错误的性质。另外，`Transform2d` 本身不做有限性保护，NaN 会经 `Apply` 直接传播，只有在 `Transformed`、`TransformedRustF32` 与 `PlacedItem::Create` 中才会被 `IsFinite()` 检查捕获（`SimplePolygon.cpp:311-315`，`341-345`）。

`Assertions.cpp` 提供了两层设施。第一层是可调用的校验函数 `ValidateLayout`、`ValidateLayoutSnapshot`、`ValidateSPSolution` 等（第 210-413 行），返回 `Result<void>`。其中 `validateStandaloneSnapshot`（第 176-185 行）会新建一个 `Layout`、执行一次完整的 `Restore` 并再次 `Save` 比较，因此是一个 O(n²) 量级的操作。第二层是 `DebugAssertLayout` 等函数（第 395 行之后），仅在非 NDEBUG 构建中执行 `assert`。由于 `Layout::PlaceItem`、`RemoveItem`、`SwapContainer` 都会调用 `DebugAssertLayout`，Debug 构建下每次操作都会触发完整校验，整体复杂度会显著上升。此外，`Assertions.cpp` 中的 `sameHazard`、`samePolygon` 与 `Layout.cpp` 中的同名函数重复实现，建议合并。

Debug 与 Release 的行为差异需要特别说明。`assert` 在失败时直接终止进程，而 `Layout::Restore` 在 Debug 下抛出 `std::logic_error`，这一异常会穿过本应返回 `Result` 的接口边界。Release 下 `Restore` 对一致性失败不报错。与 Rust 相比，Rust 侧使用 `debug_assert!`（如 `layout.rs` 中 `restore` 的 `debug_assert!(assertions::snapshot_matches_layout(...))`）后直接继续执行，并没有 C++ 版本这种异常抛出，因此两者的 Debug 语义并不完全相同。

## 八、与 Rust 版的主要差异与疑点汇总

第一，结构差异。C++ `SimplePolygon` 多保存了 `m_Centroid` 字段，Rust 无此字段；C++ `PlacedItem` 深拷贝整个 `Item`，Rust 使用 `Arc`；C++ 用 `Result` 代替 Rust 的 `expect` 与 `panic`。这些差异在语义上多数是有意的，但应在团队文档中明确列出。

第二，精度与算法差异。`Edge2d::Intersects`（double）与 Rust 算法不同，业务上应避免使用。`BoundingCircleRustF32` 的注释与实现不符，且存在同心圆 NaN 缺陷。`Quadrants()`（double）与 `QuadrantsRustF32()` 的顺序不同。`FollowedBy` 不归一化角度，导致 `sameTransform` 对等价角度误判。

第三，资源与生命周期。`Layout::Save`、`SwapContainer`、`Restore` 对空碰撞引擎与移动后的 `Layout` 缺少防护，会崩溃。`Layout::Items()` 返回的引用指向槽位内的 `PlacedItem`，在该物品被移除后即失效。`SwapContainer` 对句柄完整性的检查不足。

第四，错误被静默吞掉的位置。`Restore` 在 Release 下对碰撞引擎不一致不报错，且句柄可能保持旧值。`Container::CreateCollisionEngine` 返回空指针时丢失错误详情。`Item::Create` 对 `minQuality` 与 `Discrete` 空角度集合不做校验。

第五，大小写目录副本。源码引用如 JaguarCore/Geometry/Edge.h 与 JaguarCore/Assertions.h 使用大写路径；当前仓库存在 Include/ 与 include/ 两套目录，且相应头文件在两套副本中均可找到。因此不能仅凭大小写差异断言 Linux 或 macOS 构建失败。应比较副本、检查工程设置的 include root，并明确权威目录；当前已确认的维护风险是副本漂移。

第六，性能热点。`PlacedItem` 深拷贝、`MakeHazard` 每次复制多边形、`ClearanceRustF32` 每次分配边向量、`Transformed`（double）重新执行完整校验、`DebugAssertLayout` 在 Debug 下的全量校验，这几处是最可能的性能瓶颈。

## 九、未覆盖范围

本文没有展开 `ShapeModification` 下 Offset、Simplify、StraightSkeleton、NarrowConcavity 的内部算法，也没有分析碰撞引擎 `CollisionEngine` 与四叉树的实现，`Surrogate` 的内部算法同样未读。`Rect2d::CollidesWith(Edge)` 与 Rust `rect.rs` 的逐行对齐、`Circle::SeparationDistance` 的 Rust 对照以及 `SPProblem` 等问题层的校验函数，也只做了概要说明。后续若要深入，建议优先阅读 `Surrogate` 与 `CollisionEngine`，它们直接决定 `Layout::IsFeasible` 的结果。
