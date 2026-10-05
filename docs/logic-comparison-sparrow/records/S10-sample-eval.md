# S10. sample_eval（样本评估值类型与比较语义）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/sample_eval.rs`（43 行）
**C++**: `SparrowCore/Include/SparrowCore/optimizer/SampleEval.h`（134 行；SampleEval.cpp 仅 1 行 include，全头文件内联）
**判定**: 行为等价（比较语义经 float-cmp 0.10 默认 margin 复刻）

这是 eval 层的基础类型：枚举 `SampleEval {Clear{loss}, Collision{loss}, Invalid}` + `SampleEvaluator` trait。比较语义（含 FPA 容差）直接决定 BestSamples 的接受/淘汰与坐标下降的 better/worse 判定，因此被 `SamplingCoordinateDescentRustReference.tsv`、`LbfSearchRustTrace.tsv`、`SeparatorSearchRustTrace.tsv` 三个 fixture 的 kind/loss 位模式与 accepted 标志共同锁定。

## 函数逐条比对

### `SampleEval` 枚举（sample_eval.rs:8-13）
- C++ 对应：`struct SampleEval { SampleKind kind; float loss; }`（SampleEval.h:23-27），`SampleKind` 枚举 INVALID/COLLISION/CLEAR（h:16-21）。Rust 的带数据枚举展平为 kind+loss 结构；Invalid 变体的 loss 字段在 C++ 恒为 0.0F（约定，无人读取）。
- 判定：【一致】
- 证据：sample_eval.rs:8-13 vs SampleEval.h:16-27。

### `PartialOrd/Ord for SampleEval`（sample_eval.rs:15-37）
- C++ 对应：`SampleKindRank` + `IsBetterSample` + `operator<`（SampleEval.h:29-99）。这是全文件最关键的比对点，逐 match 臂核对：
  1. **(Invalid, Invalid) → Equal**：C++ h:80-83 `if (left.kind == SAMPLE_KIND_INVALID) return false;`（rank 相同且都 INVALID 时 `left < right` 为 false）✓。**(_, Invalid) → Less / (Invalid, _) → Greater**：C++ SampleKindRank（h:29-41）给 CLEAR=0、COLLISION=1、INVALID=2，`leftRank != rightRank → leftRank < rightRank`（h:74-78），即任何非 Invalid < Invalid、Invalid > 任何非 Invalid ✓（注意方向：Rust `(_, Invalid) = Ordering::Less` 意为 self < Invalid 当 other 是 Invalid；C++ `IsBetterSample(left, right)` 即 left<right，left=CLEAR(0) right=INVALID(2) → true ✓，反之 INVALID vs CLEAR → false ✓）。
  2. **(Collision, Clear) → Greater / (Clear, Collision) → Less**：rank COLLISION(1) > CLEAR(0)，`leftRank < rightRank` 判定同向 ✓。
  3. **同 kind 的 loss 比较**：Rust（:29-32）`FPA(l1).partial_cmp(&FPA(l2)).unwrap()`——经 FPA 包装的容差比较：**近似相等（float_cmp 0.10 默认 margin）→ Ordering::Equal，否则按原始 partial_cmp**。FPA 的 eq 实现是 `approx_eq!(f32, self.0, other.0)`（jagua-rs/src/util/fpa.rs:17-21），float-cmp 0.10.0 的 `F32Margin::default() = { epsilon: f32::EPSILON, ulps: 4 }`（eq.rs:69-76），即"绝对差 ≤ f32::EPSILON **或** ulp 距离 ≤ 4 则相等"。partial_cmp（fpa.rs:25-33）：eq → Some(Equal)，否则 self.0.partial_cmp(other)（NaN 会 None→unwrap panic）。C++ `RustApproxEqualF32`（h:43-70）逐条复刻：`left == right` → true（含 ±0 相等）✓；`|l-r| <= FLT_EPSILON` → true ✓；否则按 IEEE 有序位模式（符号位翻转法：负数取 ~bits、正数取 bits^signBit，h:59-65）计算 ulp 距离 `<= 4` ✓。与 float-cmp 的 `Ulps` 实现一致。然后 `IsBetterSample`（h:92-93）：`!RustApproxEqualF32(l, r) && l < r`——即"不近似相等且严格更小"才算 better，对应 Rust FPA 的 Equal-else-compare 语义：FPA(l1)<FPA(l2) 为 false 当近似相等 ✓；近似相等时 Rust Ordering::Greater/Less 均不会出现（Equal）✓。
  4. **NaN 处理**：Rust NaN loss 走 partial_cmp→None→**unwrap panic**（:31）；C++（h:84-91）NaN 显式处理：left NaN → return false（不 better）、right NaN → return true（better）——**不 panic，NaN 视为最差**。差异：Rust panic → C++ 定义序（NaN 殿后）。【差异】（既定 panic→定义序；正常评估器不产 NaN loss）
  5. **tie 时的传递性**：FPA 近似相等不具有严格弱序（Rust 侧 Ord 实现其实违反全序公理的传递性，jagua-rs 原样如此）；C++ 同样非严格弱序，`std::sort`（BestSamples.cpp:99）在不满足严格弱序的比较器上是 UB——但 4-ulp 容差下实际数据未触发（fixture 通过）。此为两侧共有的固有风险，非 C++ 引入。【差异】（共同边界：比较器不满足严格弱序；两侧一致地"继承"该行为）
- 判定：【一致】（NaN 分支一条形态差异）
- 证据：sample_eval.rs:21-37 vs SampleEval.h:29-99；float-cmp 0.10.0 eq.rs:69-76（F32Margin 默认）与 jagua-rs fpa.rs:17-33；fixture 的 accepted 标志（BestSamples report 接受/拒绝）与坐标下降候选轨迹（better/worse 驱动的步长乘子序列）逐位锁定此比较语义。

### `Eq for SampleEval`（sample_eval.rs:37）
- C++ 对应：`operator==`（SampleEval.h:101-116）：kind 不同→false；都 INVALID→true；NaN↔NaN→true（双方 NaN 判等，h:111-113）；否则 loss 位级 `==`。Rust 的 `#[derive(PartialEq)]`（:7）是**位级枚举相等**（Clear{loss:1.0} == Clear{loss:1.0} 仅在 loss 位同），NaN≠NaN（f32 PartialEq 语义），Invalid==Invalid ✓。**差异**：C++ 把 NaN 判等（isnan&&isnan→true），Rust derive 的 NaN==NaN 为 false；且 C++ 的 `==` 对 Clear 与 Clear 用 loss 位级相等——与 Rust derive 一致（除 NaN 外）。NaN 分支再现 panic-free 化。【差异】（NaN 相等性；无调用方依赖 NaN 相等）
- 证据：sample_eval.rs:7 vs SampleEval.h:101-116。

### `SampleEvaluator` trait（sample_eval.rs:40-44）
- C++ 对应：抽象类 `SampleEvaluator`（SampleEval.h:123-133）：`Evaluate(transform, upperBound) = 0`、`EvaluationCount() = 0`。签名对应：`evaluate_sample(dt, Option<SampleEval>) -> SampleEval` ↔ `Evaluate(const Transform2d&, const std::optional<SampleEval>&)`；`n_evals() -> usize` ↔ `EvaluationCount() -> std::size_t`。trait 的静态/动态分发 → 虚函数动态分发。
- 判定：【一致】
- 证据：sample_eval.rs:40-44 vs SampleEval.h:123-133。

### 【C++ 额外】`SampleKindRank` 的 default return（h:39-41）
- switch 外的 `return 2;`（INVALID rank）为编译器要求的兜底，无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | NaN loss 比较：Rust FPA→partial_cmp→None→unwrap panic；C++ IsBetterSample 显式定义序（NaN 最差）、operator== NaN↔NaN 判等 | 差异 | 既定 panic→定义序；正常评估器（LBF/Sep）不产 NaN，不触发 | sample_eval.rs:31 vs SampleEval.h:84-91,111-113 |
| 2 | 近似相等容差的复刻方式：Rust float-cmp 0.10（ε=f32::EPSILON 或 ulps≤4 → Equal）；C++ RustApproxEqualF32（ε=FLT_EPSILON 或 ulp 距离≤4） | 一致（核心复刻点） | 全部下游判定（BestSamples/坐标下降）依赖此语义，fixture 锁定 | fpa.rs:17-33 + float-cmp eq.rs:69-76 vs SampleEval.h:43-70 |
| 3 | 枚举展平：带数据 enum → kind+loss 结构 | 差异 | 形式差异；Invalid 的 loss 字段为死值（恒 0） | sample_eval.rs:8-13 vs SampleEval.h:23-27 |
| 4 | trait 动态分发（impl SampleEvaluator 泛型）→ 虚类（SampleEvaluator&） | 差异 | 分发形式差异，无行为影响 | sample_eval.rs:40-44 vs h:123-133 |
| 5 | 比较器不满足严格弱序（4-ulp 容差的固有特性，两侧共有） | 差异（共有风险） | std::sort 传给不满足 weak ordering 的比较器理论上是 UB；Rust sort_by_key 同样依赖全序假设。实际 fixture 数据未触发 | sample_eval.rs:29-32 vs BestSamples.cpp:99-102 + SampleEval.h:92-93 |
| 6 | `impl PartialOrd`（:15-19）委托 cmp——C++ 无单独对应（operator< 直接实现） | 一致 | 形式差异 | sample_eval.rs:15-19 vs h:96-99 |

## 结论

SampleEval 的三态序（Clear < Collision < Invalid，同态按 FPA 容差比较 loss）在 C++ 中通过 `SampleKindRank` + `IsBetterSample`（`!approxEqual && loss<`）+ `RustApproxEqualF32`（ε=f32::EPSILON 或 ulps≤4，逐位复刻 float-cmp 0.10 默认 F32Margin）完整复刻，这是采样层全部接受/淘汰/精化决策的基石，被三个 Rust trace fixture 的位模式锁定。差异仅为 NaN 的 panic→定义序（既定形态）与枚举展平/虚分发等 API 形态。

### 补充审计（2026-09-29）
1. **【核对确认：43 行逐项覆盖】** sample_eval.rs 全量复核：SampleEval 枚举 3 变体、PartialOrd、Ord（6 match 臂）、Eq derive、SampleEvaluator trait（2 方法）——主文 5 小节全覆盖无遗漏。
2. **【浅覆盖补全：Ord 的 match 臂覆盖顺序】** 主文条目 1-3 已逐臂核对；补记遗漏臂验证：`(Collision{..}, Clear{..})→Greater`、`(Clear{..}, Collision{..})→Less`（sample_eval.rs:27-28）在 C++ 侧由 rank 数值序天然覆盖（COLLISION=1 > CLEAR=0），同 kind 复合模式 `|`（:29-32）对应 C++ 的 else 分支（非 Invalid 且同 rank）——**全 6 臂无遗漏映射**，判定维持。
3. **【核对确认：FPA 的来源链】** `jagua_rs::util::FPA`（fpa.rs:17-33）经 float-cmp 0.10 `approx_eq!`（F32Margin 默认 ε+4ulps，eq.rs:69-76）——主文证据链完整（含两个上游文件行号），现场抽验 rand/float-cmp 未被引入本文件（仅 jagua-rs util），无未记依赖。
