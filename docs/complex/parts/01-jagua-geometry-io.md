# jagua-rs 几何与 IO 部分代码逻辑说明

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

锁定D5 revision的`offset_shape`（`shape_modification.rs:342-383`）使用`geo::Buffer`与`BufferStyle::line_join(LineJoin::Round(0.1))`，结果经`import_simple_polygon`重建；`MultiPolygon`非空时只返回首个外轮廓，并记录warning。JaguarCore `Offset.cpp`现有简单outline路径及有界整数自交resolver，已对齐splitCandidate、窄桥2组件和comb 3组件的固定fixture（`OffsetShapes`比较全部组件，`OffsetShape`比较首个）；其他自交/量化退化、通用多环overlay和legacy路径仍未对齐，见11号Offset记录。

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
