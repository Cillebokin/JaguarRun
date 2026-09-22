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

`OffsetShape` 的私有 rounded straight-skeleton 实现按 geo-buffer 0.2.0 的 shrink/split event 语义重实现；当内缩产生多个外轮廓时，单多边形 API 按 jagua-rs importer 行为返回第一个外轮廓。`OffsetShapes` 同时提供全部外部组件，空结果返回成功的空 vector；实现不链接 Rust，也不暴露 skeleton 内部类型。参考项目为 [geo-buffer](https://github.com/1011-git/geo-buffer)，其 Apache-2.0 许可和来源说明保留在 `JaguarCore/Source/Geometry/ShapeModification/StraightSkeleton.cpp` 文件头。

当前仅承诺上述固定多结果/空结果 fixture 的行为；尚未承诺所有事件拓扑、复杂多区域
或带孔洞 `SimplePolygon` 输入的完整公共 API 等价，这些场景仍需继续通过 Rust
fixture 扩展验证后才能作为稳定能力使用。

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
这条规则优先于对字段名 `min_quality` 的直觉解释，C++ Layout 和差分测试均按源码行为执行。

`CollisionEngine` 的动态 Hazard 只允许 `PlacedItem`；Exterior、Hole 和
`InferiorQualityZone` 必须保持静态。Snapshot restore 会在构建临时 Slot/QuadTree 前拒绝违反该约束的输入，失败时保留原 CDE 状态。

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
- SVG scene 使用 `translate(...) scale(1 -1)` 保留 JaguarCore 的笛卡尔坐标方向。

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

`CollisionEngine::Restore` 采用临时 Slot/QuadTree 重建后一次性替换的原子语义。快照包含空形状或非 dynamic Hazard 时返回 `InvalidInput`/`SnapshotMismatch`，原有动态 Hazard 和索引保持不变。

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
