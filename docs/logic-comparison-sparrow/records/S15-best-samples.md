# S15. best_samples（最优样本保留结构）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/best_samples.rs`（107 行）
**C++**: SparrowCore/Include/SparrowCore/Sampling/BestSamples.h（37 行）+ SparrowCore/Source/Sampling/BestSamples.cpp（178 行）
**判定**: 接受、正容量淘汰与相似度路径在当前固定参考轨迹上对应；S15 参考覆盖平移 f32 舍入、x/y 的 5% 严格边界、1° 严格边界、NaN/±∞ 平移阈值分类、容量未满/恰满/容量 1 的拒绝与替换，以及 32 项混合非传递插入轨迹。C++ 现先把两个 double 平移坐标各自舍入为 f32，再按 f32 相减，与 Rust `DTransformation` 一致。零容量按 Rust profile 行为处理：Debug 的下溢 panic 对应 C++ `std::underflow_error`；Release 的 wrapping 行为会使集合超过容量，C++ 也保留该结果。当前 LBF 与 Separator 搜索入口都把原始 `nCoordinateDescents` 传给 BestSamples，0 容量会实际触发这些 profile 行为；该基线边界是 Rust 自身的不安全配置，不应将其误写成“正常安全调用点将容量抬至 1”。Rust `sort_by_key` 与 C++ 稳定插入排序在列明的 S10 排列和 S15 32 项序列上逐步匹配；Rust 比较器仍非传递，不能据此保证其他一般序列。正负无穷 loss 可比较；四种固定 BestSamples NaN 比较路径分别验证了失败触发时点或跨 kind 不触发比较，C++ `std::domain_error` 映射 Rust panic。

C++ BestSamples 类与 Rust BestSamples 结构逐方法对应；既有 search trace 的 report/keep 行核对 accepted 标志、retained 数量及保留列表，离散字段精确比较，loss/transform 按各自容差比较。S10 通过真实 BestSamples 消费路径比较七组边界输入的全部六种排列，以及四项非传递链的全部 24 种输入排列；S15 再逐步比较一条 32 项混合插入序列。新增 NaN reference 覆盖满容量 upper-bound 比较失败、相似样本比较失败、插入后排序失败三种触发位置，并验证不同 kind 只按 rank 比较时不触发 NaN loss 比较；每步核对失败/接受结果、样本集合与 upper bound。BestSamples 及其 S15 直接用例不调用 RNG，因此 RNG-after 不适用；搜索编排的 RNG-after 由 S13 trace 检查。证据限定于锁定 Rust 工具链和列出的输入，不证明任意非传递序列。

## 函数逐条比对

### `BestSamples::new`（best_samples.rs:18-24）
- C++ 对应：构造函数（cpp:13-17）。字段：size→m_Capacity、samples→m_Samples（初始为空）、unique_thresh→m_UniqueThreshold。
- 判定：【一致】
- 证据：best_samples.rs:18-24 vs BestSamples.cpp:13-17。

### `BestSamples::report`（best_samples.rs:26-75）
- C++ 对应：BestSamples::Report（cpp:53-133）。逐分支核对：
  1. **接受前置**：Rust 每次先比较 eval < upper_bound()（:27）；未满容量时 upper_bound() 返回 SampleEval::Invalid（:81-87），由于 kind rank，所有非 Invalid kind（含 ±Inf loss）都排在 Invalid 前，不会在这一步比较 loss。C++ 只在容量已满时比较候选与尾部（cpp:69-71）；正容量的列明输入结果对应。Invalid kind 在正容量下两边均拒绝。容量为零时，C++ Debug 先触发与 Rust Debug 下溢相同位置的 `std::underflow_error`，C++ Release 走与 Rust Release 相同的 wrapping 结果（见下文）。NaN 只在同 kind loss 比较实际发生时触发 Rust panic/C++ domain_error；并非一律前置拒绝。
  2. **相似度扫描**：Rust（:30-31）先用 any 查找近似变换；C++（cpp:74-81）收集 similarIndices 后判空。判定条件对应，平移精度边界另见下文。
  3. **无相似样本**：Rust（:34-38）在满容量时先 pop 尾部，再 push 并按 sort_by_key 稳定排序（:58-60）；C++（cpp:110-130）先 push，再执行稳定插入排序并按容量截断。操作次序不同；S10 七组×六排列、四样本链×24排列，以及 S15 32 项混合插入序列的真实 BestSamples fixture 均逐步匹配锁定 Rust 输出。由于比较器非传递，不能据此推断其他输入序列也得到相同结果。【列明 fixture 一致；普遍行为未证明】
  4. **有相似样本**：Rust（:40-53）要求候选严格优于所有相似样本，否则拒绝；满足时淘汰全部相似项。C++（cpp:83-108）执行同样的全量比较和过滤替换。IsBetterSample 的有限值容差对应 Rust FPA 比较；NaN 失败机制见第 6 项。【一致（列明有效输入）】
  5. **push + 排序**：Rust（:58-60）使用稳定 sort_by_key；C++（cpp:110-130）使用稳定插入排序，避免把违反严格弱序前置条件的比较器传入 std::sort。S10 的七组三样本输入/全排列、四项非传递链/24 种排列，以及 S15 的 32 项混合插入序列均由真实 BestSamples 路径匹配锁定 Rust 1.98.1 Debug/Release fixture。两侧排序算法不同，且 Rust Ord 合同本身违反；证据不覆盖其他非传递序列。【固定 fixture 一致，普遍行为未证明】
  6. **输入边界与 NaN**：两边都不预过滤非有限 loss，±Inf 按比较器正常排序。Rust 的 Invalid 在 Invalid < Invalid 时不通过接受条件。新增固定轨迹分别验证：满容量 upper-bound 同 kind 比较 panic 且集合不变；相似项比较 panic 且集合不变；候选已 push、排序比较 panic 后集合含两项；Clear NaN 与 Collision loss 比较只按 kind rank、不 panic。Rust `partial_cmp(...).unwrap()` panic，C++ `IsBetterSample` 抛 `std::domain_error`（SampleEval.h:85-87）；C++/Rust 对应状态逐行核对。零容量时 Rust `upper_bound()` 计算 `size - 1`：Debug 下溢 panic；Release wrapping 后索引越界返回 Invalid，非 Invalid 候选通过容量上界比较，后续是否接受仍取决于相似度规则。互不相似且被接受的报告会继续累积并突破容量。C++ Debug 在 `UpperBound()` 抛 `std::underflow_error`；Release 对零容量返回 Invalid 并跳过容量淘汰/截断，复现相同集合结果。LBF/Separator 搜索现在原样传递零容量配置，S13 有真实搜索入口的 profile reference。
- 判定：【列明输入上对应；边界差异与一般排序限制另列】
- 证据：best_samples.rs:26-75 vs BestSamples.cpp:53-133；既有 accepted/retained fixtures 见 TestSampling.cpp:1296-1348、TestSeparator.cpp:901-958；S10 排序与 S15 边界/长序列/NaN fixture 由 TestLbfLoss.cpp 经真实 BestSamples 路径核对。Rust reference 直接编译当前 `best_samples.rs` / `sample_eval.rs` 源文件，Rust Debug/Release 逐行核对 32 项插入和四种 NaN 路径；C++ Debug/Release 全量测试程序也执行相同 fixture。边界、排序和失败后的集合状态均按 loss bits 精确比较。

### `BestSamples::best`（best_samples.rs:77-79）
- C++ 对应：BestSamples::Best（cpp:151-158）。Rust samples.first().cloned()；C++ 空→nullopt、否则 m_Samples.front()。
- 判定：【一致】
- 证据：best_samples.rs:77-79 vs BestSamples.cpp:151-158。

### `BestSamples::upper_bound`（best_samples.rs:81-87）
- C++ 对应：BestSamples::UpperBound（cpp:160-178）。Rust `samples.get(size-1)`：未满容量时返回 SampleEval::Invalid，满容量时返回尾部最差样本；正容量时 C++ 条件相同。
- 细节核对：Rust `samples.get(self.size - 1)` 在 `len == size` 时取尾部，在 `len < size` 时返回 None。Rust `size == 0` 时 Debug 下溢 panic；Release wrapping 到 `usize::MAX` 后对任意集合长度都返回 None/Invalid。C++ Debug 抛 `std::underflow_error`；Release 对零容量固定返回 Invalid，与 Rust 的有效结果相同。Rust 搜索调用点（search.rs:242-245）把配置原样传入；C++ 的 LBF 与 Separator 搜索调用点现也原样传入，真实 Search profile 行为见 S13。
- 判定：【一致】（构建 profile 相同；Debug 异常类型不同）
- 证据：best_samples.rs:81-87 vs BestSamples.cpp:160-178；LbfSearch.cpp:170-172。

### `dtransfs_are_similar`（best_samples.rs:90-107）
- C++ 对应：BestSamples::IsSimilar（cpp:20-51）。逐条件核对：
  1. Rust：`x_diff < x_thresh && y_diff < y_thresh`（x/y 阈值都传 unique_thresh，调用处 :31,:42,:47,:64 四处均同参）；然后 `r1 = rotation % (2π)`、`r2 = rotation % (2π)`、`angle_diff = |r1 - r2| < 1°`（`(1.0f32).to_radians()`）。C++（cpp:32-50）：先将平移分别 cast 为 float，再比较 `xDiff < threshold && yDiff < threshold`；`fmod(rotation, TWO_PI)` 两侧（注释明确说明保留符号、非循环最短距离，与 Rust `%` 语义一致——Rust f32 的 `%` 即 truncated 取余，负数结果为负）；`abs(leftRot - rightRot) < ROTATION_THRESHOLD`，其中 `ROTATION_THRESHOLD = 0.01745329251994329577F`（cpp:9）= 1°（π/180 ≈ 0.017453293）。【一致】
  2. Rust 平移坐标和相减均为 **f32**（`DTransformation` 字段为 f32）；C++ 输入字段为 double，因此现在先将左右 x/y 坐标分别转为 float，再做 float 相减（cpp:32-37）。这与 Rust 相同地舍入每个坐标后再求差。S15 的 `translation_f32_rounding` 用例令 C++ 输入为 1.0 与 1.05000001、阈值 0.05：Rust 第二个坐标先舍入为 f32 后差值小于阈值，候选因相似且较差而拒绝；旧 C++ 双精度相减路径会错误接受。修正后的 C++ 与 Rust 轨迹逐行一致。【已修正并验证】
  3. 相似度使用严格边界：x/y `difference < threshold`，旋转差 `< 1°`；相等时不相似，下一可表示 f32 小于边界时相似。S15 分别覆盖 x/y 的 0.05f 与其相邻下方值，以及 `1f32.to_radians()` 与其相邻下方值，C++/Rust 的 accepted 与 retained 结果一致。【固定边界一致】
  4. 阈值非有限边界：Rust 不显式过滤阈值；NaN 与负阈值会让距离比较为 false，正无穷则让有限 x/y 差通过。C++ 现只显式拒绝 NaN/负阈值，允许正无穷；S15 分别验证 `+∞` 会参与相似归并，NaN 与 `-∞` 阈值不会归并。【列明阈值域一致】
- 判定：【一致】
- 证据：best_samples.rs:96-104 vs BestSamples.cpp:32-50；`BestSamplesBoundaryRustReference.tsv` 由 Rust 参考和 C++ 真实 `BestSamples` 路径逐行比较，包含坐标舍入、x/y 严格边界及旋转严格边界。

### 【C++ 额外】访问器 Size/Capacity/Samples
- cpp:135-149 提供 Size()/Capacity()/Samples() 常量访问器，对应 Rust 的 pub 字段直读（best_samples.rs:12-14）。形式差异，无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 淘汰-插入顺序：Rust 满容量时先 pop 再 push + 稳定 sort_by_key；C++ push 后稳定插入排序再截断 | S10 固定排列及 S15 32 项混合插入序列的真实 BestSamples 轨迹逐步匹配 | Rust 比较器非传递；其他插入序列仍可能受排序算法影响，不能从固定 fixture 推广 | best_samples.rs:35-38,58-60 vs BestSamples.cpp:110-130; SampleEvalComparatorBoundaryRustReference.tsv; SampleEvalNonTransitiveSortRustReference.tsv; BestSamplesSequenceRustReference.tsv |
| 2 | NaN/Invalid/无穷边界：两侧不预过滤非有限 loss；同 kind NaN 比较时 Rust panic、C++ 抛 domain_error，±Inf 可排序 | 已覆盖 upper-bound、相似项、push 后排序三种失败状态，以及不同 kind 不触发 loss 比较 | 失败位置与失败后的集合只对四个固定轨迹成立；异常类型不同 | BestSamplesSequenceRustReference.tsv; BestSamples.cpp:64-72,110-132; SampleEval.h:73-91; best_samples.rs:27-32,81-87 |
| 3 | size=0 容量边界：Rust/C++ Debug 均失败，Release 均可能接受并持续增长、突破容量；真实 LBF/Separator 搜索入口现在原样传零容量 | 已按 Rust profile 行为覆盖直接 BestSamples 与 LBF Search 调用路径；C++ 异常类型不同 | 这是 Rust 的零容量边界缺陷，不应扩展为普通配置承诺；Release 行为受后续候选相似度与排序路径影响 | best_samples.rs:27,81-87; search.rs:242-245 vs BestSamples.cpp; LbfSearch.cpp; SeparatorWorker.cpp; `search_zero_boundary_reference.rs`; TestSampling.cpp |
| 4 | 平移精度：Rust 先将坐标存为 f32 后做 f32 相减；C++ 现在先分别 cast 坐标为 float，再相减 | `1.0`/`1.05000001` 在阈值 0.05 下的误分类已修复 | Rust 参考直接用 f32 数据，C++ 测试保留原始 double 输入以覆盖转换边界 | best_samples.rs:96-97 vs BestSamples.cpp:32-38; BestSamplesBoundaryRustReference.tsv |
| 5 | 平移阈值域：NaN/负值不相似，`+∞` 对有限平移差通过 | C++ 与 Rust 的三种边界分类已由实际 BestSamples 路径验证 | 覆盖 NaN、负无穷和正无穷阈值；不替代其他输入的相似度轨迹 | BestSamples.cpp:24-27; best_samples.rs:96-104; BestSamplesBoundaryRustReference.tsv |
| 6 | Rust debug_assert!（样本唯一性与排序不变量）→ C++ 无对应断言 | Debug-only 检查差异；无正常路径行为影响 | Rust 对不变量失败会 panic；C++ 不额外检查 | best_samples.rs:60-74 |
| 7 | 相似度边界：x/y 5% 和旋转 1° 均采用严格 `<` | 恰好等于阈值为不相似，紧邻下方为相似；已由 Rust/C++ 实际 BestSamples 路径核对 | 证据限于阈值、相邻 f32 值及列明输入 | best_samples.rs:99-104; BestSamplesBoundaryRustReference.tsv |

## 结论

BestSamples 的平移 f32 舍入、严格 5%/1° 相似度阈值和正容量淘汰路径已通过 Rust 参考轨迹核对。S15 的 32 项混合非传递插入轨迹与四种 NaN 比较路径逐步比较 accepted/失败结果、集合大小、loss bits 和 upper bound；RustReference Debug/Release 及 SparrowCore C++ Debug/Release 全量测试均通过。NaN 上界和相似项失败时集合未变，排序失败发生在候选 push 后且两项均保留；跨 kind 排序按 rank 通过。容量为零也按 profile 复现 Rust：Debug 下溢对应 C++ `std::underflow_error`；Release 返回成功并允许集合超过容量。该 Release 行为来自 Rust `size - 1` wrapping 后无法命中尾项，是基线缺陷；LBF/Separator 搜索调用点现在保留该原始配置值，使其与 Rust 一致。±Inf 仍可排序。Rust Ord 合同非传递，32 项 fixture 只证明这一条固定插入轨迹，不能据此声称任意序列等价。

### 补充审计（2026-09-29）
1. **【核对确认：107 行逐项覆盖】** best_samples.rs 全量复核：结构（3 字段）、new、report（三分支+排序+双 debug_assert）、best、upper_bound、dtransfs_are_similar——主文 5 小节全覆盖无遗漏。
2. **【浅覆盖补全：report 的相似度四连调用的重复计算】** Rust report 内 `dtransfs_are_similar` 被**调用 4 次**（any :30-31 / filter :40-42 / all 的迭代 :43-45 / retain :50-52——同一 (dt, samples) 对重复求值）；C++ 收集 similarIndices 一次（cpp:60-67）复用索引。纯性能形态差（判定不变），补记供性能对照时定位。
3. **【浅覆盖补全：upper_bound 的 get(size-1) 越界语义】** 主文条目 3 已记 size=0 下溢 panic；补记一个更常用的正确路径细节：`samples.len() < size` 时 `get(size-1)` 为 None → Invalid——**len 与 size 之间任意差值**（如 len=1,size=3）都返回 Invalid（未满即无界），C++ `m_Samples.size() < m_Capacity || empty()` 同语义（cpp:144-151）。判定维持一致；C++ 的 `|| m_Samples.empty()` 冗余防御（size>0 且非空时 size≥1 恒真于 empty false）无行为差。
4. **【历史状态，已由 2026-10-07 fixture 扩展】** 截至 2026-09-29，Rust sort_by_key 稳定而 C++ std::sort 不稳定，且当时 fixture 未覆盖相应退化排序。此后 C++ 改为稳定插入排序，并由 S10 固定五组边界×六排列的真实 BestSamples fixture 验证；不同排序算法在其他非传递序列上的一般结果仍未证明。
5. **【此前的 2026-10-07 更新：真实 BestSamples 边界参考】** S10 初版新增五组输入各六种排列，Rust 1.98.1 Debug/Release 生成并核对 30 行固定顺序；C++ TestLbfLoss.cpp 通过 BestSamples 实际插入路径逐行对照，并重复执行检查稳定性。BestSamples 不再预过滤非有限 loss：±Inf 正常比较，同 kind NaN 比较映射为 Rust panic/C++ std::domain_error；不同 kind 按 rank 比较。后续扩展见下项。
6. **【扩展 S10 非传递输入（2026-10-07）】** 边界 fixture 现覆盖七组三样本×六排列（42 行），新增负 epsilon 与 2.0 附近 ULP 链；另以四个连续 ULP 近似相等值覆盖 24 种插入排列。Rust 1.98.1 Debug/Release 的四项排序轨迹一致，C++ Debug/Release 的真实 BestSamples 路径逐行匹配。该证据没有消除 Rust Ord 合同风险，也不证明更长一般序列。
7. **【S15 长序列与 NaN 触发位置（2026-10-07）】** 新增 `BestSamplesSequenceRustReference.tsv`：32 项混合顺序插入的非传递 loss 链，以及 NaN 在满容量 upper-bound 比较、相似样本比较、push 后排序中的三种失败位置，另含不同 kind 按 rank 比较且不触发 NaN loss 比较。Rust reference 和 C++ 真实 BestSamples 路径逐步核对结果、样本顺序、容量上界及失败后的集合状态；Rust/C++ Debug/Release 均通过。只扩展到列明轨迹，不构成任意非传递序列保证。
