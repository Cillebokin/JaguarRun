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

本次分析分为两类。第一类是本人已在源码中直接核对、确认存在的问题。第二类是子代理基于阅读推断、尚未用测试验证的问题。两类在修复优先级上应区别对待。

已核实的问题有四条。其一，`CodeResource/jagua-rs/jagua-rs/src/collision_detection/cd_engine.rs:174-179` 在虚拟根的第二阶段遍历 hazard 时，`Entire` 分支直接 `unreachable!`，且没有先检查 `filter.is_irrelevant`。当 filter 忽略了一个完整覆盖虚拟根的 hazard（例如由 `min_quality` 生成的低质量区，LBF 在 `lbf_spp.rs:53-57` 中正是这样使用的），而候选形状又完全位于其内部时，第一阶段的边检测会因为被忽略而不报碰撞，第二阶段随即命中 `unreachable!` 而 panic。这一路径是真实存在的，建议为其编写单元测试并修复，修复方式是在 `Entire` 分支先判断 filter。同一逻辑在 C++ 的 `Quadtree.cpp` 中被写得更宽容，但 C++ 是否存在同样的覆盖问题也需要单独确认。

其二，`Rust filter.rs` 中 `from_irrelevant_qzones` 的实现条件是 `quality < required_quality`，即把低于要求的质量区设为无关，从而允许物品压在这些区域上。而其文档注释写的是“质量高于或等于要求的区域被忽略”，`container.rs:86` 的注释又说明“质量越高越好”。代码与文档方向相反，如果业务语义是“物品不得压在低于 `min_quality` 的区域上”，那么当前代码就是反的，会导致结果非法。这一点需要结合业务定义确认，然后用小算例验证。

其三，`JaguarCore/Source/IO/SvgExport.cpp:146` 的 `flipTranslation = viewY + viewHeight`，而第 156-157 行使用 `translate(0 F) scale(1 -1)`。翻转后 y 坐标变为 `F − y`，视口的范围会平移 `viewY`，因此 `viewY ≠ 0` 时图形会出现垂直偏移。正确的平移量应为 `2·viewY + viewHeight`。该结论由公式推导得出，但未用渲染结果验证，建议用 `viewY ≠ 0` 的用例回归。

其四，C++ 的 `#include` 大量使用 `JaguarCore/Collision/...`、`JaguarCore/Geometry/...` 这类首字母大写的路径，而磁盘上的目录是小写的 `collision/`、`geometry/`。在 Windows 的大小写不敏感文件系统上可以编译，但在 Linux、macOS 或大小写敏感的 CI 环境中会直接失败。这会阻碍任何跨平台计划，建议统一大小写。

待核实的问题较多，以下按我认为的重要性简要列出，每一条都需要用测试确认。窄凹陷闭合 `close_narrow_concavities`（`shape_modification.rs:446-449`）对 Inflate 与 Deflate 都要求子多边形面积为负，注释与模式语义是否一致需要核实；同一函数中 `shape.vertices[i..j]` 与删除的 `i..=j` 之间可能存在 off-by-one。`Edge::collides_with` 对平行或共线线段返回“不相交”，因此折返式的自相交无法被 `find_self_intersection` 检出。`piers.rs:57` 的 `forfeit_distance` 使用包围盒面积而非长度，量纲错误，并且 piers 的参数随 `debug_assertions` 变化，调试与发布结果不可直接比较。`Circle::bounding_circle` 在同心较大圆的输入下会产生 NaN。BPP 中未能放置的物品会被静默丢弃，没有任何日志或错误。`Layout` 的 C++ 版本在 Release 下对碰撞引擎不一致的情况静默忽略，而 `Layout` 对空 `m_Cde` 缺乏防护。`MSPProblem::RemoveLayout` 的修改不是原子的。`Density()` 在无布局时产生 NaN。`QuadTree::RegisterHazard(handle, bounds)`（`Quadtree.cpp:579-598`）以对角线 `Edge` 作为包围盒的代理，这与“仅包围盒、无几何”的语义并不等价，覆盖是否充分需要核实，子代理报告中“该路径被跳过”的说法与代码不符，应以本段为准。

最后，本文全部结论基于阅读源码得出，当前环境中没有 cargo，也没有 MSVC 构建环境，因此没有编译或运行任何测试。若要把上述判断转化为确定的结论，建议先修复已核实的第一条，并为其添加回归测试，然后逐条用最小算例确认第二类问题。

