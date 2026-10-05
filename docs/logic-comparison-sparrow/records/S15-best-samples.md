# S15. best_samples（最优样本保留结构）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/best_samples.rs`（107 行）
**C++**: `SparrowCore/Include/SparrowCore/sampling/BestSamples.h`（37 行）+ `SparrowCore/Source/Sampling/BestSamples.cpp`（143 行）
**判定**: 行为等价

C++ `BestSamples` 类与 Rust `BestSamples` 结构逐方法对应；接受/淘汰/相似度判定语义被 `LbfSearchRustTrace.tsv` 与 `SeparatorSearchRustTrace.tsv` 的 report/keep 行（accepted 标志 + retained 序列）锁定。

## 函数逐条比对

### `BestSamples::new`（best_samples.rs:18-24）
- C++ 对应：构造函数（cpp:14-18）。字段：size→m_Capacity、samples→m_Samples（初始为空）、unique_thresh→m_UniqueThreshold。
- 判定：【一致】
- 证据：best_samples.rs:18-24 vs BestSamples.cpp:14-18。

### `BestSamples::report`（best_samples.rs:26-75）
- C++ 对应：`BestSamples::Report`（cpp:48-108）。逐分支核对：
  1. **接受前置**：Rust `eval < self.upper_bound()`（:27）——注意当 samples.len() < size 时 upper_bound() 返回 `SampleEval::Invalid`（:81-87），任何非 Invalid eval 都 `< Invalid`（Ord 定义 Invalid 最大，见 S10），即未满容量时无条件进入相似度检查。C++（cpp:57-60）：`m_Samples.size() == m_Capacity && !IsBetterSample(evaluation, UpperBound())` 才拒绝——满容量时要求 eval 严格优于当前上界（尾部样本），未满容量时不做 loss 检查。**关键语义点**：Rust 满容量时 `eval < samples[size-1].eval`（尾部），C++ `IsBetterSample(eval, UpperBound())` 即 `!approxEqual && eval < 上界`——由于 FPA 容差下"近似相等"视为不优，C++ 比 Rust 的严格 `<` 略保守（Rust 在 eval 与上界近似相等但仍略小时会接受，C++ 拒绝）。但 Rust 的 `<` 本身经 `Ord::cmp` 也走 FPA 容差（`FPA(l1).partial_cmp(&FPA(l2))`，近似相等时 Ordering::Equal，`Equal < tail` 为 false）——所以 Rust 侧 `eval < upper_bound()` 在近似相等时同样为 false。两侧语义一致。【一致】
  2. **相似度扫描**：Rust（:30-31）`any(|(d,_)| dtransfs_are_similar(d, dt, thresh, thresh))`；C++（cpp:62-69）收集 similarIndices 后判空。相同。【一致】
  3. **无相似样本**：Rust（:34-38）`if samples.len() == size { samples.pop(); }` 先淘汰尾部再 push；C++（cpp:98-106）push 后 `if (m_Samples.size() > m_Capacity) { resize(m_Capacity); }`——先排序再截断。差异：Rust 在 push 前 pop 尾部（此时未排序的新样本未进入），C++ push 后排序再截断。由于此时新样本必然优于尾部（步骤 1 已保证满容量时 eval < 上界），两种顺序最终留下的集合相同；但 C++ 的写法在"未满容量 + 无相似"时也走同一路径，正确处理。Rust 未满容量时不 pop。【一致】
  4. **有相似样本**：Rust（:40-53）`better_than_all_similar = 所有相似样本都满足 eval < sim_eval`，否则拒绝；满足则 `retain(|(d,_)| !similar)` 淘汰全部相似样本。C++（cpp:73-95）逐索引检查 `IsBetterSample(evaluation, m_Samples[index].second)`，任一不满足则 return false；否则构造 retained 过滤后替换 m_Samples。语义相同，且 C++ 的 IsBetterSample（FPA 容差下的"更优"）与 Rust 的 `eval < *sim_eval`（同样走 FPA 容差的 Ord）一致。【一致】
  5. **push + 排序**：Rust（:58-59）`samples.push((dt, eval)); samples.sort_by_key(|(_,eval)| *eval)`——Rust `sort_by_key` 是**稳定**排序，相等 loss 保持插入顺序；C++（cpp:98-102）`std::sort`（**不稳定**）按 `IsBetterSample` 比较器。等 loss（FPA 近似相等）但不同 kind 时不影响（kind 优先）；同 kind 同 loss（近似相等）的两个样本在 C++ `std::sort` 下顺序不确定，Rust 保持先来者在先。影响极小（近似相等的样本其后的保留/淘汰由相似度判定主导），fixture 中 retained 序列（keep 行）全部匹配。【差异】（退化场景的排序稳定性；正常路径等价）
  6. **C++ 额外的防御检查**（cpp:51-54）：`m_Capacity == 0 || kind == INVALID || !isfinite(loss)` 直接拒绝。Rust 侧 INVALID eval 不可能通过步骤 1（`Invalid < Invalid` 为 false）达到 push，但 NaN loss 的 Clear/Collision eval 在 Rust 会进入比较链（FPA partial_cmp NaN→None→unwrap panic）——C++ 用 isfinite 检查替代了 panic。且注意：C++ 在**未满容量**时也会拒绝 INVALID/NaN eval（Rust 未满容量时 `eval < Invalid` 为 false 同样拒绝 INVALID，但 NaN loss 的非 Invalid eval 未满容量时 Rust 会 panic）。【差异】（NaN 防御为 C++ 额外；语义上 Rust panic → C++ 拒绝，见差异表）
- 判定：【一致】（含两条边界差异）
- 证据：best_samples.rs:26-75 vs BestSamples.cpp:48-108；fixture LbfSearchRustTrace.tsv 的 report 行（`accepted` 列）与 keep 行（`retained_count` + 序列）与 C++ trace 逐条对齐（TestSampling.cpp:1001 起的消费逻辑比对 accepted 与 retained 序列）。

### `BestSamples::best`（best_samples.rs:77-79）
- C++ 对应：`BestSamples::Best`（cpp:126-133）。Rust `samples.first().cloned()`；C++ 空→nullopt、否则 `m_Samples.front()`。
- 判定：【一致】
- 证据：best_samples.rs:77-79 vs BestSamples.cpp:126-133。

### `BestSamples::upper_bound`（best_samples.rs:81-87）
- C++ 对应：`BestSamples::UpperBound`（cpp:135-142）。Rust：`samples.get(size-1)`——注意当 `samples.len() < size` 时 `get(size-1)` 返回 None → 返回 `SampleEval::Invalid`；**当 len == size 时返回尾部样本**（已排序，最差）。C++：`m_Samples.size() < m_Capacity || m_Samples.empty()` 时返回 `{INVALID, 0.0F}`，否则 `m_Samples.back()`。
- 细节核对：Rust `samples.get(self.size - 1)` 在 `len == size` 时取 index size-1（尾部）✓；在 `len < size` 时 None ✓；`len > size` 不可能（结构不变量）。C++ 条件相同。唯一 edge：Rust `size == 0` 时 `get(usize::MAX)` 下溢——Rust 中 `size - 1` 在 size=0 时 panic（usize 下溢），C++ 返回 INVALID。调用方（search.rs:242）传入 `n_coord_descents`，S01/S03 已确认 C++ 侧 `std::max(1U, config.nCoordinateDescents)` 兜底（LbfSearch.cpp:170-172、SeparatorWorker.cpp:164-166）防 0。【差异】（size=0 的防御；Rust 会 panic，C++ 编排层兜底为 1）
- 判定：【一致】（边界防御为 C++ 额外）
- 证据：best_samples.rs:81-87 vs BestSamples.cpp:135-142；LbfSearch.cpp:170-172 `BestSamples best(std::max<std::size_t>(1U, config.nCoordinateDescents), ...)`。

### `dtransfs_are_similar`（best_samples.rs:90-107）
- C++ 对应：`BestSamples::IsSimilar`（cpp:20-46）。逐条件核对：
  1. Rust：`x_diff < x_thresh && y_diff < y_thresh`（x/y 阈值都传 unique_thresh，调用处 :31,:42,:47,:64 四处均同参）；然后 `r1 = rotation % (2π)`、`r2 = rotation % (2π)`、`angle_diff = |r1 - r2| < 1°`（`(1.0f32).to_radians()`）。C++（cpp:29-45）：`xDiff < threshold && yDiff < threshold`；`fmod(rotation, TWO_PI)` 两侧（注释明确说明保留符号、非循环最短距离，与 Rust `%` 语义一致——Rust f32 的 `%` 即 truncated 取余，负数结果为负）；`abs(leftRot - rightRot) < ROTATION_THRESHOLD`，其中 `ROTATION_THRESHOLD = 0.01745329251994329577F`（cpp:9）= 1°（π/180 ≈ 0.017453293）。【一致】
  2. Rust 平移差在 **f32** 上计算（`dt1.translation().0 - dt2.translation().0`，DTransformation 的字段就是 f32——见 S10/JaguarCore 对应，jagua-rs DTransformation 是 f32 字段、C++ Transform2d 是 double）；C++ 在 **double** 上做差再 cast float（cpp:29-32）。double 中转可能导致极边界值差一个 ulp，实际影响可忽略（fixture retained 序列锁定）。【差异】（精度路径：f32 直接差 vs double 差后转 f32）
  3. C++ 额外防御（cpp:24-26）：`!isfinite(threshold) || threshold < 0` 返回 false（不相似→不淘汰）；Rust 无此检查（NaN 阈值会导致比较全 false，效果同为不相似；负阈值同样全 false——`x_diff < 负数` 恒 false，行为等价，仅显式化）。【C++ 额外】（显式化，行为等价）
- 判定：【一致】
- 证据：best_samples.rs:96-104 vs BestSamples.cpp:33-45；keep 行（fixture）锁定 retained 序列对相似度淘汰的判定。

### 【C++ 额外】访问器 Size/Capacity/Samples
- cpp:110-124 提供 Size()/Capacity()/Samples() 常量访问器，对应 Rust 的 pub 字段直读（best_samples.rs:12-14）。形式差异，无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 淘汰-插入顺序：Rust 先 pop 尾再 push 后 sort（稳定），C++ push 后 std::sort（不稳定）再 resize | 差异 | 同 loss（FPA 近似相等）样本间的相对顺序在退化场景可能不同；正常路径留下的集合相同 | best_samples.rs:35-38,58-59 vs cpp:98-106 |
| 2 | NaN/Invalid 防御：C++ 显式拒绝 invalid kind 与非有限 loss（未满容量也拒绝）；Rust 遇 NaN loss 走 panic 路径 | 差异 | 既定 panic→拒绝形态差异；正常输入不触发 | cpp:51-54 vs best_samples.rs:27,31,43 |
| 3 | size=0 时 upper_bound：Rust `size - 1` usize 下溢 panic，C++ 返回 INVALID；调用方 C++ 以 max(1, n) 兜底 | 差异 | 既定形态差异；两侧编排层调用处 n_coord_descents ≥ 1 | best_samples.rs:82 vs cpp:137-139; LbfSearch.cpp:170-172 |
| 4 | 相似度平移差精度：Rust f32 直接相减，C++ double 相减后 cast f32 | 差异 | 极边界值可能差 1 ulp，影响可忽略 | best_samples.rs:96-97 vs cpp:29-32 |
| 5 | IsSimilar 的阈值有限性检查（C++ 显式，Rust 隐式恒 false） | C++ 额外 | 行为等价（NaN/负阈值两侧都判不相似） | cpp:24-26 vs best_samples.rs:99-103 |
| 6 | Rust `debug_assert!`（样本唯一性不变量）→ C++ 无断言 | 差异 | 仅 debug 构建生效，无行为影响 | best_samples.rs:60-68,72 |

## 结论

BestSamples 的核心语义——"上界淘汰（未满容量放行）→ 相似度聚类（更好则整簇替换，否则拒绝）→ 插入后按 eval 排序（best 在前）"——在 C++ 中逐分支复刻，包括 FPA 容差语义的 `<`（近似相等不更优）、`% 2π` 非循环角度差、1° 旋转阈值。两侧均被 LbfSearch/SeparatorSearch 的 report/keep trace 锁定。差异集中在退化边界（排序稳定性、NaN 防御、size=0 下溢）与精度路径（double 中转），正常输入下行为等价。

### 补充审计（2026-09-29）
1. **【核对确认：107 行逐项覆盖】** best_samples.rs 全量复核：结构（3 字段）、new、report（三分支+排序+双 debug_assert）、best、upper_bound、dtransfs_are_similar——主文 5 小节全覆盖无遗漏。
2. **【浅覆盖补全：report 的相似度四连调用的重复计算】** Rust report 内 `dtransfs_are_similar` 被**调用 4 次**（any :30-31 / filter :40-42 / all 的迭代 :43-45 / retain :50-52——同一 (dt, samples) 对重复求值）；C++ 收集 similarIndices 一次（cpp:62-69）复用索引。纯性能形态差（判定不变），补记供性能对照时定位。
3. **【浅覆盖补全：upper_bound 的 get(size-1) 越界语义】** 主文条目 3 已记 size=0 下溢 panic；补记一个更常用的正确路径细节：`samples.len() < size` 时 `get(size-1)` 为 None → Invalid——**len 与 size 之间任意差值**（如 len=1,size=3）都返回 Invalid（未满即无界），C++ `m_Samples.size() < m_Capacity || empty()` 同语义（cpp:135-142）。判定维持一致；C++ 的 `|| m_Samples.empty()` 冗余防御（size>0 且非空时 size≥1 恒真于 empty false）无行为差。
4. **【核对确认：sort_by_key 稳定性的实际影响面】** 主文差异 #1 的退化场景描述（FPA 近似相等的两样本 C++ std::sort 顺序不确定）判定维持；补记：Rust `sort_by_key` 稳定排序在 BestSamples 场景的键为 SampleEval（Ord 走 FPA）——近似相等键的插入序保持，C++ 不稳定排序可能交换——但**相似度淘汰在两近似相等样本间通常先触发**（1°/5% 阈值远宽于 4-ulp），实际不可达，与主文"fixture 未触发"结论一致。
