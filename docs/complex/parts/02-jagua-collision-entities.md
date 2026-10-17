# jagua-rs 碰撞检测与实体部分代码逻辑说明

**范围与约定。** 本文覆盖 jagua-rs/src/collision_detection/** 与 jagua-rs/src/entities/**，并在必要处引用调用方（lbf/src/opt/search.rs 等）说明调用链。文中路径均相对仓库根 CodeResource/jagua-rs。需注意：该本地镜像曾被记录为与 JaguarCore/tools/RustOracle 锁定的 Git revision f5e2b5f 存在差异；涉及 Rust 行为的结论和 fixture 应注明实际基线，并在 E4 完成后复核。本篇静态分析未在本次修订中重跑测试。

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

第二个疑点在 from_irrelevant_qzones（filter.rs:113-135）。源码条件 quality < required_quality 与函数注释中“质量高于或等于要求的区域被忽略”方向相反；container.rs:86 说明质量越高越好，item.rs:52 将 min_quality 描述为最低质量要求。这确认了代码与注释不一致，但业务要求究竟是忽略低质量区还是高质量区，尚需以产品契约和最小实例确认。当前应记录为“源码/注释冲突，语义待决”，不能直接定性为算法错误。

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
