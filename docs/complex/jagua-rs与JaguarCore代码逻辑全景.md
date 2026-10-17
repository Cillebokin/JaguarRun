# jagua-rs 与 JaguarCore 代码逻辑全景

## 一、两个项目的关系与分层

JaguarCore 是 jagua-rs 的 C++ 移植，两边的分层基本一一对应：几何原语，实体层（Container、Item、PlacedItem、Layout），碰撞检测引擎（四叉树加 hazard），快速失败代理（Surrogate），问题模型（SPP、BPP、MSPP），最外层的 IO（Importer、JsonCodec、SVG 导出）。两者的区别在于求解器。Rust 工作区的 `lbf` crate 承担了采样与局部搜索等求解逻辑，C++ 侧目前只包含数据模型与碰撞内核，不含求解器。JaguarCore 的正确性主要靠与 Rust 的对拍来保证，而对拍机制目前是人工生成的快照加字面量，不是在线比对（详见第七节的分析）。`SparrowCore` 与 `CodeResource/sparrow` 不在本文范围内。

## 二、端到端的主数据流

一次完整求解的数据流可以按如下顺序理解。外部 JSON 先被反序列化为 `ExtInstance`，之后 Importer 完成三件事：物品预变换（质心平移到原点），按最小间距 `s` 做膨胀（物品向外 `s/2`，容器外轮廓向内 `s/2`），以及把离散或步进旋转展开为具体角度列表。随后 `OriginalShape::ConvertToInternal` 依次执行预变换、偏移、化简、窄凹陷闭合与再次化简，得到用于碰撞的内部 `SPolygon`，并为物品生成快速失败代理。Problem（如 SPP）持有 Instance 与 Layout，`Layout::PlaceItem` 把放置物注册为 CDE 中的动态 hazard。

求解时（以 Rust `lbf` 为例），对每个待放物品，先在容器的膨胀外接正方形内做均匀采样，再以当前最优为中心做局部正态扰动。每个候选位姿先用 surrogate 做快速拒绝；通过后，用 `transform_from` 计算包围框损失 `10·x_max + y_max`；只有损失严格更优时才调用精确的多边形碰撞检测。BPP 采用 first-fit：先尝试所有已打开的 layout，再尝试仍有库存的箱型。输出阶段通过 `save` 生成快照，再由 JSON 与 SVG 导出。

## 三、碰撞检测的核心约定

碰撞检测的几个约定贯穿全部代码，理解它们是读懂其余模块的前提。第一，hazard 分为两类语义：`Interior` 类（已放置物、洞、低质量区）只要形状与其相交或互相包含即为碰撞；`Exterior` 类（容器外部）则要求形状完全位于其内部，否则即为碰撞。第二，四叉树是自适应的，每个节点对每个 hazard 记录三种状态之一：`None`、`Partial`（只与节点相交，并携带节点内相关的边子集）或 `Entire`（节点完全位于 hazard 内部）。子节点的 hazard 列表是父节点的子集。第三，形状与 hazard 之间的包含关系通过代表点（`poi`，即最大内切圆的圆心）测试，前提是边与边不相交。第四，`HazardFilter` 既是过滤器也是收集器，用于忽略特定 hazard（自身、低于质量要求的质量区等），过滤逻辑正是下文第六节第一条所涉及的部分。

## 四、放置与搜索的其他要点

`Layout` 是一个带代数号的 SlotMap 式结构。句柄的代数奇偶性承载占用状态，占用为奇、空闲为偶，因此已删除的句柄会被识别为失效。`save` 与 `restore` 通过 `Arc::ptr_eq` 比较 `base_cde` 判定是否同源，只有同源才能增量恢复，否则需要用 `from_snapshot` 重建。SPP 的宽度变化会触发 `swap_container`，从而重建 CDE，这一步的代价不能忽略。

LBF 的预算切分为 `n_samples × ls_frac` 用于局部搜索，其余用于均匀采样，默认 `n_samples = 5000`，`ls_frac = 0.2`。局部搜索的平移标准差从容器外接正方形最大边长的 1% 指数衰减到 0.05%，旋转标准差从 2° 衰减到 0.5°。

## 五、不变式清单

以下几条不变式在代码中被隐式依赖，修改任何相关逻辑前必须确认它们仍然成立。四叉树中子节点的 hazard 集合必须是父节点的子集，否则删除时的递归剪枝会出错。`Partial` 与 `Entire` 的相邻象限不能同时出现 `None` 与 `Entire` 邻居，否则 Rust 版会触发 `unreachable!`。`PlacedItem` 的 entity 必须与 CDE 中的动态 hazard 一一对应。快照与 layout 必须同源。SlotMap 或 generation 的奇偶语义不能被破坏。

## 六、已核实与待核实的问题

本文区分源码可直接确认的实现事实、由实现推导的风险和待运行验证的行为。文中 Rust 路径主要指向 CodeResource/jagua-rs 本地镜像；此前记录该镜像与 JaguarCore/tools/RustOracle 锁定的 Git revision f5e2b5f 存在文件差异。oracle 配置已统一不代表旧分析和 fixture 已自动按该 revision 更新；凡依赖 Rust 行为的结论，应注明参考 revision，并在 E4 完成后复核。

B4a 已按 Cargo.lock 锁定的 jagua-rs `f5e2b5f` / Rust 1.98.1 复核。最小合法输入确认：虚拟根中的 Hole/`Entire` 若未被过滤，两侧边查询均早退返回 true；若 `HazKeyFilter` 忽略该 Hole，Rust 先在边查询中跳过它，再在 containment pass 的无条件 `unreachable!()` panic，C++ 仅处理 `Partial` 并返回 false。C++ 的 `NodeCollidesWithEdge` `Entire → true` 对应的是 Rust 第一阶段，不是出问题的 containment 分支。该输入证明路径可达，但不证明实际排样中常见。因 filter 的契约是忽略指定 hazard，保留 C++ false 处理，不传播 Rust panic，并将其登记为 pinned Rust 的已知兼容性差异；见 B4a 执行记录。

其二，pinned `f5e2b5f` 中 `from_irrelevant_qzones` 把 `quality < required_quality` 标为无关，虽与函数 API 注释、`container.rs`/`item.rs` 契约及 SVG 的阈值方向相反。项目按 D5 保留锁定谓词；SparrowCore callback 与现有阈值断言一致。维护版语义差异登记为 R8，见 B4b 碰撞记录。

其三，SvgExport.cpp 第 146 行的 flipTranslation 为 viewY + viewHeight，第 156–157 行变换为 translate(0 F) scale(1 -1)。按坐标变换公式，viewY 不为 0 时现有平移量可能产生视口垂直偏移；推导得到的候选平移量为 2·viewY + viewHeight，但尚未通过 SVG 渲染结果确认。应以非零 viewY 的输入运行渲染回归后再定性。

其四是大小写目录副本。C++ include 引用使用大写路径，当前仓库同时存在 Include/include 与 Tests/Differential/differential 副本，且 .pro 所引用的大写路径存在；因此不能仅凭大小写差异断言 Linux、macOS 或大小写敏感 CI 必然构建失败。应比较副本内容、核对 qmake/CMake 的 include root，并明确单一权威目录；副本漂移才是已确认的维护风险。

待核实的问题较多，以下条目仍需按目标 revision 与最小用例验证。窄凹陷闭合 close_narrow_concavities（shape_modification.rs:446-449）的面积条件与注释、vertices[i..j] 与删除 i..=j 是否有差一问题；平行/共线边与自相交检测的关系；piers.rs:57 的 forfeit_distance 量纲；debug_assertions 对 piers 参数的影响；同心圆 bounding_circle 的 NaN 边界；BPP 未放置物品的处理；Layout 与 MSPProblem 的失败原子性；无布局 Density 的 NaN；JSON、SVG、MSPP 边界。另有 C++ 导出的 bounds-only RegisterHazard 扩展：仓内无此重载调用；它用对角 Edge 只在根节点登记，完全落入子节点的 Query 可能漏项。该路径已按 B5 登记为非 parity 扩展，未做运行用例。除明确标注为源码事实或源码推导的项目外，这些均不应仅凭静态阅读写成已复现缺陷。

本文结论按静态源码事实、源码推导的风险和已复现行为区分。10 月 16 日的初始文档修订没有运行构建或测试；10 月 17 日已按 Rust 1.98.1 / pinned `f5e2b5f` 复核 B2、B4a，并完成 B4b 质量方向静态核对。B4a 的 Rust Debug/Release oracle 与 C++ MSVC 2019 x64 定向碰撞 runner 均通过；完整 `JaguarCoreTests.exe` 未运行。B4b 已按 D5 保持 pinned `f5e2b5f` 的实际过滤谓词，SparrowCore callback 与现有阈值断言一致；维护版 `quality >= min_quality` 方向作为 R8 语义残差记录，不改变锁定基线行为。B5 已静态核实并登记 bounds-only C++ 导出扩展；其子节点查询漏项为源码推导，未运行专用用例。10 月 16 日 SparrowCore 与 SparrowCli 的 MSVC 执行记录由用户提供，不覆盖 JaguarCore 全量验证。


---

# 附录 A：jagua-rs 几何与 IO


本文基于 jagua-rs 0.9.0 源码，覆盖 `geometry/`、`io/`、`util/` 以及 `lib.rs` 与 feature 配置。文中路径默认相对于 crate 源码根 `jagua-rs/src/`，根目录下的 `Cargo.toml` 指工作区根 `CodeResource/jagua-rs/Cargo.toml`。

## 一、数据结构与坐标约定

几何的基本单元是 `Point`、`Edge`、`Circle`、`Rect`（`geometry/primitives/`），由它们组成 `SPolygon`。`SPolygon` 是无洞简单多边形，内部按逆时针存储，并持有若干派生量：`bbox`、`area`、`diameter`（凸包上最远两点的距离）、`poi`（最大内切圆，即 pole of inaccessibility 的近似）以及可选的 `surrogate: Option<SPSurrogate>`（`geometry/primitives/simple_polygon.rs:23-37`）。`SPSurrogate` 持有 `poles`、`piers`、凸包索引与面积，以及由配置决定的 pole 前缀长度 `n_ff_poles`（`geometry/fail_fast/sp_surrogate.rs:13-30`）。

变换分两层。`DTransformation` 是“先旋转、后平移”的刚体变换，存储角度（弧度）与平移量，用于对外和存储（`geometry/d_transformation.rs:8-16`）。`Transformation` 是 3×3 齐次矩阵，元素为 `NotNan<f32>`，用于运算（`geometry/transformation.rs:13-15`）。二者通过 `From` 互转，`decompose` 用 `atan2(m10, m00)` 取角、用 `(m02, m12)` 取平移（`transformation.rs:98-103`）。这种分解只对刚体矩阵正确，库的文档也只承诺刚体变换，不支持缩放与反射。

`OriginalShape`（`geometry/original_shape.rs:15-20`）保存输入原始多边形、预变换 `pre_transform`、修改模式 `ShapeModifyMode` 与修改参数，并通过 `convert_to_internal` 生成用于碰撞检测的内部 `SPolygon`。物品的预变换是把质心平移到原点（`io/import.rs:85`、`264-267`），容器则不做预变换（`import.rs:129`）。物品的修改模式为 Inflate，容器外轮廓为 Deflate（`import.rs:88`、`130`）。

所有坐标与单位沿用输入文件的单位，类型统一为 `f32`。精度策略有三层。第一，矩阵元素用 `NotNan` 包装，NaN 在构造时即 panic，从而可以实现 `Hash` 与 `Eq`。第二，`util/fpa.rs` 中的 `FPA` 基于 `float_cmp::approx_eq!`，容差是绝对量与 ulp 量的组合，对不同量级的坐标表现不同，具体默认参数以所用 `float_cmp` 版本为准。第三，多数碰撞判断，如 `Rect::collides_with` 与 `Circle::collides_with`，都是直接的 `f32` 比较，不带容差；`AlmostCollidesWith` 特征（`geometry/geo_traits.rs:14-19`）专门用于“宁可误报”的场景。

## 二、基础图元

`Point`（`primitives/point.rs`）是 `(f32, f32)` 的新类型。其变换公式（`point.rs:26-31`）是列向量 `M·(x, y, 1)ᵀ`，与 `Transformation` 的约定一致。`Point` 手写了 `Hash`（按 `to_bits`）与 `Eq`（`point.rs:57-66`），但 `f32` 的 `==` 认为 `0.0 == -0.0`，而它们的位模式不同，因此 `Hash` 与 `Eq` 不一致。`SPolygon::new` 与 `import_simple_polygon` 中用 `unique()` 检测重复点，会漏掉 `0.0` 与 `-0.0` 的重复。

`Edge`（`primitives/edge.rs`）是有向线段。`try_new` 拒绝退化边（`edge.rs:16-19`），但字段是公开的，`Circle::bounding_circle` 等处直接构造，不保证非退化。核心算法是 `edge_intersection`（`edge.rs:193-243`）：先做包围盒快速拒绝，再用参数化公式解出 `t`、`u`，两者都在 `[0,1]` 内则相交，交点为 `P = end1 + t(start1 − end1)`。这里有一个需要注意的行为：两线段平行（分母为 0）时直接返回不相交（`edge.rs:226-229`），因此共线重叠的线段不会被判为相交，这直接影响了 `SPolygon::find_self_intersection`（`simple_polygon.rs:205-216`）对折返式自相交的检测。另外分母判零使用精确的 `== 0.0`，没有容差。`closest_point_on_edge`（`edge.rs:70-94`）把投影参数夹紧到 `[0,1]`，退化时返回起点。

`Rect`（`primitives/rect.rs`）是轴对齐矩形，要求 `x_min < x_max`（`rect.rs:22-33`）。`relation_to` 精确比较，`almost_relation_to` 用 `FPA` 比较（`rect.rs:47-91`）。`CollidesWith<Rect>` 与 `CollidesWith<Point>` 为闭区间判断，触碰即相交（`rect.rs:282-304`）。`CollidesWith<Edge>`（`rect.rs:317-362`）先做包围盒拒绝，再检查端点是否在矩形内，最后用四个角相对于线段所在直线的叉积符号做分离轴判断：全正或全负则不相交。由于判断用的是严格不等号，角点恰在直线上时保守地判为相交。`quadrants`（`rect.rs:147-157`）按笛卡尔象限顺序切分，`QUADRANT_NEIGHBOR_LAYOUT`（`rect.rs:141`）描述相邻关系，由 `util/assertions.rs:235-260` 校验。

`Circle`（`primitives/circle.rs`）要求半径有限且非负（`circle.rs:22-33`）。与矩形相交时，把圆心夹紧到矩形得到最近点，再比较距离平方与 `r²`（`circle.rs:118-131`）；与线段相交则判断 `edge.sq_distance_to(center) <= r²`（`circle.rs:112-116`）。圆与圆的 `separation_distance` 相交时返回深度 `(r1+r2) − d`，分离时返回 `d − (r1+r2)`（`circle.rs:187-198`）。`Circle::sq_distance_to(Point)` 返回的是“到圆周距离的平方”，与 `Point::sq_distance_to` 的语义不同，使用时要注意。

`Circle::bounding_circle`（`circle.rs:36-58`）是顺序合并算法，每个新圆若不被包含，就用两圆外切直径构造新圆。它不是最小外接圆。它还有一个缺陷：当当前圆与新圆同心且新圆更大时，`Edge{start: c, end: c}` 的长度为 0，`extend_at_front` 中的 `d / l` 得到 `inf`，`0 * inf` 得到 NaN，结果圆心变为 NaN 而不报错。仓库内部没有调用该函数，但它是公共 API。

## 三、SPolygon：构造、面积与点包含

`SPolygon::new`（`simple_polygon.rs:41-74`）依次执行：点数至少为 3；`unique()` 检查重复点；`find_self_intersection` 做 O(n²) 的非相邻边两两相交检测；鞋带公式求面积，面积为 0 则拒绝，为负则翻转点序。之后计算直径、包围盒与 poi。`calculate_area`（`simple_polygon.rs:147-160`）的累加项 `(y_i + y_j)(x_i − x_j)` 与标准鞋带公式符号相反，因此结果对逆时针为正。`calculate_diameter`（`simple_polygon.rs:115-128`）先求凸包再两两取最大距离，这是正确的，因为直径端点必在凸包上。`centroid`（`simple_polygon.rs:184-203`）使用标准形心公式，分母依赖 `area > 0` 这一前提。

点包含判断（`simple_polygon.rs:279-315`）采用射线法，射线水平向右，终点取包围盒右侧之外的一点。若某边的端点与查询点同高且位于其右侧，则只有当该边位于射线下方时才计数（`simple_polygon.rs:300-305`），这样能正确处理穿过顶点与触碰顶点两种情形。代价是点恰在边界上时结果不确定，代码没有承诺边界归属。`DistanceTo<Point>` 在内部返回 0，外部取所有边的最近距离（`simple_polygon.rs:317-331`），其中 `partial_cmp().unwrap()` 遇到 NaN 会 panic。

`transform` 与 `transform_from`（`simple_polygon.rs:219-277`）对顶点、poi 与 surrogate 施加变换并重算包围盒，面积与直径在刚体变换下保持不变。`transform_from` 从 reference 复制而非原地修改，避免重新分配。需要注意两处不对称：`transform_from` 在 `self` 有 surrogate 时会对 `reference.surrogate()` 调用 `expect`，reference 没有 surrogate 就会 panic，而 `transform` 对 `None` 是安全的；另外 `zip` 在长度不一致时静默截断。`generate_surrogate`（`simple_polygon.rs:76-83`）仅在配置变化时才重建。

## 四、凸包与形状修改

`convex_hull_from_points`（`convex_hull.rs:33-53`）是 Andrew 单调链算法，`sort_by_key` 只按 x 排序（`convex_hull.rs:37`），同 x 点的顺序依赖输入。在常见情形下结果正确，但标准写法应当按 `(x, y)` 字典序排序，这一点建议改进。返回的点序为逆时针。

`shape_modification.rs` 是模块中最复杂的部分，包括三类操作。

`simplify_shape`（`shape_modification.rs:52-152`）逐步删除顶点。每轮先把顶点分为凹、凸、共线三类（`CornerType::from`，`shape_modification.rs:320-336`，依据叉积符号）。Inflate 模式下，删除凹顶点相当于用弦填补凹袋，形状变大；删除共线顶点不改变面积；相邻两个凸顶点可以用两条外延线段的交点替换（`calculate_intersection_in_front`，`shape_modification.rs:257-287`）。Deflate 模式通过 `corners.reverse()` 与 `Corner::flip` 翻转角的方向（`shape_modification.rs:80-86`），使同一套分类对收缩成立。候选按面积变化排序，取第一个通过 `candidate_is_valid` 的候选（`shape_modification.rs:184-216`），该检查要求新边与所有不涉及被删顶点的原边都不相交。面积变化相对 `original_area` 累计计算（`shape_modification.rs:121-122`），超过 `max_area_change_ratio` 即停止。`execute_candidate`（`shape_modification.rs:232-239`）中对索引的修正处理了环绕情形，我用 n=5 的例子手工核对过 i=0 与 i=1 两种情况。该算法最坏复杂度约为 O(n³)。

`offset_shape`（`shape_modification.rs:340-379`）借助 `geo_buffer::buffer_polygon_rounded` 做圆角偏移，计算在 f64 下进行，结果经 `import_simple_polygon` 重建。Inflate 为正偏移，Deflate 为负偏移。若结果有多个连通分量，代码只取第一个并打印警告（`shape_modification.rs:358-367`），这意味着悄悄丢弃了其余部分，属于语义损失。

`close_narrow_concavities`（`shape_modification.rs:384-519`）枚举所有非相邻顶点对 `(i, j)`，把连接二者的弦缩短为 0.9999 倍作为候选。过滤条件包括：弦长不超过 `cutoff_distance_ratio · diameter`；Inflate 时两端都不在形状内、Deflate 时两端都在形状内；弦与任何原边不相交；被跳过的子多边形面积符号与绝对值满足约束。最后选择删除顶点最多的候选，循环直到无候选。这里有两处我认为需要核实的疑点。

第一处，面积符号判断（`shape_modification.rs:446-449`）对两种模式都只接受负面积，即 `if sub_shape_area >= 0.0 { continue }`，但注释（`shape_modification.rs:437`）写的是 Inflate 取负、Deflate 取正。按鞋带符号，向内的凹袋对应负面积，向外的凸包对应正面积。因此按源码逻辑推断，Deflate 模式会去填补凹袋，使形状变大而非变小。`shape_modification_valid` 作为 `debug_assert` 可能在调试构建中捕获这一问题，发布构建则不会。

第二处，子多边形取的是 `vertices[i..j]`（不含 `j`），闭合边为 `j−1 → i`（`shape_modification.rs:439-444`），而实际被替换的是 `i..=j` 的链，闭合边为 `j → i`，且删除逻辑是 `drain(i+1..=j−1)`（`shape_modification.rs:485-498`）。两者相差三角形 `(i, j−1, j)`，属于典型的 off-by-one，建议用单元测试验证。

`shape_modification_valid`（`shape_modification.rs:521-550`）只检查原多边形中不在新形状顶点集合里的那些顶点的位置，是抽样式的启发式检查，并不证明整体包含关系，容差为 `diameter · 1e-6`。`OriginalShape::convert_to_internal`（`original_shape.rs:23-52`）给出完整流水线：预变换、偏移、简化、窄凹闭合、以 `tolerance / 10` 再次简化。第二次简化的容差缩小为十分之一，这是未在文档中说明的经验设定。`debug_assert` 只比较了简化前后，没有覆盖偏移与闭合。

## 五、Transformation 的组合规则

`transformation.rs` 中有四个基本矩阵构造（`transformation.rs:130-169`）。`rot_transl_m` 表示先旋转后平移，即 `T·R`，与 `DTransformation` 的语义一致；`transl_rot_m` 表示先平移后旋转，即 `R·T`，其平移列为 `R·(tx, ty)`，与代码中的 `h*cos − k*sin`、`h*sin + k*cos` 一致。

实例方法的组合语义容易混淆。`rotate`、`translate`、`rotate_translate` 等都是把新变换左乘到已有矩阵上，即 `M_new = M_op · M_self`（`transformation.rs:40-66`）。因此 `Transformation::empty().translate(t).rotate(a)` 得到的是 `R·T`，即**先平移后旋转**。而 `transform(&other)` 是 `other · self`（`transformation.rs:68-73`），即先执行 `self` 再执行 `other`。`piers.rs:118-119` 的 `translation.rotate(angle)` 就属于前一种语义：先沿 x 平移，再绕**原点**旋转。这个设计只有射线族中心位于原点时才正确，而物品经过居中预变换后质心在原点，因此成立。但 `convert_to_internal` 中的偏移与简化会使内部质心轻微偏移，使覆盖变得不均匀。`clip` 中的 `assert!`（`piers.rs:127`）有一定余量，因为射线端点距原点约 2S，而包围盒半对角线不超过约 0.71S，S 为扩展正方形的边长。

`inverse` 是通用 3×3 求逆（`transformation.rs:196-223`），对刚体矩阵行列式恒为 1，数值上没有问题。`is_empty` 用精确比较判断单位阵（`transformation.rs:88-90`）。`normalize_rotation`（`d_transformation.rs:76-85`）把角度归一到 `[0, 2π)`。

`io/import.rs` 与 `io/export.rs` 的两个换算函数体现了这一约定。`ext_to_int_transformation` 计算 `E · P⁻¹`（`import.rs:274-285`），即先撤销预变换，再施加外部变换。`int_to_ext_transformation` 计算 `I · P`（`export.rs:37-48`），即先施加预变换，再施加内部变换。两者互逆。

## 六、Fail-fast 代理形状

代理形状用少量圆与线段近似多边形内部，供快速失败检测使用。`SPSurrogate::new`（`sp_surrogate.rs:35-72`）依次计算凸包索引与面积，调用 `generate_surrogate_poles` 生成 pole，按 `ff_pole_area_ratio` 选取前缀，再以前缀 pole 作为约束生成 piers。

`compute_pole`（`pole.rs:54-79`）用四叉树分支界定搜索最大内切圆。根节点是包围盒的最小外接正方形（`pole.rs:55`）。每个节点以其中心为圆心，`distance` 为中心到多边形边界以及到已有 pole 边界的带符号距离（中心在多边形外或落在已有 pole 内时取负），`radius` 为节点半对角线。若 `distance + radius` 不超过当前最优则剪枝（`pole.rs:68`）。带符号距离近似 1-Lipschitz，因此剪枝是安全的。搜索队列是 `VecDeque` 的 FIFO，并非 polylabel 那样的优先队列，剪枝效率较低，但结果仍正确。`MAX_POI_TREE_DEPTH = 10`（`pole.rs:81`）决定空间分辨率约为边长的 1/1024。返回的圆一定合法，因为其中心处的距离已被精确计算，但未必是全局最优。

`generate_surrogate_poles`（`pole.rs:14-49`）循环生成 pole，直到满足 `n_pole_limits` 中的某个约束。约束形如 `(n, coverage)`，覆盖率超过 coverage 时该约束生效，取生效约束中 n 最小者为上限。若某覆盖率下没有任何约束生效，循环会持续到 1000 个 pole 时触发 `assert!` panic。因此配置中应始终包含形如 `(n, 0.0)` 的兜底约束，`lbf/src/config.rs:35` 的默认值 `[(100, 0.0), (20, 0.75), (10, 0.90)]` 满足此条件。

`n_ff_poles`（`sp_surrogate.rs:47-59`）是使累计面积首次达到 `ff_pole_area_ratio · area` 的最小前缀长度，若达不到则取全部。piers 只针对该前缀生成（`sp_surrogate.rs:60-62`），以便覆盖 fail-fast 阶段中 pole 未覆盖的区域。`ff_poles()` 返回前缀，而 `ff_piers()` 返回全部 piers（`sp_surrogate.rs:76-83`），两者范围不一致，需要注意。

`generate_piers`（`piers.rs:25-94`）是贪心过程。它在扩展正方形包围盒上生成 `N_ANGLES` 个角度与 `RAYS_PER_ANGLE` 个平移量的射线族（`piers.rs:97-122`），再用 `clip` 把射线裁剪为形状内的线段（`piers.rs:125-152`）。裁剪方法是求射线与所有边的交点，按距起点排序后两两配对。顶点交点会在相邻两条边中各出现一次，形成 `(p, p)`，被 `start == end` 过滤，奇偶得以保持。结果段再以 `CLIPPING_TRIM = 0.999` 缩短，保证端点严格位于内部。随后在包围盒上取网格点，保留落在形状内且不在已选 pole 内的点（`piers.rs:154-173`）。每轮对每条候选射线计算损失：每个待覆盖点的平方距离，其中只有与射线距离小于影响半径 `ACTION_RADIUS_RATIO · width`（0.1 倍）时才计入射线，否则只计 pole（`piers.rs:175-205`）。损失最小的射线被选中。

这里有两个值得注意的地方。其一，`forfeit_distance` 定义为 `bbox.width() * bbox.height()`（`piers.rs:57`），它是**面积**而非长度，用作“无穷远”哨兵。对细长或很小的形状，哨兵值可能小于真实距离，使结果偏差。应改为长度量纲，例如包围盒对角线。其二，参数由 `cfg!(debug_assertions)` 决定（`piers.rs:16-18`）：调试构建为 10 条/角、4 个角度、10×10 网格；发布构建为 200、90、100×100。根 `Cargo.toml` 的 `[profile.test]` 设置了 `debug-assertions = true`，因此测试也会使用小参数。同一输入在不同 profile 下产生的 piers 不同，做结果对比时必须注意。默认 `lbf` 配置中 `n_ff_piers: 0`（`lbf/src/config.rs:37`），即默认流程不使用 piers，影响较小。

## 七、IO：外部表示、导入与导出

`io/ext_repr.rs` 定义了所有可序列化的外部结构，包括 `ExtItem`、`ExtContainer`、`ExtShape`（`Rectangle`、`SimplePolygon`、`Polygon`、`MultiPolygon` 四种，`ext_repr.rs:100-114`）、`ExtSPolygon(Vec<(f32, f32)>)`、`ExtLayout`、`ExtPlacedItem`，以及角度单位为度的 `ExtTransformation`（`ext_repr.rs:162-184`）。`ExtItem` 手写了 `Deserialize`（`ext_repr.rs:43-81`），用 `reject_legacy` 拒绝旧字段 `allowed_orientations`、`allowed_rotations` 等，并给出迁移提示。`ExtOrientation` 与 `ExtRotation` 使用 `deny_unknown_fields`，三种模式为 `discrete`、`stepped`、`continuous`。

`Importer`（`import.rs:17-198`）持有 `ShapeModifyConfig` 与 `CDEConfig`。`with_min_item_separation` 把最小间距 `s` 转化为 `offset = s/2`（`import.rs:47-54`）：物品向外膨胀 `s/2`，容器外轮廓向内收缩 `s/2`，因此物品之间以及物品与容器边界之间的间距都是 `s`。容器洞与品质区同样按 Inflate 处理（`import.rs:184-190`），与物品的间距也为 `s`。

`import_simple_polygon`（`import.rs:247-260`）的流程是：去掉与首点重复的末点；用 `eliminate_degenerate_vertices` 删除连续近似重复的点（`import.rs:287-311`，使用 `approx_eq!`）；检查非连续重复点；最后交给 `SPolygon::new`，后者会统一朝向。`import_rotation`（`import.rs:200-245`）把三种模式展开为 `RotationRange`。`Discrete` 取模 360、排序去重；`Stepped` 要求 `360/step` 四舍五入的整数 `count` 满足 `count·step ≈ 360`，容差为 `360·f32::EPSILON`（`import.rs:224-230`），然后生成 `i·360/count`；若结果只剩 `[0]`，返回 `RotationRange::None`。

导入中有若干 panic 式的错误处理值得注意。`import_container` 用 `assert!` 检查品质等级（`import.rs:108-111`），对 Polygon 类型的品质区与 MultiPolygon 使用 `unimplemented!`（`import.rs:145`、`165-168`）。这些都会直接 panic 而非返回错误，若输入不可信，需要在调用前校验。

`io/export.rs` 的 `export_layout_snapshot`（`export.rs:8-30`）把每个 `PlacedItem` 的内部变换换算回外部坐标，依赖 `item.shape_orig.pre_transform`。SVG 在 `io/svg/layout_to_svg.rs` 中。物品定义为 `<g id="item_{external_id}">`，放置时用 `<use transform="translate(tx ty), rotate(r)">` 引用（`layout_to_svg.rs:283-286`、`481-486`）。SVG 的变换从右到左作用，即先旋转后平移，与 `DTransformation` 一致。容器、品质区、代理形状、四叉树与碰撞线都可通过 `SvgDrawOptions` 开关（`svg/svg_util.rs:14-41`）。SVG 的 y 轴向下，输出图像与数学坐标系上下颠倒，但数值一致。`Color` 的 `From<String>` 对非法字符串直接 `unwrap`（`svg_util.rs:136-146`），而 `Deserialize` 会走这条路径，因此不可信的主题配置可能引发 panic。

## 八、util、lib.rs 与 feature

`util/assertions.rs` 中的检查函数仅用于调试，例如 `layout_qt_matches_fresh_qt`（`assertions.rs:98-111`）通过重建四叉树比对状态，`quadrants_have_valid_layout`（`assertions.rs:235-260`）检查象限邻接。`util/fpa.rs` 对 `float_cmp` 做了薄封装，其 `PartialOrd` 在近似相等时返回 `Equal`（`fpa.rs:24-32`），因此不满足严格全序的传递性，只适合近似比较。

`lib.rs` 的 `#![warn(clippy::pedantic, ...)]` 只影响 lint，不影响编译。`pub type Instant = web_time::Instant`（`lib.rs:34-35`）为 wasm 环境提供兼容的时间类型。顶层模块包括 `collision_detection`、`entities`、`geometry`、`io`、`util` 与 `probs`。`Cargo.toml` 中定义了 `spp`、`bpp`、`mspp` 三个 feature，它们只控制 `probs::spp`、`probs::bpp`、`probs::mspp` 的编译（`probs/mod.rs:1-11`），`geometry`、`io`、`util` 始终参与编译，且没有 `default` feature。`document-features` 会把 `##` 注释渲染为文档。

## 九、疑点与建议核实事项

本轮阅读中，以下几点值得团队核实或修复，按我判断的重要性排列。第一，`close_narrow_concavities` 的 Deflate 分支面积符号与注释不一致，可能导致形状变大；第二，同一函数中子多边形取 `i..j` 而非 `i..=j`，疑似 off-by-one；第三，`Edge::collides_with` 对共线重叠返回不相交，导致 `find_self_intersection` 无法检出折返自相交；第四，`piers` 的 `forfeit_distance` 量纲错误；第五，`piers` 参数随 `debug_assertions` 变化，结果不可跨 profile 比较；第六，`generate_surrogate_poles` 在缺少 `(n, 0.0)` 兜底约束时会在 1000 个 pole 处 panic；第七，`offset_shape` 静默丢弃多连通分量；第八，`Circle::bounding_circle` 在同心较大圆输入下产生 NaN；第九，`SPolygon::transform_from` 对 reference 缺少 surrogate 时 panic，而 `transform` 不会；第十，`Point` 的 `Hash` 与 `Eq` 对 `±0.0` 不一致；第十一，凸包仅按 x 排序。

以上多数结论来自源码阅读与推断，尚未通过运行验证。建议优先为 `close_narrow_concavities` 的 Deflate 分支、`Circle::bounding_circle` 的同心情形以及 `Edge::collides_with` 的共线情形各写一个最小用例，三者都可以在较短时间内被确认或排除。

# 附录 B：jagua-rs 碰撞检测与实体


范围与约定：本文覆盖 jagua-rs/src/collision_detection/**、jagua-rs/src/entities/** 及必要的 lbf 调用链。文中路径主要相对 CodeResource/jagua-rs 本地镜像；该镜像与 JaguarCore/tools/RustOracle 锁定的 Git revision `f5e2b5f` 存在文件差异，因此 Rust 行为结论以对应记录注明的基线为准。B2 与 B4a 已按 Rust 1.98.1 和该 pinned revision 复核；其他行为仍需逐项对照，不能由这两项结果外推。

## 一、数据结构总览与所有权

碰撞检测引擎 `CDEngine`（`cd_engine.rs:20-30`）包含四部分：`quadtree: QTNode` 根节点、`hazards_map: SlotMap<HazKey, Hazard>` 持有全部 hazard 的所有权、`config: CDEConfig`，以及私有缓存 `hkey_exterior`。四叉树节点并不持有 `Hazard` 本体，只保存 `HazKey`、`HazardEntity` 与 presence 的拷贝；几何形状以 `Arc<SPolygon>` 形式共享（`hazard.rs:16-24`）。因此 hazard 的生命周期由 `hazards_map` 决定，四叉树是它的一个可以随时重建的索引视图。

`Container`（`container.rs:14-24`）持有一个 `base_cde: Arc<CDEngine>`，它只包含静态 hazard（外部、洞、低质量区）。`Layout`（`layout.rs:16-23`）持有 `container`、`placed_items: SlotMap<PItemKey, PlacedItem>` 与私有的 `cde: CDEngine`。`Layout::new`（`layout.rs:27-34`）通过 `base_cde.as_ref().clone()` 深拷贝一份可变 CDE。所以所有权关系是：Container 通过 Arc 共享只读底板，每个 Layout 拥有自己的可变副本，CDE 拥有 hazard，四叉树只借用 key。`PlacedItem`（`placed_item.rs:106-113`）通过 `Arc<Item>` 共享物件定义，通过 `Arc<SPolygon>` 与 CDE 中对应的 hazard 共享同一份变换后形状。

## 二、hazard 的种类与 entity

`HazardEntity`（`hazard.rs:40-53`）有四种变体：`PlacedItem { id, dt, pk }`、`Exterior`、`Hole { idx }` 与 `InferiorQualityZone { quality, idx }`。`scope()`（`hazard.rs:55-66`）决定碰撞语义：`PlacedItem`、`Hole`、`InferiorQualityZone` 属于 `Interior`，即障碍是形状内部，只要待检测形状与其相交或互相包含即碰撞；`Exterior` 属于 `Exterior`，即碰撞条件是待检测形状不在容器内部。

容器构造在 `Container::new`（`container.rs:27-71`）中完成：先插入 `Exterior` hazard（第 55 行），再由 `InferiorQualityZone::to_hazards`（`container.rs:115-126`）把各质量区的每个多边形转为 hazard。质量为 0 的区域被视为 `Hole`。导入时容器洞被并入质量 0 的区域（`jagua-rs/src/io/import.rs:176`）。CDE 的根 bbox 取 `outer.bbox.inflate_to_square()`（`container.rs:61`），即容器外接正方形，这一点决定了后续很多边界行为。`Layout::place_item`（`layout.rs:93-105`）注册的 hazard 为 `dynamic = true`，静态 hazard 为 `dynamic = false`。

entity 的唯一性依赖于 `PItemKey`：它是带代际版本号的 slotmap key（`placed_item.rs:99-102`），删除后再次分配同一槽位时版本号会变化，因此旧 entity 不会误匹配新物件。`HazardEntity::PlacedItem` 还包含 `dt: DTransformation`，因此 entity 的相等比较包含 f32 变换参数。`register_hazard` 在调试模式下检查 entity 是否重复（`cd_engine.rs:60-63`）。

## 三、四叉树的构建、划分与存放

`QTNode`（`qt_node.rs:13-25`）包含 `level`（0 为叶，根为 `quadtree_depth`）、`bbox`、`children: Option<Box<[QTNode; 4]>>`、`hazards: QTHazardVec` 与 `cd_threshold`。`QTHazard`（`qt_hazard.rs:13-22`）是一个 hazard 在某个节点上的投影，包含 `qt_bbox`、`hkey`、`entity` 与 `presence`。`presence` 有三种取值（`qt_hazard.rs:26-33`）：`None` 表示完全不相交，`Partial(QTHazPartial)` 表示只与节点相交，并携带落入该节点的边子集 `edges` 与用于快速拒绝的 `ff_bbox`，`Entire` 表示节点完全位于 hazard 内部。

插入由 `QTNode::register_hazard`（`qt_node.rs:39-82`）完成。当节点没有子节点、`level > 0`、且新 hazard 为 `Partial` 时，节点会用 `bbox.quadrants()` 生成四个子节点（层级减一），并把节点上已有的所有 hazard 依次 `constrict` 到子节点上（第 61-76 行）。随后若存在子节点，新 hazard 同样被 constrict 到子节点，只有 presence 不为 `None` 的结果才被注册（第 77-80 行）。最后新 hazard 才加入本节点（第 81 行）。由此形成两个重要性质：节点上的 hazard 列表是所有与该节点相交的 hazard 的超集；子节点的列表是父节点列表的子集。树是自适应的，只有 `Partial` 的区域才会继续细分，完全落在 hazard 内或外的区域在该层停止，且 presence 的传播从根开始逐层进行。

切分的核心是 `QTHazard::constrict`（`qt_hazard.rs:50-169`）。对 `Entire` 的 hazard，四个子象限都直接复制为 `Entire`（第 60 行）。对 `Partial` 的 hazard，代码先检查 hazard 的整体 bbox 是否完全落入某个象限（第 67-70 行），若是则只有该象限保留原样，其余象限为 `None`（第 72-87 行）。否则进入一般情形：对每个象限收集与之相交的边（第 93-111 行），相交的象限生成 `Partial`，并通过 `QTHazPartial::from_parent` 得到缩小后的边集与 `ff_bbox`。对于没有任何边相交的象限（第 118-162 行），代码利用 `Rect::QUADRANT_NEIGHBOR_LAYOUT`（`rect.rs:141`）查看已经确定了 presence 的相邻象限：若邻居中有 `None` 则判为 `None`，有 `Entire` 则判为 `Entire`；若两者同时出现则触发 `unreachable!`（第 138-140 行）。两者都没有时，代码用象限中心点 `quadrant.centroid()` 测试 hazard 形状是否包含该点，再依据 `scope` 决定 `Entire` 或 `None`（第 143-152 行）。这一做法成立的前提是：无边象限必然整体位于 hazard 的内部或外部。

删除由 `QTNode::deregister_hazard`（`qt_node.rs:84-95`）完成。它先从本节点移除 hkey，若确实移除（`modified`）且剩余 hazard 中已经没有 `Partial`，则丢弃全部子节点（`no_partial_hazards`，`qt_hazard_vec.rs:73-78`）；否则递归到所有子节点。递归能够成立依赖一个不变式：某 hazard 若出现在子节点中，则必然出现在父节点中。如果父节点上没有该 hkey，代码直接不向下递归，这正是依赖该不变式的地方。另外，子节点只有在存在 `Partial` hazard 时才存在，因此“有子节点”与“有 Partial hazard”在 `level > 0` 时等价。

每个节点的 hazard 集合由 `QTHazardVec`（`qt_hazard_vec.rs:13-17`）保存，它按 presence 降序排列（`Entire` > `Partial` > `None`，见第 90-101 行），并维护 `n_active_edges`，即所有 `Partial` hazard 的边数之和。`add` 使用二分插入（第 24-43 行），`remove` 为线性查找（第 45-55 行）。`strongest`（第 61-64 行）返回第一个未被 filter 忽略的 hazard，由于排序，这一查询在通常情况下能很快停止。

## 四、四叉树的查询流程

四叉树有两类查询，它们的策略不同。第一类是 `collides`（`qt_node.rs:100-145`），做二值判断并返回一个命中的 entity。它先取 `strongest`，若为 `Entire` 则直接命中；若为 `Partial` 且存在子节点，则用 `QTQueryable::collides_with_quadrants` 算出与 entity 相交的子象限，再按 `quadrant_order` 给出的顺序递归，第一个命中即返回（第 112-124 行）；若没有子节点，则遍历相关的 `Partial` hazard，逐个调用 `QTHazPartial::collides_with`（`qt_partial_hazard.rs:245-251`），该函数先用 `ff_bbox` 快速拒绝，再遍历边。`collides` 不使用 `cd_threshold`，总是下探到叶子。第二类是 `collect_collisions_until`（`qt_node.rs:164-210`），用于收集所有命中的 hazard，并允许通过回调提前终止。它以 `n_active_edges <= cd_threshold` 决定是否停止下探（第 176 行）：边数足够少时，直接在本节点对所有 `Partial` hazard 的边做精确判断；否则只对与 entity 相交的子节点递归，并用 `.any` 实现短路（第 178-190 行）。已存在于 collector 中的 hkey 被跳过（第 194 行），因此同一 hazard 不会被重复报告。

entity 与象限的相交判定由 `QTQueryable` trait 决定（`qt_traits.rs:132-144`）。`Rect` 使用默认实现，`Circle` 实现了按圆心位置排序的 `quadrant_order`（第 146-170 行），使最可能命中的子节点先被检查。`Edge` 的 `collides_with_quadrants`（第 173-251 行）先用坐标范围与端点判断，若仍无法确定，则用 `half_intersect`（第 255-285 行）对象限的四条边和两条中线做边与边的相交测试，两条中线分别对应 `edge_intersection_half` 返回的前半段或后半段。这一过程是保守的：f32 计算可能误报，但不会漏报。

## 五、CDEngine 的入口与调用链

上层调用以 LBF 为例。`lbf/src/opt/search.rs` 中的 `search` 对每个采样变换先调用 `detect_surrogate_collision`（第 44、85 行）。这一步只对 `ff_poles`（`Circle`）和 `ff_piers`（`Edge`）调用 `quadtree.collides`（`cd_engine.rs:205-224`），是廉价的快速过滤。只有通过 surrogate 检测后，才会生成 `buffer` 并调用 `detect_poly_collision`（`search.rs:57`、`92`）做精确检测。`ff_poles` 只取前缀（`sp_surrogate.rs:76-78`），由 `ff_pole_area_ratio` 决定长度。需要完整碰撞集合的场景则使用 `collect_poly_collisions` 系列，例如 SVG 输出（`jagua-rs/src/io/svg/layout_to_svg.rs:356`）。

`detect_poly_collision`（`cd_engine.rs:161-198`）的流程如下。首先检查引擎 bbox 是否完全包含形状 bbox（第 162 行），若不包含则直接返回 `true`，即超出外接正方形视为碰撞。否则调用 `get_virtual_root`（第 370-383 行）从根节点出发，沿着完全包含形状 bbox 的子节点下降，得到“虚拟根”，从而跳过无关的上层节点。第一阶段（第 167-171 行）对形状的每条边调用 `collides`，任一命中即返回 `true`。第二阶段（第 174-191 行）遍历虚拟根上的 hazard，对 `Partial` 且未被 filter 忽略者调用 `detect_containment_collision`，处理形状包含于 hazard 或 hazard 包含于形状的情形。由于边不相交的前提已经满足，只需测试一个代表点（形状的 `poi`）即可判定整体包含关系。

`collect_poly_collisions_until`（`cd_engine.rs:273-321`）的流程与之相似，但有三处不同。第一，当形状不被 bbox 完全包含时，它先把 `Exterior` 插入 collector（第 283-290 行）。第二，它对每条边调用 `collect_collisions_until` 并累积结果，回调可在第一次命中后提前终止。第三，在包含检测阶段，`Entire` 被直接忽略（第 305 行），注释认为它已经被边检测覆盖，这对 collector 路径是成立的，因为该路径不使用 filter。`collect_surrogate_collisions_until`（`cd_engine.rs:343-365`）只检查 pole 前缀，文档明确要求其后续必须跟随完整的 poly 收集。

filter 与 collector 的关系需要特别留意。`HazardFilter` trait（`hazards/filter.rs:100-102`）只有一个方法 `is_irrelevant(hkey)`。实现包括：`HazKey` 本身（忽略自身 hazard，`filter.rs:145-149`，`Layout::is_feasible` 即用它，`layout.rs:160-168`）；`HazKeyFilter`（`filter.rs:105-142`），可由 `from_keys` 构造，也可由 `from_irrelevant_qzones` 按 `min_quality` 构造，后者被 `lbf_spp.rs:53-57` 与 `lbf_bpp.rs:128-131` 使用；`NoFilter`（`filter.rs:152-159`）；以及所有 `HazardCollector`（`filter.rs:163-169` 的 blanket impl）。也就是说，collector 同时充当输出容器与过滤器。`BasicHazardCollector`（`hazards/collector.rs:45-53`）以 `SecondaryMap<HazKey, HazardEntity>` 存储，并用一个 64 位的 bit 集合做快速否定判断（`collector.rs:75-77`、`106-108`）：bit 未置位即可确定 key 不存在，否则再查 map。该集合在删除后不清除 bit，因此可能产生误报，但不会漏报。

## 六、containment 判定的语义

`detect_containment_collision`（`cd_engine.rs:233-257`）先用 `almost_relation_to`（`rect.rs:72` 起，带 FPA 容差，边界情形偏向 `Surrounding` 与 `Enclosed`）判断两个 bbox 的关系。若 hazard 包围形状，则测试形状的 `poi` 是否位于 hazard 内；若 hazard 被形状包围，则测试 hazard 的 `poi` 是否位于形状内。随后依据 scope 解释结果：`Interior` hazard 只要任一方向上的包含成立即为碰撞；`Exterior` hazard 则在 `contained == true` 时视为不碰撞。这一解释对 `Surrounding` 分支是正确的，但对 `Enclosed` 分支值得怀疑，详见第九节。`poi` 的存在使得代表点测试能在 O(1) 次 `collides_with` 内完成，但它依赖 `SPolygon.poi` 必须位于多边形内部这一前提。

## 七、Layout 与 CDE 的同步

`Layout::place_item`（`layout.rs:93-105`）先把 `PlacedItem` 插入 `placed_items` 得到 `pk`，然后用 `(pk, pi).into()` 生成 entity，并与 `pi.shape` 的 Arc 共享地构造 `Hazard`，最后调用 `cde.register_hazard`。注意 `place_item` 本身不做碰撞检测，调用方必须事先检查。`Layout::remove_item`（`layout.rs:108-120`）先从 `placed_items` 删除，再以 entity 调用 `deregister_hazard_by_entity`。后者（`cd_engine.rs:72-85`）线性扫描 `hazards_map` 寻找 entity 相等的 hazard，复杂度为 O(H)。

`Layout::swap_container`（`layout.rs:46-55`）先保存动态 hazard 快照，然后用新容器的 `base_cde` 替换当前 CDE，再把动态 hazard 逐个重新注册。`placed_items` 保持不变，但代码不会检查这些物件是否仍然位于新容器内，这一责任留给调用方。

保存与恢复的逻辑如下。`Layout::save`（`layout.rs:59-65`）复制 `container`、`placed_items`（SlotMap 的 clone 保留原有 key 与版本号）以及 `cde.save()` 得到的动态 hazard 快照（`cd_engine.rs:99-107`，只保存 `dynamic` 的 hazard）。`Layout::restore`（`layout.rs:74-89`）首先用 `Arc::ptr_eq` 比较两者的 `base_cde`（第 75-80 行），不一致则返回 `ContainerMismatch`，状态保持不变。随后它 clone `placed_items` 并调用 `cde.restore`。`CDEngine::restore`（`cd_engine.rs:110-150`）对当前的动态 hazard 与快照中的 hazard 按 entity 相等做差分：当前有而快照没有的被 `deregister_hazard_by_key` 删除，快照有而当前没有的被 `register_hazard` 添加，两边都有的保持不变。由于 entity 中包含 `PItemKey` 的代际版本号，同一槽位的不同放置不会被混淆。

## 八、索引方式与性能相关的所有权问题

`HazKey` 与 `PItemKey` 都由 `slotmap::new_key_type!` 生成（`hazard.rs:9-12`、`placed_item.rs:99-102`）。SlotMap 的代际机制保证旧 key 在删除后失效；`Layout::remove_item` 对失效 key 直接 `expect` 并 panic（`layout.rs:108-112`）。四叉树使用 `HazKey` 而不是引用，从而避免自引用生命周期问题，代价是每次访问形状都要通过 `self.hazards_map[qt_hazard.hkey]` 做一次查找（`cd_engine.rs:182`、`308`），并且 `QTHazard.hkey` 与 `hazards_map` 之间必须始终保持一致。`hkey_exterior` 被缓存在 `CDEngine` 中（`cd_engine.rs:29`），因此它同样依赖这一一致性。

`Layout::new` 所做的深拷贝代价值得注意。`base_cde.as_ref().clone()`（`layout.rs:28`）会复制整棵四叉树，包括每个 `QTHazPartial` 的 `Vec<Edge>`（`qt_partial_hazard.rs:184-190`）。静态 hazard 越多、边越多，每个 Layout 的创建成本越高。`PlacedItem::new`（`placed_item.rs:117-126`）对 `item.shape_cd` 做 `transform_clone`，生成新的 `Arc<SPolygon>`，其 `transform` 会同时变换 `poi` 与 surrogate（`simple_polygon.rs:219-244`），因此 surrogate 的快速检测能直接使用变换后的副本。`Item::new`（`item.rs:59-82`）在构造时就生成 surrogate，之后 `Item` 本身通过 `Arc` 被共享。

## 九、疑点、隐含假设与风险

`detect_poly_collision` 的风险已在 B4a 通过 pinned revision 的运行用例确认可达：过滤掉 virtual-root 中的 Hole/`Entire` 后，Rust 的 containment pass 仍遍历该条目并执行 `unreachable!()`，C++ 返回 false。该结果表明此 Rust 路径的可观察性已经确认；`min_quality` 与 `lbf_spp.rs:53–57` 仍只是候选业务调用路径，不能据此说该失败在排样中常见。处置见 B4a 记录。

第二个已核对项是 `from_irrelevant_qzones`（pinned `filter.rs:20,31–35`）。实现把 `quality < required_quality` 的 key 标记为无关，但函数 API 注释、`container.rs:86` 的质量递增定义、`item.rs:23–24` 的最低质量要求和 SVG 对 `quality >= min_quality` 的处理指向达标区可用。维护版 `QualityZoneFilter` 也按 `quality >= min_quality` 忽略。项目按 D5 保留 pinned `f5e2b5f` 的实际谓词；SparrowCore 四处 callback 和现有阈值断言与其一致。该固定基线决定不代表谓词符合意图语义，维护版方向差异登记为 R8，见碰撞记录。

第三个疑点在 `detect_containment_collision` 的 `Enclosed` 分支（`cd_engine.rs:248`、`254-255`）。对 `Exterior` hazard，若容器 bbox 被形状 bbox 包围，则代码测试容器的 `poi` 是否在形状内，结果为真时视为不碰撞。但此时容器轮廓整体位于形状内部，形状显然超出了容器，应当碰撞。由于引擎 bbox 是容器的外接正方形，`Enclosed` 只可能发生在形状 bbox 在容器长边方向与容器完全重合、并在短边方向伸入正方形 padding 的边界情形中。这种情形不常见，但语义上与 `Surrounding` 分支不对称，应当补充测试。

第四个问题涉及象限分类的精度。`QTQueryable` 对 `Edge` 的分类（`qt_traits.rs:182-249`）使用 f32 计算，`quadtree/tests.rs:201-229` 专门记录了 bisector 附近 f32 误报的案例。误报保证了保守性（不会漏报碰撞），代价是多余的 Partial 节点与偶发的误拒绝。debug 构建中，`qt_traits.rs:242-247` 只检查 f64 oracle 的结果是否被保守地包含，不检查 release 下的行为。`edge_intersection_half` 在平行或共线时直接返回 `None`（`qt_traits.rs:301-304`），其正确性依赖端点判断与相邻象限的结论。`qt_hazard.rs:138-140` 的 `unreachable!` 同样依赖“相邻象限不会同时出现 `None` 与 `Entire` 邻居”这一不变式，而该不变式依赖于边与象限边界的相交判断在两侧保持一致。若将来修改分类逻辑，需要同时检查这两处。

第五个问题涉及 Entire 的 `qt_bbox`。`constrict` 对 `Entire` 直接 `self.clone()`（`qt_hazard.rs:60`），子节点的 `qt_bbox` 因此保留为祖先的 bbox，而不是子象限。`qt_bbox` 仅在 `debug_assert` 中使用（`qt_hazard.rs:54`），所以不影响正确性，但该字段的语义在 Entire 路径上并不可靠，后续修改时不应依赖它。

第六个隐含假设是 Exterior 不会落入单个象限。`qt_hazard.rs:67-87` 的 `Enclosed` 分支对 `Exterior` 不做 `Entire` 填充，只有当容器 bbox 完全落入某个象限时才会出错。由于根是 `inflate_to_square` 得到的正方形，容器 bbox 的长边等于根的边长，因此任何象限都容纳不下它，`Exterior` 不会进入该分支。但 `CDEngine::new` 是 pub 的，若以其他 bbox 构造，这一前提会失效，文档中并未声明。

第七个问题是不变式没有被类型系统保证。`Layout::placed_items`（`layout.rs:20`）以及 `CDEngine` 的 `quadtree`、`hazards_map`、`config`（`cd_engine.rs:23-27`）都是 pub 字段，外部代码可以绕过 `place_item`/`remove_item` 直接修改，导致 `placed_items` 与 CDE 不同步。`deregister_hazard_by_key`（`cd_engine.rs:87-96`）也没有禁止删除静态 hazard，删除 `Exterior` 后 `hkey_exterior` 会悬空，之后的 `collect` 路径会在索引时 panic。

第八个问题是调试断言的代价。`layout_qt_matches_fresh_qt`（`util/assertions.rs:98-111`）在每次 `place_item` 与 `remove_item` 后，都会从底板重新构建整棵四叉树（`layout.rs:102`、`117`），debug 构建下整体为 O(n²)。`qt_contains_no_dangling_hazards`（`util/assertions.rs:58-67`）同样在每次注册与删除后全树遍历（`cd_engine.rs:68`、`82`、`93`）。此外，`snapshot_matches_layout`（`util/assertions.rs:17-34`）以 `item.idx` 与 `d_transf` 比较而非 `pk`，断言偏弱。

第九个是性能细节。`Layout::is_feasible`（`layout.rs:160-168`）对每个物件调用 `haz_key_from_pi_key`（`cd_engine.rs:391-399`），后者线性扫描 `hazards_map`，整体为 O(n·H)，可以维护一张 `pk → hkey` 的 SecondaryMap 来替代。`collides` 忽略 `cd_threshold`，而 `collect` 使用它（`qt_node.rs:112` 与 `176`），两者下探深度不同，结果一致但性能特征不同。`detect_surrogate_collision` 直接从根节点查询（`cd_engine.rs:213`、`219`），没有利用 `get_virtual_root`，这在大容器上会有额外开销。每个经过的 Partial 节点都会复制一份边 Vec（`qt_hazard.rs:104-107`、`qt_partial_hazard.rs:198-238`），大物件在多层多节点上存储边副本，内存与边数成正比。`from_irrelevant_qzones` 每次放置尝试都会扫描全部 hazard 构建 `SecondaryMap`（`lbf_spp.rs:53-57` 位于 `while` 循环中），可以按 `min_quality` 缓存。

最后是几处次要的不一致。`InferiorQualityZone::new` 用 `assert!` 检查质量范围（`container.rs:96-99`），而 `Container::new` 用 `ensure!` 返回错误（`container.rs:34-44`）。`swap_container` 不检查物件是否仍在新容器内。`CDEngine::hazards()` 与 `hazards_map` 的 pub 访问方式也让外部能够绕过一致性约束。

## 十、建议的阅读顺序与验证

阅读时建议先看 `hazard.rs` 与 `cd_engine.rs` 的 `new`、`register_hazard` 与 `detect_poly_collision`，理解 hazard 的 scope 与虚拟根的意义；再看 `qt_hazard.rs` 的 `constrict`，理解每一层 presence 如何推导；然后看 `qt_node.rs` 的 `register_hazard` 与 `collides`/`collect_collisions_until` 的分支；最后看 `layout.rs` 的 `place_item`、`remove_item`、`restore`，以及 `util/assertions.rs` 中的调试断言，它们精确表达了作者对不变式的预期。第九节列出的几个问题中，第一、二、三项最值得尽快用测试确认，因为它们分别涉及 panic、语义方向与容器边界的正确性。

# 附录 C：jagua-rs 问题模型与 lbf


本文基于源码阅读整理，路径相对仓库根 `CodeResource/jagua-rs`，行号取自阅读时的版本。核心碰撞检测只读到调用边界。

## 一、总体结构

`jagua-rs/src/probs/` 下的 `spp`、`bpp`、`mspp` 三个子模块由 feature 开关控制，互不依赖。每个子模块都是四层结构：`entities/instance.rs` 保存不可变实例，`entities/problem.rs` 保存可修改的工作态，`entities/solution.rs` 保存快照，`io/` 负责与 `ext_repr.rs` 定义的外部 JSON 互转。`lbf` 只启用了 `spp` 与 `bpp`（`lbf/Cargo.toml`），`mspp` 只在 dev-dependencies 中出现，所以 MSPP 目前是"数据模型完成、求解器与解导入缺失"的状态。

三类问题都遵循"实例与工作态分离"：`Problem` 持有 `Layout` 与按物件类型记录的剩余需求 `item_demand_qtys`，需求由 `place_item`/`remove_item` 增减，而不是从 layout 反推。

## 二、三类问题的数据模型与状态转移

SPP 的 `SPInstance`（`spp/entities/instance.rs`）包含 `(Arc<Item>, demand)` 列表与 `base_strip`。`Strip`（`spp/entities/strip.rs`）是固定高度、可变宽度的条带，经 `From<Strip> for Container` 转为矩形外轮廓的 `Container`。导入时初始宽度为 `Σ(area×demand)/height`，即 100% 密度宽度（`spp/io/import.rs:43-57`）。`SPProblem`（`spp/entities/problem.rs:11-17`）持有 `strip`、`layout` 与 `item_demand_qtys`。`place_item`（第 61-67 行）先扣减需求，再由 `Layout::place_item`（`jagua-rs/src/entities/layout.rs:93-105`）把放置物登记为 CDE 中的 hazard；`remove_item`（第 69-79 行）为逆操作。`change_strip_width`（第 34-37 行）调用 `Layout::swap_container`（`layout.rs:46-56`），从 `base_cde` 重建 CDE 并重新注册动态 hazard。`fit_strip`（第 40-59 行）把宽度收缩到所有放置物 `bbox.x_max × 1.00001 + offset`。`save`/`restore`（第 81-114 行）基于 `LayoutSnapshot`，`Layout::restore` 要求快照与当前容器的 `base_cde` 为同一 `Arc`（`layout.rs:74-89`），宽度变化后只能走 `from_snapshot` 重建。

BPP 的 `Bin`（`bpp/entities/bin.rs`）带有 `stock` 与 `cost`。导入时按 `base.id` 而非成本排序并过滤零库存（`bpp/io/import.rs:36-58`）。`BPProblem`（`bpp/entities/problem.rs:18-25`）的核心是 `SlotMap<LayKey, Layout>`、`layout_bins` 以及 `bin_stock_qtys`。`place_item`（第 48-66 行）对 `BPLayoutType::Closed` 会新建 layout 并调用 `register_layout`，后者通过 `open_bin` 扣减库存；`remove_item`（第 69-80 行）在 layout 变空时关闭箱子并归还库存。`bin_cost`（第 203-208 行）由库存差值乘以箱型成本得到。

MSPP 的 `Strip`（`mspp/entities/strip.rs`）比 SPP 多一个 `max_width`，初始宽度直接取 `max_width`（`mspp/io/import.rs:38-48`）。`MSPProblem`（`mspp/entities/problem.rs:16-27`）用 `SlotMap` 存 layout，并用 `SecondaryMap<LayKey, Strip>` 存每个条带。`add_layout_from_strip`、`remove_layout`、`change_strip_width`（第 180-188 行）、`fit_strip(lk)`（第 57-76 行）分别管理条带。`extendable_strips` 与 `total_strip_width`（第 234-245 行）对应多条带的扩展与目标。

三类问题的 `save` 都带有 `debug_assert!(problem_matches_solution(...))`。BPP 的校验（`bpp/util/assertions.rs:7-27`）是 O(L²) 的嵌套遍历，而 `Cargo.toml` 的 `[profile.test]` 开启了 debug-assertions，测试中会真实付出这部分开销。

## 三、lbf 主流程

入口是 `lbf/src/main.rs:38-87`。它用 clap 解析 `Cli`（`lbf/src/io/cli.rs`，参数为 `-i -s -c -l -p bpp|spp`），配置缺省时使用 `LBFConfig::default()`（`lbf/src/config.rs:197-217`，`n_samples=5000`、`ls_frac=0.2`、`prng_seed=None`），然后用 `read_spp_instance`/`read_bpp_instance`（`lbf/src/io/mod.rs:267-277`）把 JSON 读成 `ExtSPInstance`/`ExtBPInstance`。

随后 `main_spp`（第 89-127 行）构造 `Importer`，按 `prng_seed` 选择 `SmallRng::seed_from_u64` 或 `rand::make_rng()`（第 100-103 行），调用 `spp::io::import_instance`（`spp/io/import.rs:12-60`）得到实例，再执行 `LBFOptimizerSP::new(...).solve()`，最后导出 JSON 与 SVG。`main_bpp`（第 129-169 行）结构相同，但导出时需要 `instance` 以解析箱的外部 id。

`LBFOptimizerSP::solve`（`lbf/src/opt/lbf_spp.rs:40-128`）先用 `item_placement_order`（`lbf/src/opt/search.rs:252-258`）按直径降序排列物件类型，然后对每个需求未满足的物件调用 `search`。若物件带 `min_quality`，则用 `HazKeyFilter::from_irrelevant_qzones` 构造过滤器（第 59-70 行）。搜索成功则 `place_item`；失败则把宽度乘以 1.1 并通过 `strip_width_is_in_check`（`lbf/src/util/assertions.rs:5-13`）检查（第 91-104 行）。全部放完后执行 `fit_strip` 并 `save`（第 108-114 行）。

`LBFOptimizerBP::solve`（`lbf/src/opt/lbf_bpp.rs:39-96`）的骨架相同，但每次放置前调用 `search_layouts`（第 99-144 行）。候选顺序是：先按 `layouts.keys()` 遍历所有打开的 layout，再遍历 `bin_stock_qtys>0` 的箱型，箱型用 `container.base_cde`（空箱 CDE）搜索。第一个成功的候选即被采用，这是 first-fit。若全部失败则 `break 'inner`（第 73 行）跳过该物件的剩余需求，且没有任何日志或错误。

候选搜索是核心，位于 `lbf/src/opt/search.rs:161-250`。预算在第 180-181 行切分：`ls_sample_budget = n_samples × ls_frac`，其余给均匀采样。均匀阶段（第 183-213 行）用 `UniformRectSampler`（`lbf/src/samplers/uniform_rect_sampler.rs`）在 `cde.bbox()` 内随机采样平移，旋转按 `allowed_rotation` 从连续区间、离散集合或固定值中选取。每个样本先用 `detect_surrogate_collision`（`jagua-rs/src/collision_detection/cd_engine.rs:205`）做快速拒绝，通过后用 `buffer.transform_from` 计算包围框损失 `LBFLoss`（`lbf/src/opt/loss.rs`，成本为 `10·x_max + y_max`）。只有损失严格更优时才调用精确的 `detect_poly_collision`（`cd_engine.rs:161`）。一旦接受新最优，`tighten_sample_bbox`（`loss.rs:163-171`）就把采样区域的 `x_max` 收紧到 `cost/10`。局部搜索阶段（第 216-249 行）以最优为均值，用 `LSSampler`（`lbf/src/samplers/ls_sampler.rs`）做正态扰动，平移标准差从容器包围框最大边长的 1% 指数衰减到 0.05%，旋转标准差从 2° 衰减到 0.5°，每次改进调用 `shift_mean`。若均匀阶段一无所获，第 216 行的 `?` 直接返回 `None`，局部搜索预算被整体跳过。

输出阶段，JSON 由 `io/output.rs` 中的 `SPOutput`/`BPOutput` 合并实例、解与配置后写出（`lbf/src/io/mod.rs:279-290`），SVG 由 `s_layout_to_svg` 生成。lbf 自身不调用 `remove_item`、`restore`、`remove_layout`，因此这些路径只被测试或外部调用方使用。

## 四、与 jagua-rs 核心的调用关系

lbf 只使用 `Layout::cde()`、`Layout::place_item`、`Layout::save`、`CDEngine::detect_surrogate_collision`、`CDEngine::detect_poly_collision`、`CDEngine::bbox`、`Container::base_cde` 与 `Item::shape_cd.surrogate()`。`Container::new`（`jagua-rs/src/entities/container.rs:54-69`）用 `outer.bbox.inflate_to_square()` 构造 `base_cde`，所以 `cde.bbox()` 是容器的膨胀正方形而非容器本身。`Layout::new`（`layout.rs:27-34`）与 `swap_container` 都会深克隆 `base_cde`，BPP 每开一个箱子、SPP 每改一次宽度都要付出一次 CDE 重建的代价。`place_item` 与 `remove_item` 在 debug 构建中还会执行 `layout_qt_matches_fresh_qt` 全量校验。

## 五、随机数与并行

随机源是贯穿 `search`、`search_layouts` 与所有采样器的单个 `SmallRng`，通过 `&mut impl Rng` 传递。有种子时结果可复现，无种子时依赖系统熵。WASM 演示在无种子时使用固定种子 `0x12345678`（`lbf/src/util/wasm_demo.rs:84`、`161`）。并行只出现在导入阶段，SPP/BPP/MSPP 的 `import_instance` 使用 `into_par_iter`/`par_iter`（如 `spp/io/import.rs:27`、`bpp/io/import.rs:25`、`46`），`collect` 保证顺序。求解器本身是串行的，采样未使用 rayon，这是潜在的加速点。

## 六、疑点、隐含假设与风险

第一，质量过滤器的阈值方向已按 B4b 处理。锁定 `f5e2b5f` 的 `from_irrelevant_qzones` 把 `quality < required_quality` 的区域标为无关，与 API 注释、`container.rs`/`item.rs` 契约、SVG 条件和维护版 `QualityZoneFilter` 的方向相反。按 D5，本项目保留 pinned 实际谓词以维持锁定基线行为；该选择不代表符合 `min_quality` 意图，差异留在 R8。

第二，`strip_width_is_in_check` 是启发式：宽度不小于物件直径和的两倍时 panic，且不考虑条带高度。若某物件直径超过条带高度，它永远放不下，宽度会按 10% 步长增长直至 panic，而不是返回错误。

第三，BPP 的失败是静默的。`break 'inner` 之后未放置的需求既不记录也不报错，返回值中数量悄悄变少。

第四，BPP 不做成本决策。候选顺序由 SlotMap 遍历顺序与外部 id 决定，`bin_cost` 只用于报告。每个候选都用完整的 5000 次预算搜索，一个放不下的物件会在所有打开的 layout 与所有有库存箱型上各采样一遍，开销约为 `(打开 layout 数 + 有库存箱型数) × n_samples`，是箱型多时的主要瓶颈。

第五，采样区域是膨胀正方形（`container.rs:61`）。宽高比悬殊的条带会浪费大量样本在外壳之外，局部搜索的标准差也因此偏大（`ls_sampler.rs:98-100`）。

第六，`tighten_sample_bbox` 的正确性依赖于形状局部坐标非负这一未经断言的前提。若 `cost/10 ≤ x_min`，`UniformRectSampler::new` 中的 `Uniform::new(...).unwrap()`（`uniform_rect_sampler.rs:22-23`）会 panic。

第七，`search.rs:196` 的 `partial_cmp(...).unwrap()` 遇到 NaN 会 panic；`rotation_distr.rs:192` 的 `choose(...).unwrap()` 在离散旋转集为空时 panic。两者都属低概率。

第八，`SPProblem::fit_strip`（`spp/entities/problem.rs:40-59`）与 `MSPProblem::fit_strip`（`mspp/entities/problem.rs:57-76`）对 `max_by(...).unwrap()` 没有空检查，布局为空时会 panic。lbf 路径保证至少放入一个物件，但作为库 API 是隐患。

第九，`restore` 不保留 `LayKey`。`BPProblem::restore`（`bpp/entities/problem.rs:133-139`）与 `MSPProblem::restore`（`mspp/entities/problem.rs:149-155`）对快照中新增的 layout 调用 `insert`，产生新 key。调用方若在快照前保存了 key，恢复后可能失效，返回的 `bool` 需要被显式检查。

第十，文档与实现不一致。`MSPProblem::change_strip_width` 的注释说宽度非正时 layout 会被移除（`mspp/entities/problem.rs:178-179`），实际是 `assert!` 直接 panic。`restore` 注释中的"different bin"（第 141 行）是从 BPP 复制的残留。`config.rs:22` 的 `ls_frac` 注释提到的 `n_samples_per_item` 并不存在。

第十一，`bpp::io::import_solution`（`bpp/io/import.rs:63-67`）与 `mspp::io::import_solution` 都是 `unimplemented!()`，BPP 与 MSPP 无法热启动，只有 SPP 能通过逐个放置重建解（`spp/io/import.rs:63-80`）。


## 七、测试与基准

`lbf/tests/` 以标准算例（`assets/`）做完整求解回归，并包含与 C++ 参考实现的差分测试（从文件名判断，断言细节未逐行核对）。`lbf/benches/` 主要测 CDE 的收集、检测、更新与快速失败性能。

总体上 SPP 路径完整，BPP 路径可用但策略粗糙，MSPP 仅完成数据模型。优先核实的是第六节第一条的过滤器方向，其次是 BPP 候选搜索成本与采样区域膨胀问题。

# 附录 D：JaguarCore 几何与实体


本文覆盖 `Source/Geometry/Primitives`、`Transformation`、`ConvexHull`、`OriginalShape`，以及 `Source/Entities` 中的 `Container`、`Item`、`PlacedItem`、`Layout`，并覆盖 `Source/Support/Assertions.cpp` 与 `result.h`、`NumericPolicy.h`、`RustF32.h` 的数值与错误策略。Rust 对照对象为 `CodeResource/jagua-rs/jagua-rs/src` 下的对应文件。路径中的行号均指 C++ 源文件，除非特别注明。`Source/Geometry/ShapeModification` 下的 Offset、Simplify、StraightSkeleton、NarrowConcavity 未在本文展开，仅说明其在 `OriginalShape` 中的调用位置。

## 一、数值策略：模拟 Rust f32 的基础设施

整个几何层的核心设计是“双轨并存”。每个几何函数通常有两个版本，一个是以 `double` 计算的普通版本（如 `Edge2d::ClosestPoint`，`Edge.cpp:232`），另一个是带 `RustF32` 后缀、以 `static_cast<float>` 逐步模拟 Rust `f32` 运算的版本（如 `Edge2d::ClosestPointRustF32`，`Edge.cpp:246`）。业务路径（碰撞检测、放置）应当统一使用 `RustF32` 版本，以尽量复现 jagua-rs 的 f32 舍入顺序；`double` 版本主要供工具函数和测试使用。逐函数的对应关系与验证依据见 `SparrowCore/docs/Differential.md`。阅读代码时务必注意这一点，否则很容易在同一条调用链中混用两套精度。

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

# 附录 E：JaguarCore 碰撞、快速失败与形状修改


本文按调用链自上而下梳理 JaguarCore 的碰撞检测（CDE）子系统，随后讨论快速失败代理（FailFast Surrogate）与形状修改模块，并在关键处与 jagua-rs 的 Rust 实现对照。源码根目录记为 `JaguarCore/`，Rust 参考根目录记为 `CodeResource/jagua-rs/jagua-rs/`。

## 一、总体结构与公开接口

碰撞检测的对外入口是 `CollisionEngine`，声明位于 `include/JaguarCore/collision/CollisionEngine.h`（1-181 行）。它对外提供三类能力：障碍物的注册与注销（`RegisterHazard`、`DeregisterHazard`），多边形与快速代理的碰撞检测和收集（`DetectPolygonCollision`、`CollectPolygonCollisionsUntil`、`DetectSurrogateCollision`、`CollectSurrogateCollisionsUntil` 等），以及快照的保存与恢复（`Save`、`Restore`）。引擎内部持有四个核心成员：`m_QuadTree` 空间索引、`m_Slots` 障碍物槽位表、`m_FreeSlotIndices` 空闲槽位栈（LIFO），以及两把互斥量 `m_ThreadPoolMutex` 与 `m_ParallelQueryMutex`（第 172-179 行）。

槽位结构 `Slot` 包含 `std::optional<Hazard>`、`std::unique_ptr<PolygonSoA> shapeCache` 与 `generation` 代数号（第 133-138 行）。句柄 `HazardHandle` 由 `index` 与 `generation` 两部分组成，`Get` 系列函数在访问时校验代数号，从而使已注销句柄失效，避免悬挂访问。需要注意的是，`shapeCache` 虽然在构建时被赋值，却没有任何路径读取它：`GetShapeCache`（`CollisionEngine.cpp` 168-176）无调用方，`ShapesCollide`（196-227）、`EdgeCollides`（229-233）与 `CircleCollides`（441-452）同样是死代码。这些遗留代码增加了阅读负担，也意味着 SoA 缓存在当前版本中并未真正参与计算。

## 二、注册、注销与句柄分配

`RegisterHazard`（`Source/Collision/CollisionEngine.cpp` 95-120）首先通过 `FindHazardHandle` 做重复实体检查，该检查是线性扫描，复杂度为 O(n)，因此批量注册时整体为 O(n²)。随后 `AllocateHandle`（122-134）优先从 `m_FreeSlotIndices` 弹出空闲槽位，否则追加新槽位，并在注册时把形状交给 `m_QuadTree.RegisterHazard`。`DeregisterHazard`（178-194）逆向执行：先从四叉树注销，再清空槽位并递增代数号，最后把索引压入空闲栈。代数号递增保证旧句柄在槽位复用后仍然会被判定为无效。

`HazardEntity` 的相等性（`include/JaguarCore/collision/hazard.h` 1-83）对 `PlacedItem` 类型采用对变换矩阵做 float 转换后的比较，这一点与 Rust 侧 f32 的精度语义保持一致，但也意味着两个在双精度下略有差异的变换会被视为不同实体。

## 三、碰撞查询的调用链

`DetectPolygonCollision`（`CollisionEngine.cpp` 568-599）先检查越界，再逐边调用 `NodeCollidesWithEdge`，最后仅对 virtual root 中的 `Partial` hazard 执行包含检测；它不调用 collector API。`CollectPolygonCollisionsUntil`（486-557）是独立路径：先用 `isOutside`（40-45）处理越界并报告 `Exterior`，随后逐边委托 `CollectEdgeCollisionsUntil`，最后对 `Partial` hazard 做包含检测。`DetectContainmentCollision`（58-76）以代表点（POI）测试包含，并依据 `HazardKind` 与 scope（Interior 或 Exterior）决定碰撞方向；其真值表与 Rust `detect_containment_collision`（`cd_engine.rs` 233-258）相对应。B4a 已另外确认 `DetectPolygonCollision` 的过滤 `Entire` 行为与 pinned Rust 有运行差异。

Rust 版的 `detect_poly_collision`（`cd_engine.rs` 161-200）与此流程同构：先做边界外检查，再遍历边，最后做包含检测。`collect_poly_collisions_until`（273-325）在 Rust 中同样以回调返回 `bool` 控制提前停止，C++ 的 `stopAfterCollision` 语义与之相同。

快速代理的检测路径为 `DetectSurrogateCollision`（657-689）与 `CollectSurrogateCollisionsUntil`（691-724）。后者只使用代理的极（poles）进行圆形查询，并不对 piers 做边检测，这与 Rust `collect_surrogate_collisions_until`（`cd_engine.rs` 343-368，仅使用 poles）保持一致。piers 只在更高层的快速失败判断中作为辅助信息存在。

## 四、四叉树：构建、懒扩展与查询

四叉树定义于 `include/JaguarCore/collision/Quadtree.h`（1-140 行），实现于 `Source/Collision/Quadtree/Quadtree.cpp`（756 行）。其设计逐行移植自 jagua-rs 的 quadtree：每个节点持有一组 `NodeHazard`，按 presence 降序排列（Entire 优先于 Partial，Partial 优先于 None），并且只有 Partial 的障碍物才携带一个 `PartialHazard`，其中保存节点内仍然相关的边子集与快速失败包围盒 `ffBbox`。

注册流程从 `MakeRootHazard`（273-298）开始，障碍物以根节点的 Partial presence 进入，然后由 `RegisterIntoNode`（469-528）负责把它分发到子节点。`Constrict`（300-467）是这一过程的核心：它先检查障碍物是否完整覆盖某个象限（若是则判为 Entire），否则为每个象限计算相交的边子集，并通过中心点包含测试处理没有边穿过的情况。在第二遍处理邻接关系时，C++ 版对 `None` 邻居采取静默偏好（435-436 行），而 Rust 版在相同分支中使用 `unreachable!`。这说明 C++ 版在该分支上更宽容，但也掩盖了潜在的前置条件破坏，阅读时应当注意。

节点是懒扩展的：子节点只在需要时创建，`RegisterIntoNode` 在 492-500 行展开子节点。注销时 `DeregisterFromNode`（529-547）递归移除句柄，若某子树中已不存在 Partial 障碍物，则折叠该子树，以回收节点内存。`edgeCollidesWithQuadrants`（117-194）与 `edgeIntersectionHalf`（40-77）负责线段与象限的分类，后者的 t/u 分类逻辑与 Rust `edge_intersection_half`（`quadtree/qt_traits.rs` 150-208）一致。`circleQuadrantOrder`（209-223）决定圆形查询时象限的遍历顺序，与 Rust `Circle::quadrant_order` 对应。

查询入口 `Query`（611-650）支持矩形、线段三种形态，并在节点的活动边数超过 `m_CdThreshold` 时退化为直接对边列表逐一检测，而不是继续下降到子节点。这个阈值（`CDEConfig::cdThreshold` 默认 32，见 `CollisionEngine.h` 35-36）是一个性能折中：边数很少时直接检测比递归更便宜。`PresenceAt`（663-689）计算某句柄在给定节点矩形上的覆盖状态，`VirtualRootFor`（701-721）则为查询矩形选择最小的覆盖节点作为虚拟根，从而缩小遍历范围。

bounds-only RegisterHazard 重载（Quadtree.h:46–47、Quadtree.cpp:579–598）创建 Partial hazard，以边界左下到右上的对角 Edge 作为唯一代理，不保存完整形状，也不向子节点登记。Query 会先由 VirtualRootFor 下钻到完整包含查询范围的子节点，再只遍历该子树；因此完全落入子节点的查询可能漏掉仅保存在根节点的 bounds-only hazard。这是源码路径推导，未运行专用用例。重载带 `JAGUARCORE_API`，但仓内无调用；按 B5 保留并登记为 C++ 专有扩展，不计 Rust parity，未来接入前需要专门定义并验证覆盖契约。

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

代理的变换有两条路径。`Transformed`（674-723 附近）使用双精度，而 `TransformedRustF32` 使用 `*RustF32` 系列辅助函数模拟 f32 运算，以便复现 Rust 的 f32 舍入顺序并供跨语言对拍。这种双轨设计是为了跨语言验证，正常运行时应当明确选择其中一条，否则两侧结果可能在边界情况下出现分歧。

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

综合上述分析，当前仍需按证据核查以下事项：线性去重、Contains 和 SetRegistered 扫描的规模成本、GetThreadPool 首次调用决定线程数、线程池工作线程异常边界、AVX2 编译/运行期开关、形状修改回退路径、未使用的 ShapeModifyConfig 与 OffsetShape 语义，以及 Quadtree.cpp 的格式问题，分别需要性能测量、调用约定检查或实现验证。bounds-only RegisterHazard 的去留已按 B5 登记，但其查询漏项限制仍未用专用运行用例复核。include 大小写风险应按 Include/include 双目录漂移处理，不应写成已确认的 Linux 构建失败。

在与 Rust 参考的对照中，碰撞检测主流程、包含真值表、象限约束、半相交分类以及代理的 poles 使用基本一致；主要待核对点包括本地镜像与 pinned revision 的 constrict 分支语义，以及形状修改部分的回退与容差策略。bounds-only RegisterHazard 是带 `JAGUARCORE_API` 的 C++ 专有扩展，仓内无调用且 Rust 无对应入口；源码显示子节点范围查询可能漏掉该项，因此它不属于当前 parity 覆盖。阅读时需标明 Rust 参考源码基线。

# 附录 F：JaguarCore 问题模型、IO 与公开 API


## 一、范围与阅读方法

本文覆盖 `JaguarCore/Source/Problems`（SPP、BPP、MSPP）、`Source/IO` 与 `include/JaguarCore/io`，以及 `Source/Support/Version.cpp`、`include/JaguarCore/export.h`、`include/JaguarCore/result.h` 与 `Source/Support/Result.cpp`。Rust 对照实现位于 `CodeResource/jagua-rs/jagua-rs/src/probs/` 与 `src/io/`。阅读时以实现体为准，头文件中的签名只作为入口索引。行号均指向当前工作区文件，如有改动需重新核对。

整体分层可以概括为三层。最底层是 `Entities` 中的 `Layout`、`Item`、`Container` 等几何实体，负责碰撞引擎（CDE）与变换。中间层是三类 Problem，它们持有 Instance 并维护"剩余需求、剩余库存、已放置布局"等状态。最外层是 `Importer`（外部表示到内部 Instance 与 Solution 的转换）、`JsonCodec`（JSON 文本与 `ExternalRepr` 之间的编解码）与 `SvgExport`（渲染）。

## 二、三类 Problem 的状态机

SPP 的状态由 `SPProblem`（`include/JaguarCore/problems/spp.h`，156 行）持有。构造函数（`Source/Problems/SPP/Problem.cpp` 第 9–17 行）从 Instance 初始化 `RemainingDemand`。`PlaceItem`（第 30–53 行）在 NDEBUG 未定义时才检查剩余需求是否为正（第 38–44 行），随后在第 50 行递减。`RemoveItem`（第 55–71 行）先移除布局、再检查实例索引，若索引非法则已经移除的布局不会回滚，这是一个非原子操作。`ChangeStripWidth`（第 73–90 行）复制 Strip 后调用 `SwapContainer`。`FitStrip`（第 92–111 行）在第 104 行使用 1.00001 的乘数加偏移来收紧宽度。`Save`（第 113–116 行）输出 `SPSolution`。`Restore`（第 118–158 行）优先尝试恢复布局，否则按 `SameBase` 重建，然后恢复 Strip 并重新计算需求；第 146 行在调试模式下抛出 `std::out_of_range`，第 149 行在调试模式下抛出 `std::underflow_error`。`RemainingDemand` 对非法索引返回 0（第 160–163 行）。

Rust 的 `SPProblem::place_item` 没有需求检查，C++ 仅在调试模式检查，因此发布版本中需求可能下溢，这一点与 Rust 在 release 下按补码回绕的行为接近，但 C++ 无法得到与 Rust 一致的错误报告。Rust 的 `fit_strip` 使用 `unwrap` 取最大值，C++ 的对应实现更宽松。Strip 的 `SetWidth` 与 `Create` 见 `Source/Problems/SPP/Strip.cpp` 第 181–214 行，`fixedHeight` 与宽度只检查 `!(x > 0.0)`，因此无穷大宽度会通过检查，这是一个需要注意的边界。

BPP 的状态由 `BPProblem`（`include/JaguarCore/problems/bpp.h`，201 行）与 `Source/Problems/BPP/BppProblem.cpp`（516 行）实现。布局存放在 `LayoutSlot` 数组中，每个槽位包含 `optional<Layout>`、`binIndex` 与 `generation`。槽位 0 是哑元，`generation` 初始为 0。`AllocateLayoutSlot`（第 114–130 行）复用空闲槽并把 generation 设为 `generation | 1`，使其为奇数表示占用；`ReleaseLayoutSlot`（第 132–144 行）则把 generation 置为偶数。这种设计模拟 Rust 的 `SlotMap`，其对外的布局键由 `kLayoutKeyPartBits = 32`（第 14–38 行，64 位平台）编码槽位与代数。`OpenLayout`（第 146–164 行）先检查库存再递减库存。`PlaceItem`（第 166–221 行）在目标为 `Closed` 时先打开布局，并在第 196–200 行与第 209–213 行提供回滚；需求检查只在调试模式下（第 174–180 行）；需求递减位于第 217 行。`RemoveItem`（第 223–258 行）先删除布局，再检查实例索引（第 238–242 行），布局为空时自动关闭（第 248–255 行），并返回 `Closed` 的 `layoutIndex = 0`。`RemoveLayout`（第 260–291 行）先校验所有物品，再恢复需求与库存，最后释放槽位，这是 BPP 中唯一做到先验证再修改的路径。

`Restore`（第 307–454 行）的顺序是：第 329–368 行做快照校验（包括 `SameBase` 与 `sameItemDefinition`），第 367 行递减库存，第 372–376 行按槽位排序准备数据，第 378–390 行尝试保留原键，第 392–446 行为无法保留的布局分配新槽位，第 448–453 行提交，并返回 `layoutKeysChanged` 标志。Rust 中 BPP 的 restore 通过 `insert` 生成新键，因此键会变化，C++ 尽量保留键，这是一个行为差异，调用方不应依赖键在 restore 后不变，也不应依赖其变化。`RemainingBinStock`（第 461–465 行）与 `BinCost`（第 501–510 行）分别返回库存与成本。`Density`（第 486–499 行）在没有任何布局时执行 0/0，结果为 NaN，这一点应当在调用方处理。`BppSolution.cpp` 的 `Density`（第 37–49 行）按槽位顺序累加，`Cost`（第 51–60 行）忽略越界的 `binIndex`，静默跳过错误数据。

MSPP 的状态由 `MSPProblem`（`include/JaguarCore/problems/mspp.h`，211 行）与 `Source/Problems/MSPP/MspProblem.cpp`（512 行）实现。每条 `MultiStrip` 包含 `maxWidth`、`fixedHeight` 与当前 `width`。`AddLayout`（第 142–157 行）对布局做规范化，但不检查它是否与实例的基础定义相符。`RemoveLayout`（第 159–181 行）存在非原子问题：第 176 行在循环中已经递增需求，而第 173 行的后续校验可能提前返回，导致部分修改已经生效。`PlaceItem`（第 183–212 行）在第 191–197 行做调试模式的需求检查。`RemoveItem`（第 214–236 行）同样先删布局后检查索引。`ChangeStripWidth`（第 238–260 行）原地调用 `SwapContainer`。`FitStrip`（第 262–286 行）在第 279 行使用与 SPP 相同的 1.00001 乘数。`Restore`（第 302–437 行）没有对快照中的 Strip 做校验，键保留逻辑与 BPP 类似，提交位于第 432–436 行。`ExtendableStrips`（第 470–480 行）、`TotalStripWidth`（第 482–491 行）与 `Density`（第 493–506 行）均使用 float 运算，`Density` 在无布局时同样产生 NaN。MSPP 的 `MspStrip::Create`（`Source/Problems/MSPP/MspStrip.cpp` 第 5–32 行）没有检查宽度不超过 `maxWidth`，而 `SetWidth`（第 34–50 行）在第 38 行检查该条件，但错误信息写的是"exceeds f32 range"，与实际含义不符。Rust 中 `Strip::set_width` 对此使用断言。`MspSolution.cpp` 的 `Density`（第 101–113 行）使用的是 Strip 高度乘宽度，而不是容器面积，与 SPP 的定义不同。

三类 Problem 的 `Create` 都有 `Source/Problems/*/...Instance.cpp` 中的前置校验，但 MSPP 实例创建中没有任何正需求检查。调试期的 `DebugAssert*Solution`（位于 `Source/Support/Assertions.cpp` 约第 387 行及其 BP、MSP 对应函数）包裹在 `#ifndef NDEBUG` 中，每次修改后都会调用 `Save()` 并校验，因此调试模式下每次变更的代价为 O(n)，性能分析时应当使用发布构建。

## 三、Importer 的映射、校验与错误

`Importer`（`Source/IO/Importer.cpp`，714 行；接口在 `include/JaguarCore/io/importer.h`，102 行）的主要流程是：先把外部形状转为内部多边形，再施加中心化预变换与形状膨胀，最后交给 Instance 工厂。`withSeparation`（第 114–122 行）把最小物品间距转换为偏移量 `separation / 2`，这是每侧各让出一半间距的做法。`externalToInternal`（第 131–151 行）先应用预变换的逆变换，再应用外部变换，全部以 f32 计算以与 Rust 对齐。

`rustFpaEqual`（第 21–53 行）按 ULP 比较浮点数，用于判断相邻重复顶点。`normalizedRectangleBounds`（第 61–82 行）处理矩形边界。`normalizeDegreesRustF32`（第 90–96 行）与 `radiansRustF32`（第 98–102 行）分别规范化角度与转换弧度，均采用 f32 语义。

`importPositiveItems`（第 153–189 行）是物品导入的入口校验：第 158–168 行检查所有物品（包括需求为零的物品）的 ID 唯一性，第 170–182 行跳过需求为零的物品，第 183–187 行在最终为空时报错。也就是说，ID 唯一性检查先于零需求过滤，这意味着一个零需求物品与另一个同 ID 物品仍会产生冲突错误。

形状导入分为几个函数。`ImportSimplePolygon`（第 238–266 行）做 f32 舍入，闭合 Strip 的首尾顶点，并通过 `rustFpaEqual` 去掉连续重复顶点。这一步与 Rust 的 `eliminate_degenerate_vertices` 对应，但 C++ 中对退化情况的处理是否完全一致，需要对照测试样例确认。`ImportItemShape`（第 268–293 行）与 `ImportContainerOuter`（第 295–316 行）分别处理物品与容器外轮廓，`ImportZoneShape`（第 318–325 行）处理质量区。

旋转导入（`ImportRotation`，第 327–397 行）允许的最大角度数为 65536。离散模式（Stepped）的步长必须能整除 360。角度列表会排序并去重。若只有单个角度 0，会被折叠为 `None`，即不允许旋转。这一折叠是隐式行为，调用方若期望保留显式的 `[0.0]` 列表，会得到不同的内部表示。

`ImportItem`（第 399–426 行）执行中心化（第 417–420 行）、按 `ShapeModifyConfig` 做 Inflate，然后调用 `Item::Create`。`ImportContainer`（第 428–497 行）中，`MAX_QUALITIES` 为 10（第 442、457 行），与 Rust 的 `N_QUALITIES` 一致（Rust 位于 `entities/container.rs:81`）。孔洞会合并到 `grouped[0]`（第 468–469 行），这意味着多个外轮廓或多个孔洞的归属规则是隐式的，依赖输入的排列顺序。质量区通过 `ConvertToInternal`（第 480–486 行）转换，最后由 `Container::Create` 构造（第 495 行）。

Instance 层的导入：`ImportSPInstance`（第 517–535 行）调用 `SPInstance::Create` 并传入 `stripHeight`。`ImportSPSolution`（第 537–570 行）依次执行 `ChangeStripWidth`、通过 `findItemByExternalId`（线性扫描）查找物品、`externalToInternal`、`PlaceItem` 与 `Save`。线性查找在大规模实例上的代价为 O(n²)。`ImportBPInstance`（第 590–642 行）检查 ID 唯一性（第 606–614 行），按 ID 排序（第 617–619 行），跳过库存为零的 bin（第 625–626 行），并把 bin 重新编号（第 633–634 行），这一重编号意味着外部 bin 索引与内部索引可能不一致。若结果为空则报错（第 636–640 行）。`ImportMSPInstance`（第 669–688 行）以 `maxWidth` 与高度调用 `MSPInstance::Create`。`ImportBPSolution`（第 644–650 行）与 `ImportMSPSolution`（第 690–696 行）直接返回 `UnsupportedOperation`，Rust 中对应的 `import_solution` 也是 `unimplemented!()`，因此两侧都没有 BP 与 MSP 解决方案导入能力。

## 四、JsonCodec 协议与版本策略

`JsonCodec`（`Source/IO/JsonCodec.cpp`，1977 行；接口在 `include/JaguarCore/io/JsonCodec.h`，120 行）包含一个手写的递归下降解析器（第 79–475 行）。解析器没有深度限制（第 142–182 行的递归处理），因此极深嵌套的输入可能导致栈溢出。数字通过 `strtod`（第 453、463 行）转换，并在 `ExternalRepr` 的 float 字段上做截断（第 728、738 行），这意味着 double 精度会在导入时丢失。非有限数被拒绝（第 461–466 行）。末尾多余数据被拒绝（第 92–94 行）。

解析错误统一包装为 `SerializationFailure`（第 627–631 行与第 646–651 行），这是唯一的错误码，调用方无法区分语法错误与语义错误。`JsonWriter`（第 477–625 行）输出的键按字典序排列，使用 `setprecision(17)`（第 563 行）保证浮点往返，并在第 561–562 行拒绝非有限数。

字段名与枚举约定集中在几个区段：旋转模式在第 1007–1124 行，形状类型在第 873–1005 行，物品字段在第 1178–1187 行，实例与解决方案在第 1453–1735 行附近。`None` 旋转被写为 `{"mode":"discrete","angles":[0.0]}`（第 1095–1101 行），因此写出后再读入会得到单角度离散模式，而不是原始的 `None`，这与 Importer 中的折叠规则形成对称但不完全互逆的关系。

版本策略方面，JSON 中没有 schema version 字段，编解码不检查版本号。版本信息只能通过 `Version.cpp` 的 `JaguarCoreApiVersion()` 获取，其值硬编码为 `2U`。因此协议演进时没有兼容性闸门，升级前后必须依靠外部约定。

## 五、SvgExport 渲染流水线

`ExportLayoutSvg`（`Source/IO/SvgExport.cpp`，410 行；接口在 `include/JaguarCore/io/SvgExport.h` 第 67–69 行，返回 `Result<std::string>`）的流水线大致为：计算视口（第 114–145 行），构造 flip 平移（第 146 行），生成场景组（第 156–157 行），绘制容器、布局与碰撞高亮，最后输出坐标轴（第 396–397 行）。

第 146 行的 flipTranslation 计算为 viewY + viewHeight，而第 156–157 行的变换写为 translate(0 F) scale(1 -1)。按公式推导，当 viewY 不为 0 时可能产生视口垂直偏移；这是静态推导的疑点，尚未通过渲染结果确认。应以 viewY 不为 0 的输入运行 SVG 渲染回归后再判定。每个布局的 id 在第 184 行生成。碰撞高亮（第 269–384 行）通过重建一个 CollisionEngine 来判定冲突，因此导出一次带高亮的 SVG 代价较大。

错误路径方面，`compatibility.md` 第 120 行声称 SVG 导出只返回 `SerializationFailure`，但代码还会返回 `CollisionEngineFailure` 与 `SnapshotMismatch`，文档与实现不一致。

## 六、公开 API 列表与典型调用顺序

对外入口主要包括：Importer 的 `ImportItem`、`ImportContainer`、`ImportItemJson`、`ImportContainerJson`、`ImportSPInstance`、`ImportSPSolution` 及其 JSON 版本，`ImportBPInstance`、`ImportBPSolution` 及其 JSON 版本，`ImportMSPInstance`、`ImportMSPSolution` 及其 JSON 版本。JsonCodec 提供 `Parse*Json` 与 `Serialize*Json` 成对接口，以及从内部对象生成 `External*` 的 `External*FromSolution` 与 `ExternalLayoutFromLayout`。渲染入口为 `ExportLayoutSvg`。版本接口为 `Version()`、`JaguarCoreVersion()` 与 `JaguarCoreApiVersion()`，版本号分别硬编码为 `0.1.0-dev` 与 `2U`，Windows 的 PE 版本号 `0.1.0.0` 写在 `JaguarCore.pro:18` 与 `Package/JaguarCorePackage.pri:16` 中。

典型调用顺序为：先用 `ImporterConfig`（包含 `cdeConfig`、`shapeModifyConfig` 与 `minItemSeparation`）调用 `ImportItem` 与 `ImportContainer` 生成内部对象，或直接调用 `ImportSPInstanceJson` 等一步到位的接口；然后用 Instance 创建 `SPProblem`、`BPProblem` 或 `MSPProblem`；接着用 `PlaceItem` 放置物品，必要时调用 `RemoveItem`、`ChangeStripWidth`、`FitStrip`；通过 `Save` 得到解决方案快照；最后用 `ExportLayoutSvg` 渲染，或用 `Serialize*Json` 输出。若需要从外部解决方案恢复，应调用 `ImportSPSolution`，它内部会完成 ChangeStripWidth 与 PlaceItem 的回放。注意 BPP 与 MSPP 的解决方案导入目前不可用。

`Result<T>` 定义于 `include/JaguarCore/result.h`（约 90 行）。`ErrorCode` 共 14 个取值（第 10–26 行），`Error` 结构（第 29–33 行），`Result<T>` 基于 `std::variant` 实现（第 41–57 行），在成功状态下调用 `GetError` 会抛出 `bad_variant_access`。`Result<void>` 见第 87–88 行。`Source/Support/Result.cpp` 只有一行 include。与 Rust 的 panic 与 anyhow 不同，C++ 的失败路径以值返回为主，但部分内部断言仍然依赖异常。

export.h 在 Windows 下根据 JAGUARCORE_BUILD 选择 dllexport 或 dllimport，在 Linux 下展开为空。头文件引用使用大写目录名，而仓库中 Include/ 与 include/ 两套目录都存在，且对应头文件在两套路径下均可找到。当前不能据此断言 Linux 构建失败；应比较两套副本，确认权威目录以及 qmake/CMake 的 include root 是否一致。大小写副本漂移仍是维护风险。

## 七、差分测试与 Rust Oracle 机制

差分测试位于 JaguarCore/Tests/Differential，JaguarCore/Tests/differential 两个大小写不同的目录都存在，JaguarCoreTests.pro 当前引用大写 Differential/ 路径。因此“区分大小写系统必然找不到该目录”的结论不成立；需要检查两份目录的内容是否一致，并明确权威副本。测试本身比较 C++ 结果与字面量及快照，不会在每次运行时调用 Rust oracle。CompareGeometry 面积使用 1e-3 与 1e-9 两档，CompareOptimizer 的密度容差为 5e-3。CompareProblem.cpp 的 Windows 反斜杠路径仍是独立的跨平台风险；tests/differential/README.md 的运行说明也需与实际工程核对。

`tools/RustOracle/src/main.rs`（2666 行）是一个依赖 `../../../CodeResource/jagua-rs/jagua-rs`（启用 spp、bpp、mspp 特性）的 Rust 二进制。`main()` 位于约第 557 行，根据命令行标志分派：`--restore-boundary-parity-only`、`--model-boundary-parity-only`、`--offset-boundary-parity-only`、`--import-boundary-parity-only`、`--zero-separation-parity-only`、`--collision-parity-only`，若均不匹配则输出固定场景的 key=value 文本。`tools/RustOracle/last_oracle_output.txt`（53 行）是陈旧且不完整的输出，不能作为当前基准。`tools/rust_oracle/` 目录仅包含 `target/`，属于构建残留。因此，当前的差分机制是"人工生成的快照 + 字面量"，并不是在线对比，Rust 与 C++ 行为一旦分叉，测试不会自动发现，除非有人重新运行 oracle 并更新 fixtures。

## 八、Rust 差异、可疑点与陷阱汇总

第一，发布版本中的需求下溢：SPP、BPP、MSPP 的 PlaceItem 在 NDEBUG 下都不检查需求，Rust 在 release 下虽然也会回绕，但 C++ 没有等价的错误返回路径，调用方无法感知。第二，多处 RemoveItem 与 RemoveLayout 为非原子操作，SPP 与 MSPP 的 RemoveItem、MSPP 的 RemoveLayout 在失败时可能留下部分修改，BPP 的 RemoveLayout 是唯一先校验再修改的实现。第三，Restore 的键保留策略与 Rust 的 SlotMap 不同，Rust 会生成新键，C++ 尽量保留。第四，SPP 与 MSPP 的 Restore 对快照中的 Strip 缺少校验，恶意或损坏的快照可以写入非法宽度。第五，BP 与 MSP 解决方案导入两侧均未实现。第六，无布局时的 Density 返回 NaN，且 BP 与 MSP 的密度定义与 SPP 不一致。第七，JSON 解析的 float 截断与缺少版本字段，导致协议演进缺乏闸门。第八，SVG 在 viewY 不为 0 时可能产生垂直偏移（公式推导，待渲染回归）。第九，错误码契约与文档不一致，compatibility.md 第 120 行需要更新。第十，Importer 中的零需求过滤发生在 ID 唯一性检查之后，旋转单角度被折叠为 None，孔洞归属依赖输入顺序，这些都是隐式假设。第十一，MSPP 的 Create 与 SetWidth 对宽度边界的检查不一致，且 SetWidth 的错误信息误导。第十二，头文件大小写与差分测试路径大小写不一致。

本文列出的下溢、非原子修改、SVG 偏移、导入契约与错误码事项来自源码检查，须按各项的证据等级处理；公式推导或源码分支差异不等于已经复现的运行缺陷。后续应为高风险项补最小输入和对应测试，并确认记录所引用的 Rust revision、C++ 路径与当前仓库布局一致。
