# 38. FPA（浮点近似比较）逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/util/fpa.rs`（38 行）
**C++**: `JaguarCore/Include/JaguarCore/geometry/RustF32.h`（81 行，`detail::AlmostEqualRustF32` 等）+ `Importer.cpp` 私有 `rustFpaEqual`（30-53 行）+ `NumericPolicy.h`（52 行）
**判定**: 行为等价（比较算法逐行复刻；形态差异：Rust 为公共 newtype 类型 `FPA`，C++ 为私有自由函数族 + 公共 `RustF32` 后缀工具集；`NumericPolicy` 的 1e-9 体系为 C++ 额外，与 Rust 无对应）

## 逐条比对

### `struct FPA(pub f32)` + `impl From<T: Into<f32>>`（Rust 6-16 行）
- C++ 对应：无类型包装；等价能力由 `detail` 命名空间自由函数承担（RustF32.h:10-80）。
- 判定：差异（形态：公共类型 vs 函数族）。
- 说明：Rust `FPA` 是可放入集合/作为 `PartialEq`/`PartialOrd` 使用的包装类型（如 `Point` 排序场景）；C++ 不引入类型，只提供比较谓词。

### `PartialEq for FPA` → `float_cmp::approx_eq!(f32, a, b)`（Rust 18-22 行）
- C++ 对应：两份实现：
  1. `detail::AlmostEqualRustF32`（RustF32.h:49-64，公共头文件）；
  2. `Importer.cpp:30-45` 匿名命名空间私有 `rustFpaEqual(float, float)`（含分量级重载 cpp:47-53）。
- 判定：一致（算法逐行等价，为 float-cmp 0.10 `approx_eq!` 默认参数的复刻）。
- 证据：float-cmp 0.10 `approx_eq!(f32, a, b)` 默认 `F32Margin`：`|a-b| <= f32::EPSILON || ulps_difference(a,b) <= 4`。对照：
```rust
impl PartialEq<Self> for FPA {
    fn eq(&self, other: &Self) -> bool {
        float_cmp::approx_eq!(f32, self.0, other.0)
    }
}
```
（fpa.rs:18-22）↔ RustF32.h:49-64 / Importer.cpp:30-45：
```cpp
if (first == second) return true;                       // 恰等（含 ±0.0、NaN→false 由后续分支保证）
if (std::fabs(first - second) <= std::numeric_limits<float>::epsilon()) return true;  // 绝对容差 = f32::EPSILON
const std::uint32_t firstBits = OrderedRustF32Bits(first);   // 有序位模式
const std::uint32_t secondBits = OrderedRustF32Bits(second);
const std::uint32_t difference = firstBits > secondBits ? firstBits - secondBits : secondBits - firstBits;
return difference <= 4U;                                // ulps 容差 = 4
```
- `OrderedRustF32Bits`（RustF32.h:40-47）与 Importer.cpp:21-28 `orderedFloatBits` 逐行相同（负数 `~bits+1`、正数 `bits|0x80000000`），复刻 float-cmp 的 ulps 排序。
- **分支次序差异（Importer 私有版）**：`rustFpaEqual`（cpp:30-45）把非有限检查提前（`first == second` → `!isfinite → false` → 绝对容差 → ulps），而 `AlmostEqualRustF32`（RustF32.h:49-64）无显式 isfinite 检查。行为分析：非有限输入下——(a) `first == second`（如 inf==inf）两侧都 true；(b) `fabs(inf - x) <= EPSILON` 恒 false（两侧一致）；(c) ulps 分支：RustF32.h 版对 NaN/inf 的位模式做减法——NaN 位模式差可能 ≤4（两个相邻 NaN）返回 true，而 Importer 版返回 false；float-cmp 0.10 的 `Approx::ulps_eq` 对 NaN/inf 的行为：`==` 短路后 `ulps_cmp` 对非有限值返回 `None`→false。故 **`rustFpaEqual`（Importer 版）与 float-cmp 语义完全一致；`AlmostEqualRustF32`（RustF32.h 版）在"两个位模式相邻的 NaN"这一极端输入下偏离**（返回 true）。实际影响可忽略（导入/几何路径输入已过 finite 校验），如实记录。
- **重复实现**：`orderedFloatBits`/`rustFpaEqual`（Importer.cpp:21-53）与 `OrderedRustF32Bits`/`AlmostEqualRustF32`（RustF32.h:40-64）是同一算法的两份拷贝，一处私有。技术债项。

### `PartialOrd for FPA`（Rust 24-32 行）
- C++ 对应：`LessRustF32`/`LessEqualRustF32`/`GreaterRustF32`（RustF32.h:66-79）。
- 判定：一致。
- 证据：Rust `partial_cmp`：`eq → Some(Equal)`，否则 `self.0.partial_cmp(&other.0)`（fpa.rs:25-31）——即"近似相等则 Equal，否则严格比较"。C++ 三谓词：`LessEqual = a < b || AlmostEqual`；`Less = a < b && !AlmostEqual`；`Greater = a > b && !AlmostEqual`（h:66-79），与该偏序语义完全对应（相等吸收近似带，比较越过近似带）。注意 Rust 的 `FPA` 偏序不是全序（近似带内 `a<b` 与 `b<a` 同假），C++ 谓词族同性质，一致。

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
| 1 | 近似相等算法（EPSILON 绝对容差 + ulps≤4） | 一致 | float-cmp 0.10 默认参数逐行复刻，`OrderedRustF32Bits` 对齐其 ulps 排序 | fpa.rs:18-22 vs RustF32.h:40-64 |
| 2 | 偏序/比较谓词族 | 一致 | `PartialOrd`（近似带吸收）↔ `Less/LessEqual/GreaterRustF32` | fpa.rs:24-32 vs RustF32.h:66-79 |
| 3 | 公共类型 vs 私有函数 | 差异 | Rust `FPA` newtype 可作 `PartialEq` 键使用；C++ 无类型，调用点分散 | fpa.rs:7 vs RustF32.h:10-80 |
| 4 | 非有限输入行为（NaN 位模式相邻时） | 差异（极端） | `rustFpaEqual`（Importer 版）与 float-cmp 一致（false）；`AlmostEqualRustF32`（RustF32.h 版）可能返回 true。实际路径已过滤非有限输入 | Importer.cpp:30-45 vs RustF32.h:49-64 |
| 5 | 算法双份拷贝 | 差异（技术债） | Importer.cpp 私有 `orderedFloatBits`/`rustFpaEqual` 与 RustF32.h 公共版重复 | Importer.cpp:21-53 vs RustF32.h:40-64 |
| 6 | `RustF32` 附加工具（ToDegrees/Midpoint/Min/Max） | C++ 额外 | 度换算、安全中点、fmin/fmax 复刻，服务各处 f32 语义对齐，无 Rust 单一对应点 | RustF32.h:12-38 |
| 7 | `NumericPolicy` 1e-9 绝对容差体系 | C++ 额外 | 无 Rust 对应；服务 C++ 校验层（Assertions 等），与 FPA 体系并存不互通 | NumericPolicy.h:8-51 |

## 结论

FPA 的核心语义——f32 域"EPSILON 绝对容差 + ulps≤4"近似相等及由此构成的偏序——被 C++ 完整复刻（`AlmostEqualRustF32` 及其私有拷贝 `rustFpaEqual`），逐行为等价；差异仅在形态（newtype 类型 vs 函数族）与一处极端 NaN 输入的分支次序。C++ 另有整套 `NumericPolicy`（1e-9 绝对容差）体系服务自身校验层，属无 Rust 对应的额外设施，使用时须注意它与 RustF32 族不可混用。
