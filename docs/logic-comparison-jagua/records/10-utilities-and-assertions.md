# 数值辅助与断言记录

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 对应**：`JaguarCore/Include/JaguarCore/Geometry/RustF32.h`、`Assertions.h`、`NumericPolicy.h`；`JaguarCore/Source/Support/Assertions.cpp`  
**判定**：B9 的静态比较规则、断言映射、相邻源码归属和最小边界 fixture 矩阵已登记；未运行边界对拍，不能据此标为数值或断言行为等价

## `util/fpa.rs`

Rust `FPA(pub f32)`、泛型 `From<T: Into<f32>>`、`PartialEq`、`PartialOrd` 与 `Display` 位于 pinned 源码 7–37 行。`PartialEq` 用锁定的 `float-cmp 0.10.0` 默认 `F32Margin`：`epsilon = f32::EPSILON` 或 ordered-bit ULP 距离不超过 4；`PartialOrd` 先将近似相等值视为 `Equal`，否则调用 `f32::partial_cmp`。因此非有限值通常比较为 `None`，但 ULP 分支会把相邻编码视为近似相等：例如 `f32::MAX` 与 `+∞`、相邻 NaN payload，均可能返回 true/`Some(Equal)`。该 wrapper 不实现 `Eq`。

C++ 最近的算法落点是 `Geometry/RustF32.h` 的 `AlmostEqualRustF32`（49–64 行）及 `LessEqualRustF32`、`LessRustF32`、`GreaterRustF32`（66–79 行），已被 `Rect.h`、`SimplePolygon.cpp` 和 `Surrogate.cpp` 使用。有限值分支采用 epsilon 或 4 ULP，与 Rust 默认 margin 的数值阈值相符；C++ 对不同的非有限值会在 ULP 比较前返回 false，而 Rust 可能因 ULP 邻近把有限/无穷或无穷/NaN 视为近似相等，这是已确认的静态边界差异，是否落入有效几何输入域由 B9/B1 核验。C++ 没有 `FPA` 类型、`From` 转换或 `Display` 对应物。`NumericPolicy::NearlyEqual` 是 double、默认 1e-9（`NumericPolicy.h:10-48`），属于另一套比较策略，不能拿它代表 FPA。

除 `FPA` 外，pinned `io/import.rs:294` 直接以 `approx_eq!(f32, ...)` 比较相邻顶点坐标；它使用相同的 `float-cmp 0.10.0` 默认 margin。C++ 导入器没有对应的近似删除函数，而是把点交给 `SimplePolygon::Create`；该函数先量化为 f32，再按精确 `Point2d::operator==` 拒绝重复点（`Importer.cpp:265`、`SimplePolygon.cpp:192-223`）。这与 Rust“相邻点近似相等则删除”的行为不是同一规则，边界输入归 B8/B9 对拍。

## B9 数值策略归类

“统一 `RustF32` 与 `NumericPolicy`”按一个清晰的策略目录和调用点台账执行，不把语义不同的阈值压成同一常数。`RustF32` 的比较 helper 是 f32 兼容谓词；`NumericPolicy` 则是 C++ double API 的可配置业务容差。替换彼此会改写几何边界和验证条件，须先按对应 Rust f32 行为取得差分证据。当前不改生产阈值。

| 规则 | 类型与默认值 | 当前生产用途 | B9 归属 |
|---|---|---|---|
| `float-cmp::F32Margin` | `f32`，`epsilon = f32::EPSILON`，`ulps = 4`；相等、绝对差、ULP 三段判断 | Rust `FPA`；`Rect` 的 relation/intersection/contains；`SimplePolygon` 的射线判定；`io::import::eliminate_degenerate_vertices` 直接调用宏 | Rust f32 比较 oracle；C++ `AlmostEqualRustF32`/关系 helper 需要按有限与非有限边界对照 |
| `NumericPolicy::equalityTolerance` | `double`，默认 `1e-9` | C++ snapshot/问题验证、`Edge`/`SimplePolygon`/`SoaGeometry` 的非 RustF32 API 默认参数、Importer 内的 double `near` | 业务比较容差；不等同于 `FPA` |
| `NumericPolicy::geometryTolerance` | `double`，默认 `1e-7` | 当前生产代码只通过 `IsValid()` 校验该字段；未发现几何计算读取该值 | 已声明但未见生产算法消费者 |
| `NumericPolicy::areaTolerance` | `double`，默认 `0` | `SimplePolygon::Create` 用它判断 `abs(rawArea) <= areaTolerance` | 面积退化门槛；不是相等谓词 |
| `NumericPolicy::arcChordTolerance` | `double`，默认 `1e-3` | 当前生产代码只通过 `IsValid()` 校验该字段；未发现圆弧计算读取该值 | 已声明但未见生产算法消费者 |

`NumericPolicy::IsValid()` 要求四个容差有限且非负；`NearlyEqual()` 只实现绝对差 `abs(lhs-rhs) <= equalityTolerance`，没有相对误差或 ULP 项。`+0/-0` 结果为 true；`NaN/NaN` 和 `+Inf/+Inf` 因差值为 NaN 而返回 false。`AlmostEqualRustF32` 则先做 `first == second`，所以相同正无穷返回 true；NaN 仍进入 ULP 前即被有限值检查拒绝。

### 最小边界 fixture 矩阵（待执行）

以下期望由 pinned `float-cmp 0.10.0` 与当前 C++ 源码推导，尚未运行，不是对拍结果。

| Fixture | 输入 | Rust 预期 | C++ 源码推导 | 归属 |
|---|---|---|---|---|
| FPA-EPS | `0` 与 `f32::EPSILON`；再与 `next_up(f32::EPSILON)` | 前者近似相等，后者不相等 | `AlmostEqualRustF32` 同结果 | B9 |
| FPA-ULP | `1.0` 与第 4、5 个相邻可表示值 | 4 ULP 相等；5 ULP 不相等 | 有限值分支预期同结果 | B9 |
| FPA-ZERO | `+0/-0`；最小负/正 subnormal | 均近似相等 | 精确相等/epsilon 分支预期相同 | B9 |
| FPA-NONFINITE | `f32::MAX/+Inf`；相同及相邻 NaN payload | ULP 距离不超过 4 时相等；`PartialOrd` 返回 `Some(Equal)` | 非有限 guard 返回 false | B9；合法输入域由 B1/B8 确认 |
| FPA-ORDER | `f32::MAX` 与 `+Inf`，以及 `+Inf` 与相邻 NaN payload | 近似相等，`<` 为 false、`<=` 为 true | 第一组 `LessRustF32` 为 true；第二组 `LessEqualRustF32` 为 false | B9 |
| NUMERIC-ABS | double `0` 与 `1e-9`；再与 `nextafter(1e-9,+Inf)` | 不适用 | `NearlyEqual` 分别 true、false | B9 |
| NUMERIC-NONFINITE | `+0/-0`、`NaN/NaN`、`+Inf/+Inf` | 不适用 | 分别 true、false、false | B9 |
| IMPORT-NEAR-DUP | 相邻顶点坐标分别差 `f32::EPSILON`、4 ULP、5 ULP、NaN/Inf | `approx_eq!` 命中时删除顶点 | `SimplePolygon::Create` 路径不做近似删除；其拒绝/接受结果另行核对 | B8/B9 |

### 邻接映射与范围归属

| Rust 文件 | 已映射的 C++ 落点或排除理由 | 后续行为验收 |
|---|---|---|
| `geometry/geo_traits.rs` | `CollidesWith`、`AlmostCollidesWith`、`DistanceTo`、`SeparationDistance`、`Transformable`、`TransformableFrom` 是泛型操作契约；具体实现落在 `Geometry/{Rect,Edge,SimplePolygon,Circle,Transformation}` 成员，不定义独立浮点比较器 | 按具体几何/碰撞函数归 B1/B4；其中使用 `FPA` 的调用点见本记录 |
| `geometry/geo_enums.rs` | `GeoPosition` 对应 `Rect.h::GeoPosition`；`RotationRange` 对应 `Item.h::RotationRange`/Importer 旋转策略；`GeoRelation` 无通用 C++ 类型，`RectRelation` 仅是矩形关系的具体落点 | 类型覆盖见 B0；数值边界归 B1/B7/B8 |
| `collision_detection/quadtree/qt_traits.rs` | `QTQueryable` 的默认 quadrant 碰撞、Circle 搜索顺序、Edge 保守分类落在 `Quadtree.cpp`/`CollisionEngine.cpp` 私有 helper；`CollisionHalf` 为私有 helper，不属于 crate 外 API | 算法与边界归 B4；Debug 检查启用条件见下表/E8 |
| `io/svg/svg_util.rs` | 模块私有；颜色亮度/混合及 SVG 节点构造由 `SvgExport.cpp` 内部 helper 组合实现，不是浮点比较 API | 输出与数字格式归 B8；从 B9 比较器清单排除 |

上述四项原先在 B0/B13 分别有静态落点；本表完成 B9 所需的跨项登记，算法/输出等价仍由对应工作项验证。

上述 `FPA-EPS` 至 `FPA-ORDER` fixture 已把 NaN payload、有限值/无穷相邻编码、ULP 阈值、正负零及 `PartialOrd` 的 `None`/`Some(Equal)` 与 C++ 布尔 helper 差异逐项登记；尚未运行。有限值阈值结构相符仍不能推成完整等价。

## `util/assertions.rs`

这些函数由 `util/mod.rs` 无条件公开；其调用点多数包在 Rust `debug_assert!` 中，但直接调用公开函数本身仍会执行函数体和其中的 `panic!`/`assert!`。

| Rust 符号与行 | 分支、返回或失败出口 | JaguarCore 对应 | 静态差异/后续 |
|---|---|---|---|
| `snapshot_matches_layout`（17–34） | `Arc::ptr_eq` 不同返回 false；对快照中的每个 placed item 检查当前 layout 是否有相同 item index 与 `d_transf`，未找到返回 false；全部通过返回 true | `ValidateLayoutSnapshot`、`ValidateLayout`（`Assertions.cpp:210-223`） | Rust 是单向成员存在性判断，不检查 layout 中额外 placement；C++ 对快照做更完整状态核对。覆盖归 B6/E8 |
| `collision_hazards_sorted_correctly`（37–55） | Partial 后出现 Entire 返回 false；None 直接 panic；其余返回 true | 无同名公开 C++ validator；碰撞排序归 Quadtree/Collision 实现 | `None` 在 Rust 是 panic 出口，不是 false；需结合 B4 的 hazard 生成不变量核验 |
| `qt_contains_no_dangling_hazards`、私有 `qt_node_contains_no_dangling_hazards`（58–95） | 递归确认子节点 hazard entity 都存在于父节点；发现 dangling 返回 false，并向 stdout 打印诊断；全树通过返回 true | `ValidateLayout` 是状态验证入口，但没有发现同名树结构 validator | 结构检查粒度不同；debug 条件及是否影响产品路径归 E8/B4 |
| `layout_qt_matches_fresh_qt`（98–111） | 从 base CDE 复制并重新注册当前 placed items，再用树结构和 hazard 集合比较；不相等返回 false | `ValidateLayout` / `ValidateLayoutSnapshot`（`Assertions.cpp:210-223`） | Rust 显式重建 quadtree；C++ 通过 layout snapshot、restore 与当前状态验证，不能仅凭入口相近视作相同算法 |
| `qt_nodes_match`（113–216） | 比较活动 hazard `(entity,presence)` 集合；节点缺失但另一侧有活动 hazard 时 false；`(None,None)` panic；子节点两侧都存在时用 `zip` 递归；只有一侧存在时仅在该侧含 Partial hazard 时继续检查 | C++ `ValidateLayout` 的相关快照校验（`Assertions.cpp:129-209`） | Rust 将节点 hazard 当集合比较、忽略顺序；两侧子数组使用 zip 前未核对长度，是需在 B4/E8 追踪的边界疑点，不在本记录下判定为缺陷 |
| `hazards_match`（218–231） | 将两侧 hazard entity 收为 HashSet；集合不同则打印并返回 false，否则 true | `sameLayoutSnapshot` 的 dynamic hazard 匹配（`Assertions.cpp:129-169`） | 两侧均以身份集合/匹配方式比较，不把迭代顺序作为此检查条件；对象属性范围不同，需由 B4/B6 继续核对 |
| `quadrants_have_valid_layout`（235–260） | 对每个 quadrant 的两个邻居要求共享恰好两个 corner；不满足时 `assert_eq!` panic，通过后 true | `Rect::QUADRANT_NEIGHBOR_LAYOUT` 与 Quadtree quadrant 构造；未找到直接同名公开 validator | Rust 通过断言检查拓扑前置条件；C++ 对应断言是否同样启用归 E8/B4 |
| `print_layout`（263–279） | 将 layout 转成 Rust 重建代码写入 stdout；无 `Result` 错误通道 | 未找到产品侧同名导出 | 仅诊断输出，不映射为产品 API |

Rust 顶层 `qt_nodes_match` 在不等集合分支中对两个差集都取 `next().unwrap()`（147–148）；按前置条件预期两侧差集均非空，但该前提若被破坏会 panic。Rust `assertions.rs` 还使用 `HashSet` 比较，不用于产品求解决策；其确定性风险按清单 R9/E8 分类。

## `collision_detection/quadtree/assertions.rs`

Rust 文件只在 `debug_assertions` 或 `test` 条件下编译，位置见 `quadtree/mod.rs:2`。

| Rust 符号与行 | 分支、返回或失败出口 | JaguarCore 对应 | 静态差异/后续 |
|---|---|---|---|
| `edge_quadrant_classification_is_conservative`（3–12） | 以 f64 重算的四个矩形相交结果作基准；若真实相交边未被 production 分类覆盖则 false，否则 true | `Quadtree` 中 Edge/Rect 的 quadrant 分类与几何相交代码 | 是验证器，不是生产算法；固定边界对拍归 B4/E8 |
| `edge_collisions_with_rects_f64`（14–16） | 对四个 Rect 映射调用下方 predicate | 无同名公开 API | 仅供断言计算 |
| `edge_collides_with_rect_f64`（21–48） | f64 AABB 无重叠则 false；任一端点在闭矩形内则 true；否则用四个 side 值判断线段交叉。边界接触因严格分离 `>` 不会被排除 | JaguarCore `Edge`、`Rect` 相交谓词 | Rust 用 f64 重算以避免继承生产 f32 的保守舍入；该测试 oracle 的闭边界结果与 C++ production 路径尚无逐项证据 |

## SPP/BPP/MSPP 断言模块

三个问题级模块都公开断言函数，并被对应 `Problem` 的 `debug_assert!` 调用。直接调用以下函数时，Rust `assert!` 仍可能 panic；C++ 对应 validator 返回 `Result<void>`，再由 `DebugAssert*` 包装器在 `#ifndef NDEBUG` 下断言（`Assertions.cpp:378-412`）。函数启用条件与 build flags 归 E8，不在这里推断 Debug/Release 实际编译状态。

### 启用条件映射

| 范围 | Rust 源码条件 | C++ 源码条件 | 行为边界 |
|---|---|---|---|
| `util/assertions.rs` | `util/mod.rs` 无条件 `pub mod assertions`；内部调用点通常包在 `debug_assert!`，直接调用 public helper 时仍执行函数体内 `assert!`/`panic!` | `Validate*` 始终可调用并返回 `Result<void>`；`DebugAssert*` 仅在 `#ifndef NDEBUG` 下执行 `assert` | Rust helper 可在 Release 被直接调用；C++ Debug wrapper 失败终止进程，且 Release wrapper 不运行校验 |
| SPP/BPP/MSPP assertion modules | 三个 `util/mod.rs` 均无条件公开；`Problem::save/restore` 等调用点由 `debug_assert!` 控制 | `DebugAssertSPSolution/BPSolution/MSPSolution` 的调用受 JaguarCore DLL 的 `NDEBUG` 控制 | 断言函数存在不等于产品路径始终执行 |
| Quadtree assertions 与 `qt_traits` | `quadtree/assertions.rs` 在 `cfg(any(debug_assertions, test))` 下编译；`qt_traits` 的 quadrants 一致性、Circle 顺序和 Edge f64 oracle 检查使用 `debug_assert!`。Pinned workspace 的 `[profile.test]` 明确设 `debug-assertions = true` | C++ 的相邻查询实现没有对应同形的断言 helper | Rust Debug/test 与 Release 的验证覆盖不同；实际命令/profile 仍由 E8/B4 核实 |
| C++ Release 测试目标 | — | `JaguarCoreTests.pro` 移除测试程序的 `NDEBUG`，加 `/UNDEBUG` 并强制包含 `TestReleaseAsserts.h`；同一文件注明链接的 Release DLL 保持正常 Release 配置 | 测试里的 `assert` 会执行，但不能据此推断 Release DLL 内的 `DebugAssert*` 生效 |

| Rust 文件/符号 | 检查和错误出口 | C++ 对应 | 结论与证据 |
|---|---|---|---|
| `probs/spp/util/assertions.rs` `problem_matches_solution`（7–19） | `assert_eq!` 精确检查 strip；density 差的绝对值不超过 `f32::EPSILON`；检查 snapshot；任一失败 panic，成功返回 true | `ValidateSPSolution`、`DebugAssertSPSolution`（`Assertions.cpp:225-253,387-393`） | C++ 用 double `1e-9` 比较 strip 的高度/宽度，验证 snapshot、实例身份和 demand；不显式核对 density 字段。与 Rust 的精确 strip、epsilon density 条件不是同一谓词，是否对应同一产品字段由 B7 核实 |
| 同文件 `instance_item_indices_correct`（22–27） | 要求 items 顺序索引与 `item.idx` 相同；返回 bool | SPP 实例构造校验 | 找到 Rust 构造点调用（`spp/entities/instance.rs:20`）；C++ 对应构造条件待 B7 核实 |
| `probs/bpp/util/assertions.rs` `problem_matches_solution`（7–27） | density 差不超过 `f32::EPSILON`；检查 layout 数、`bin_cost()` 与 solution cost；随后每个 problem layout 要能找到同 bin 且 snapshot 相符者；失败 panic，索引缺失可 panic | `ValidateBPSolution`、`DebugAssertBPSolution`（`Assertions.cpp:255-319,396-402`） | C++ 做 layout 一对一匹配、实例身份、demand 和 bin stock 校验；validator 不显式比较 density/cost。Rust 的 `all(...any(...))` 未消费匹配项，可能容许重复匹配，作为 B7 结构差异核验 |
| 同文件 `instance_item_bin_ids_correct`（30–33） | 要求 item 与 bin 的索引各自连续；返回 bool | BPP 实例构造校验 | Rust 调用点 `bpp/entities/instance.rs:19`；C++ 构造契约待 B7 核实 |
| `probs/mspp/util/assertions.rs` `problem_matches_solution`（7–28） | density 差不超过 `f32::EPSILON`；检查 layout/strip 数；每个 problem layout 寻找 strip 精确相等且 snapshot 相符的 solution layout；失败 panic | `ValidateMSPSolution`、`DebugAssertMSPSolution`（`Assertions.cpp:321-375,405-411`） | C++ 用 double `1e-9` 比较 strip 数值，另核对布局快照、item identity 和 demand；validator 不显式比较 density。Rust any 匹配未消费候选，结构差异归 B7/E8 核验 |
| 同文件 `instance_item_indices_correct`（31–36） | 要求 item index 与输入次序一致；返回 bool | MSPP 实例构造校验 | Rust 调用点 `mspp/entities/instance.rs:22`；C++ 构造契约待 B7 核实 |

## 证据与待办

- Rust 文件位置与行号来自 pinned `jagua-rs` revision；C++ 位置来自当前工作区静态源码。
- 本轮对 pinned checkout 与 `CodeResource/jagua-rs` 做换行归一化核对：`fpa.rs`、通用/SPP/BPP/MSPP assertion 源、quadtree assertion/trait、`geo_traits.rs`、`geo_enums.rs`、`svg_util.rs`、`io/import.rs` 均相同。Pinned SPP/MSPP `entities/problem.rs` 与镜像不同，但断言调用点已按 pinned 文件复核（SPP 96/121、BPP 97/166、MSPP 115/177）。
- `float-cmp` 版本与 checksum 见 `JaguarCore/tools/RustOracle/Cargo.lock`；`F32Margin::default` 的 epsilon/ULP 值来自锁定版本 0.10.0。
- 已有 C++ 覆盖：`Tests/TestSupport.cpp::testNumericPolicy` 检查默认值、无效容差、`0.5e-9` 与 `1e-3` 两个比较点；`TestBoundary.cpp` 检查无效 policy 与非有限几何输入。源码搜索未发现针对 `FPA`/`AlmostEqualRustF32` 的直接边界测试；Rust `FPA` 也没有本地专用单测。
- 源码启用条件：Rust `util/assertions` 与 SPP/BPP/MSPP assertion modules 无条件公开；常规调用点包在 `debug_assert!` 中，直接调用 helper 仍会执行内部 `assert!`。Quadtree assertion module 为 `cfg(any(debug_assertions, test))`；`qt_traits` 的检查使用 `debug_assert!`。
- C++ `Validate*` 始终可直接调用并返回 `Result<void>`；`DebugAssert*` 仅在 `#ifndef NDEBUG` 下执行 `assert`。`JaguarCoreTests.pro` 的 Release 测试目标通过移除 `NDEBUG`、`/UNDEBUG` 与 `TestReleaseAsserts.h` 保持测试程序断言启用，但注释明确 DLL 仍使用常规 Release 配置，因此这不证明 Release DLL 内部 `DebugAssert*` 会执行。
- 上述均为静态源码/工程配置核对；本次没有运行 C++、Rust 测试或构建。实际 profile、DLL 宏与边界 fixture 运行仍归 B1/B4/B8/E8/B11，不能在本记录下宣称行为等价。

