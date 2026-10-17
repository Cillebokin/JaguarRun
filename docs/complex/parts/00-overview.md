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

其二，`Rust filter.rs` 锁定版的 `from_irrelevant_qzones` 实际把 `quality < required_quality` 的区域设为无关，这与 API 注释、`container.rs:86` 的质量递增定义、`item.rs:23–24` 的最低质量要求及 SVG 对 `quality >= min_quality` 的处理相反。维护版上游也按 `quality >= min_quality` 忽略达标区。项目按 D5 保留锁定 `f5e2b5f` 的实际谓词；SparrowCore callback 与现有测试断言和 pinned 行为一致。B4b 已按固定基线收口，意图语义差异留在 R8，见碰撞记录。

其三，JaguarCore/Source/IO/SvgExport.cpp:146 的 flipTranslation = viewY + viewHeight，而第 156–157 行变换为 translate(0 F) scale(1 -1)。按公式推导，当 viewY 不为 0 时可能产生视口垂直偏移；这是静态推导的疑点，尚未通过渲染结果确认，需以非零 viewY 的 SVG 渲染用例回归。

其四是大小写目录副本。C++ include 引用使用大写路径，当前仓库同时存在 Include/include 与 Tests/Differential/differential 副本，且 .pro 所引用的大写路径存在；因此不能仅凭大小写差异断言 Linux、macOS 或大小写敏感 CI 必然构建失败。应比较副本内容、核对 qmake/CMake 的 include root，并明确单一权威目录；副本漂移才是已确认的维护风险。

待核实的问题较多，以下按重要性简要列出，每一条都需要用测试确认。窄凹陷闭合 close_narrow_concavities（shape_modification.rs:446-449）的面积条件与注释、vertices[i..j] 与删除 i..=j 是否有差一需要核实；Edge::collides_with 对平行或共线线段的行为需用自相交反例确认；piers.rs:57 的 forfeit_distance 量纲和 debug_assertions 对参数的影响需核对；Circle::bounding_circle 的 NaN 边界、BPP 未放置物品处理、Layout Release 失败语义、MSPProblem::RemoveLayout 原子性、Density 空布局、MSPP 边界与导入器隐含约束均需按目标 revision 和最小用例复验。另有带 `JAGUARCORE_API` 的 bounds-only 扩展：以对角 Edge 只保存在根节点，完全落入子节点的 Query 可能漏项；仓内无调用，已登记为 C++ 专有扩展，不计 Rust parity。该查询结论为源码推导，尚未运行专用用例。

本文结论按静态源码事实、由源码推导的风险和已复现行为区分。10 月 16 日的初始文档修订没有运行构建或测试；后续已按 Rust 1.98.1 / pinned `f5e2b5f` 复核 B2、B4a，并静态核对 B4b。B4a 的 Rust Debug/Release oracle 与 C++ MSVC 2019 x64 定向碰撞 runner 均通过，详见移植工作项清单第十四节；完整 `JaguarCoreTests.exe` 仍未运行。B4b 已按 D5 保持锁定 `f5e2b5f` 的实际谓词，SparrowCore callback 与阈值断言一致；维护版方向差异作为 R8 语义残差保留，不再是基线决策待办。B5/G10 已静态登记 bounds-only C++ 公共扩展及其子节点查询漏项推导；未运行专用用例，也不计 parity。10 月 16 日 SparrowCore 测试与 SparrowCli 的 MSVC 执行记录由用户提供，不覆盖 JaguarCore 的全量 MSVC 验证。

