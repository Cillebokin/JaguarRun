# JaguarCore 与 jagua-rs 外部表示兼容性

本文记录 JaguarCore JSON codec 对 jagua-rs `external representation` 的兼容边界。JSON codec 位于 `JaguarCore/IO`，核心几何、碰撞检测和问题模型不依赖 JSON parser、Qt 或 Rust。

## 支持的根对象

| jagua-rs 类型 | JaguarCore 类型 | JSON API |
|---|---|---|
| `ExtItem` | `ExternalItem` | `ParseExternalItemJson` / `SerializeExternalItemJson` |
| `ExtContainer` | `ExternalContainer` | `ParseExternalContainerJson` / `SerializeExternalContainerJson` |
| `ExtLayout` | `ExternalLayout` | `ParseExternalLayoutJson` / `SerializeExternalLayoutJson` |
| `ExtSPInstance` / `ExtSPSolution` | `ExternalSPInstance` / `ExternalSPSolution` | `Parse...` / `Serialize...` |
| `ExtBPInstance` / `ExtBPSolution` | `ExternalBPInstance` / `ExternalBPSolution` | `Parse...` / `Serialize...` |
| `ExtMSPInstance` / `ExtMSPSolution` | `ExternalMSPInstance` / `ExternalMSPSolution` | `Parse...` / `Serialize...` |

所有 API 都以 `std::string` 和 JaguarCore 自己的 external structs 为边界，不暴露 parser 内部类型。
`ExternalLayoutFromLayout` 可将现有 `Layout` 转换为 `ExternalLayout`；它按 jagua-rs 规则把 Item 的 `preTransform` 与内部 placement 合成为外部旋转/平移。
`Importer::ImportItemJson` 和 `Importer::ImportContainerJson` 提供 JSON 到内部 Item/Container 的便捷入口；解析失败保留 `SerializationFailure`，几何或旋转校验仍由原有 Importer 逻辑完成。

问题模型也提供一站式 JSON 闭环：

- `Importer::ImportSPInstanceJson` / `ImportSPSolutionJson`；
- `Importer::ImportBPInstanceJson` / `ImportBPSolutionJson`；
- `Importer::ImportMSPInstanceJson` / `ImportMSPSolutionJson`；
- `ExternalSPSolutionFromSolution`、`ExternalBPSolutionFromSolution`、`ExternalMSPSolutionFromSolution`。

这些入口通过真实 `SPProblem`、`BPProblem`、`MSPProblem` 放置流程重建内部状态，因此会校验外部 item ID、需求、库存、Bin/Container ID、布局可行性和变换有限性；不会直接拼接未经验证的内部 Snapshot。外部变换遵循 jagua-rs 语义：导入时撤销 Item 的 `preTransform`，再应用外部旋转/平移；导出时先合成 `preTransform`，再合成内部 placement。

## 字段和语义

- 字段名保持 Rust 的 snake_case，例如 `min_quality`、`strip_height`、`run_time_sec`、`max_width`。
- Item 必须提供 `orientation.rotation` 和 `shape`。旧字段 `allowed_orientations`、`allowed_rotations`、`allowed_reflection_axes` 会被拒绝。
- 旋转模式保持 `discrete`、`stepped`、`continuous`。内部 `RotationPolicyKind::None` 写出为 `discrete` 的 `[0]`，因为 jagua-rs 的 external enum 没有 `none` 模式。
- `ExtShape::Polygon` 的 Rust 字段名是 `inner`；C++ 内存结构中的 `ExternalPolygon::holes` 仅是语义化命名，读写 JSON 时始终使用 `inner`。
- `ExternalContainer::zones` 在空时省略，符合 Rust 的 `skip_serializing_if`；读取时省略等价于空数组。
- SPP/BPP/MSPP 的需求数量、库存、成本使用无符号整数；浮点几何、密度、宽度和旋转角必须是有限 JSON number。
- Item 顶层允许 `dxf` 等非约束元数据，便于直接读取仓库中的实例文件；orientation 和 rotation 对象拒绝未知强约束字段。

## 错误语义

JSON 语法、字段缺失、类型错误、重复键、未知旋转模式和不支持的 JSON 结构统一返回 `ErrorCode::SerializationFailure`，消息包含稳定的字段上下文。几何合法性和旋转步长的算法约束仍由 `Importer` 在 external representation 转换为内部 Item/Container 时检查，并返回 `InvalidInput`、`UnsupportedShape` 或几何专用错误码。

这一区分避免 codec 悄悄改变 jagua-rs 的导入边界：codec 负责忠实读写 external representation，Importer 负责几何和排料内部不变量。

## 数值和格式

- 输出 JSON 采用有限数值；不能表示的 NaN/Infinity 返回 `SerializationFailure`。
- 默认输出是紧凑 JSON；`JsonCodecOptions::pretty = true` 可生成缩进格式。
- 编码后再解码应保持 ID、需求、库存、轮廓点、旋转模式、布局变换和统计字段的语义一致。对象字段顺序不是兼容性的一部分。

## Offset 实现边界

旧`OffsetShape` straight-skeleton实现对应本地jagua-rs fork曾使用的geo-buffer 0.2.0 shrink/split event语义；当前锁定Rust revision改用`geo::Buffer`。C++按D5 `Round(0.1)`规则构造outline，并新增限于单条量化outline的整数自交分割与组件重建；目前25个固定有限结果输入配置（含普通方形Inflate/Deflate、斜边五边形Inflate 0.5、凹L形Inflate/Deflate 0.5及Deflate 1.0近消失、短边为2^7和2^20的矩形近半宽Deflate、窄桥/comb分裂边界、梳形Inflate 2.25同步闭合两个凹槽、T/H/U/C/十字形Deflate 2.0和两种sub-grid舍入）匹配D5。另测凹L消失阈值相邻`f32`值及0.5×、2×、10×缩放：低侧Rust报简单多边形少于3点，而C++ 10层POI估值较低、提前返回空结果；两侧错误码相同但消息不同，阈值值两侧消息一致。旁路POI短路会在0.5×与2×的阈值值产生Rust已判空的22顶点多边形，因此未删除该保护。resolver有点数与坐标范围限制，且不支持通用多环/洞或全部overlay边界；无法解析时仍可能走旧路径，不能描述为完整等价移植。Stage22的20个平移大矩形近半宽输入中，C++ fallback导入候选同样少于3点；单组件`OffsetShape`现返回与Rust一致的错误消息，额外的`OffsetShapes`仍成功返回空vector。极小矩形Deflate输入（短边2^-21）半宽前连续5个f32值均进入fallback；修正交点行列式的尺度相关平行判定后，C++与锁定Rust均在导入去重点后因少于3点报错，活动fixture覆盖该错误出口。`OffsetShape`取首个外轮廓，`OffsetShapes`是C++额外的多结果API，空结果返回成功的空vector；实现不链接Rust，也不暴露skeleton内部类型。旧参考项目为[geo-buffer](https://github.com/1011-git/geo-buffer)，其Apache-2.0许可和来源说明保留在`JaguarCore/Source/Geometry/ShapeModification/StraightSkeleton.cpp`文件头。当前矩阵及剩余差异见`docs/logic-comparison/records/11-offset.md`。

当前仅对登记的固定fixture作出行为承诺；尚未承诺所有事件拓扑、复杂多区域
或带孔洞 `SimplePolygon` 输入的完整公共 API 等价。resolver无法处理的输入仍可能进入历史路径，
这些场景仍需继续移植并通过Rust fixture扩展验证。

2026-09-18 已补充凹形内缩分裂的逐组件数值断言：两个组件均为三角形，面积约
`1.419689345789`，并分别校验 `x_min/y_min/x_max/y_max`，容差为 `1e-6`。
这保留了 Rust `f32` round-trip 与 C++ `double` skeleton 实现之间的可解释数值边界。
同日增加 L 形 Deflate fixture；当 skeleton 在该事件拓扑下返回空组件时，`OffsetShapes`
复用既有 rounded fallback，得到 1 个 22 顶点组件，面积差异控制在 `2e-6` 内。

容器的孔洞不是多环 `SimplePolygon` offset 输入，而是按 jagua-rs importer
语义作为 quality=0 的独立区域；设置最小裁片间距时，孔洞和质量区使用 Inflate
半间距，外轮廓使用 Deflate 半间距。C++ I/O 回归已验证无间距与 `2.0` 间距下的
孔洞碰撞和外轮廓越界行为分别发生变化。带孔洞的 Item 和带孔洞的质量区仍按
jagua-rs 规则拒绝，不把未支持的多环 offset 误称为已完成。

质量区过滤保留 jagua-rs 当前实现的实际条件：`HazKeyFilter::from_irrelevant_qzones(required_quality)` 忽略
`quality < required_quality` 的 `InferiorQualityZone`，而质量 0 的 Hole 不在该过滤条件内，始终作为碰撞 Hazard。
按 D5 锁定的 jagua-rs `f5e2b5f`，这条规则是本项目的行为兼容目标；它与 `min_quality` 的意图语义及维护版 `QualityZoneFilter` 相反，差异登记为 R8。C++ Layout 和差分测试均按 pinned 源码行为执行。

正常 `Layout` 用法把 PlacedItem 注册为动态 Hazard，把 Exterior、Hole 和
`InferiorQualityZone` 作为静态 Hazard；但公开的 CDE 快照恢复入口不会在输入层限制
Hazard entity 类型或快照列表中的 `dynamic` 标志。Restore 只按 entity 匹配当前动态 Hazard：
匹配项沿用当前 Hazard 内容，未匹配的当前动态项移除，未匹配的快照项按快照内容注册。
Rust Debug 的重复 entity/动态数量断言发生在恢复过程中，失败可能留下部分更新状态；C++
复刻了这些已测 Debug/Release 状态边界。Release 会接受已测损坏快照。C++ null shape 是额外的预变更输入保护。

`containment_matrix.tsv` 现在由 C++ differential comparator 和 Rust `lbf` workspace 的真实 CDE 测试共同消费；9 个 Hole/Exterior 包含、穿越、disjoint 和 Hole 角点接触 case 在两侧均通过。

共享 `edge_matrix.tsv` 现包含 9 个案例，额外覆盖 `5e-10` 小间隙和 `5e-7`
明显间隙；C++ `double` 容差与 Rust `f32` 解析后的结果在两侧均通过。

C++ CDE 回归另包含固定种子的较大随机 differential：64 个动态 PlacedItem
Hazard、256 个查询，分别比较深四叉树和不细分索引，并在初始快照恢复、删除部分
Hazard 后的快照恢复前后比较结果；四套 Debug/Release、AVX2/Scalar 均通过。

句柄生命周期回归覆盖槽位复用：注销旧 Hazard 后重新注册的新 generation 句柄可用，
旧句柄不能注销新 Hazard，且 QuadTree presence/query 不返回旧句柄。

句柄生命周期回归覆盖槽位复用：注销旧 Hazard 后重新注册的新 generation 句柄可用，
旧句柄不能注销新 Hazard，且 QuadTree presence/query 不返回旧句柄。

Layout 回归另包含 64 个内缩网格位置、512 步随机放置/移除/恢复，并每 64 步用
全量新 Layout 重建比较数量、密度、可行性和 validator 结果；四套配置均通过。

Surrogate/shape modification 回归还使用固定种子生成 8 个 6--12 顶点的极角排序
轮廓，验证 Inflate/Deflate 包含关系、Deflate 组件非空，以及所有 Pole/Pier 端点
仍位于原轮廓内；四套配置均通过。

SPP/BPP/MSPP solution 已覆盖“内部 solution -> external representation -> JSON -> external representation -> Importer -> 内部 solution”的 round-trip 测试；回归比较布局数量、需求/库存、Bin 成本、条带宽度和密度。

随机状态回归现在在每个模型的固定种子序列中定期执行同一 JSON 闭环；MSPP 的空
Layout 也是合法内部状态，Importer 会保留该 layout，而 BPP 的空 Bin layout 仍按
模型语义拒绝并由问题模型自动关闭。

## SVG 调试导出

`ExportLayoutSvg` 是只读诊断 API，不修改 `Layout`。输出包含：

- 容器原始轮廓和碰撞轮廓；
- 每个 Item 的原始轮廓、碰撞轮廓、外部旋转/平移、external ID；
- 可选 Surrogate poles/piers；
- `data-layout-feasible` 和坐标轴；
- SVG scene 仅作为分组容器，不再附加translate/scale轴向翻转；布局坐标按与Rust基线一致的方式直接写入SVG。C++保留文档title、class/data诊断属性、collision-debug状态标记和坐标轴等扩展，DOM结构不与Rust逐项相同。

SVG 失败只返回 `SerializationFailure`，不会改变布局或碰撞检测状态。

2026-09-17 本机浏览器验收：通过 `http://127.0.0.1` 临时只读预览打开
`JaguarCoreSvgTest.svg`，浏览器标题为 `JaguarCore Layout`，DOM 可识别 SVG 图像和 Item 标题，截图可见容器、Item 原始/碰撞轮廓、Surrogate 和坐标轴。当前环境未安装 Inkscape/rsvg 等常用矢量工具，因此矢量工具人工验收仍未完成。

2026-09-18 使用最新 Debug 测试输出重新通过本机浏览器渲染：标题为
`JaguarCore Layout`，AX/DOM 可识别 SVG 图像和 Item 变换，截图可见容器、Item
原始/碰撞轮廓、Surrogate 和坐标轴；临时 localhost 服务随后已停止。

同日使用本机已有 CairoSVG/Pillow 对四套 Debug/Release、AVX2/Scalar SVG 做第二种
矢量渲染：全部生成 `110×110` 非空 PNG，非透明内容 bbox 均为 `(0,5,110,110)`；
生成文件位于各测试构建目录的 `JaguarCoreSvgTest.cairosvg.png`。Inkscape/rsvg
命令行工具仍未安装，因此不宣称其专用工具人工验收已完成。

## CDE 状态和虚拟根

`CollisionEngine::Restore` 对非空形状先在临时 Slot/QuadTree 中按 entity 差分重建，再提交状态；C++ 对 null shape 在任何变更前返回 `InvalidInput`。Debug 遇到待新增的重复 entity 时，会先提交已清理的旧动态项和此前成功新增的前缀，再抛出 `logic_error`；动态 Hazard 数量断言也在提交后执行。Release 接受目前测得的重复 entity、错误 shape/key/transform、非 dynamic 列表项及与静态 Exterior 冲突等快照。此处的 Debug 失败不是普遍原子回滚语义，失败后的状态取决于触发的分支。

四叉树同时保留完整根查询和 virtual-root 查询；CDE 的 Polygon、Surrogate 查询使用完全包围查询 bbox 的最低节点，结果集合与完整根查询保持一致。

`HazardCollector` 提供按句柄和按实体的去重/移除，以及带 `StopAfterCollision` 回调的
`CollectPolygonCollisionsUntil`。回调在首次加入碰撞实体后执行，返回 `true` 即停止遍历；
已加入 collector 的句柄不会重复触发回调；CDE 查询按稳定 `HazardHandle` 去重，因此同一
Item 的不同放置实例即使 `HazardEntity` 字段相同也不会被错误合并。`CollectSurrogateCollisionsUntil`
只遍历 fail-fast pole 前缀，不把 Pier 或完整多边形碰撞混入 Surrogate 筛选结果。

越界查询优先报告容器注册的 Exterior Hazard 及其稳定句柄；只有没有注册 Exterior 时才
使用隐式边界 Exterior，避免同一外部区域被报告两次。`ContainsEntity` 仍作为 Collector
的显式实体查询/移除工具保留，但不替代 CDE 的句柄级唯一性。
Polygon 和 Surrogate 的越界检测都使用同一条规则：优先解析注册 Exterior 的真实句柄，
再交给 `HazardFilter`；没有注册 Exterior 时才使用隐式句柄。

边界数值约定与 jagua-rs 保持一致：非共线端点接触、T 形相交和相切属于碰撞；共线重叠
和共线端点接触不属于 `Edge2d::Intersects` 碰撞。CDE 仍使用完整多边形精确检查，边界
矩阵同时覆盖 `5e-10` 近接和 `5e-7` 明显间隙样例。

问题模型的随机一致性回归使用固定 `mt19937_64` 种子，分别对 SPP、BPP、MSPP 执行
1000 步放置、移除、恢复、宽度/条带操作；每步都与独立重建问题比较 demand、stock、
layout 数量、宽度、密度和成本。无最小间距时 `ShapeModifyConfig::offset` 保持
`std::nullopt`；显式间距时为半间距。

测试工程的 Release 配置显式加入 `/UNDEBUG`，因此 Release JaguarCoreTests 和
JaguarCoreBench 也会执行断言表达式；这不改变 JaguarCore DLL 的 Release ABI 或优化配置。

Surrogate 固定 oracle 位于 `JaguarCore/Tests/Fixtures/RustReference/surrogate_u_shape_*.txt`。
Pole 使用 Rust FIFO BFS 与四象限顺序；Pier 网格内部使用 Rust 的 f32/FPA 点包含规则，
Debug 与 Release 分别对应 Rust 的低/高分辨率搜索参数。

越界 Surrogate Pole 同样遵守 `HazardFilter`；忽略 Exterior 时不会误报越界碰撞。

## 核心逻辑等价验收状态（2026-09-29）

当前不能宣称 JaguarCore 与 `jagua-rs/jagua-rs` 核心 crate 已完全等价。以下为2026-09-29状态快照，按当时RustOracle的本地fork基线整理；2026-10-22已确认RustOracle锁定D5 revision，Offset等项目应以相应当前记录复核，不能将这份旧快照直接视为当前等价状态。本轮完成的闭合项：

- **四叉树已改为 jagua-rs quadtree 的逐行结构移植**（per-node hazard 边子集、懒扩展 constrict、`cd_threshold` 查询时语义、`QTNode::collides/collect_collisions_until` 短路查询、Circle 象限优先顺序、Edge 半段相交象限分类）。碰撞/矩阵/oracle 用例 Debug/Release 双侧一致。
- **SPP/MSPP `Restore` 接受边界已对齐 Rust**：不再前置拒绝配置/宽度不一致的快照，快照 strip 值直接生效；三个导出函数不再前置 `Validate*Solution`（jagua-rs export 是纯函数）。
- **Offset 的历史本地fork基线审计（2026-09-29）**曾与 geo-buffer 0.2.0 逐函数核对并修复cleanup分叉，当时本地fork对应的12项fixture在Debug/Release通过。2026-10-22核实RustOracle已锁定D5 Git revision，其 `offset_shape` 改用 `geo::Buffer`；旧12项通过结果不代表当前D5基线。当前状态及已确认分叉见11号Offset记录，E1仍未收口。
- **float 存储保持 double + RustF32 双轨**，为既定接口差异（含 C++ 额外的 `Intersects(tolerance)` 容差语义）。

仍开放的差异（下列项目是2026-09-29快照中的记录；应按当前锁定revision和具体调用路径复核，不能概括为均不影响Release求解语义）：

- Rust Debug panic 与 C++ `Result` 错误的形态差异：`total_item_qty()` 溢出（Rust Debug panic、Release 回绕；C++ 两配置均回绕）；BPP/MSPP Debug 超需求放置（Rust 先改状态再 panic，C++ 原子 `Infeasible`）。panic 后状态不属于验收契约，是否补齐 Debug 对照待用户口径决定。
- SVG 输出为有意重写（非格式兼容）；`time_stamp`/feature 门控等诊断与结构层差异维持排除。
- 当前D5 Offset部分对齐：25个有限结果输入配置（44条有限fixture行，含普通方形Inflate/Deflate、斜边五边形Inflate 0.5、凹L形Inflate/Deflate 0.5及Deflate 1.0近消失、U形凹口闭合Inflate 2.25、斜向U形凹口闭合Inflate 2.25、splitCandidate完整25顶点首结果、短边为2^7和2^20的矩形近半宽Deflate、窄桥/comb分裂边界、梳形Inflate 2.25同步闭合两个凹槽、T/H/U/C/十字形Deflate 2.0及sub-grid舍入）通过；Rust Debug/Release各98个profile-active fixture项与Oracle输出匹配（44条有限结果、53条稳定错误、1条活动NaN；完整fixture共99个唯一键）。活动差分入口核对±Inf、方形Deflate 2.0、凹L消失阈值原始及0.5×/2×/10×缩放值、tiny Deflate fallback前5个相邻f32值及半宽临界控制值、NaN；凹L消失阈值低侧C++与Rust均返回`OffsetFailed`但消息不同，阈值值消息相同；tiny Deflate半宽前连续5个f32值在Stage24修正后均与Rust一致报少于3点错误，Stage25又以短边2^-24、2^-30、2^-38、2^-40、2^-42复核同一出口，Stage26在这些尺度补测恰好半宽与后继值：C++ `OffsetShapes`为空成功结果，`OffsetShape`保留相同错误；恰好半宽及其后继两侧`OffsetShape`均返回相同空多边形错误。另有20个大尺度近半宽矩形平移组合（短边2^7/2^20、起点2^0/2^20，各取半宽前连续5个f32距离）均进入C++ fallback并得到空组件；Rust返回“少于3点”错误，C++ OffsetShape则返回空多边形错误。旁路POI短路会在0.5×与2×阈值值产生Rust已判空的22顶点多边形，故保留当前短路；C++ NaN结果匹配Rust Release错误出口，并与Rust Debug panic不同。临时探针确认comb Deflate 2.0触发两次、H/U/C形Deflate 2.0各触发一次正长度共线重叠处理。T形回归覆盖量化重复点导致的零长度段回退，C形回归覆盖并列最小x时的组件顺序及首组件，十字形临界收缩覆盖1个64顶点分量。当前25组有限结果矩阵及53条稳定错误用例经LLVM-MinGW scalar-fallback Debug/Release定向全源码runner验证；其他自交/退化outline、fallback和通用overlay仍按11号Offset记录待移植或裁决。

可重复证据与具体输出见 [parity closure plan](superpowers/plans/2026-09-28-jaguarcore-parity-closure.md) 和 [Differential README](../JaguarCore/Tests/Differential/README.md)。

本轮 Debug 与 Release `JaguarCoreTests.exe` 全套退出码均为 0，六个 `--*-parity-only` Rust oracle 均已复验。等价结论需等差异逐项修复/明确约定并完成同一清单的双侧复验后再更新。
