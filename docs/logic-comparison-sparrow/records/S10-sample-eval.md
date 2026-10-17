# S10. sample_eval（样本评估值类型与比较语义）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/sample_eval.rs`（44 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/SampleEval.h`（127 行；SampleEval.cpp 仅 1 行 include，全头文件内联）
**判定**: 单次有限值及可排序无穷值的比较规则对应；固定 Rust 工具链下七组边界输入的六排列（42 行）和一条四样本非传递链的 24 种输入排列均有排序参考，C++ BestSamples 稳定插入排序匹配所列 fixture；coordinate descent 近似并列消费者轨迹也一致。Rust Ord 合同仍被非传递比较器违反，因此只认可所列输入/profile/排列。同 kind NaN loss 排序比较两侧均失败（Rust panic、C++ std::domain_error），不同 kind 先按 rank；operator== 的 NaN 语义与 Rust PartialEq 一致。

这是 eval 层的基础类型：枚举 `SampleEval {Clear{loss}, Collision{loss}, Invalid}` + `SampleEvaluator` trait。比较语义（含 FPA 容差）直接决定 BestSamples 的接受/淘汰与坐标下降的 better/worse 判定。相关 fixtures 精确比较 kind/accepted 等离散字段，浮点 loss/transform 按各测试容差比较；保存 `loss_f32_bits` 文本不代表测试逐位比较。

## `SampleEval` 排序消费者清单

| Rust 消费点 | C++ 对应 | 操作与覆盖 |
|---|---|---|
| `sample/best_samples.rs::report` | `BestSamples::Report` | 先比较 upper bound 与相似项，再按 `sort_by_key` 稳定排序；C++ 使用 Rust 近似相等比较与稳定插入，不调用 `std::sort` |
| `sample/coord_descent.rs::refine_coord_desc` / `CoordinateDescent::tell` | `RefineCoordinateDescent` | 两候选以 `min_by_key` 选首个最小项，随后比较新旧 loss 更新 pose、步长和随机轴 |
| `sample/uniform_sampler.rs::new` 的 `min_by_key` | `UniformRectSampler::Create` | 排序 key 是旋转几何 bbox 高度，不是 `SampleEval`；不消费 SampleEval comparator |

直接 `cmp`/排序消费者为 BestSamples 和 CoordinateDescent 两类；Rust/C++ 其余 `SampleEval` 使用是字段分类或 loss 传递。当前 golden 覆盖七组有限/无穷边界、四样本非传递链、BestSamples 32 项插入轨迹、NaN 同 kind 失败与 coordinate-descent 近似并列轨迹。由于 Rust `Ord` 比较关系本身不满足传递性，有限夹具不能证明任意序列上的稳定排序结果。

## 函数逐条比对

### `SampleEval` 枚举（sample_eval.rs:8-13）
- C++ 对应：`struct SampleEval { SampleKind kind; float loss; }`（SampleEval.h:24-28），`SampleKind` 枚举 INVALID/COLLISION/CLEAR（h:17-22）。Rust 的带数据枚举展平为 kind+loss 结构；Invalid 变体的 loss 字段在 C++ 恒为 0.0F（约定，无人读取）。
- 判定：【一致】
- 证据：sample_eval.rs:8-13 vs SampleEval.h:17-28。

### `PartialOrd/Ord for SampleEval`（sample_eval.rs:15-37）
- C++ 对应：`SampleKindRank` + `IsBetterSample` + `operator<`（SampleEval.h:30-96）。这是全文件最关键的比对点，逐 match 臂核对：
  1. **(Invalid, Invalid) → Equal**：C++ h:80-83 `if (left.kind == SAMPLE_KIND_INVALID) return false;`（rank 相同且都 INVALID 时 `left < right` 为 false）✓。**(_, Invalid) → Less / (Invalid, _) → Greater**：C++ SampleKindRank（h:30-42）给 CLEAR=0、COLLISION=1、INVALID=2，`leftRank != rightRank → leftRank < rightRank`（h:75-79），即任何非 Invalid < Invalid、Invalid > 任何非 Invalid ✓（注意方向：Rust `(_, Invalid) = Ordering::Less` 意为 self < Invalid 当 other 是 Invalid；C++ `IsBetterSample(left, right)` 即 left<right，left=CLEAR(0) right=INVALID(2) → true ✓，反之 INVALID vs CLEAR → false ✓）。
  2. **(Collision, Clear) → Greater / (Clear, Collision) → Less**：rank COLLISION(1) > CLEAR(0)，`leftRank < rightRank` 判定同向 ✓。
  3. **同 kind 的 loss 比较**：Rust（:29-32）`FPA(l1).partial_cmp(&FPA(l2)).unwrap()`——经 FPA 包装的容差比较：**近似相等（float_cmp 0.10 默认 margin）→ Ordering::Equal，否则按原始 partial_cmp**。FPA 的 eq 实现是 `approx_eq!(f32, self.0, other.0)`（jagua-rs/src/util/fpa.rs:17-21），float-cmp 0.10.0 的 `F32Margin::default() = { epsilon: f32::EPSILON, ulps: 4 }`（eq.rs:69-76），即"绝对差 ≤ f32::EPSILON **或** ulp 距离 ≤ 4 则相等"。partial_cmp（fpa.rs:25-33）：eq → Some(Equal)，否则 self.0.partial_cmp(other)（NaN 会 None→unwrap panic）。C++ `RustApproxEqualF32`（h:44-71）逐条复刻：`left == right` → true（含 ±0 相等）✓；`|l-r| <= FLT_EPSILON` → true ✓；否则按 IEEE 有序位模式（符号位翻转法：负数取 ~bits、正数取 bits^signBit，h:61-66）计算 ulp 距离 `<= 4` ✓。与 float-cmp 的 `Ulps` 实现一致。然后 `IsBetterSample`（h:89-91）：`!RustApproxEqualF32(l, r) && l < r`——即"不近似相等且严格更小"才算 better，对应 Rust FPA 的 Equal-else-compare 语义：FPA(l1)<FPA(l2) 为 false 当近似相等 ✓；近似相等时 Rust Ordering::Greater/Less 均不会出现（Equal）✓。
  4. **NaN 处理**：Rust 同 kind 的 NaN loss 比较在 partial_cmp→None→unwrap 处 panic（:31）；C++ 同 kind 比较抛 std::domain_error（SampleEval.h:85-87），不同 kind 仍先按 rank 比较。C++ operator== 现与 Rust PartialEq 一致，NaN 不等于自身。两侧都不会把 NaN 定义为可排序的最差值；失败机制仍是 panic vs exception。【失败语义对应，机制有差异】
  5. **tie 时的传递性**：FPA 近似相等不具有严格弱序，Rust Ord 实现违反传递性。Rust 官方将违反 Ord 要求归为逻辑错误；实际观察不能成为对所有输入的排序保证。Rust comparator fixture 覆盖正 ULP 链、接近零的正 epsilon 链、负 epsilon 链、负 ULP 链、2.0 附近正 ULP 链、最大有限值至无穷链、正负零 ties，每组分别核对全部六种排列（共 42 行）。另固定 A/B/C/D loss bits `0x3f800000/0x3f800004/0x3f800008/0x3f80000c`：相邻值比较 Equal，而 A<C、B<D；Rust 对四项输入的 24 种排列均有 fixture。锁定 rustc/cargo 1.98.1 的 Debug 与 Release 轨迹一致，C++ BestSamples 实际插入路径在 Debug/Release 均匹配这些行。C++ 原 std::sort 对旧 CBA 用例不匹配且不满足其前置条件。另以 4 ULP 近似并列候选调用 Rust refine_coord_desc 和 C++ RefineCoordinateDescent，比较 6 个候选、最终变换/loss 和评估数。该证据覆盖列出的输入和消费者，不推广到任意非传递组或序列。【已建立边界与消费路径参考；一般合同风险仍存在】
- 判定：有限值与可排序无穷值上的单次比较规则在源码层面对应，但 4 ULP/epsilon 近似相等关系非传递。Rust Ord 合同违反属于逻辑错误；实测可重复不等于 Rust 对所有输入保证某一排序结果。锁定 Rust 1.98.1 的参考用例覆盖七组输入各自六种排列及一条四样本链的 24 种排列，C++ 稳定插入排序在 BestSamples 实际消费路径匹配；coordinate descent 另有固定消费者轨迹。NaN 同 kind 排序比较为 Rust panic、C++ std::domain_error；不同 kind 先按 rank 比较。正负无穷不会被当作 NaN 拒绝。所有结论限于列出的输入与 profile。([Rust Ord 合同](https://doc.rust-lang.org/std/cmp/trait.Ord.html#violating-the-ordering-of-ordering-traits)，[C++ 排序比较要求](https://eel.is/c++draft/alg.sorting.general)，[标准库前置条件](https://eel.is/c++draft/structure.specifications))
- 证据：sample_eval.rs:21-37 vs SampleEval.h:30-96；float-cmp 0.10.0 eq.rs:69-76（F32Margin 默认）与 jagua-rs fpa.rs:17-33。fixture 的 accepted/kind/计数按测试精确断言，候选变换/loss 按容差断言；它们只覆盖列出的输入轨迹。

### `Eq for SampleEval`（sample_eval.rs:37）
- C++ 对应：operator==（SampleEval.h:98-109）：kind 不同→false；都 INVALID→true；有限值按浮点数值比较。Rust derive PartialEq 同样使用浮点数值相等而非位模式相等：+0.0 == -0.0 为 true，NaN 与自身比较为 false；C++ 当前行为一致。
- 证据：sample_eval.rs:7 vs SampleEval.h:98-109。

### `SampleEvaluator` trait（sample_eval.rs:40-44）
- C++ 对应：抽象类 `SampleEvaluator`（SampleEval.h:116-126）：`Evaluate(transform, upperBound) = 0`、`EvaluationCount() = 0`。签名对应：`evaluate_sample(dt, Option<SampleEval>) -> SampleEval` ↔ `Evaluate(const Transform2d&, const std::optional<SampleEval>&)`；`n_evals() -> usize` ↔ `EvaluationCount() -> std::size_t`。trait 的静态/动态分发 → 虚函数动态分发。
- 判定：【一致】
- 证据：sample_eval.rs:40-44 vs SampleEval.h:116-126。

### 【C++ 额外】`SampleKindRank` 的 default return（h:40-42）
- switch 外的 `return 2;`（INVALID rank）为编译器要求的兜底，无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | NaN loss 比较：Rust FPA→partial_cmp→None→unwrap panic；C++ 同 kind comparator 抛 std::domain_error，kind rank 不同则先比较 rank | 失败语义相同，错误机制不同 | NaN 不会被静默排序为最差；Rust panic，C++ exception。正常 LBF/Sep evaluator 不产 NaN | sample_eval.rs:29-32 vs SampleEval.h:73-96 |
| 2 | 近似相等容差的复刻方式：Rust float-cmp 0.10（ε=f32::EPSILON 或 ulps≤4 → Equal）；C++ RustApproxEqualF32（ε=FLT_EPSILON 或 ulp 距离≤4） | 源码语义对应；七类三样本边界排序、四样本链的 24 种排列和 coordinate descent 消费 trace 均有固定 Rust reference | 仍未穷尽容差边界、其他非传递组、更长输入或任意排序行为 | fpa.rs:17-33 + float-cmp eq.rs:69-76 vs SampleEval.h:44-71; SampleEvalComparatorBoundaryRustReference.tsv; SampleEvalNonTransitiveSortRustReference.tsv; SampleEvalCoordinateDescentRustReference.tsv |
| 3 | 枚举展平：带数据 enum → kind+loss 结构 | 差异 | 形式差异；Invalid 的 loss 字段为死值（恒 0） | sample_eval.rs:8-13 vs SampleEval.h:24-28 |
| 4 | trait 动态分发（impl SampleEvaluator 泛型）→ 虚类（SampleEvaluator&） | 差异 | 分发形式差异，无行为影响 | sample_eval.rs:40-44 vs h:116-126 |
| 5 | FPA 非传递比较器违反 Rust Ord 合同；C++ BestSamples 以稳定插入排序承接，并在七组三样本边界的 42 行和一条四样本链 24 种排列的实际消费 fixture 上匹配锁定 Rust 输出 | 固定 fixture 的排序顺序与一个 coordinate descent 消费轨迹已匹配；其他非传递序列不声明 parity | 覆盖列明的七组三样本输入、一个四样本链和一个近似并列 coordinate descent 轨迹；Rust 仍将违反 Ord 视为逻辑错误 | sample_eval.rs:29-32；SampleEvalComparatorBoundaryRustReference.tsv；SampleEvalNonTransitiveSortRustReference.tsv；SampleEvalCoordinateDescentRustReference.tsv；TestLbfLoss.cpp；TestSampling.cpp:1912-2012；Rust Ord; C++ sort |
| 6 | impl PartialOrd（:15-19）委托 cmp——C++ 无单独对应（operator< 直接实现） | 一致 | 形式差异 | sample_eval.rs:15-19 vs SampleEval.h:93-96 |

## 结论

SampleEval 的三态单次比较规则（Clear < Collision < Invalid，同态按 FPA 容差比较 loss）在 C++ 中通过 SampleKindRank、IsBetterSample 和 RustApproxEqualF32 对应。七组三样本边界的 42 种排序输入、一条四样本链的 24 种排列、BestSamples 消费顺序和 coordinate descent 近似并列轨迹均有 RustReference，并由 C++ Debug/Release 实际测试匹配。浮点 trace 中记录的 bits 不等于 raw-bit 断言；coordinate descent fixture 对候选变换和 loss 逐位编码后比较，边界排序则比较离散 ID 顺序。Rust Ord 合同仍违反，因此 fixture 不构成任意序列保证。SampleEval 的 NaN 比较在 Rust panic、C++ 抛 domain_error；operator== 对 NaN 的结果和 Rust derive 一致。

### 补充审计（2026-09-29）
1. **【核对确认：44 行逐项覆盖】** sample_eval.rs 全量复核：SampleEval 枚举 3 变体、PartialOrd、Ord（6 match 臂）、Eq derive、SampleEvaluator trait（2 方法）——主文 5 小节全覆盖无遗漏。
2. **【浅覆盖补全：Ord 的 match 臂覆盖顺序】** 主文条目 1-3 已逐臂核对；补记遗漏臂验证：`(Collision{..}, Clear{..})→Greater`、`(Clear{..}, Collision{..})→Less`（sample_eval.rs:27-28）在 C++ 侧由 rank 数值序天然覆盖（COLLISION=1 > CLEAR=0），同 kind 复合模式 `|`（:29-32）对应 C++ 的 else 分支（非 Invalid 且同 rank）——**全 6 臂无遗漏映射**，判定维持。
3. **【核对确认：FPA 的来源链】** `jagua_rs::util::FPA`（fpa.rs:17-33）经 float-cmp 0.10 `approx_eq!`（F32Margin 默认 ε+4ulps，eq.rs:69-76）——主文证据链完整（含两个上游文件行号），现场抽验 rand/float-cmp 未被引入本文件（仅 jagua-rs util），无未记依赖。

### 补充审计（2026-10-07）
4. **【此前的 Rust 边界与消费路径扩展（2026-10-07）】** 初版 SampleEvalComparatorBoundaryRustReference.tsv 固定五组各六种排序排列；Rust tests/sample_eval_boundary_reference.rs 另固定近似并列 coordinate descent 的 6 个候选、终值和评估数。C++ TestLbfLoss.cpp 实际 BestSamples 顺序与 TestSampling.cpp 的 RefineCoordinateDescent 路径通过固定 fixture 对照。NaN 比较改为抛 std::domain_error、相等判断改为 false，与 Rust 的失败/PartialEq 语义对应；异常类型仍不同。后续扩展见下项。
5. **【扩展非传递边界与更长插入序列（2026-10-07）】** 比较器 fixture 增至七组三样本×六排列（42 行），新增负 epsilon 链和 2.0 附近 ULP 链；另将 BestSamples 真实消费序列从 3 项扩为 4 项，并固定 A/B/C/D 的 24 种排列。Rust 1.98.1 Debug/Release 轨迹一致，C++ BestSamples Debug/Release 逐行匹配。此证据仍不覆盖一般长度或任意 non-transitive 输入。

### P0 顺序核验（2026-10-09）

现有证据足以闭合计划中“列明合法输入序列”的工作项：RustReference 的 `sample_eval_boundary_reference.rs` 固定七组比较边界的 42 种排列，`sample_eval_order_reference.rs` 固定四项非传递链的 24 种排列，`best_samples_sequence_reference.rs` 固定 32 次 BestSamples 插入及四种 NaN 失败状态，`sample_eval_boundary_reference.rs` 另有近似并列 CoordinateDescent 消费轨迹。`TestLbfLoss.cpp` 与 `TestSampling.cpp` 在 SparrowCore Debug/Release 中消费对应 golden；2026-10-09 锁定 Rust 验证脚本的 Debug/Release 全量运行通过。该项只关闭这些固定用例；Rust `Ord` 对非传递比较仍违反排序合同，任意序列继续标为未定义/未认证域。

### 固定 profile 范围收口（2026-10-17）

本轮静态复核确认，清单和决策矩阵先前所写“仍需覆盖其他非传递组和更长插入序列”已过时：七组比较器边界共 42 个排列、四值非传递链共 24 个排列、BestSamples 32 项混合插入及四种 NaN 失败状态、CoordinateDescent 近似并列消费者轨迹均已有 Rust reference 和 C++ 对应断言。Rust `sample_eval_boundary_reference.rs`、`sample_eval_order_reference.rs`、`best_samples_sequence_reference.rs` 与 C++ `TestLbfLoss.cpp` / `TestSampling.cpp` 的直接消费者落点已逐项确认。

据 D1 的声明输入域口径，S10 按上述固定 Rust 1.98.1 Debug/Release profile 与已枚举消费者范围标记完成。已有执行记录显示 RustReference 与 SparrowCore C++ Debug/Release 对应运行通过（2026-10-09）；本轮仅静态复核并同步文档，未重新运行测试。Rust `Ord` 对近似相等采用非传递关系，违反排序合同；其他任意非传递序列仍不认证并继续由 R3 跟踪。S15 的独立容量、相似度、精确 tie 等余项不因 S10 收口而关闭。
