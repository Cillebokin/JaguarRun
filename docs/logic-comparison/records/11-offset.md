# 11. Offset（shape_modification.rs 偏移部分）逐文件逻辑比对
**Rust基准**：RustOracle Cargo.lock 锁定的 jagua-rs checkout（revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`）中 `jagua-rs/src/geometry/shape_modification.rs`（D5 `offset_shape`：342-383 行）；`CodeResource/jagua-rs` 是本地fork历史源，不再作为当前Offset基准
**C++**: `JaguarCore/Source/Geometry/ShapeModification/Offset.cpp`与`StraightSkeleton.cpp`，对照`Geometry/ShapeModification.h`
**判定（当前D5锁定基线）**：整体仍不等价，固定有限子集部分对齐。C++新增outline与有界整数自交resolver已逐组件匹配32个登记有限输入配置（61条有限fixture行），覆盖方形Inflate/Deflate、带同向共线顶点的正方形Inflate 0.25、两种短边尺度矩形近半宽Deflate、斜边五边形Inflate、凹L形及轴向/斜向U形、splitCandidate、窄桥/comb分裂、梳形Inflate同时闭合两条凹槽、放大并平移的梳形Deflate交点取整边界、T/H/U/C/十字形精确收缩和量化舍入边界、密集齿轮Deflate 3.1（25齿：304输入顶点、2912点outline、832输出顶点；30齿：364输入顶点、3492点outline、resolver拆分至4252段、Rust/C++均为1012顶点；37齿：448输入顶点、4304点outline、首轮拆分至5232段、Rust/C++均为1264顶点，面积相差6 ULP并采用0.02绝对容差）；另以原始及0.5×/2×/10×相邻`f32`输入覆盖凹L消失错误出口，确认C++ POI短路会使低侧错误文本与Rust不同；Stage24已修正tiny矩形半宽前5值的尺度相关交点判定，并与Rust错误出口对齐；Stage25又在短边`2^-24`至`2^-42`的五个二次幂尺度上复核同一错误出口；Stage26在这五个尺度补齐恰好半宽与后继`f32`控制值；Stage28对齐固定单outline resolver中的`i_overlay 4.5.2`交点舍入与轴向整数除法；Stage31补齐不连通图各外部面的winding根，Stage32将resolver点数上限提高至4096并对齐2912点outline样例；Stage34将上限提高至8192并对齐4304点outline样例。未覆盖输入仍可能使用对应本地fork历史事件模型的straight-skeleton/fallback，且C++保留额外保模校验与双API。

> **当前基准说明（2026-10-22）**：`JaguarCore/Tools/RustOracle/Cargo.toml` 直接锁定 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。该 revision 的 `offset_shape` 使用 `geo::Buffer` 和 `BufferStyle::line_join(LineJoin::Round(0.1))`。Oracle 中的 `geo-buffer = "0.2"` 是额外的原始组件诊断依赖，不能据此把它视为 pinned jagua-rs 的 offset 实现。下方 2026-09-29 的源码审计与 12 项结果是当时针对本地fork基线的历史记录；当前D5验收以本记录末尾的补充复核为准。

> **2026-09-29 历史更新（本地fork基线）**：当时与本地fork依赖的 geo-buffer 0.2.0 源码完成逐函数审计：VertexQueue/Ray/Timeline/find_split_vertex/apply_event/apply_vertex_queue_rounded 均逐行对应；修复主循环 cleanup 分叉（Rust 在每次迭代末尾、含 continue 路径清理，C++ 原先只在 split 分支）。当时全量 fixture（12 项）Debug/Release 对照通过。该结果不代表当前D5锁定基线，当前结论已由本记录末尾补充复核修订。comb 第二原始组件不属于该历史核心 API（Rust 只取首组件），维持不验收。

**依赖与实现来源**：本地fork的 `CodeResource/jagua-rs/Cargo.lock` 锁定 `geo-buffer 0.2.0`（checksum 267bf037…）；C++ 侧 `StraightSkeleton.cpp:1-9` 头注释说明其独立 C++17 重实现参考了该 crate 的事件模型。当前RustOracle则通过 Cargo.toml 的 Git revision 使用 D5 锁定 jagua-rs；该 revision 的 offset 实现走 `geo::Buffer`。

## 函数逐条比对

### `offset_shape`（D5 Rust 342-383 行）
- C++ 对应：`OffsetShape`（Offset.cpp:261-287，单轮廓 API）+ `OffsetShapes`（Offset.cpp:289-342，全轮廓 API）。
- 判定：差异（结构性）。

逐段对照：

**符号化距离**（D5 Rust 343-346：`Deflate→-distance, Inflate→distance`）
- Rust API 的距离参数本身是 `f32`，随后转换为 `f64` 交给 `geo::Buffer`。C++ `normalizeOffsetRustF32`（Offset.cpp:26-36）同样按 mode 生成有符号距离，并将负值归一为 Deflate、非负值归一为 Inflate，之后由 `OffsetShapes` 转为 skeleton 的有符号距离（cpp:301-305）。
- 对普通有限距离和负距离，两侧的符号变换语义一致；C++ 的 mode 归一是内部表示差异，不构成已证实的行为分叉。当前发现的主要差异来自后续offset算法路径。

**当前D5核心算法调用**（锁定 revision 的 `shape_modification.rs::offset_shape`）
- Rust 将 `SPolygon` 坐标转为 `geo_types::Polygon`，根据 mode 生成有符号偏移，再通过 `BufferStyle::new(f64::from(offset)).line_join(LineJoin::Round(0.1))` 调用 `geo::Buffer::buffer_with_style`；结果为空时报错，多组件时取第一个外轮廓并导回核心多边形。
- `geo_buffer::buffer_polygon_rounded` 是 `CodeResource/jagua-rs` 本地fork的历史实现路径，不是当前锁定D5 revision的 `offset_shape` 路径。
- C++ `OffsetShape`（cpp:274-275）委托 `OffsetShapes` → `detail::roundedStraightSkeletonOffsets`（StraightSkeleton.cpp:888-909）：
  - `Skeleton` 构造（cpp:516-539）：对每个顶点建 `makeInitialVertex`（左/右射线 + 内角平分线轴，cpp:425-435），初始化环形 `VertexQueue`，为每个活动顶点生成 shrink 事件（相邻射线相交，cpp:607-632）与 split 事件（角点反向射线与其它边前向平分相交，cpp:634-734）。
  - `buildEvents`（cpp:736-818）：按 `timelineLess`（cpp:471-494，时间→种类 Split 优先→坐标→索引的字典序）取最小事件，应用 shrink（合并两顶点为新树节点，cpp:744-778）或 split（一拆二，cpp:781-816），直至事件耗尽。
  - `loops(offsetDistance)`（cpp:541-599）：以初始队列重放所有 `time <= offsetDistance` 的事件，每个幸存顶点输出 `axis.point(remaining)`（直边中点情形）或按 `ROUNDED_STEP = 0.1` 弧度步进在左右法线间插圆弧点（cpp:586-595）——对应 geo-buffer 的 "rounded" 语义。常量 `ROUNDED_STEP 0.1`（StraightSkeleton.cpp:28）。
  - `cleanLoop`（cpp:859-872，EPSILON=1e-9 去重点）+ `signedArea > EPSILON` 正面积过滤（cpp:902）+ `createRustImportedOffsetPolygon`（先 `eliminateDegenerateOffsetVertices` cpp:56-78 用 `rustApproxEqual`（f32 相等或 ULP≤4，cpp:39-54）去重复点，再 `SimplePolygon::Create`）。
- 判定：差异（算法路径不同）。C++ 是对本地fork的 geo-buffer 0.2.0 事件模型（顶点队列 + shrink/split 时间线 + 圆角步进）的语义级重实现；当前D5 Rust 则走 `geo::Buffer`。除内部求交、epsilon 和排序细节外，算法实现本身已不同，不能以历史事件模型逐行审计推定当前D5等价。

**多轮廓处理**（D5 Rust 362-370）
```rust
match geo_poly_offsets.len() {
    0 => bail!("Offset resulted in an empty polygon"),
    1 => &geo_poly_offsets[0],
    _ => { warn!("Offset resulted in multiple polygons, taking the first one."); &geo_poly_offsets[0] }
}
```
- C++ `OffsetShape`：`OffsetShapes` 成功且非空 → 取 `.front()`（取第一个，一致）；空 → 通常返回`OffsetFailed "Offset resulted in an empty polygon"`。Stage23/24的窄范围恢复仅覆盖凸形Deflate、距离严格小于POI半径且fallback明确因少于3点失败的情况，并保留Rust对应的导入错误消息。
- 判定：取首组件逻辑一致；**差异**在于：
  1. Rust 的 `warn!` 多分支仅日志不同，行为与单轮廓相同（都是取 [0]）；C++ 无日志。
  2. Rust `offset_shape` 在导入前就选首组件；C++ `OffsetShape` 经 `OffsetShapes` 选首个有效 `SimplePolygon`。C++ 的 `OffsetShapes` 是额外 API，逐轮廓校验/过滤只影响该额外 API；需验证其首组件仍与 Rust 结果相同。
  3. C++ `OffsetShape` 在骨架结果为空时**不再直接报错**，而是落入 `fallbackOffsetShape`（cpp:286）——但注意 `OffsetShape` 路径中 `OffsetShapes` 已在内部先行处理空结果（可能返回空 vector、fallback 单元素或错误，见下），到 cpp:278 的空判断时实际值取决于 `OffsetShapes` 的内部分支。

**多组件诊断边界：** `print_buffer_components` 调用Oracle单独声明的 geo-buffer 依赖，探测该crate的原始多组件结果，并将距离先按核心API的 `f32` 输入量化。它不是D5 `jagua-rs::offset_shape`（使用 `geo::Buffer`）的返回值，不能拿来替代当前Rust核心输出。D5核心实现多组件时取第一个外轮廓；C++ `OffsetShapes` 暴露可构造成 `SimplePolygon` 的组件。旧梳形fixture中的第二环与首组件比较只属于本地fork历史证据。

**回转内部表示**（D5 Rust 373-383：取 exterior 点集 → `ExtSPolygon` → `import::import_simple_polygon`）
- C++：`reimportOffsetPolygon`（Offset.cpp:38-41）→ `createRustImportedOffsetPolygon`（StraightSkeleton.cpp:875-886）——先去退化点再 `SimplePolygon::Create`（即 Rust import 的多边形构造校验：简单性、定向、最小顶点数等，见记录 34/05）。
- 判定：一致（语义上同为"重建为内部多边形并走导入校验"；Rust 走 f64→f32 收缩，C++ 直接用 double 顶点但构造前有 f32 近似去重）。

### `preservesModificationMode`（C++ Offset.cpp:148-178）—— Rust 无对应
- 判定：C++ 额外。
- 内容：面积相等时按 `samePolygonCycle`（cpp:121-146，环形同序列比较）判等；Inflate 要求新面积 ≥ 原面积且原顶点全在新形内（容差 1e-8，`Contains(point, tolerance)`）；Deflate 要求新面积 ≤ 原且新顶点全在原形内。
- 影响：Rust 只 `warn!` 取首组件、完全信任 geo-buffer 的保模性；C++ 会因骨架输出轻微违反包含关系（浮点误差量级）而**硬失败**（`OffsetFailed "straight-skeleton offset does not preserve the requested relation"`，cpp:336-338）。这是 C++ 的强校验机制，与 Rust "只 warn 取首组件" 的宽容策略相反。

### `fallbackOffsetShape`（C++ Offset.cpp:180-257）—— Rust 无对应
- 判定：C++ 额外。
- 内容：逐顶点法线平移 + 交点/圆角连接（`appendRoundedJoin` 步长 0.1 弧度，cpp:87；deflate 时法线取反，cpp:219）+ `preservesModificationMode` 校验失败报错（cpp:250-255）。仅在骨架路径失败/为空的分支被调用（Offset.cpp:286、317-323）。已有 L 形 deflate fixture 的状态、组件数、面积和边界框与 Rust oracle 一致；其他 fallback 输入仍需对照。
- 调用点分析（OffsetShapes cpp:309-330）：骨架为空时——若 Deflate 且 `distance >= shape.PointOfInaccessibility().radius`（收缩到 POI 半径及以内），返回空 vector（**有意接受"收缩到消失"**）；否则尝试 fallback；fallback 也失败时 Inflate 报错、Deflate 返回骨架的空结果。
- **Deflate 收缩到消失的半径判定**：Rust 侧 geo-buffer 对过度 deflate 的表现由 crate 内部决定（通常空结果），Rust `offset_shape` 对空结果一律 `bail!`（358-359）。C++ 用 `PointOfInaccessibility().radius` 作为阈值把"合理消失"与"异常失败"区分开——**与 Rust 失败边界不同**：Rust 会报错的输入（deflate 超过 POI 半径）在 C++ `OffsetShapes` 返回空成功、`OffsetShape` 报 "empty polygon" 错误，而**未超阈值**的空结果 Rust 报错、C++ 走 fallback 可能成功返回。

### `OffsetShapes` 双 API（C++ ShapeModification.h:45-46, Offset.cpp:289-342）—— Rust 无对应
- 判定：C++ 额外。Rust 单 API `offset_shape` 只暴露首组件；C++ 增加返回全部有效轮廓的 `OffsetShapes`，`OffsetShape` 保留兼容性只取第一个（h:43-44 注释自述）。头部注释（StraightSkeleton.cpp:5-8）说明私有原因：skeleton 天然产生零或多个分量，而公开 API 是单多边形。

### StraightSkeleton 内部构件与 Rust/geo-buffer 的对应关系（抽样式核对）
| Rust（geo-buffer 0.2 语义） | C++ | 判定 |
|---|---|---|
| 圆角偏移的弧步进（buffer "rounded"） | `ROUNDED_STEP = 0.1`（StraightSkeleton.cpp:28, 589-590） | 常量需与 crate 源核对；C++ 注释声明按 0.2.0 事件模型，无 crate 内常量可逐位对照（闭源重实现） |
| 顶点事件（edge collapse/shrink） | `makeShrinkEvents` + Timeline::Kind::Shrink（cpp:607-632, 744-778） | 结构对应 |
| 分裂事件（split event） | `findSplit`/`makeSplitEvents`（cpp:634-734） | 结构对应；含 initial/non-initial 两套方向过滤（cpp:683-702） |
| 事件排序 | `timelineLess`：time→Split 优先→location→real index（cpp:471-494） | 对应 crate 的 BinaryHeap+Reverse 语义（tie-break 无法逐位验证） |
| 队列重放生成 offset 环 | `loops(distance)` 重放 `time <= d` 事件（cpp:541-551） | 结构对应 |
| 输出环清理 | `cleanLoop`（EPSILON 1e-9）+ `eliminateDegenerateOffsetVertices`（rustApproxEqual: f32 相等或 ULP≤4） | 对应 crate 输出清理语义；阈值选取为 C++ 自定 |

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 算法本体：D5 `geo::Buffer` vs C++分阶段outline实现 | 部分对齐 | 新outline与有界自交overlay路径已匹配32个有限输入配置（61条有限fixture行），包括方形Inflate/Deflate、近半宽矩形Deflate、斜边五边形Inflate、凹L形及U形/斜向U形凹口、splitCandidate、bridge/comb分裂边界、梳形双凹槽同步闭合Inflate、放大平移梳形Deflate交点取整、共线顶边梳形Deflate 2.0三组件、triangle_notch Inflate 3.0凹角join、密集齿轮Deflate 3.1（25齿：304输入点、2912点outline、832输出顶点；30齿：364输入点、3492点outline、resolver由3492段拆为4252段、Rust/C++均输出1012顶点；37齿：448输入点、4304点outline、首轮拆为5232段、输出1264顶点，面积差6 ULP）、T/H/U/C/十字形精确收缩和sub-grid舍入；其他拓扑仍可能落入旧straight-skeleton/fallback | D5 Rust 342-383；Offset.cpp `simpleOffsetWithIOverlayOutlineRules`、`resolveSelfIntersectingOutline`；有限fixture |
| 2 | `offset_shape` 单 API vs `OffsetShape`+`OffsetShapes` 双 API | C++ 额外 | 接口面差异；单多边形API都选择第一个可用外轮廓，但具体外轮廓结果仍由不同算法决定 | D5 Rust 342-383 vs ShapeModification.h:39-46, Offset.cpp:261-342 |
| 3 | 多轮廓：Rust 核心 API 取首 / C++ 额外 `OffsetShapes` 暴露有效组件 | 接口面额外 | Rust `OffsetShape` 与 C++ `OffsetShape` 都取首个公开结果；Oracle独立geo-buffer诊断的raw组件数不等于D5核心API返回数，也不等于C++可构造的有效 `SimplePolygon` 数 | D5 Rust 362-370 vs Offset.cpp:274-342；raw probe仅作诊断 |
| 4 | `preservesModificationMode`（面积+双向顶点包含，容差 1e-8） | 仅旧路径保留 | 新的简单outline结果直接按Rust导入规则接受；没有被该C++保模检查二次覆盖。helper未能构造单一简单轮廓时，旧路径仍保留此检查 | Offset.cpp `simpleOffsetWithIOverlayOutlineRules` 与legacy skeleton分支 |
| 5 | `fallbackOffsetShape`（法线平移+圆角连接的简易后备） | 仅旧路径保留，未对齐 | outline量化或导入失败时仍会运行该C++后备；这类结果不计为D5对齐 | Offset.cpp `OffsetShapes` legacy分支与`OffsetShape` |
| 6 | Deflate 收缩到消失：`OffsetShapes` 可返回空 vector | 额外 API 差异 | Rust核心 `offset_shape` 把空结果报错；C++ `OffsetShape` 也映射为空多边形错误，只有额外 `OffsetShapes` 暴露空 vector | D5 Rust 362-363 vs Offset.cpp:274-281, 311-314 |
| 7 | 空结果错误消息 | 一致（返回错误的路径） | D5空结果消息为 `Offset resulted in an empty polygon`；C++映射为 `OffsetFailed` 和相同消息 | D5 Rust 363 vs Offset.cpp:281 |
| 8 | 回转内部表示（exterior→导入校验） | 导入步骤对应，数值实现不同 | 两侧都将外轮廓转换为内部简单多边形并做构造校验；具体顶点已受不同buffer算法影响 | D5 Rust 373-383 vs StraightSkeleton.cpp:875-886 |
| 9 | 距离 f32 输入与 mode 符号化 | 一致（有限距离） | Rust `f32` 距离转为 `f64` 交给buffer；C++ 将同一有符号距离归一为mode与正距离 | D5 Rust 342-360 vs Offset.cpp:26-36, 301-305 |
| 10 | 非有限距离与收缩到消失 | ±Infinity、原始及0.5×/2×/10×凹L消失阈值和NaN的profile出口均有固定fixture；低侧错误文本差异已固定 | D5 Rust Debug 对 NaN panic、Release 返回空多边形错误；±Infinity两种profile均返回空多边形错误。活动C++差分入口检查±Infinity、方形Deflate 2.0、凹L多个尺度阈值及NaN的错误通道；低侧C++与Rust均为`OffsetFailed`，但前者通用空结果消息不同；阈值值及NaN Release消息一致，NaN Debug仍是Rust panic与C++ `Result`的通道差异 | RustOracle `--offset-boundary-parity-only`；`offset_boundary.txt`；`CompareGeometry.cpp::runOffsetBoundaryDifferentialTests`；Offset.cpp:1294-1323 |
| 11 | 圆角采样方向 | 登记fixture已对齐 | C++路径使用D5的`Round(0.1)`语义；splitCandidate、窄桥与comb的登记输出逐点匹配；StraightSkeleton legacy路径仍是本地fork事件模型 | D5 `offset_boundary.txt` 与 C++差分入口；legacy `StraightSkeleton.cpp` |
| 12 | Round间距0.1及内部量化常量 | 有界子集对齐 | outline以0.1采样并复用i_float风格int32量化；临界接触和sub-grid距离fixture已固定，但resolver仍只处理单条量化outline，不是通用overlay移植 | `Offset.cpp` outline/overlay helpers；D5 `geo`/`i_overlay`锁定源码 |

## 结论
当前D5基线下，Offset仍未收口。C++ outline与有界整数resolver已在32个有限输入配置（61条有限fixture行）上对拍通过，覆盖方形、凸/凹轮廓、U形凹口闭合、自交解析、多组件分裂、精确收缩、量化舍入、放大平移梳形交点取整、共线顶边梳形Deflate三组件和triangle_notch Inflate 3.0凹角join及密集齿轮Deflate 3.1（25齿304顶点/2912点outline/832输出顶点；30齿364顶点/3492点outline/4252段/1012输出顶点；37齿448顶点/4304点outline/5232首轮段/1264输出顶点，面积容差0.02）。Rust Debug/Release各115个profile-active fixture项与Oracle输出匹配（61条有限结果、53条稳定错误、1条活动NaN；完整fixture共116个唯一键）；这表示Rust参考数据固定且C++比较器执行了已登记结果/出口断言，不表示所有错误行都与C++消息等价。凹L形消失阈值低侧仍记录有Rust/C++错误消息差异；tiny矩形半宽前连续5个相邻距离经尺度相对交点修正后按Rust少于3点错误出口对齐；五个极小尺度的前驱值也一致报错，恰好半宽及后继控制值均一致返回空偏移错误；Stage22四组大尺度平移矩形共20个半宽前驱值的C++ fallback候选均因少于3点而失败，单组件`OffsetShape`现返回与Rust相同消息，C++额外`OffsetShapes`仍返回空组件vector。凹L阈值测试中旁路POI短路会在两个尺度产生Rust已判空的小组件，因此短路保留。resolver最多处理8192个outline点、8192条工作段和64轮，要求x/y跨度不超过`INT32_MAX`以保证int64叉积安全，且使用O(n²)成对交点扫描；它只支持单条outline，未实现多环/洞、通用布尔overlay和全部数值退化。无法解析的输入仍可能进入旧straight-skeleton/fallback；NaN的C++错误出口对应Rust Release，与Rust Debug panic不同。因此本矩阵只证明受限子集对齐，不能证明Offset整体等价。

## 补充审计（2026-09-29）

**历史本地fork审计（2026-09-29）**：当时逐行复核本地依赖的 `shape_modification.rs` offset 段（338-379 行：offset_shape 单函数）。该行段归属只描述本地fork及旧的 Oracle 基线，不是当前D5 `geo::Buffer` 实现。以下结论仅作历史来源记录：

### 【浅覆盖→补齐】geo-buffer 弧步进常量 0.1 的源码级确认
- 主记录差异表 #12 称 ROUNDED_STEP "源码可核对"。现核对：geo-buffer-0.2.0/src/skeleton/mod.rs:274 `left_normal.rotate_by(if orient {0.1} else {-0.1})`——**常量 0.1 为 f64 字面量**（无命名常量）；C++ StraightSkeleton.cpp:28 `constexpr double ROUNDED_STEP = 0.1` + cpp:592-593 `rotate(polygonOrientation ? ROUNDED_STEP : -ROUNDED_STEP)`。数值与符号分支（orient 控制 ±）逐字对应。**注意 Rust 的 0.1 无后缀默认 f64**——与 C++ double 0.1 位级相同（0.1 的最近 f64）。
- 判定：【一致】（常量位级 + 符号分支）。
- 证据：geo-buffer mod.rs:274 vs StraightSkeleton.cpp:28, 592-593。

### 【浅覆盖→补齐】get_orientation 与 C++ polygonOrientation 的判定式核对
- geo-buffer mod.rs:319-323 `get_orientation()`：`self.ray_vector[0].unwrap_base_ray().0（左射线）.orientation(ray_vector[0].ray().point_by_ratio(1.)) == 1`——用**顶点 0 的左射线对角线方向**判定。C++ StraightSkeleton.cpp:545-547：`m_Vertices[0].leftRay.orientation(m_Vertices[0].ray().point(1.0)) == 1`——**逐字对应**（首个顶点、左射线、角平分线单位点、==1）。主记录差异表 #11 称"已对齐"，此处补源码行号证据。
- 判定：【一致】（确认）。
- 证据：geo-buffer mod.rs:319-323 vs StraightSkeleton.cpp:545-547。

### 【浅覆盖→补齐】圆角起点/终点的闭合点语义（left_normal 起、right_normal 止）
- geo-buffer mod.rs:259-281：循环先 push `left_normal.point_by_ratio(time_left)`，rotate 步进，以 left_normal 到达 right_normal 方向为退出条件，退出后 push `right_normal.point_by_ratio(time_left)`。C++ StraightSkeleton.cpp:596-600：`while(true){ push(leftNormal.point(remaining)); leftNormal.rotate(±ROUNDED_STEP); if ((polygonOrientation && sideOrientation == -1) || ...) break; } push(rightNormal.point(remaining))`——**循环体次序完全一致**（先 push 当前 left、再旋转、再判退出、循环外补 right 终点）。
- 判定：【一致】（逐语句）。
- 证据：geo-buffer mod.rs:259-281 vs StraightSkeleton.cpp:596-600。

### 【浅覆盖→补齐】cleanup 调用点分布的最终核对（修复注记的验证）
- 2026-09-29 更新提到"修复主循环 cleanup 分叉（Rust 每次迭代末尾含 continue 路径清理）"。核对 geo-buffer mod.rs 的 cleanup 出现点：420（apply_event 内 EdgeEvent 分支后）、474、497（split 事件处理路径）与 312（make_shrink 相关）——Rust 主循环（构建 skeleton 的 timeline 循环）在**每轮迭代末尾**（mod.rs:497 `vertex_queue.cleanup()` 在 SplitEvent 分支闭合后、循环体末）统一清理。C++ StraightSkeleton.cpp 的 cleanup 出现于 552（loops 重放循环，与 Rust 的事件重放对应）、755/785/792/795/802/826（buildEvents 各分支）。行号分布与修复注记描述吻合。
- 判定：【一致】（维持修复后基线）。
- 证据：geo-buffer mod.rs:312, 420, 474, 497 vs StraightSkeleton.cpp:552, 755-826。

### 【历史基线】非有限距离边界 oracle 证据链
- `--offset-boundary-parity-only` 当前仍会对 NaN/±Inf 使用 `catch_unwind` 并输出边界值；但 `geo-buffer = "0.2"` 是 Oracle 的独立诊断依赖，不能证明 pinned jagua-rs 的 `offset_shape` 使用该 crate。
- 当前D5实测：Rust Debug 的 NaN 路径 panic，Rust Release 的 NaN 路径为空多边形错误；±Inf 在 Debug/Release 均为空多边形错误。该 profile 差异和C++的错误映射需单独看待。
- 证据：`JaguarCore/Tools/RustOracle/Cargo.toml` 的直接 Git revision；锁定checkout中的 `shape_modification.rs`；本轮 Debug/Release `--offset-boundary-parity-only` 输出。

### 【遗漏→补齐】cfg(test)——offset 段无测试模块；fixture 驱动为 C++ 侧自有
- shape_modification.rs 338-379 无 `#[cfg(test)]`；12 项 fixture 的 Debug/Release 对照属 C++ TestShapeModification 驱动（见更新注记）。判定：不适用（Rust 侧无测试契约）。

补充结论：2026-09-29 的源码级核对（0.1 常量、`get_orientation`、圆角循环、cleanup 分布）确认的是本地fork的 geo-buffer 0.2.0 路径，不能作为D5算法等价证据。2026-10-22第一阶段已补入D5 splitCandidate fixture并实现简单轮廓outline子集；当前剩余范围见本记录末尾的实施更新。

## E1补充复核：D5锁定实现与有限输出矩阵（2026-10-22）

RustOracle直接依赖Cargo.lock锁定的jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。该revision的`shape_modification.rs::offset_shape`使用`geo::Buffer`及`BufferStyle::line_join(LineJoin::Round(0.1))`；`CodeResource/jagua-rs`本地fork的同名函数改用`geo_buffer::buffer_polygon_rounded`。C++保留的`StraightSkeleton` legacy路径重实现了后者的事件模型；新简单outline路径则已与D5有限fixture局部对齐。不能把历史事件模型或fixture覆盖范围当作D5全域等价证据。

迁移前新增`offset_boundary.txt`和`CompareGeometry.cpp::runOffsetBoundaryDifferentialTests`，消费D5 Rust Debug/Release一致的3条有限输出：2×2方形Deflate 0.25，以及1,000,000坐标方形的0.01 Inflate/Deflate小于坐标ULP的情况。该阶段的MSVC14.29结果属于迁移前证据；本轮实现后的验证另见下节。

迁移前同一splitCandidate（`[(0,0),(4,0),(4,4),(2,1),(0,4)]`，Deflate 0.45）有明确算法差异：D5 Rust输出25顶点、面积`2.898932695`；旧C++路径输出3顶点、面积`1.419689416885`。第一阶段已将D5 25顶点结果纳入fixture及差分入口，C++新outline路径逐顶点、面积和方向均通过比较；旧输出只作为历史差异记录。

## D5算法迁移可行性核对（2026-10-22）

- `JaguarCore/Tools/RustOracle/Cargo.lock` 锁定 `geo 0.33.1`、`i_overlay 4.5.2`、`i_float 1.16.0`。D5 `geo` 的 `Polygon::buffer_with_style` 先规范环方向，再把rings转换为shape paths，调用 `subject.outline(&style.outline_style())`，最后将overlay shapes重建为 `MultiPolygon`（`geo-0.33.1/src/algorithm/buffer.rs:392-399`）；`outline_style` 将距离和 `LineJoin` 交给 `i_overlay::mesh::outline::offset::OutlineOffset`（同文件:244-247）。
- 因此D5结果由圆角offset轮廓生成、浮点布尔overlay消解自交/交叠、组件重建共同决定。仅调整当前straight-skeleton的事件顺序或容差无法覆盖这条算法路径。
- JaguarCore工程当前没有几何buffer/overlay第三方C++依赖（`JaguarCore.pro:4`禁用Qt Core/Gui）。若维持无Rust运行时的既有边界，需移植 `geo::Buffer` 实际用到的outline与overlay核心子集，再逐步以D5 fixture替换现有Offset路径；完整 `i_overlay` crate包含95个Rust源文件、约16.8k行，实际所需子集尚未裁剪，不能据此声称是小型局部改动。Rust静态库/FFI可缩短算法复用路径，但会改变构建和运行时依赖边界。
- 用户已选择保持无Rust运行时并分阶段移植C++子集；第一阶段实施结果如下。

## 第一阶段实施：简单outline路径（2026-10-22）

- `Offset.cpp`将现有outline helper扩展为Inflate/Deflate共用路径，保留int32适配器、`Round(0.1)`采样和Rust f32导入；成功生成简单有效轮廓后不再经过`preservesModificationMode`或旧fallback覆盖。
- 对Deflate距离不小于最大内切圆半径的输入提前返回空组件，使`OffsetShape`继续映射到D5空多边形错误。
- `offset_boundary.txt`增加`splitCandidate`完整25顶点D5首结果；`CompareGeometry`逐顶点比较加入该输入。RustOracle锁定Debug/Release输出一致；LLVM-MinGW C++自建Debug/Release差分runner、形状修改runner均通过。
- 该路径只覆盖量化后的单一简单轮廓。自交、量化退化或拓扑变化导致outline导入失败时，当前仍落入旧straight-skeleton/fallback；多组件和overlay输出顺序尚未与D5对齐。Rust Debug NaN panic与Release错误差异也仍未处理。本轮没有用MSVC重建验证。

## 第二阶段实施：有限自交outline重建（2026-10-22）

- `Offset.cpp`新增有界整数平面图resolver：对量化后的outline线段求交并切分半边，按有向边贡献传播winding数，再提取填充区域边界、清理共线点、按D5规则旋转/排序组件并经Rust导入路径重建。仅当outline不能直接导入为单一简单多边形时尝试该resolver；限制为最多2048点且x/y跨度不超过`INT32_MAX`，不支持多个输入ring/洞或任意通用overlay，超限或解析失败时保留原legacy过渡路径。
- RustOracle tooling新增直接锁定`geo 0.33.1`的D5 `geo::Buffer`多组件输出探针；这只是Oracle工具依赖，不进入JaguarCore生产构建。fixture新增窄桥Deflate（2组件，各27顶点）和comb Deflate（3组件，16/28/16顶点），记录组件数、全部有序顶点及面积；C++ `OffsetShapes`逐组件比较，`OffsetShape`比较首组件。
- Rust Debug/Release的7条多组件fixture行一致；C++ LLVM-MinGW Debug/Release完整Offset差分runner重建后均通过，形状修改专项runner通过。坐标和面积按`AlmostEqualRustF32`比较，组件数/顶点数严格相等，顶点环允许循环起点旋转。本轮未用MSVC验证。
- 结论限于已登记的6个有限输入配置及两种overlay输出拓扑。超出2048点或安全坐标跨度的outline会拒绝resolver；通用多环/洞和其他自交、共线重叠、量化退化及非有限距离边界仍待移植或裁决；Offset/E1仍为部分完成。

## 第三阶段验证：临界接触与sub-grid舍入（2026-10-22）

- 在已有fixture上新增4个有限输入配置：窄桥Deflate 1.0、comb Deflate 2.0、2×2方形Deflate 1e-9，以及百万坐标方形Inflate 1e-5。前两者固定了特征恰好收缩到接触/分裂阈值的结果；后两者固定了offset小于int overlay网格尺度时的输出舍入。
- RustOracle锁定D5的`geo::Buffer`输出补入`offset_boundary.txt`；Rust Debug/Release的20条有限fixture行逐行一致。C++ `CompareGeometry`新增`OffsetShapes`逐组件和`OffsetShape`首组件断言；新矩阵全部通过，因此本轮只扩展覆盖，没有再改生产算法。
- LLVM-MinGW与VS2022 MSVC14.44的全源码Offset差分runner均在Debug/Release通过；MSVC通过直接`cl` response-file构建，未依赖qmake。MSVC Release显式保留断言。LLVM-MinGW仍报告仓库现存`MSpp.h`大小写路径警告。
- 当前fixture共10个有限输入配置，含14条多组件计数/轮廓行。MSVC Debug临时插桩确认comb Deflate 2.0在resolver中触发两次正长度共线重叠处理；这只证明该固定输入实际进入该分支，不能推出所有共线重叠、量化退化或一般`i_overlay`边界均已支持。Offset/E1仍为部分完成。

## 第四阶段验证：稳定错误出口fixture（2026-10-22）

- 将D5 Rust Debug/Release共有的正无穷、负无穷和方形Deflate 2.0错误输出登记到`offset_boundary.txt`；`CompareGeometry`活动读取器检查Rust参考错误文本前缀、C++ `OffsetFailed`代码和消息。
- 按测试优先验证：缺少错误fixture时MSVC Debug runner在读取首个缺失键处失败；登记三条Rust输出后，Rust Debug/Release各3条精确匹配，MSVC 14.44 Debug/Release全源码Offset差分runner均退出码0，Release仍保留断言。
- 本阶段没有改生产算法。第四阶段结束时，Rust Debug NaN panic与Release错误尚未加入此活动fixture比较；其后续裁决见第五阶段。一般overlay边界继续开放，Offset/E1仍为部分完成。

## 第五阶段验证：NaN profile出口裁决（2026-10-22）

- RustOracle将NaN结果分别输出为`offset.boundary.nan.debug`与`offset.boundary.nan.release`；fixture同时锁定Debug `panic`和Release `error:Offset resulted in an empty polygon`。每个Rust profile只输出并匹配自身对应的profile行。
- C++ `CompareGeometry`读取两条Rust基准，断言Debug参考为`panic`，并核对C++ `OffsetShape(NaN)`的`OffsetFailed`代码和消息与Rust Release相同；因此固定确认C++返回错误、而Rust Debug会panic的失败通道差异。
- 按测试优先验证：缺少profile行时MSVC Debug runner失败；补齐Oracle输出键和fixture后，Rust Debug/Release各24条活动参考行与fixture匹配，`cargo fmt --check`通过；MSVC 14.44 Debug/Release差分runner退出码均为0，Release断言保持启用。未改Offset生产算法。

## 第六阶段：量化重复点与共线重叠回归（2026-10-22）

- 新增T形Deflate 2.0回归：D5输出1个33顶点组件；修复前C++在量化outline含相邻重复点时因resolver拒绝零长度线段而回退，返回0个组件。按失败优先复现后，在导入/解析前去除连续及首尾重复点，C++组件数、面积和顶点与D5 fixture一致。
- 新增H形Deflate 2.0回归：Rust Debug/Release均输出2个33顶点组件；C++ `OffsetShapes`逐组件比较，`OffsetShape`比较首组件。去重后的MSVC Debug临时插桩确认该输入进入resolver并处理1处正长度共线重叠。
- Rust Debug/Release各29条活动fixture行逐字匹配；`cargo fmt --check`通过。VS2022 MSVC 14.44直接构建的全源码Offset差分runner在Debug/Release均退出码0，Release断言保持启用。T形负责锁定量化重复点修复，H形负责锁定resolver共线重叠分支；此结论仍限于固定fixture，不代表通用overlay支持，Offset/E1仍部分完成。

## 第七阶段：U/C形临界收缩与组件顺序（2026-10-22）

- 新增U形与C形Deflate 2.0固定输入；D5 Debug/Release均输出两个18顶点分量。将D5组件坐标写入活动fixture，并让`CompareGeometry`比较全部分量及`OffsetShape`首分量。
- 失败优先验证先因缺少fixture键失败；补入Rust参考后，C形首分量面积揭示组件集合虽一致但顺序相反。C++ resolver原先仅按分量最小x稳定排序，x相同时沿遍历顺序保留，导致下方与上方分量的次序不同于D5。改为按量化最小点(x,y)字典序排序后，C++两分量和首分量均通过RustF32差分比较。
- Rust Debug/Release各35条profile-active fixture行与参考一致，`cargo fmt --check`通过；VS2022 MSVC 14.44全源码Offset差分runner的Debug/Release均退出码0，Release断言保持启用。临时探针确认U/C形各进入一次正长度共线重叠处理。
- 该排序仅由当前登记的固定fixture支持；i_overlay组件提取、其他并列拓扑和一般overlay排序仍未实现或裁决，Offset/E1继续标记为部分完成。

## 第八阶段：十字形临界收缩（2026-10-22）

- 新增十字形（plus-shape）Deflate 2.0固定输入，C++差分比较`OffsetShapes`全部分量及`OffsetShape`首分量。先运行C++验证缺失Rust fixture键时按预期失败；随后从锁定D5 Rust revision采集Debug参考，Rust Debug/Release均输出1个64顶点分量、面积3.453804493。
- Rust Debug/Release输出逐行一致；C++ VS2022 MSVC 14.44全源码Offset差分runner在Debug/Release均退出码0，Release断言保持启用。Rust Debug/Release各37条profile-active fixture行匹配，`cargo fmt --check`通过。
- 本轮只验证新增固定输入，未改Offset生产算法。十字形结果不证明一般临界收缩、组件提取或overlay等价；Offset/E1仍部分完成。

## 第九阶段：普通方形Inflate（2026-10-22）

- 新增2×2方形Inflate 0.25固定输入，在活动C++比较器中核对完整`OffsetShape`结果。先编译并运行差分入口，因缺少`offset.finite.square_inflate`参考键而按预期失败。
- 锁定D5 Rust Debug/Release输出逐行一致，结果为68顶点；将唯一输出行登记到fixture。LLVM-MinGW scalar-fallback全源码Offset差分runner的Debug/Release均退出码0，Release使用`-DNDEBUG -UNDEBUG`保持断言开启；Rust Debug/Release各38条profile-active fixture行匹配，`cargo fmt --check`通过。
- 本轮未改Offset生产算法。该用例补齐普通方形扩张的固定覆盖，不代表通用Inflate、非正交圆角或overlay等价；Offset/E1仍部分完成。

## 第十阶段：斜边凸五边形Inflate（2026-10-23）

- 新增斜边凸五边形Inflate 0.5固定输入，C++活动差分比较完整`OffsetShape`结果。先运行差分入口，因缺少`offset.finite.oblique_pentagon_inflate`参考键按预期失败。
- D5 Rust Debug/Release输出完全相同，为69顶点；新增fixture后，C++ LLVM-MinGW scalar-fallback全源码Offset runner的Debug/Release均退出码0，Release保持断言开启。Rust Debug/Release各39条profile-active fixture行匹配，`cargo fmt --check`通过。
- 本轮未改Offset生产算法。该样例补充斜边圆角采样覆盖，不证明所有非正交角度、凹轮廓或overlay等价；Offset/E1仍部分完成。

## 第十一阶段：凹L形Inflate（2026-10-23）

- 新增凹L形Inflate 0.5固定输入，C++活动差分比较完整`OffsetShape`结果。先编译并运行差分入口，确认缺少`offset.finite.concave_l_inflate`参考键时按预期失败；Rust Debug/Release输出一致，为86顶点、面积32.730175018。
- 登记D5 pinned输出后，Rust Debug/Release各40条profile-active fixture行匹配；LLVM-MinGW scalar-fallback全源码Offset差分runner Debug/Release均通过，Release通过`-DNDEBUG -UNDEBUG`保持断言开启，`cargo fmt --check`通过。
- 本轮未改生产Offset算法，只补凹角Inflate的固定覆盖；不证明凹轮廓一般情形、自交拓扑或overlay等价，E1与Offset仍部分完成。

## 第十二阶段：凹L形Deflate（2026-10-23）

- 新增同一凹L形Deflate 0.5固定输入，C++活动差分比较完整`OffsetShape`结果。先运行缺少`offset.finite.concave_l_deflate` fixture键的差分runner，确认按预期失败。
- 锁定D5 Rust Debug/Release输出一致，为22顶点、面积9.053965569；登记fixture后，Rust Debug/Release各41条profile-active行与fixture逐字匹配，另有两条未纳入活动比较器的历史诊断输出。`cargo fmt --check`通过；LLVM-MinGW scalar-fallback全源码Offset runner Debug/Release均通过，Release使用`-DNDEBUG -UNDEBUG`保持断言开启。
- 本轮未改生产Offset算法，只补凹角Deflate固定覆盖；不证明凹轮廓一般输入、自交拓扑或overlay等价，E1与Offset仍部分完成。

## 第十三阶段：凹L形Deflate 1.0近消失（2026-10-23）

- 在同一凹L形上增加Deflate 1.0固定输入。锁定D5 Rust Debug/Release均输出单个18顶点组件、面积0.215862826；C++活动比较器分别核对`OffsetShapes`完整组件及`OffsetShape`首组件。
- 先运行不含新fixture键的C++ Debug runner，按预期因缺少`offset.finite.concave_l_deflate_1_0`触发断言；登记Rust参考后重建LLVM-MinGW scalar-fallback全源码runner，Debug/Release均通过，Release通过`-DNDEBUG -UNDEBUG`保持断言开启。Rust Debug/Release各42条profile-active行逐字匹配；fixture共43个唯一键（38条有限结果、3条稳定错误、2条profile-specific NaN），`cargo fmt --check`通过。
- 本轮未改生产Offset算法，只补凹轮廓小面积剩余区的固定覆盖；不外推至普遍消失阈值或一般overlay，E1与Offset仍部分完成。

## 第十四阶段：凹L形Deflate消失阈值（2026-10-23）

- 对同一凹L形以相邻`f32`输入夹定几何消失阈值两侧：1.1715728044509888（`0x3f95f619`）与1.1715729236602783（`0x3f95f61a`）。锁定Rust Debug/Release在低侧均报`Simple polygon must have at least 3 points`，高侧均报`Offset resulted in an empty polygon`。
- 活动C++比较器核对两个Rust fixture出口。C++高侧错误码和消息均一致；低侧C++同为`OffsetFailed`，但其10层PointOfInaccessibility估值为1.1711455583572388，已将该输入视作消失并返回通用空结果消息。此处记录并固定消息差异，不修改POI或Offset生产算法，以免由一个边界输入推导出未验证的通用空结果映射。
- C++差分runner在缺少fixture键时先按预期失败；补入两条Rust错误fixture后，Rust Debug/Release各44条profile-active输出匹配fixture，`cargo fmt --check`通过，LLVM-MinGW scalar-fallback全源码Offset runner Debug/Release均通过，Release断言保持启用。该矩阵只支持该凹L形相邻`f32`边界，不证明其它轮廓的消失阈值；E1与Offset仍部分完成。

## 第十五阶段：凹L形消失阈值的尺度敏感性（2026-10-23）

- 将凹L顶点统一按0.5×、2×、10×缩放，在每个尺度探测理论最大内切半径对应的`f32`前驱值与最近阈值值。Rust Debug/Release在三组输入上均稳定表现为：前驱值报`Simple polygon must have at least 3 points`，最近阈值值报`Offset resulted in an empty polygon`；新增6条稳定错误fixture。
- C++10层POI半径依次为0.585572779、2.342291117、11.711456299，约为对应Rust消失阈值的99.9636%。当前短路下低侧提前返回空结果，错误码与Rust一致但消息不同；阈值值的错误消息一致。临时全源码实验旁路POI短路后，0.5×和2×阈值值返回22顶点小多边形而Rust为空，10×仍为空；因此不能通过整体删除短路来修正错误消息，生产代码保持不变。
- C++活动比较器逐项消费新fixture并显式记录低侧消息差异；Rust Debug/Release各50条profile-active输出匹配fixture，`cargo fmt --check`通过；LLVM-MinGW scalar-fallback全源码Offset runner的C++ Debug/Release均通过，Release断言保持启用。结果限于同一凹L形的三个尺度，不证明一般多边形阈值行为；E1与Offset仍部分完成。

## 第十六阶段：U形凹口闭合Inflate（2026-10-23）

- 对既有U形轮廓执行Inflate 2.25；凹口宽4，两侧圆弧相交，D5 Rust Debug/Release输出同为93顶点、面积233.474487305。该候选轮廓在两个内侧圆角处发生相交，活动C++比较器覆盖解析后的`OffsetShapes`组件及`OffsetShape`首结果。
- 新增1条有限fixture后，Rust Debug/Release各51条profile-active行与fixture一致；完整fixture 52个唯一键（39条有限输出、11条稳定错误、2条profile-specific NaN配置行），`cargo fmt --check`通过。LLVM-MinGW scalar-fallback全源码Offset runner在Debug/Release均通过，Release断言保持启用。
- 本轮未改生产Offset算法。结果限于此U形凹口闭合输入；多环/洞、一般布尔overlay、广泛退化及fallback仍未闭合，E1与Offset仍部分完成。

## 第十七阶段：斜向U形凹口闭合Inflate（2026-10-23）

- 将既有U形轮廓按3-4-5旋转构造斜向输入，执行Inflate 2.25；内凹圆角仍相交。D5 Rust Debug/Release均输出93顶点、面积233.474487305；C++活动比较器核对`OffsetShapes`单组件及`OffsetShape`首结果。
- 新增1条有限fixture后，Rust Debug/Release各52条profile-active行与fixture一致；完整fixture 53个唯一键（40条有限结果、11条稳定错误、2条profile-specific NaN配置行），`cargo fmt --check`通过。LLVM-MinGW scalar-fallback全源码Offset runner在Debug/Release均通过，Release断言保持启用。
- 本轮未改生产Offset算法。该用例仅扩展旋转后的单outline自交证据，不证明通用overlay、多环/洞或fallback等价，E1与Offset仍部分完成。

## 第十八阶段：tiny Deflate fallback可达性与已知差异（2026-10-23）

- 构造短边为2^-21、长边为2^-20且起点为(2^-20,2^-20)的矩形，Deflate距离取短边一半的前一个f32值（0x347fffff）。该输入通过C++ POI半径保护，触发后续空skeleton结果的fallback路径。
- RustOracle Debug/Release对同一输入均返回Simple polygon must have at least 3 points。临时插桩在C++ Debug/Release均观测到fallbackOffsetShape被调用；生产C++ OffsetShapes在两种配置均返回1个4顶点组件，OffsetShape也成功。因此这是已确认的Rust错误出口与C++成功结果分歧，不能计作等价通过。
- 将Rust错误行加入活动fixture，CompareGeometry断言错误参考及C++成功出口。Rust Debug/Release各55条原始输出均与fixture值一致；其中53条profile-active键直接比较，两条首组件别名按对应component0 fixture值核对。profile-active为53条（40条有限结果、12条稳定错误、1条profile-specific NaN），完整fixture为54个唯一键。C++ LLVM-MinGW scalar-fallback全源码runner Debug/Release均通过，Release使用-DNDEBUG -UNDEBUG保持断言开启；cargo fmt --check通过。
- 本轮未修改生产Offset算法。此样例确认了一个fallback可达输入和一项已知分歧，不覆盖一般小尺度几何、其他fallback输入或通用overlay；E1与Offset仍部分完成。

## 第十九阶段：tiny Deflate相邻f32阈值矩阵（2026-10-23）

- 沿用第十八阶段矩形与坐标，只改变Deflate距离。补测半宽前连续5个f32值（0x347ffffb至0x347fffff）、恰好半宽（0x34800000）及其后继（0x34800001）。
- 锁定Rust Debug/Release在前5个值均返回Simple polygon must have at least 3 points；恰好半宽及其后继均返回Offset resulted in an empty polygon。C++ Debug/Release插桩对前5个值各观测到OffsetShapes和OffsetShape两次fallback调用，二者均成功返回一个组件；临界值和后继不进入fallback，OffsetShapes成功返回空vector，OffsetShape错误消息与Rust相同。
- 为新增4个fallback前驱值和2个临界控制值登记Rust fixture，并由CompareGeometry活动比较器逐项核对Rust错误参考、C++单组件结果或空结果/错误出口。Rust Debug/Release各61条原始输出与fixture或首组件别名值对应；59条profile-active输出（40条有限结果、18条稳定错误、1条profile-specific NaN），完整fixture共60个唯一键。C++ LLVM-MinGW scalar-fallback全源码runner Debug/Release均通过，Release断言启用；cargo fmt --check通过。
- 当前证据把该输入的成功fallback差异限定在半宽前连续5个f32距离；不外推到其他尺度、平移、形状或一般fallback输入。本轮未改生产算法，E1与Offset仍部分完成。

## 第二十阶段：两组大尺度近半宽Deflate的fallback复核（2026-10-23）

- 从585个可构造的固定长宽/坐标指数/前驱距离组合中筛出短边指数7与20、起点指数-20的矩形。Deflate距离均取半宽下第五个f32值（分别为0x427ffffb、0x48fffffb）。
- C++ Debug/Release临时插桩确认这两个输入均进入fallback并成功返回单个4顶点组件。Rust Debug/Release均返回单个4顶点组件；活动比较器逐顶点/面积比较全部组件及OffsetShape首结果，两组均在RustF32容差内匹配，面积分别为0.004394531和360448。
- 增加两条Rust fixture和两个活动有限结果配置。Rust Debug/Release各63条原始输出均与fixture或首组件别名值对应；61条profile-active输出（42条有限结果、18条稳定错误、1条profile-specific NaN），完整fixture共62个唯一键。C++ LLVM-MinGW scalar-fallback全源码runner Debug/Release均通过，Release断言启用。
- 此结果只证明两组所选尺寸和坐标下fallback输出与Rust一致，不抵消半宽前5个f32值已确认的微小矩形分歧，也不代表其他尺寸/位置的一般fallback等价。E1与Offset仍部分完成。

## 第二十一阶段：大尺度近半宽Deflate的平移错误出口（2026-10-23）

- 沿用第二十阶段短边指数2^7与2^20的矩形及半宽前第五个f32距离（0x427ffffb、0x48fffffb），将起点分别设为2^0和2^20，形成4个有限输入组合。
- Rust Debug/Release四个组合均返回`Simple polygon must have at least 3 points`。C++ Debug/Release插桩确认每个组合都进入fallback；`OffsetShapes`成功返回空组件vector，`OffsetShape`返回`OffsetFailed`和`Offset resulted in an empty polygon`。这是错误出口分歧，不是有限几何结果对拍。
- 四个Rust错误fixture由CompareGeometry活动断言消费。Rust Debug/Release各67条原始输出与fixture或首组件别名值对应；65条profile-active输出（42条有限结果、22条稳定错误、1条profile-specific NaN），fixture共66个唯一键。C++ LLVM-MinGW scalar-fallback全源码Offset runner Debug/Release通过，Release断言启用；`cargo fmt --check`通过。
- 本轮未改生产Offset算法。证据仅限所列尺寸、坐标与距离，不证明一般fallback或overlay等价；E1与Offset仍部分完成。

## 第二十二阶段：大尺度平移矩形的相邻f32错误边界（2026-10-23）

- 将第二十一阶段4个有限输入组合扩展为半宽前连续5个f32距离：短边指数2^7对应0x427fffff至0x427ffffb，短边指数2^20对应0x48ffffff至0x48fffffb；起点分别为2^0和2^20，共20个配置。
- Rust Debug/Release的20个输出均为`Simple polygon must have at least 3 points`。C++ Debug/Release插桩对每个配置均观察到`fallbackOffsetShape`被`OffsetShapes`和`OffsetShape`各调用一次；最终`OffsetShapes`为空成功，`OffsetShape`为`OffsetFailed`且消息为`Offset resulted in an empty polygon`。
- 新增16条稳定错误fixture，CompareGeometry活动循环覆盖全部20个配置。Rust Debug/Release各83条原始输出与fixture或首组件别名值对应；81条profile-active输出（42条有限结果、38条稳定错误、1条profile-specific NaN），fixture共82个唯一键。C++ LLVM-MinGW scalar-fallback全源码runner Debug/Release通过，Release断言启用；`cargo fmt --check`通过。
- 本轮未改生产Offset算法。结论限于所列4种尺寸/起点组合及半宽前5个距离，不外推至其它位置、尺寸或一般fallback；E1与Offset仍部分完成。

## 第二十三阶段：平移矩形Deflate错误出口收口（2026-10-23）

- 沿锁定Rust revision核对`offset_shape`导回链：`geo::Buffer`外轮廓坐标转成f32后进入`import_simple_polygon`，连续近似重复点被移除，随后`SPolygon::new`因剩余点数少于3而返回错误。C++ fallback对Stage22的20个配置也在导入候选时得到少于3点错误；Deflate的`OffsetShapes`为保留多组件API的消失语义将其返回为空成功，单组件`OffsetShape`此前把空vector转换成通用空偏移错误。
- 在单组件`OffsetShape`的凸形Deflate空结果出口补充窄范围恢复：仅当距离严格小于POI半径、fallback明确报少于3点时，返回与Rust一致的`OffsetFailed`消息。`OffsetShapes`继续返回成功的空vector。
- CompareGeometry覆盖四组尺寸/平移组合与半宽前连续5个f32值，共20项。LLVM-MinGW scalar-fallback全源码runner的Debug与`-O2` Release均通过，Release断言有效；RustOracle Debug/Release各20条Stage22结果与fixture逐字匹配。Rust Debug的NaN探针会输出已捕获panic hook诊断，进程正常退出。
- 这只收口Stage22已覆盖的凸形矩形错误出口，不外推到一般POI估计、其它凸/凹边界、fallback或overlay等价；E1与Offset仍部分完成。

## 第二十四阶段：tiny Deflate尺度相关交点判定修正（2026-10-23）

- Stage19的tiny矩形短边为`2^-21`、长边为`2^-20`，半宽前5个距离均经POI保护进入fallback。根因为`intersectLines`使用绝对行列式阈值`1e-12`：该几何的垂直线行列式约`4.55e-13`，被误判为平行，fallback错误采用平均法线偏移并保留4点组件。
- 将平行判定改为相对阈值`1e-12 * |d1| * |d2|`，恢复垂直偏移线交点计算；保留`distance < POI.radius`的严格边界。既有5条fixture现在均由C++ `OffsetShapes`返回空成功、`OffsetShape`返回`Simple polygon must have at least 3 points`，与Rust错误出口一致；恰好半宽和后继值继续匹配空偏移错误。
- CompareGeometry复用现有7个边界fixture键，无Rust fixture改动。LLVM-MinGW scalar-fallback全源码runner在Debug和`-O2` Release通过，Release断言启用；全套`JaguarCoreTests`未运行。本轮只覆盖这组tiny矩形阈值，不外推至一般尺度交点、fallback或overlay等价；E1与Offset仍部分完成。

## 第二十五阶段：tiny Deflate尺度矩阵复核（2026-10-23）

- 沿用Stage19的2:1矩形与半宽前一个`f32`距离，增加短边`2^-24`、`2^-30`、`2^-38`、`2^-40`、`2^-42`五个尺度，起点均设为短边的2倍。锁定Rust Debug/Release五项均返回`Simple polygon must have at least 3 points`。
- CompareGeometry新增活动循环，并在缺fixture时先按预期失败；登记Rust参考后，C++ `OffsetShapes`五项均为空成功结果，`OffsetShape`均返回同一错误。LLVM-MinGW scalar-fallback全源码runner的Debug与`-O2 -UNDEBUG` Release均通过，Rust Debug/Release各86条profile-active行匹配fixture。
- 新增5条稳定错误fixture后，完整fixture为87个唯一键（42条有限结果、43条稳定错误、1条profile-specific NaN）；`cargo fmt --check`通过。本轮未改生产Offset算法；结果只覆盖同一2:1矩形和五个已列尺度，不外推至一般轮廓、平移或overlay等价，E1与Offset仍部分完成。

## 第二十六阶段：tiny Deflate半宽控制值跨尺度复核（2026-10-23）

- 在Stage25的5个短边尺度（`2^-24`、`2^-30`、`2^-38`、`2^-40`、`2^-42`）上，分别核对恰好半宽和后继`f32`距离。锁定Rust Debug/Release十项均返回`Offset resulted in an empty polygon`。
- CompareGeometry为十项增加活动断言：C++ `OffsetShapes`返回空vector，`OffsetShape`返回同一空偏移错误。缺fixture时先确认C++ runner因缺失键失败；补入Rust结果后，LLVM-MinGW scalar-fallback全源码Debug与`-O2 -UNDEBUG` Release均通过。
- Rust Debug/Release各96条profile-active行匹配fixture，完整fixture为97个唯一键（42条有限结果、53条稳定错误、1条profile-specific NaN）；`cargo fmt --check`通过。本轮只增加固定尺度边界控制，不改生产算法，不证明一般输入等价，E1与Offset仍部分完成。

## 第二十七阶段：梳形轮廓双凹槽同步闭合Inflate（2026-10-23）

- 复用既有梳形简单多边形，以Inflate 2.25同时闭合两条2单位宽凹槽，覆盖两条凹槽同步闭合后形成的候选输出边界。D5 Rust Debug/Release均输出1个90顶点组件，面积为432.565429688。
- RustOracle新增组件数与完整有序轮廓fixture；CompareGeometry同时比较C++ `OffsetShapes`全组件和`OffsetShape`首组件。LLVM-MinGW scalar-fallback全源码Offset定向runner的Debug与`-O2 -UNDEBUG` Release均通过，Release断言有效。
- Rust Debug/Release各98个profile-active fixture项与Oracle输出匹配（44条有限结果、53条稳定错误、1条活动NaN）；完整fixture共99个唯一键，`cargo fmt --check`通过。LLVM-MinGW标量回退全源码临时插桩构建成功并运行成功：梳形Inflate 2.25的140点outline进入resolver，产生2对非平行相交、2对共线相交，构成142个节点、144条无向边和4个面；winding传播后3个面填充、最大正winding为2，最终提取1条边界并返回1个component。该固定输入确认走完整resolver重建路径，不代表所有交点取整、winding冲突、退化出口均被覆盖；插桩只在build目录副本中，本轮没有修改生产算法。通用overlay与legacy fallback仍未收口，E1与Offset保持部分完成。

## 第二十八阶段：i_overlay交点取整与轴向除法对齐（2026-10-23）

- 按锁定Rust revision `f5e2b5f`追踪`geo 0.33.1`的Buffer结果进入`i_overlay 4.5.2`的单outline resolver。`Segment::create_and_validate`会规范化端点，交点处理先按端点排序；`cross_point`以目标线段起点为局部原点，轴向分支使用向零截断的整数除法，斜向分支使用`UInt128`乘除并按绝对值半入取整。
- 新增一组梳形Deflate 2150.4输入：在梳形上按1024倍放大并平移，使同一类交点的单格取整差异放大到超过既有`f32`比较容差。修正前，水平目标线段的两处Rust交点分别比C++结果偏一个overlay格；C++结果组件1的顶点x为`-0.842956543`，Rust为`-0.842987061`，活动差分回归按预期失败。
- `Offset.cpp`现按Rust规则规范化交点线段端点和线段对顺序；轴向交点使用整数截断，斜向交点使用定宽乘除及半入取整。新输入由活动`CompareGeometry`同时比较`OffsetShapes`完整组件和`OffsetShape`首组件，新增5条有限fixture行。
- 锁定Rust Debug/Release新增5行逐字匹配；当前源码构建的LLVM-MinGW scalar-fallback全源码`CompareGeometry` Debug及`-O2 -UNDEBUG` Release差分runner均通过，`cargo fmt --check`通过。活动Offset fixture为104条profile-active输出（50条有限结果、53条稳定错误、1条活动NaN），共105个唯一键。
- 本修正只对齐当前有界单outline resolver中的交点整数规则，不实现`i_overlay`完整snap-radius多轮处理、一般多环/洞overlay或所有大范围/2048点限制路径；不构成Offset整体等价声明，E1与Offset仍部分完成。

## 第二十九阶段：凹角section join与Rust outline对齐（2026-10-23）

- 对照锁定Rust revision `f5e2b5f`的`i_overlay 4.5.2 OutlineBuilder`，定位`triangle_notch` Inflate 3.0首次分歧：Rust以97条offset section边进入自交solver，旧C++直接对凹角做miter后只有95条边。离线复刻Rust section和join规则后，C++生成的97条边集合与Rust一致。
- `Offset.cpp`现逐段输出offset edge；凸角按round/bevel策略连接，凹角依次连接前段顶部端点、原顶点和后段顶部起点。新增有限输入`offset.finite.triangle_notch_inflate_round_fix`，比较`OffsetShapes`完整组件及`OffsetShape`首组件。
- Rust Debug/Release均与新增fixture逐字匹配；活动矩阵更新为每个profile 104行（50条有限结果、53条稳定错误、1条活动NaN），共105个唯一fixture键。全源码C++差分runner在断言启用的`-O0`与`-O2`下通过；该用例输出1个85顶点组件，面积`667.256774902`。
- 本修正只验证已登记凹角样例的outline构造与结果，不代表通用凹角、通用多环/洞overlay或全部数值退化等价；旧fallback与resolver的其他限制仍在，Offset仍部分完成。

## 第三十阶段：同向共线顶点的segment抵消重建（2026-10-23）

- 按锁定Rust revision复核`i_overlay 4.5.2`：同向共线输入点仍会参与section构造，`UniqueSegmentsIter`产生的反向重叠边在segment合并后形成零winding并被移除。C++现在允许正向CCW轮廓包含同向共线点；只在此类源轮廓确实抵消了初始零winding边时启用无交点图重建，普通非共线轮廓仍只有在既有交点标记路径命中时进入图重建。
- 新增正方形一边含中点的`offset.finite.collinear_vertex_square_inflate`，Inflate 0.25。活动比较器分别核对`OffsetShapes`单组件和`OffsetShape`首组件；Rust参考为68顶点、面积`6.196033478`。首轮扩大范围曾使既有凹L近塌缩断言失败；收窄零winding清理与无交点重建条件后，该差分回归恢复通过。
- Rust Debug/Release各有105条活动fixture值与参考逐字匹配，另各输出2条既有首组件别名；`cargo fmt --check`退出码0。当前源码全源`CompareGeometry`由LLVM-MinGW重建，断言启用的`-O0`与`-O2`运行均退出码0；未执行MSVC/qmake正式构建。
- 证据只覆盖此共线正方形样例及既有矩阵回归，不代表通用共线重叠、多轮snap、一般多环/洞overlay或legacy fallback等价；Offset仍部分完成。

## 第三十一阶段：共线梳形Deflate的多组件winding重建（2026-10-23）

- 在既有comb exact-collapse Deflate 2.0输入的顶边插入同向共线点`(19,12)`。锁定Rust Debug/Release均输出3个组件；新增组件数和三条有序轮廓fixture，`CompareGeometry`同时校验`OffsetShapes`全组件和`OffsetShape`首组件。
- 新用例的临时诊断显示，outline未通过`SimplePolygon::Create`；resolver建图后有14个面，旧的单一外部面winding root只传播到5个面并失败。C++现将每个负有向面积外部面初始化为winding 0，以覆盖不连通图中各分量自己的外部面；共线源轮廓仍先尝试直接导入，只有导入失败才调用resolver。
- 锁定Rust Debug/Release各109条profile-active值均与fixture逐字一致（55条有限结果、53条稳定错误、1条活动NaN），fixture共110个唯一键；另有2条既有首组件别名输出。`cargo fmt --check`通过。LLVM-MinGW scalar-fallback全源码差分runner在断言启用的`-O0`与`-O2 -UNDEBUG`下均通过。
- 本轮只验证这组含冗余共线点的梳形精确收缩，以及完整Offset固定矩阵回归；不证明通用不连通overlay、多环/洞或legacy fallback等价，E1与Offset仍部分完成。
