# JaguarCore 碰撞检测、快速失败与形状修改：代码逻辑说明

本文按调用链自上而下梳理 JaguarCore 的碰撞检测（CDE）子系统，随后讨论快速失败代理（FailFast Surrogate）与形状修改模块，并在关键处与 jagua-rs 的 Rust 实现对照。源码根目录记为 `JaguarCore/`，Rust 参考根目录记为 `CodeResource/jagua-rs/jagua-rs/`。

## 一、总体结构与公开接口

碰撞检测的对外入口是 `CollisionEngine`，声明位于 `include/JaguarCore/collision/CollisionEngine.h`（1-181 行）。它对外提供三类能力：障碍物的注册与注销（`RegisterHazard`、`DeregisterHazard`），多边形与快速代理的碰撞检测和收集（`DetectPolygonCollision`、`CollectPolygonCollisionsUntil`、`DetectSurrogateCollision`、`CollectSurrogateCollisionsUntil` 等），以及快照的保存与恢复（`Save`、`Restore`）。引擎内部持有四个核心成员：`m_QuadTree` 空间索引、`m_Slots` 障碍物槽位表、`m_FreeSlotIndices` 空闲槽位栈（LIFO），以及两把互斥量 `m_ThreadPoolMutex` 与 `m_ParallelQueryMutex`（第 172-179 行）。

槽位结构 `Slot` 包含 `std::optional<Hazard>`、`std::unique_ptr<PolygonSoA> shapeCache` 与 `generation` 代数号（第 133-138 行）。句柄 `HazardHandle` 由 `index` 与 `generation` 两部分组成，`Get` 系列函数在访问时校验代数号，从而使已注销句柄失效，避免悬挂访问。需要注意的是，`shapeCache` 虽然在构建时被赋值，却没有任何路径读取它：`GetShapeCache`（`CollisionEngine.cpp` 168-176）无调用方，`ShapesCollide`（196-227）、`EdgeCollides`（229-233）与 `CircleCollides`（441-452）同样是死代码。这些遗留代码增加了阅读负担，也意味着 SoA 缓存在当前版本中并未真正参与计算。

## 二、注册、注销与句柄分配

`RegisterHazard`（`Source/Collision/CollisionEngine.cpp` 95-120）首先通过 `FindHazardHandle` 做重复实体检查，该检查是线性扫描，复杂度为 O(n)，因此批量注册时整体为 O(n²)。随后 `AllocateHandle`（122-134）优先从 `m_FreeSlotIndices` 弹出空闲槽位，否则追加新槽位，并在注册时把形状交给 `m_QuadTree.RegisterHazard`。`DeregisterHazard`（178-194）逆向执行：先从四叉树注销，再清空槽位并递增代数号，最后把索引压入空闲栈。代数号递增保证旧句柄在槽位复用后仍然会被判定为无效。

`HazardEntity` 的相等性（`include/JaguarCore/collision/hazard.h` 1-83）对 `PlacedItem` 类型采用对变换矩阵做 float 转换后的比较，这一点与 Rust 侧 f32 的精度语义保持一致，但也意味着两个在双精度下略有差异的变换会被视为不同实体。

## 三、碰撞查询的调用链

多边形检测的主路径是 `DetectPolygonCollision`（`CollisionEngine.cpp` 568-599），它调用 `CollectPolygonCollisionsUntil`（486-557）并在首个命中处停止。`CollectPolygonCollisionsUntil` 的第一步是包围盒预筛：`isOutside`（40-45）判断被查询形状的包围盒是否完全落在引擎边界外，若是则直接报告外部障碍物（`Exterior`）的命中，并跳过四叉树遍历。随后它对每条边调用 `NodeCollidesWithEdge` 或 `CollectEdgeCollisionsUntil`，在四叉树节点上递归。对于形状内部包含障碍物的情况，`DetectContainmentCollision`（58-72）以一个代表点（点在多边形内部的 POI）测试，并依据障碍物的 `HazardKind` 与 `scope`（Interior 或 Exterior）决定是否算作碰撞，其真值表与 Rust 版 `detect_containment_collision`（`collision_detection/cd_engine.rs` 233-258）逐条一致，即内部障碍物只在被查询形状位于其内部时才算碰撞，外部障碍物则相反。

Rust 版的 `detect_poly_collision`（`cd_engine.rs` 161-200）与此流程同构：先做边界外检查，再遍历边，最后做包含检测。`collect_poly_collisions_until`（273-325）在 Rust 中同样以回调返回 `bool` 控制提前停止，C++ 的 `stopAfterCollision` 语义与之相同。

快速代理的检测路径为 `DetectSurrogateCollision`（657-689）与 `CollectSurrogateCollisionsUntil`（691-724）。后者只使用代理的极（poles）进行圆形查询，并不对 piers 做边检测，这与 Rust `collect_surrogate_collisions_until`（`cd_engine.rs` 343-368，仅使用 poles）保持一致。piers 只在更高层的快速失败判断中作为辅助信息存在。

## 四、四叉树：构建、懒扩展与查询

四叉树定义于 `include/JaguarCore/collision/Quadtree.h`（1-140 行），实现于 `Source/Collision/Quadtree/Quadtree.cpp`（756 行）。其设计逐行移植自 jagua-rs 的 quadtree：每个节点持有一组 `NodeHazard`，按 presence 降序排列（Entire 优先于 Partial，Partial 优先于 None），并且只有 Partial 的障碍物才携带一个 `PartialHazard`，其中保存节点内仍然相关的边子集与快速失败包围盒 `ffBbox`。

注册流程从 `MakeRootHazard`（273-298）开始，障碍物以根节点的 Partial presence 进入，然后由 `RegisterIntoNode`（469-528）负责把它分发到子节点。`Constrict`（300-467）是这一过程的核心：它先检查障碍物是否完整覆盖某个象限（若是则判为 Entire），否则为每个象限计算相交的边子集，并通过中心点包含测试处理没有边穿过的情况。在第二遍处理邻接关系时，C++ 版对 `None` 邻居采取静默偏好（435-436 行），而 Rust 版在相同分支中使用 `unreachable!`。这说明 C++ 版在该分支上更宽容，但也掩盖了潜在的前置条件破坏，阅读时应当注意。

节点是懒扩展的：子节点只在需要时创建，`RegisterIntoNode` 在 492-500 行展开子节点。注销时 `DeregisterFromNode`（529-547）递归移除句柄，若某子树中已不存在 Partial 障碍物，则折叠该子树，以回收节点内存。`edgeCollidesWithQuadrants`（117-194）与 `edgeIntersectionHalf`（40-77）负责线段与象限的分类，后者的 t/u 分类逻辑与 Rust `edge_intersection_half`（`quadtree/qt_traits.rs` 150-208）一致。`circleQuadrantOrder`（209-223）决定圆形查询时象限的遍历顺序，与 Rust `Circle::quadrant_order` 对应。

查询入口 `Query`（611-650）支持矩形、线段三种形态，并在节点的活动边数超过 `m_CdThreshold` 时退化为直接对边列表逐一检测，而不是继续下降到子节点。这个阈值（`CDEConfig::cdThreshold` 默认 32，见 `CollisionEngine.h` 35-36）是一个性能折中：边数很少时直接检测比递归更便宜。`PresenceAt`（663-689）计算某句柄在给定节点矩形上的覆盖状态，`VirtualRootFor`（701-721）则为查询矩形选择最小的覆盖节点作为虚拟根，从而缩小遍历范围。

有一处确定的缺陷需要单独说明。仅使用包围盒注册的 `RegisterHazard` 重载（`Quadtree.cpp` 579-598）会把障碍物标记为 Partial 且不附带形状。然而 `RegisterIntoNode`（515-526 行）和懒扩展逻辑（492-500 行）会跳过 `shape` 为空的障碍物，因此这类障碍物对于需要下降到子节点的查询是不可见的。该重载的头文件注释也写明"没有多边形几何时统一标记为部分覆盖"，但下游逻辑并未兑现这一语义。如果上层使用包围盒注册路径，就可能出现漏检，这是本模块最需要修复或加注释说明的地方。

## 五、SoA 布局与 AVX2 的选择

几何数据的紧凑表示位于 `Source/Collision/SoaGeometry.h` 与 `SoaGeometry.cpp`。`PolygonSoA` 把顶点的 x、y 坐标拆分为两组平行的 double 数组，便于向量化访问。边相交检测的分派发生在 `intersectsBoundary`（`SoaGeometry.cpp` 187-197），它根据 `runtimeAvx2Supported`（32-55 行）的结果选择 `SoaGeometryAvx2.cpp` 或 `SoaGeometryAvx2Fallback.cpp` 中的实现。

运行时检测的前提是宏 `_MSC_VER && JAGUARCORE_AVX2_COMPILED` 同时成立，因此在非 MSVC 编译器下，即使代码中包含了 AVX2 实现，运行时也会始终走标量分支。编译期的开关由 `JaguarCore.pro` 的 22-31 行控制：`CONFIG+=avx2` 会启用 AVX2 编译，而 `jaguarcore_no_avx2` 则强制关闭。这种双层开关意味着"编译了 AVX2"与"运行时启用 AVX2"是两件不同的事，排查性能问题时应同时确认这两项。

`onSegment` 的共线判断（`SoaGeometry.cpp` 20-30）使用叉积与容差比较，但容差是绝对值且未归一化，因此其判定强烈依赖于边长尺度。在坐标量级很大或很小的布局中，这一判断可能失效。

## 六、线程池与并行查询

`ThreadPool`（`Source/Collision/ThreadPool.h` 与 `ThreadPool.cpp`）采用固定数量的工作线程，工作循环 `WorkerLoop`（41-74）从队列取任务执行。`ParallelFor`（94-123）把区间按 `grainSize` 切分为若干批，每批作为一个任务提交，主线程等待全部完成。`BatchCollisionOptions` 中的 `grainSize` 默认值为 32（`CollisionEngine.h` 21-22），它决定了任务粒度与调度开销之间的平衡。

`DetectPolygonCollisionsParallel`（`CollisionEngine.cpp` 624-655）是并行入口。它在访问线程池前持有 `m_ThreadPoolMutex`，并在整个并行过程中持有 `m_ParallelQueryMutex`，以保证同一引擎上的并行查询相互串行。`GetThreadPool`（613-622）在首次调用时按 `workerCount` 创建线程池，之后的调用即使传入不同的 `workerCount` 也会沿用已有线程池，这一点与参数的直观含义不符，属于需要注意的行为。

错误传播方面，`ParallelFor` 会记录第一个出错的结果并在完成后返回。然而 `WorkerLoop` 没有 try/catch 保护（`ThreadPool.cpp` 60 行附近），因此工作函数若抛出异常，会导致线程终止进而整个进程崩溃。目前 CDE 内部查询路径基本不抛异常，但任何未来加入的异常源都会直接击穿线程池。

## 七、快速失败代理 Surrogate

`Surrogate` 的声明位于 `include/JaguarCore/FailFast/Surrogate.h`（93 行），实现位于 `Source/FailFast/Surrogate.cpp`（724 行）。它由三部分组成：极（poles，内切圆）、桥（piers，选取的弦边）以及凸包顶点索引与面积。

极的生成由 `generatePoles`（49-84）完成，它以 `poleLimits` 中的 `(count, coverage)` 对为停止条件，在覆盖率达到阈值时停止添加新极，判断采用严格的 `>` 比较。`Surrogate::Create`（575-641）先计算凸包与极，再生成 piers，并通过 ff 前缀决定哪些顶点参与快速失败判断，该前缀逻辑与 Rust `sp_surrogate.rs` 的 `SPSurrogate::new` 对应，核对结果一致（`Surrogate.cpp` 593-606）。

piers 的生成分为两步。`generateCandidatePiers`（186-241）沿若干射线发射候选弦，射线数量在 `#ifdef NDEBUG` 下为 200 个角度乘以 90 个采样，调试版本则为 10 乘以 4。`generatePiers`（440-490）随后用贪心策略，每次挑选使 `pierLoss` 最小的候选，直到满足数量或覆盖条件。`generateUnrepresentedGrid`（329-373）在调试与发布间同样存在 100 与 10 的采样差异（`Surrogate.cpp` 333-337）。Rust 版的 `piers.rs` 第 16-20 行使用 `cfg!(debug_assertions)` 实现同样的分支，因此这一差异是忠实移植的结果，而不是 C++ 独有的缺陷。但它意味着调试版与发布版生成的代理在数值上不同，对比测试时必须统一构建配置。

代理的变换有两条路径。`Transformed`（674-723 附近）使用双精度，而 `TransformedRustF32` 使用 `*RustF32` 系列辅助函数模拟 f32 运算，以便与 Rust 的结果逐位对齐。这种双轨设计是为了跨语言验证，正常运行时应当明确选择其中一条，否则两侧结果可能在边界情况下出现分歧。

## 八、形状修改

形状修改模块分为四个部分：偏移（Offset）、简化（Simplify）、直骨架（StraightSkeleton）与窄凹陷处理（NarrowConcavity），统一接口声明在 `include/JaguarCore/geometry/ShapeModification.h`。

偏移的主路径在 `Source/Geometry/ShapeModification/Offset.cpp` 中，它先把输入量化为 int32 并调用叠加（overlay）快速路径，失败时才进入直骨架计算。在 `OffsetShape` 的末尾（737 行附近），函数只返回第一条轮廓，因此对于带孔或多轮廓的结果，调用方会丢失信息。直骨架计算抛出异常时，代码会在 727-740 行静默回退到 `fallbackOffsetShape`，该回退使用带圆角连接的偏移策略。静默回退的好处是可用性高，代价是调用方无法判断当前结果来自哪条路径，也无法统计失败率。`normalizeOffsetRustF32`（86-96）负责根据模式翻转符号，以对齐 Rust 的约定。

Rust 版 `offset_shape`（`geometry/shape_modification.rs` 340-379）只使用 `geo_buffer::buffer_polygon_rounded`，没有 C++ 中的多级回退链。`geo_buffer` 的源码不在本仓库树内，因此无法逐行核对其数值行为，两者的几何结果在复杂输入上可能存在差异。

直骨架的事件队列在 `StraightSkeleton.cpp` 中实现。`findSplit` 的排序比较器（712-723）使用带容差的 `eq` 判断相等，这意味着相近的事件可能被排序为不稳定顺序，属于中等严重程度的问题。事件循环 `while(true)`（589-599）没有迭代上限，理论上当法向量退化为零时可能不收敛。文件开头的绝对容差 `1e-9`（`StraightSkeleton.cpp` 27 行）以及 Offset 中的 `1e-9`、`1e-12`（`Offset.cpp` 502、516 行）都是与尺度无关的绝对阈值，对于大坐标或微小形状都可能误判。`acos` 的参数若因舍入超出 [-1, 1]，会产生 NaN（`Offset.cpp` 412-414），需要做钳制。

简化模块 `simplify_shape` 与 Rust 一一对应，候选顶点按代价排序后逐个移除。窄凹陷处理 `CloseNarrowConcavities`（`NarrowConcavity.cpp` 76-166）对所有边对进行暴力配对，时间复杂度为 O(n²)，并且在 `Create` 失败时以 `break` 静默退出（159-161 行）。该函数直接返回值而不是 `Result`，调用方无法区分"无需处理"与"处理失败"。此外，`ShapeModifyConfig` 中的字段在整个模块中均未被读取（`ShapeModification.h` 21-26 行），给人以可配置的错觉。

## 九、线程安全与数值容差

线程安全的约定在 `CollisionEngine.h` 56-58 行写明：常量查询可以并发执行，而注册、注销和恢复必须与其他操作外部串行化。并行查询期间，过滤器（`HazardFilter`）可能被多个工作线程同时调用，因此自定义过滤器必须保证自身的线程安全。`m_ParallelQueryMutex` 只串行化并行入口之间的调用，并不阻止普通查询与修改同时发生，这一保证需要调用方自己维护。

数值方面存在两套约定。CDE 和几何核心大多使用 double，而快速代理与 Rust 对齐的路径使用 f32 模拟。`HazardEntity` 的 `PlacedItem` 相等性依赖 float 转换，这使得实体比较在精度上比 double 更粗。`onSegment` 的容差与形状修改中的绝对 epsilon 一样，都依赖于输入尺度，建议在上层统一做尺度归一化，或把容差改为相对值。

## 十、快照与恢复

`Save`（`CollisionEngine.cpp` 736-745）只收集动态障碍物，与 Rust `CDESnapshot`（`cd_engine.rs`）的语义一致。`Restore`（747-887）通过对比当前动态障碍物与快照中的实体列表，计算需要移除与需要新增的集合，然后逐一执行，这与 Rust 版 `restore` 的做差逻辑相同。不过 C++ 版在 796-812 行附近会在恢复后完整重建四叉树，代价与引擎规模线性相关。同时，`SetRegistered`（`Quadtree.cpp` 744-755）通过线性扫描维护登记表，使批量注册整体退化为 O(n²)。这两处都是性能上的已知瓶颈。

## 十一、主要陷阱与建议

综合上述分析，阅读和使用这一子系统时需要注意以下几点。第一，包围盒注册路径会在四叉树下降时漏掉障碍物，若业务依赖该路径，应优先修复 `RegisterIntoNode` 与懒扩展对空形状的处理。第二，线性去重、线性 `Contains`（`Collector.cpp` 7-11）与 `SetRegistered` 的线性扫描在障碍物数量大时会成为瓶颈，可以改为哈希索引。第三，`GetThreadPool` 忽略后续的 `workerCount` 参数，调用方应当意识到线程数只在首次调用时确定。第四，线程池工作线程缺乏异常保护。第五，AVX2 的运行时检测仅在 MSVC 下启用，而编译期开关需要在 qmake 中显式配置。第六，形状修改的多处容差与回退策略会静默改变结果，建议为回退路径增加计数或日志。第七，`ShapeModifyConfig` 未被使用，`OffsetShape` 只返回首条轮廓，这些接口语义需要在文档或代码中明确。第八，源码中若干 include 路径使用 `JaguarCore/Collision/...` 的大写形式，而实际目录为小写的 `include/JaguarCore/collision`，这在大小写敏感的文件系统上会导致编译失败，应统一路径大小写。第九，`Quadtree.cpp` 529 行存在 `}void` 的格式错误，虽不影响编译，但容易掩盖函数边界。

在与 Rust 参考的对照中，碰撞检测主流程、包含真值表、象限约束、半相交分类以及代理的 poles 使用基本一致；主要分歧集中在 C++ 对 `constrict` 的宽容处理、包围盒注册的缺陷、以及形状修改部分的回退与容差策略。阅读时可以把这些差异作为重点核对项。
