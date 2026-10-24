# 38. FPA（浮点近似比较）逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/util/fpa.rs`（38 行）
**C++**: `JaguarCore/Include/JaguarCore/geometry/RustF32.h`（81 行，`detail::AlmostEqualRustF32` 等）+ `Importer.cpp` 私有 `rustFpaEqual`（21-31 行）+ `NumericPolicy.h`（52 行）
**判定**: 公共比较器按 pinned `float-cmp 0.10.0` 实现；Importer连续顶点比较直接复用该实现，包含非有限 `f32` 位模式。该结论只覆盖重复顶点比较，不改变多边形构造对非有限坐标的校验。另有形态差异：Rust 为公共 newtype 类型 `FPA`，C++ 为函数族；`NumericPolicy` 的 1e-9 体系为 C++ 额外，与 Rust 无对应。

## 逐条比对

### `struct FPA(pub f32)` + `impl From<T: Into<f32>>`（Rust 6-16 行）
- C++ 对应：无类型包装；等价能力由 `detail` 命名空间自由函数承担（RustF32.h:10-80）。
- 判定：差异（形态：公共类型 vs 函数族）。
- 说明：Rust `FPA` 是可放入集合/作为 `PartialEq`/`PartialOrd` 使用的包装类型（如 `Point` 排序场景）；C++ 不引入类型，只提供比较谓词。

### `PartialEq for FPA` → `float_cmp::approx_eq!(f32, a, b)`（Rust 18-22 行）
- C++ 对应：公共 `detail::AlmostEqualRustF32`（RustF32.h:55-70）；Importer 私有 `rustFpaEqual`（Importer.cpp:21-31）直接委托公共比较器。
- 判定：Importer连续顶点比较对所有 `f32` 输入复用同一实现，包括不相等的 NaN/Inf 编码。
- 证据：锁定的 `float-cmp 0.10.0` `ApproxEq for f32` 先做精确相等，再比较 `f32::EPSILON`，最后按有序位模式计算 `i32` wrapping ULP 距离并做 saturating absolute value，阈值为 4。它不先过滤 NaN/Inf。对照：
```rust
impl PartialEq<Self> for FPA {
    fn eq(&self, other: &Self) -> bool {
        float_cmp::approx_eq!(f32, self.0, other.0)
    }
}
```
（fpa.rs:18-22）↔ RustF32.h:55-70 / Importer.cpp:21-31：
```cpp
if (first == second) return true;
if (std::fabs(first - second) <= std::numeric_limits<float>::epsilon()) return true;
const std::uint32_t firstBits = OrderedRustF32Bits(first);  // 负数 ~bits，非负数 bits ^ sign_bit
const std::uint32_t secondBits = OrderedRustF32Bits(second);
const std::uint32_t wrappedDifference = firstBits - secondBits;
const std::uint32_t absoluteDifference =
    (wrappedDifference & 0x80000000U) == 0U
        ? wrappedDifference
        : (wrappedDifference == 0x80000000U
               ? 0x7fffffffU
               : ~wrappedDifference + 1U);
return absoluteDifference <= 4U;  // 等价于 Rust i32 wrapping_sub 后的 saturating_abs
```
- `OrderedRustF32Bits`（RustF32.h:40-47）使用负数 `~bits`、非负数 `bits ^ 0x80000000`。C++ 通过无符号减法复现 Rust 的 `i32::wrapping_sub`，并把 `i32::MIN` 的绝对值饱和到 `i32::MAX`，与 pinned `float-cmp` 的 ULP 距离计算一致。
- **非有限值边界**：Rust probe 与 C++ 回归确认 `f32::MAX`/`+Inf`、`-Inf`/`-f32::MAX` 都近似相等；位模式 `0xffffffff` 与 `0x7fffffff` 的交叉符号 NaN 对也因 wrapping ULP 距离为 1 而相等。故 `AlmostEqualRustF32` 的这些结果是 pinned Rust 行为，不是偏差。
- **Importer比较路径已收敛**：Importer.cpp 原私有负数位序映射使用 `~bits + 1`，与 pinned 规则的 `~bits` 在跨符号ULP边界上不同；该算法副本与 finite guard 均已删除，所有值直接委托 `AlmostEqualRustF32`。
- **范围说明**：这只对齐 `eliminate_degenerate_vertices` 的相邻顶点谓词。C++ `SimplePolygon::Create` 随后仍显式拒绝残留非有限点；Rust `SPolygon::new` 没有同层 finite 扫描，非有限多边形的完整成功/失败契约须结合后续几何计算单独核对，不能由比较器一致推导。

### `PartialOrd for FPA`（Rust 24-32 行）
- C++ 对应：`LessRustF32`/`LessEqualRustF32`/`GreaterRustF32`（RustF32.h:74-87）。
- 判定：一致。
- 证据：Rust `partial_cmp`：`eq → Some(Equal)`，否则 `self.0.partial_cmp(&other.0)`（fpa.rs:25-31）。C++ 三谓词：`LessEqual = a < b || AlmostEqual`；`Less = a < b && !AlmostEqual`；`Greater = a > b && !AlmostEqual`（h:66-79）。普通有限值下比较关系对应；按 pinned FPA 规则，`f32::MAX` 与 `+Inf` 也会先被近似相等吸收为 `Equal`，C++ `LessEqualRustF32(+Inf, f32::MAX)` 同样为 true。C++ bool 谓词仍把 Rust `None` 折叠为 false；一般 NaN 的具体排序行为不能据此宣称类型级完全等价。

### `Display for FPA`（Rust 34-38 行）
- C++ 对应：无（无 FPA 类型即无需 Display）。
- 判定：C++ 缺失（随类型形态消失）。

### `NumericPolicy`（NumericPolicy.h:8-51）与 Rust 的关系
- Rust 对应：无。
- 判定：C++ 额外。
- 证据：`NumericPolicy { equalityTolerance = 1e-9, geometryTolerance = 1e-7, areaTolerance = 0.0, arcChordTolerance = 1e-3 }` + `NearlyEqual(lhs,rhs) = |lhs-rhs| <= equalityTolerance`（h:26-50）。这是 C++ 侧**绝对容差体系**（double 域、1e-9 量级），供 Assertions.cpp（记录 39）等做数值一致性比较；Rust 侧同类比较全部走 `FPA`/`approx_eq!`（f32 域、EPSILON+ulps）。两套体系并存于 C++：`RustF32` 族对齐 Rust 行为（导入顶点去重、变换合成等），`NumericPolicy` 族服务 C++ 自有的校验层（快照对比、解校验），后者无 Rust 对应源。使用点：Assertions.cpp:18-23 `TOLERANCE = NumericPolicy::DefaultEqualityTolerance()`、Importer.cpp:219-222 `near()`。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 近似相等算法（EPSILON 绝对容差 + saturating ULP≤4） | 一致（RustF32.h） | 对齐 pinned float-cmp 0.10.0 的有序位模式、wrapping 距离及饱和绝对值 | fpa.rs:18-22 vs RustF32.h:40-70 |
| 2 | 偏序/比较谓词族 | 一致 | `PartialOrd`（近似带吸收）↔ `Less/LessEqual/GreaterRustF32` | fpa.rs:24-32 vs RustF32.h:74-87 |
| 3 | 公共类型 vs 私有函数 | 差异 | Rust `FPA` newtype 可作 `PartialEq` 键使用；C++ 无类型，调用点分散 | fpa.rs:7 vs RustF32.h:10-80 |
| 4 | 非有限输入的比较行为 | 一致（相邻顶点谓词） | Importer与Rust都按 pinned ULP 规则比较所有 `f32` 编码；多边形构造的 finite 校验不在此项范围内 | Importer.cpp:21-31 vs RustF32.h:55-70 / import.rs:290-297 |
| 5 | 比较算法重复实现 | 已收敛 | Importer所有输入直接委托公共比较器，无单独 ULP 或 finite 策略分支 | Importer.cpp:21-31 vs RustF32.h:55-70 |
| 6 | `RustF32` 附加工具（ToDegrees/Midpoint/Min/Max） | C++ 额外 | 度换算、安全中点、fmin/fmax 复刻，服务各处 f32 语义对齐，无 Rust 单一对应点 | RustF32.h:12-38 |
| 7 | `NumericPolicy` 1e-9 绝对容差体系 | C++ 额外 | 无 Rust 对应；服务 C++ 校验层（Assertions 等），与 FPA 体系并存不互通 | NumericPolicy.h:8-51 |

## 结论

`AlmostEqualRustF32` 已按锁定的 `float-cmp 0.10.0` 对齐，Importer相邻顶点比较也已对所有输入直接委托该比较器。Rust `FPA` newtype 与 C++ 谓词族在类型形态上不同；C++ 另有 `NumericPolicy`（double域、默认1e-9）服务自身校验层，不能与RustF32的f32兼容规则混用。本记录只覆盖比较器，不代表E1的全部数值精度工作已完成。

### 补充审计（2026-09-29）

对 fpa.rs 38 行逐行复核。原记录已覆盖全部 4 个 impl（From/PartialEq/PartialOrd/Display）与 float-cmp 参数语义；补充遗漏点：

1. **`FPA` 使用点清单（原记录缺"谁在用"的全景，影响等价性判断）（浅覆盖→补齐）**——Rust `FPA` 的实际调用点：
   - `simple_polygon.rs:296-298`：`SimplePolygon::contains` 中把 edge 端点与 point 包成 FPA 做 `>=`/`<=` 判定（原记录记录 05 应覆盖，交叉引用）；
   - `rect.rs:76-83`：`Rect::overlaps`/`contains` 的 FPA 比较（记录 02 交叉引用）。
   即 **FPA 作为类型的使用全部在几何谓词层**；C++ 对应点使用 `LessEqualRustF32` 等谓词族（Rect.cpp/SimplePolygon.cpp）。判定：原记录"形态差异（newtype vs 函数族）"在使用点全景上成立。
2. **`impl<T> From<T> for FPA where T: Into<f32>`（Rust 9-16 行）的泛型收窄语义（遗漏细节）**——`From<T: Into<f32>` 意味着 f64 输入经 `as f32` 收窄（Rust `f64: Into<f32>` 不可用——实际只有 u8/u16/i8 等整型与 f32 本身满足；f64 不 `Into<f32>`，需显式 `as`）。C++ 无类型即无对应；`rustFpaEqual` 的分量重载（Importer.cpp:26-31）在调用前显式 `static_cast<float>`。判定：一致（收窄位置不同，均在比较前完成）。
3. **PartialOrd for FPA 的 PartialEq 短路次序（补齐）**——Rust partial_cmp 先用 self.eq 判 Equal，否则调用 f32 partial_cmp。普通 NaN 对通常得到 None；但位模式 0xffffffff 与 0x7fffffff 的交叉符号 NaN 对因 ULP wrapping 距离命中近似相等，得到 Some(Equal)。C++ bool 谓词不能表达 None，且 LessEqualRustF32 对该编码对可能为 true。该异常编码通常不在已验证几何输入域内，不将结论推广到其他 NaN payload。
4. **float-cmp 版本依赖核实（补齐证据）**——workspace 锁定 `float-cmp 0.10.0`；`approx_eq!` 默认 `F32Margin(4, f32::EPSILON)`。除阈值常数外，还须保留其有序位模式、i32 wrapping ULP 差和 saturating absolute value 语义，已在2026-10-22补充复核中记录。
5. **`Display for FPA`（原记录判"C++ 缺失"，补充影响）**——Rust FPA Display 转发 f32 Display；库内 FPA 的 Display 无调用点（调试打印用 `{}` 的场合在 simple_polygon/rect 中不存在）。判定维持：C++ 缺失（无行为影响）。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | FPA 使用点全景（几何谓词层） | 一致（补齐） | simple_polygon contains / rect overlaps 走 FPA ↔ C++ RustF32 谓词族 | simple_polygon.rs:296-298, rect.rs:76-83 |
| S2 | 普通 NaN 下 PartialOrd None vs bool false | 一致（形态差异） | 普通NaN下 Rust 为 None、C++ bool谓词为false；指定交叉符号NaN编码对另可近似相等，见E1补充复核 | fpa.rs:24-32 vs RustF32.h:74-87 |
| S3 | From 泛型收窄 | 一致（补齐） | 比较前收窄，位置不同 | fpa.rs:9-16 vs Importer.cpp:26-31 |

### E1 补充复核（2026-10-22）

- 锁定依赖 `float-cmp 0.10.0` 的本地源码复核与 Rust probe 均确认：`max/+Inf=true`、`-Inf/-max=true`、指定交叉符号 NaN 位模式对为 `true`、ULP 距离 4/5 的结果为 `true/false`，且 `partial_cmp(max, +Inf)=Some(Equal)`。
- `RustF32.h` 已按该实现修正有序位模式和saturating ULP距离，并增加直接回归断言；Importer当前也对所有值委托该比较器。本轮本机未找到qmake/MSVC，因此只用llvm-mingw `clang++ -std=c++17 -fsyntax-only`检查 `Importer.cpp`，exit 0；未运行测试runner，也未做MSVC生产库构建。此前公共比较器修正后的Release全量runner exit 0；此前Debug全量runner exit 1且没有诊断输出，不能据此宣称Debug全量通过。
- **2026-10-22 后续运行时复核**：从现有qmake Makefile定位VS2019 MSVC14.29与Qt6.5.3工具链，重建当前JaguarCore Debug/Release库和测试runner均exit 0。只执行`testRustF32ApproximateComparison()`的Debug定向runner exit 0；Importer现有`runIoTests()` Debug定向runner exit 0；Release全量runner exit 0。当前Debug全量runner退出码3已定位到`runCollisionTests()`注册覆盖根节点的`coveringShape`时触发Quadtree“无Partial子项”Debug断言（`TestCollision.cpp:61-64`、`Quadtree.cpp:423-426`），与FPA/Importer定向用例无关。锁定RustReference测试在另一边界fixture上捕获同一断言条件；不据此声称输入几何完全相同。
- Importer连续顶点比较已删除 finite guard，匹配Rust的`approx_eq!`输入比较域。此项不等于放宽形状构造：C++ `SimplePolygon::Create`仍拒绝处理后残留的非有限坐标；Rust `SPolygon::new`未做同层预检，整体失败路径仍需作为独立契约核对。
- E1 仍部分：完整RustF32调用点偏差预算、Importer非有限多边形整体契约、变换的完整外部角度域与非有限输入仍未收口。变换有限矩阵及Importer形态定向探针已按四系数矩阵状态修复并通过已测集合；Edge/Circle共享midpoint helper已完成10,012组逐位对拍，但历史1ULP调用点向量仍未复现。

### E1 RustF32调用点的比较器与偏差预算复核（2026-10-22）

本次静态核对了JaguarCore 16个含RustF32的生产实现单元及对应差分/固定期望比较代码。结果表明：`AlmostEqualRustF32`是部分差分入口的统一预算，不是全部RustF32调用点的默认验收规则。

| 生产入口 | 当前参考比较路径 | 实际比较规则与边界 |
|---|---|---|
| `Geometry/Primitives/{Circle,Edge,PointOfInaccessibility,SimplePolygon}.cpp`、`OriginalShape.cpp`、`Transformation.cpp`及`Point/Edge/Circle/Rect/SimplePolygon/Transformation.h`中的内联入口 | `CompareGeometry.cpp::expectRustNumbers`、`expectRustBooleans`、`expectRustPositions` | 数值数组先把Rust期望值与C++ `double`都收窄为`float`，再用`AlmostEqualRustF32`；布尔、位置分类、数组长度与顶点顺序严格比较。只对已登记的固定几何矩阵生效；Transformation另有54组定向raw-bit样本逐位比较，不能外推到全输入域。 |
| `ShapeModification/NarrowConcavity.cpp` | `CompareGeometry.cpp::runB2ShapeModificationDifferentialTests` | 固定三组输出向量经同一`expectRustNumbers`规则比较；向量长度先严格相等，再比较收窄后的数值。B2矩阵不覆盖同文件中其他配置组合。 |
| `ShapeModification/Simplify.cpp` | `CompareGeometry.cpp::runB2ShapeModificationDifferentialTests`消费`shape_modification_b2.txt`中的6条Simplify Rust参考 | 2种形状（窄凹口、凸bevel）分别覆盖Inflate/Deflate，另含冗余共线点的零阈值与负最小次正规阈值；先精确比较向量长度，再以`AlmostEqualRustF32`比较顶点数、面积和有序顶点。仅限这6个固定输入/阈值，其他简化分支与错误出口仍未形成完整输出预算。 |
| `ShapeModification/Offset.cpp` | `CompareGeometry.cpp::runOffsetBoundaryDifferentialTests`消费`offset_boundary.txt`中的10个有限D5输入配置；`TestShapeModification.cpp`补有splitCandidate逐点回归 | 方形、splitCandidate、大坐标sub-ULP/sub-grid偏移、窄桥Deflate 0.6/1.0及comb Deflate 2.0/2.1均比较组件数、顶点数、面积和有序顶点；顶点数严格相等，坐标/面积用`AlmostEqualRustF32`，循环起点允许旋转。两档分裂阈值下的所有多组件输出均与D5固定值匹配。该矩阵不覆盖其他自交/退化overlay、通用多环/洞、legacy fallback及非有限错误出口。 |
| `FailFast/Surrogate.cpp`与`Surrogate.h` | `CompareSurrogate.cpp::expectRustNumbers`、`expectRustExactNumbers`、`expectRustText` | 几何数值按`AlmostEqualRustF32`；索引/计数等数字及碰撞结果文本严格相等。覆盖范围限于B3固定Debug/Release fixture，1000-pole安全断言和其他几何配置不在矩阵内。 |
| `Collision/CollisionEngine.cpp`、`Collision/Quadtree/Quadtree.cpp` | `CompareBoundary.cpp`、`CompareMatrix.cpp`及`CompareGeometry.cpp`中的碰撞/关系fixture | `bool`、关系枚举及顺序结果严格比较，不设数值输出容差。只能证明登记边界和固定树/碰撞路径；差分README已标出未被fixture消费的通用碰撞oracle与部分Quadtree路径。 |
| `Importer.cpp` | `CompareProblem.cpp::expectRustNumbers`；`TestIo.cpp`/`TestJsonCodec.cpp`局部断言；RustOracle `--import-boundary-parity-only`目前没有活动C++差分读取器 | B7指定数值字段按`AlmostEqualRustF32`；若干局部用例用绝对阈值`1e-9`、`1e-6`或`1e-12`。54组外部角度探针逐位比的是度转弧度和点应用，不是完整Importer组装链。错误出口也不等价：Rust类型化Inf平移在`import_solution → ext_to_int_transformation`的`dot_prod`中由`Inf * 0 → NaN`触发`NotNan` panic；C++类型化入口返回`SnapshotMismatch`。JSON路径中，Rust默认serde_json将`3.5e38`解为f32 Inf并panic；C++ `numberValue`收窄后在解析阶段返回`SerializationFailure`。Rust Debug/Release oracle及C++ MSVC14.29 Release全量runner均exit 0，覆盖`TestIo`与`TestJsonCodec`的定向断言，详见记录34/37。 |
| `Importer.cpp`（JSON rotation overflow） | RustOracle `--import-boundary-parity-only`；`TestIo.cpp`/`TestJsonCodec.cpp` | 对同一`rotation: 3.5e38`：Rust Debug/Release解析为f32 Inf，`cos(Inf)`产生NaN并触发panic；C++ JSON入口返回`SerializationFailure`，类型化`rotationDegrees=3.5e38`返回`SnapshotMismatch`。这是入口边界断言，不构成完整Importer差分读取器。 |
| `Importer.cpp`（JSON负向溢出与最大有限f32边界） | RustOracle `--import-boundary-parity-only`；`TestIo.cpp`/`TestJsonCodec.cpp` | 两变换字段的`-3.5e38`均在Rust转为`-Inf`后导入panic；C++ JSON入口为`SerializationFailure`，类型化入口为`SnapshotMismatch`。`3.4028234663852886e38`在Rust serde和C++ JsonCodec解析/窄化后均为最大有限f32；该断言只到解析边界。 |
| `Importer.cpp`（f32→Inf舍入临界点） | RustOracle `--import-boundary-parity-only`；`TestJsonCodec.cpp` | f64中点前一个值`340282356779733623858607532500980858880`在两侧可解析并收窄为±最大有限f32；中点`340282356779733661637539395458142568448`在Rust变为±Inf，在C++ JSON解析阶段返回`SerializationFailure`。只覆盖JSON数值解析/窄化，未执行完整Importer。 |
| `Entities/PlacedItem.cpp` | `CompareProblem.cpp`固定SPP/BPP/MSPP流程实际调用`PlaceItem`；`CompareLayoutLifecycle.cpp`实际测试建成、保存恢复与删除；trajectory/optimizer只间接消费 | Rust `PlacedItem::new`将`d_transf.compose()`应用到`shape_cd`；C++ `PlacedItem::Create`先将角度/平移收窄至f32，再调用`TransformedRustF32`。`CompareProblem`的placed断言只比较需求/数量/density/strip width等聚合值；density两侧都基于原始item面积，与变换后碰撞顶点无关。生命周期snapshot比较顶点是同一C++状态恢复前后自比较，变换文本精度为0.1；trajectory checksum不含实际顶点。2026-10-22新增一条锁定revision的完整Importer有限输入对拍：同一2×2 SPP item使用外部角度37.25°、平移[4.25,-1.75]，Rust Debug/Release逐raw-bit给出变换、4个碰撞顶点、bbox、area、diameter、centroid、原始Item area、POI圆及surrogate两极点/空piers/凸包数据；C++ `TestJsonCodec`对这些字段逐raw-bit断言，MSVC14.29 Debug隔离runner与Release全量runner通过。它补上一个完整有限样例，未覆盖其他变换、形状或surrogate配置输入。 |
| `IO/JsonCodec.cpp`、`IO/SvgExport.cpp` | `TestJsonCodec.cpp`选定转换探针；`TestSvgExport.cpp`结构断言；RustOracle import-boundary mode目前没有活动C++差分读取器 | 现有raw-bit断言只锁定少数转换字段；SVG角度断言先用`std::stof`从属性文本回读，再比较f32位模式，检查的是解析后的数值，不是文本相等。Rust `Cargo.lock`固定`serde_json 1.0.151`/`zmij 1.0.23`，`lbf::io::write_json`通过`serde_json::to_writer_pretty`写出；`serde_json`的f32路径使用`zmij`最短正确舍入十进制。C++ JSON writer以`setprecision(17)`输出double；外部变换中的f32结果提升为double后，文本可能不同，例如同一`0.1f`可分别写作`0.1`和`0.10000000149011612`，但再解析为f32仍可相同。非有限f32在Rust序列化为`null`，C++ writer拒绝非有限number并返回序列化错误。当前没有Rust/C++ JSON字节级对照，因此这些是由锁定源码和实现路径确认的文本/出口差异，尚无运行时输出对拍。SVG方面，锁定`svg 0.18.0`的path `Number`为f32，`Value`对f32调用`to_string()`；Rust多边形path坐标按f32写出。C++ `SimplePolygon`顶点先收窄到f32再存为double，`SvgExport::number`再以`setprecision(17)`输出，几何path坐标和变换角度/平移都可能有文本表示差异。`TestSvgExport`只检查结构，没有完整Rust SVG逐串比较；其他角度、完整Importer链及非有限出口仍未覆盖。 |

**IO验收口径**：现有JSON记录以字段/wire语义比较，明确JSON对象键顺序不影响兼容；S22的运行记录比较字段结构与回读稳定性，没有字节级验收要求。对声明为f32的字段，两种十进制文本均可回读到同一f32；若消费者将它们当作通用f64数值，则观察到的数值可能不同。SVG记录36明确判定为有意重写、非兼容输出，并已将数字文本格式列作单独差异；不以逐字节或逐属性相同作为当前目标，绘制功能与视觉差异仍按记录36分别验收。 |

**预算解释**：对有限`f32`，`AlmostEqualRustF32(a,b)`接受`|a-b| <= f32::EPSILON`或有序f32 ULP距离`<=4`；它不是固定`1e-9`绝对误差，也没有一个对所有数量级通用的绝对误差上限。差分helper先将`double`投影为`float`，该投影本身会丢弃超出f32精度的差异。非有限值应依赖独立类别、位模式或错误出口断言，不能把有限域的误差预算套用过去。

`NumericPolicy`默认double equality `1e-9`、geometry `1e-7`、area `0`、arc chord `1e-3`属于C++校验/业务策略，不是RustF32差分预算。`CompareOptimizer`的密度重建阈值`5e-3`及上界`1+1e-6`同样是外部优化结果验收阈值，不是JaguarCore逐函数兼容容差。

**E1裁定**：已将16个实现单元归入实际比较路径，但仍不能声称73个唯一RustF32标识符均有逐调用点预算。当前未闭合项至少包括Simplify完整输出矩阵、Offset替代算法的剩余分支、PlacedItem其他变换/形状/surrogate配置输入、IO其他角度/非有限输入，以及所有固定fixture之外的合法输入域；E1继续部分。
