# collision_detection：Hazard、CDE 与 quadtree

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 对照**：`JaguarCore/Include/JaguarCore/Collision/*`、`JaguarCore/Source/Collision/*`  
**关联工作项**：B4、B5、B6、B9、B11、B13  
**证据类型**：pinned Rust 与当前 C++ 静态核对；B4a 的最小 `Entire` 过滤用例已在 Rust 1.98.1 Debug/Release 与 C++ MSVC 2019 x64 Debug/Release 定向 runner 实测。B4 其余矩阵仍未由本轮覆盖

## `jagua-rs/src/collision_detection/cd_engine.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `CDEngine` (21–30) | `QTNode`、`SlotMap<HazKey,Hazard>`、配置和外界 hazard key 均为公开/内部状态 | `CollisionEngine`（`CollisionEngine.h:60–179`）用 `QuadTree`、自管 slot/generation、CDE 配置和 SoA cache；数据组织不同 |
| `new` (34–56) | 创建 root；逐项插入静态 hazard 并注册到树；扫描第一条 `Exterior` 并 `expect`，不存在则 panic。重复 entity 只会在 `register_hazard` 的 Debug 断言暴露 | `CollisionEngine` 构造（`CollisionEngine.cpp:79–86`）不查找必需 Exterior；C++ 在外部查询路径处理 Exterior，注册则对空形状/重复实体返回 `InvalidInput`（95–120） |
| `register_hazard` (59–69) | Debug 下断言 entity 唯一；插入 slotmap、生成 root hazard、增量注册树；Debug 下校验无 dangling hazard；没有 `Result` 失败出口 | `RegisterHazard` 返回 `Result<HazardHandle>` 并检查空 shape、重复 entity；分配 slot、SoA cache 和树节点（95–120）。C++ 将 Rust 的不变量断言变成运行时错误 |
| `deregister_hazard_by_entity/key` (72–96) | 按 entity 线性查 key，未找到 `expect` panic；按 key remove 的 `None` 也 `expect` panic；先/后更新树的次序不同，随后 Debug 检 dangling；返回移除 Hazard | `DeregisterHazard(handle)`（178–194）检查无效句柄并返回 `InvalidInput`；不暴露按 entity 注销入口；错误而非 panic |
| `save/restore` (99–150) | snapshot 只 clone 动态 hazards。restore 将当前动态实体列出，按 entity 做差分：当前有、snapshot 无则移除；snapshot 有、当前无则添加；双方有则不替换 shape 等字段；最后 Debug 断言动态 hazard 数相等。函数无 `Result`，无显式错误恢复 | `Save/Restore`（`CollisionEngine.cpp:736–887`）同样聚焦动态 hazards，但 Restore 校验快照、使用 C++ generation handles，错误以 `Result<void>` 返回，并在更新期间重建/验证内部索引；具体顺序差异由 B4/B6 对拍确认 |
| `hazards` (153–155) | 按 SlotMap 返回所有 hazards 的借用迭代器（包括静态与动态） | C++ 未提供同形公开全量 hazards iterator；快照只提供 dynamic hazards |
| `detect_poly_collision` (161–198) | 查询 bbox 未被 CDE bbox 完全包围时立即 `true`；否则逐边查询 virtual root，再对其中的 hazard 做 containment 检查。第二阶段 `None` 跳过、`Partial` 先检查 filter 再测试包含，而 `Entire` 无条件 `unreachable!`。若 relevant `Entire` 存在，第一阶段会先返回 true；但若 filter 忽略该 hazard，边查询会跳过它，随后第二阶段仍会 panic | `DetectPolygonCollision`（`CollisionEngine.cpp:568–599`）逐边调用 `NodeCollidesWithEdge`，后续 containment 只处理 `Partial` 并应用 filter。`NodeCollidesWithEdge` 的 `Entire → true`（255 行）对应 Rust 第一阶段对 relevant `Entire` 的早退，不对应第二阶段的 Rust `unreachable!`。B4a 实测：合法 Hole 被 filter 忽略后，pinned Rust panic，C++ 返回 false；未过滤时两侧均返回 true |

| `detect_surrogate_collision` (205–224) | 依次变换 fast-fail pole 与 pier，并用整棵 quadtree 查询；任一碰撞早退。仅作快速拒绝，不表示精确多边形检查 | C++ `DetectSurrogateCollision`（657–689）接受 `Surrogate + Transform2d`，通过 Result 报错；C++ 的圆/边碰撞路径在 `CollisionEngine.cpp` 内实现。API 错误形态不同 |
| `detect_containment_collision` (233–257) | 先以 bbox `almost_relation_to`；Surrounding 测 query POI 是否在 hazard 内，Enclosed 测 hazard POI 是否在 query 内，Disjoint/Intersecting 不做 containment。再按 scope 真值表：Interior 且 contained → collision；Exterior 且非 contained → collision | C++ 匿名函数 `DetectContainmentCollision`（`CollisionEngine.cpp:58–76`）用 `AlmostRelationToRustF32` 和 POI 测试，`interiorScope == contained` 对应上述真值表；精确边界需 B4 fixture |
| `collect_poly_collisions(_until)` (263–321) | 普通版本以永不停止的 callback 包装 until 版。until 版：若 shape bbox 未在引擎 bbox 内且 collector 没有 Exterior key，则先插 Exterior 并回调；边逐条委托 quadtree collector，回调 true 即早退；最后扫描 virtual-root 的 Partial hazards 做 containment，去重依 key；遍历顺序文档标为 unspecified | C++ `CollectPolygonCollisions[Until]`（454–567）提供同等入口；返回 vector 的重载对实体排序，因而顺序被稳定化；collector until 有 handle/entity 双过滤。C++ API 有显式 `HazardFilter`，Rust 收集接口主要把 collector 自身作为 filter |
| `collect_surrogate_collisions(_until)` (327–365) | 仅遍历 `shape.surrogate.ff_poles()`；surrogate 缺失返回 false；不会检查 piers，也不构成完整多边形碰撞收集。回调可早停 | C++ `CollectSurrogateCollisionsUntil`（691–735）也基于 pole 集合并返回 `Result<bool>`；C++ 同样有显式错误层。勿与 `detect_surrogate_collision` 的 poles+piers 检查混为一谈 |
| `get_virtual_root/bbox/haz_key_from_pi_key` (370–399) | 沿完全包围 query bbox 的第一个 child 向下直到无法继续；bbox 返回 root；PItemKey 映射以遍历 hazards 的线性查找，缺失时返回 `None` | C++ `QuadTree::VirtualRootFor/VirtualRootBounds` 与 `CollisionEngine::FindHazardHandle` 提供近似入口；C++ slot/generation 为内部 handle 管理。索引性能与边界由 B4/B6 核实 |
| `CDEConfig/CDESnapshot` (404–417) | 配置含 quadtree depth、edge-count threshold、item surrogate config；snapshot 仅含动态 hazard Vec | C++ `CDEConfig` 字段名/默认值及 `CollisionSnapshot` 对应类型见 `CollisionEngine.h:20–54`；C++ 另有批量/并行查询配置，不属于 pinned Rust API |

### B4a：虚拟根中的 filtered `Entire` hazard

基线为 Cargo.lock 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`，Rust 1.98.1。最小输入为 `[0,10]²` CDE bounds、同 bounds 的 Exterior、`[2,8]²` 的合法 Hole、四叉树深度 4，以及位于 Hole 内部的小查询多边形 `[4.1,4.3]²`。运行时确认该 Hole 在 query virtual root 中为 `Entire`。

`NoFilter` 时，输入边查询先命中 `Entire`，Debug/Release 均返回 true，后续 `unreachable!()` 不会执行。使用 `HazKeyFilter` 忽略该 Hole 时，边查询正确跳过它；Rust 随后在 `cd_engine.rs` containment pass 对仍存在于 `virtual_root.hazards` 的 `Entire` 无条件 panic。该 panic 在 Debug 和 Release 均由 oracle 捕获，三个输出键见 `collision_entire_b4a.txt`。

同一输入下 C++ `DetectPolygonCollision` 对 relevant `Entire` 仍由边查询早退；忽略 Hole 后，边查询过滤掉该 hazard，containment pass 只访问 `Partial`，最终返回 false。MSVC 2019 x64 Debug/Release 定向 runner 均通过，Release 测试断言启用。

**处置**：B4a 核对完成，确认这是 pinned Rust 上可达的 panic 与 C++ 正常 false 返回之间的运行时差异。过滤器契约是忽略指定 hazard；该输入下 false 符合查询语义，因此不把 Rust panic 映射成 C++ 碰撞或失败，也未改生产算法。将 pinned 行为作为上游缺陷/兼容性残差登记；结论限定于此固定合法输入，B4 整体仍为部分。

## `jagua-rs/src/collision_detection/hazards/hazard.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `HazKey`、`Hazard` (9–35) | HazKey 由 slotmap 生成；Hazard 是 entity、`Arc<SPolygon>` 和 dynamic 标记的直接组合；`new` 无校验 | `HazardHandle`（`Hazard.h:15–27`）用 index+generation；`Hazard` shape 可为空，因此 C++ 注册时另行拒绝空 shape |
| `HazardEntity` (37–66) | enum 分支：PlacedItem 携 item index、DTransformation、PItemKey；Exterior 无载荷；Hole 携区内 index；InferiorQualityZone 携 quality 与区内 index。scope 对后三种 interior entity 返回 Interior，只有 Exterior 返回 Exterior | C++ `HazardEntity` 是 kind 加多个字段的 struct（`Hazard.h:38–74`），手动 equality 忽略当前 kind 不用的字段；PlacedItem equality 对变换分量转 f32 比较。Rust `PartialEq/Eq` 对 enum payload 使用精确字段相等；需 B4 检查舍入入口能否确保相同 |
| `From<(PItemKey,T)>` (68–79) | `T: Borrow<PlacedItem>`，由 key、`item.idx`、`d_transf` 构造 PlacedItem variant；支持借用和拥有值，未找到/转换失败分支不存在 | C++ `Layout::MakeHazard`（`Layout.cpp:98–106`）以 ItemIndex、handle、transform 填充同类 entity；C++ 多存一份句柄而 Rust 通过 PItemKey 表达 |

## `jagua-rs/src/collision_detection/hazards/filter.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `HazardFilter` (7–9) | 仅按 `HazKey` 决定相关性 | C++ `HazardFilter`（`HazardFilter.h:10–28`）支持 entity predicate 和 `(handle, entity)` predicate 两种 callback；过滤表达能力/调用线程安全约束不同 |
| `HazKeyFilter::from_keys` (13–18) | 将输入 iterator 收集到 SecondaryMap，key 在 map 内则 irrelevant | C++ 可由 `irrelevantHandle` callback 实现相同集合过滤，但无同名具体类型 |
| `from_irrelevant_qzones` (20–42) | 遍历 hazard map，仅把 `InferiorQualityZone { quality }` 且 `quality < required_quality` 的 key 收入过滤器；Hole、Exterior、quality 达标区不忽略 | C++ 质量区过滤通常通过 `HazardFilter::irrelevant` callback 表达；没有相同 helper |
| `HazardFilter for HazKey/NoFilter` (45–65) | HazKey 仅忽略自身 key；NoFilter 全部返回 false | C++ `NoFilter` 设置恒 false callback（`HazardFilter.h:31–38`）；忽略自身可用 handle callback |
| collector blanket impl (68–77) | 任意 `HazardCollector` 自动作为过滤器，已收集 key 返回 irrelevant | C++ collector 与 filter 是两种独立参数；引擎调用 collector 时还会自行按 collector 是否已含 handle 去重 |

## `jagua-rs/src/collision_detection/hazards/collector.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `HazardCollector` trait (7–42) | required: `contains_key/insert/remove_by_key/len/iter`；默认 `contains_entity` 线性扫描；`remove_by_entity` 找不到会 `expect` panic；默认 `is_empty/keys/entities` 都由 iterator/len 派生 | C++ `HazardCollector`（`HazardCollector.h`、`Collector.cpp:7–68`）提供 Contains、ContainsEntity、Insert、Remove、RemoveEntity(bool)、Entries；移除不存在 entity 返回 false，不 panic；具体实现不可替换成 Rust trait 对象语义 |
| `BasicHazardCollector` (46–109) | SecondaryMap 保存 key→entity，另以 key 的低 6 位选 64-bit mask 作 negative filter；bit 未置位即可返回 false，置位再查 map（75–77）。insert 置 bit；remove/retain 不清 bit，因此 stale bit 只会多一次 map 查询、不造成漏报；clear 同时清 map 和 bits。无输入错误出口 | C++ 以 vector 保存 entries（`Collector.cpp:25–35`），移除时实际 erase；Rust 的 mask 优化和 C++ 的顺序 vector 在性能/迭代顺序上不同 |
| Default、trait 与 IntoIterator (111–146) | Default 转调 new；trait 方法转调 inherent 方法；借用 IntoIterator 直接遍历 SecondaryMap | C++ `Entries()` 暴露 vector 引用；返回和迭代次序不应当作为跨语言稳定契约，除非 fixture 明确约定 |

## `jagua-rs/src/collision_detection/quadtree/qt_hazard_vec.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `QTHazardVec` 状态 (13–17) | hazards 按 `Entire > Partial > None` 排序；缓存 Partial active-edge 总数 | 对应 `QuadTree::Node::hazards/activeEdges` 与 `NodeHazard`（`Quadtree.h:79–96`），C++ 使用 `vector` 内嵌节点结构 |
| `add` (24–43) | Debug 下拒绝 None、相同 entity/key 重复；按 presence binary search 插入，累计 `n_edges`。release 下依赖调用方不变量 | C++ `Node::AddHazard`（`Quadtree.cpp` 节点实现段）维护排序与 edge cache；具体重复项策略需结合 B4 对拍 |
| `remove` (45–55) | 线性按 hkey 删除；找到才扣 active-edge cache 并返回元素，否则 None | C++ `Node::RemoveHazard` 返回 bool，不返还节点 hazard |
| `strongest` (61–64) | Debug 下先断言排序/cache正确；从强到弱找第一个 filter 认可的条目 | C++ 查询由 `CollisionEngine::NodeCollidesWithEdge/Circle` 遍历节点 hazard；内部排序以 presence 降序作同一早退优化 |
| 查询与统计 (66–88) | `no_partial_hazards` 检查是否不存在 Partial；`n_active_edges` Debug 校验后返回缓存 | C++ `Node::HasPartialHazard/EdgeCount` 与 activeEdges 近似；断言启用条件在 Rust `assertions.rs`/C++ DebugAssert 源分别定义 |
| sort/assert helpers (90–121) | 比较只依 presence，平级比较为 Equal；断言滑窗顺序与 edge 总和一致，失败触发 assertion panic | C++ 实现另存有序 vector 与计数缓存；在 B4 记录实际 profile 与排序稳定性 |

## `jagua-rs/src/collision_detection/quadtree/qt_hazard.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `QTHazard/QTHazPresence` (13–33) | 每个 node-local hazard 存 node bbox、HazKey、entity 和 None/Partial(edge subset)/Entire 状态 | `QuadTree::NodeHazard/PartialHazard/HazardPresence`（`Quadtree.h:20–95`）为私有嵌套实现；C++ `None/Entire` 不存 partial，字段组织近似 |
| `from_root` (37–44) | root 一律 Partial，partial 边集从完整 shape 建立 | `MakeRootHazard`（`Quadtree.cpp:273–298`）相同起始策略 |
| `constrict` (50–169) | Debug 检 quadrant containment 与 layout；None 状态进入 `unreachable!`，Entire 克隆到全部四区。Partial 先找 bbox 完全落入的 quadrant，找到后该区延续原状态，其余 None；否则逐区筛相交 edges，命中边创建 Partial。无边 quadrant 通过邻区 None/Entire 传播状态；若同时见到两类邻居 `unreachable!`；若仍无邻居则测 quadrant centroid 是否在 hazard 内，并按 entity scope 决定 Entire/None。最终 unresolved child 由 `expect` 失败 | C++ `QuadTree::Constrict`（`Quadtree.cpp:300–467`）实现同类分区。静态审阅发现 C++ 对一个 Rust `unreachable!` 邻区组合采用宽容分支（`parts/05-jaguar-collision-failfast.md` 的既有记录）；该差异须以 pinned revision 和最小用例复核，暂记已确认的源码分支差异、非运行缺陷 |
| `n_edges` (171–177) | None/Entire 为 0，Partial 返回存储边数 | `NodeHazard::EdgeCount`（`Quadtree.h:88–91`）同样仅 Partial 计边 |

## `jagua-rs/src/collision_detection/quadtree/qt_partial_hazard.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `QTHazPartial` (7–12) | 存节点中活跃边子集和能包住全部子集边的 fail-fast bbox | C++ `QuadTree::PartialHazard`（`Quadtree.h:73–77`）同类字段 |
| `from_entire_shape` (15–19) | 收集整个 polygon edge iterator，ff bbox 取 shape bbox | C++ root hazard 初始化（`Quadtree.cpp:273–298`）保存完整边集/包围框 |
| `from_parent` (20–60) | Debug 下要求子集非空且每条 edge 在 parent 集；若数量相同沿用 parent bbox；否则扫描端点求 bbox，只有 x/y 两个跨度均正才建紧 bbox，轴对齐退化时退回 parent bbox | C++ `Constrict` 对边子集也维护 fail-fast bbox；精度和退化边界属于 B4 几何对拍范围 |
| `n_edges/collides_with` (62–74) | edge 数返回 vector 长度；碰撞先测 query primitive 与 ff bbox，再 any 测 partial edges | C++ `NodeHazard::EdgeCount` 与 CollisionEngine 节点谓词对应；C++ 还对 PolygonSoA 使用加速 cache |

## `jagua-rs/src/collision_detection/quadtree/qt_node.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `QTNode/new` (14–37) | 保存剩余 level、bbox、可选四子节点、HazardVec 和阈值；new 不分配子树 | `QuadTree::Node`、构造器（`Quadtree.h:97–110`、`Quadtree.cpp`）相同懒分配策略 |
| `register_hazard` (39–82) | 仅当 children 不存在、level>0 且新 hazard 是 Partial 时扩展四区；先把本节点已有 hazard constrict 并分发，再分发新 hazard，最后存父节点项；None child 不递归注册。child 数组异常以 `unwrap` 失败 | C++ `RegisterIntoNode`（`Quadtree.cpp:469–528`）增量分发 hazard；节点内 vector/unique_ptr 表示替换 Rust Box<[QTNode;4]> |
| `deregister_hazard` (84–95) | 本 node 找到并移除时：若没有 Partial 则释放整层子树，否则向孩子递归注销；本节点未找到则不递归。无效 key 静默无操作 | C++ `DeregisterFromNode`（`Quadtree.cpp:529–547`）同为删除并在无 Partial 时折叠；需确保父/子登记不变量与 Rust 一致 |
| `collides` (100–145) | 先取 strongest relevant hazard；None→无碰撞，Entire→直接命中；Partial 有 children 时按 query primitive 的 quadrant mask 和偏好顺序递归、首命中早退；没有 children 时过滤后逐 hazard 测 partial edge；若遇到此处不应出现的 Entire 则 `unreachable!` | C++ `NodeCollidesWithEdge/Circle`（`CollisionEngine.cpp:239–344`）与 QuadTree 查询分支实现二态查询；C++ 还有批量并行入口，未见于 Rust |
| `collect_collisions` (149–155) | 用 until 版本和恒 false callback 收齐所有碰撞 | C++ `Collect*CollisionsUntil`（`CollisionEngine.cpp:345–453`）对应 |
| `collect_collisions_until` (164–210) | active-edge 数 `<= threshold` 或无 children 时在本节点检查，否则按 query 的 quadrant masks 探索所有相交 child；当前节点路径按 collector key 去重，None=false、Entire=true、Partial 做几何查询；插入后回调可提前停止。无错误返回分支 | C++ Collector 路径也支持 callback；额外 filter 独立于 collector，Rust 由 HazardCollector blanket filter/contains 承担部分去重职责 |

## `jagua-rs/src/collision_detection/quadtree/qt_traits.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `QTQueryable` (11–23) | 约束 `CollidesWith<Edge> + CollidesWith<Rect>`；默认四象限碰撞逐区调用 rect predicate；默认顺序 `[0,1,2,3]`；Debug 仅校验传入 quadrants 与 `r.quadrants()` 一致 | C++ 没有公开 trait；`CollisionEngine.cpp` 与 `Quadtree.cpp` 的 `edgeCollidesWithQuadrants`、`circleCollidesWithQuadrants`、`circleQuadrantOrder` 是静态专用实现 |
| Circle/Rect impl (25–50) | Circle 以圆心相对 split 的 left/right/top 分类，按中心所在象限优先搜索，再沿邻区排序；Debug 对四种顺序做精确断言。Rect 用默认顺序 | C++ `circleQuadrantOrder`（`Quadtree.cpp:209–223`）对应 Circle；矩形查询按固定次序 |
| Edge `collides_with_quadrants` (52–130) | 先按 x/y interval 排除不相交区，再以端点落区判真；若四区均确定提前返回；其余构造外边与水平/垂直 bisector，调用 `half_intersect` 分类相交线段落在边的哪一半。未知项最后按 false 收敛；Debug 对照 f64 oracle 检查保守性 | C++ `edgeCollidesWithQuadrants`、`edgeIntersectionHalf`（`Quadtree.cpp:40–194`）做同类优化。具体 f32 bisector 回归由下一文件固定；当前搜索没有发现 C++ 对应的同一输入回归用例 |
| `half_intersect/edge_intersection_half` (132–202) | 仅当任一关联象限仍未分类且存在有限线段交点才写入结果；平行/共线（denominator 0）返回 None；交点 t/u 必须都在 `[0,1]`；按 0.5 划 First/Halfway/Second，分类用 `partial_cmp(...).unwrap()` | C++ 对应辅助函数在 `Quadtree.cpp:40–115`；浮点零、端点、共线语义要纳入 B4 数值 fixture |
| `CollisionHalf` (204–208) | FirstHalf/Halfway/SecondHalf 枚举，无额外逻辑 | C++ 用内部枚举/输出参数表示线段半段，不是公开 API |

## `jagua-rs/src/collision_detection/quadtree/tests.rs`

| 测试/行 | 覆盖点与断言 | C++ 对应与状态 |
|---|---|---|
| `edge_quadrants_allow_f32_false_positive_at_bisector` (6–36) | 构造固定 Rect/Edge；用 f64 交点证明 crossing 只比 bisector 高不到一个 f32 ULP；逐个 `Edge::collides_with(Rect)` 会给出 `[false,true,true,false]`，f64 oracle 与优化 `collides_with_quadrants` 均给 `[false,true,false,false]`。这是四象限边线分类的数值回归，不是完整 CDE polygon 碰撞测试 | C++ 有 `TestCollision.cpp` 的四叉树 presence/query 测试和对应分类实现，但仓库搜索没有找到同一组浮点输入；未运行 C++ 测试，B4 应将该 Rust case 纳入 differential/boundary fixture |

## B4/B5 结论边界

- 本记录已覆盖 10 个原先待记的 Rust 源文件；B4 的运行 parity、B5 的 C++ bounds-only 扩展路径审查、B11 差分矩阵均未因此完成。
- C++ API 相比 Rust 增加 `Result` 错误出口、显式双重 filter、batch/parallel 查询及排序后的 vector 收集结果；Rust 主 quadtree 以 `HazKey/SlotMap` 与 trait 组合表达，C++ 以 generation handle、私有节点向量和 callback 表达。
- Rust 多处不变量在 Release 会失去 Debug 断言保护；调用者输入错误可能表现为不变量失守、缺项或后续 panic。C++ 多数入口改成可恢复错误，但 quadtree 的内部逻辑仍需要相同几何边界 fixture 证明。
- 碰撞顺序在 Rust API 注释中明确为 unspecified；C++ vector collector 的返回重载执行了排序。只有碰撞集合相同的证据时才可宣称集合 parity，不能要求逐项迭代顺序一致。

## 证据位置

- pinned Rust：`collision_detection/cd_engine.rs:18–417`、`hazards/{hazard,filter,collector}.rs`、`quadtree/{qt_hazard_vec,qt_hazard,qt_partial_hazard,qt_node,qt_traits,tests}.rs`。
- C++：`JaguarCore/Include/JaguarCore/Collision/{CollisionEngine,Hazard,HazardFilter,HazardCollector,Quadtree}.h`；`JaguarCore/Source/Collision/CollisionEngine.cpp`、`Quadtree/Quadtree.cpp`、`Hazards/Collector.cpp`。
- 相关现存测试：`JaguarCore/Tests/TestCollision.cpp`；这里只登记静态对应位置，本轮未执行。
