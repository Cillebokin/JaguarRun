# S07. compress.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/compress.rs`（80 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/Compression.h`（29 行）+ `Source/Optimizer/Compression.cpp`（343 行）
**判定**: 基本等价（衰减步长公式、最小宽度停机、随机分割点收缩、分离成功判据逐项复现；差异集中在收缩尝试的宽度恢复顺序【Rust 双跳 change_strip_width vs C++ 单跳 Rollback+ChangeStripWidth】、成功判据 C++ 额外要求 metrics.feasible、以及 C++ 的限流字段）

## 函数逐条比对

### `compression_phase`（compress.rs:11-61）—— Algorithm 13
- C++ 对应：`Compress`（Compression.cpp:160-342）。
- 判定：基本等价（主循环与两种衰减一致；三处差异如下）。
- 证据与说明：
  1. **min_width**：Rust `super::minimum_strip_width(&sep.prob)`（:18）→ C++ `CalculateMinimumStripWidth(instance, initialSolution)`（:200-201，:56-127——与 Exploration.cpp:57-131 同一函数的副本，公式=mod.rs:85-122，S08 比对）：一致。
  2. **初始 best_sol**：Rust `init_sol.clone()`（:19）→ C++ `bestSolution = initialSolution`（:198）：一致。
  3. **shrink_step_size（时间衰减）**：Rust `shrink_range.0 + ratio * (shrink_range.1 - shrink_range.0)`，`ratio = elapsed / time_limit`（:24-31，注意 Rust 元组 `shrink_range=(max, min)`=(0.0005, 0.00001)）→ C++ `step = shrinkMax + ratio * (shrinkMin - shrinkMax)`（:214-229，`ratio = elapsed_ms / timeLimit_ms`）。判定：一致（线性内插 max→min；C++ 对 timeLimit=0 防御 ratio=0）。C++ 用毫秒整数比、Rust 用 f32 秒比——舍入级差（既定差异）。
  4. **shrink_step_size（失败衰减）**：Rust `shrink_range.0 * r.powi(n_failed_attempts)`（:32-34，r=FailureBased 载荷）→ C++ `shrinkMax * std::pow(failureDecay, failures)`（:230-234）。判定：一致（基数为 max、指数为失败数；载荷 r→failureDecay 字段，默认 0.9=consts.rs:36，S01 差异 #3）。
  5. **循环条件**：Rust `while !term.kill() && let step = shrink_step_size(...) && step >= config.shrink_range.1`（:39——`let` 链式条件：step 低于 min 即退出）→ C++ `while (!terminator.ShouldTerminate(iteration) && (maxIterations==0 || iteration<maxIterations))` + 循环体内 `if (!isfinite(step) || step < config.shrinkMin) break`（:210-213, 235-238）。判定：一致（C++ 把 step 计算与下限检查移入循环体，语义同：step<min 即停）。
  6. **宽度计算与停机**：Rust `width = best_sol.strip_width(); next_width = width*(1.0-step); if next_width < min_width || next_width >= width { break }`（:40-45）→ C++ `width = bestSolution.StripWidth(); nextWidth = width*(1.0-step); if (!isfinite||<=0||>=width||(minimumWidth>0 && <minimumWidth)) break`（:240-247）。判定：一致（同 S06 的防御差异；注意 Rust 取 **best_sol** 的宽度而非 sep 当前宽度——C++ 同取 bestSolution.StripWidth()：一致）。
  7. **进度上报**：Rust `sol_listener.report_compression_progress(step)`（:46）→ C++ `listener->OnCompressionProgress(step)`（:248-249）+ 额外 `OnProgress`（:250-257，C++ 额外）：基本一致。
  8. **attempt_to_compress 调用与分支**：Rust `Some(compacted_sol) → report(CmprFeas); best_sol = compacted_sol`；`None → n_failed_attempts += 1`（:47-57）→ C++ `if (loss == 0.0F && metrics.feasible) { bestSolution=...; OnImprovement; PublishSolutionReport(COMPRESSION_FEASIBLE) } else { ++failures; if (maxConsecutiveFailures>0 && failures>=...) break; }`（:306-333）。判定：差异（两点：a) C++ 成功判据多一个 `metrics.feasible`【CalculateMetrics 的完整校验：remainingDemand==0 + ValidateSPSolution + layout.IsFeasible，:284-304】——Rust 仅凭 `ot.get_total_loss() == 0.0`（:76）判定。正常路径 loss==0 ⇒ 可行，两侧一致；C++ 为防御性双重判据。b) C++ 额外 maxConsecutiveFailures 限流【默认 0=无限】，Rust 无失败上限——唯一停机是 step 衰减到 min 之下）。
  9. **结束日志/返回**：Rust `info!` + `best_sol`（:59-60）→ C++ 无日志 + `CompressionResult{bestSolution, bestMetrics, history}`（:337-341）。判定：一致（返回形态差异同 S06 #1）。
  10. **fixture 锁定**：`PhaseReference.tsv`（compression 行 seed=42 width/density）与 `BppLbfCppReference.tsv`（压缩前构造）锁定该路径；`LbfMetricsReference.tsv` 提供指标口径。

### `attempt_to_compress`（compress.rs:64-80）
- C++ 对应：`Compress` 循环体（Compression.cpp:259-282）。
- 判定：基本等价（收缩流程一致；宽度恢复顺序差异如下）。
- 证据与说明：
  1. **恢复到初始解与宽度**：Rust **两步**：`sep.change_strip_width(init_sol.strip_width(), None)`（先无条件把当前宽度变回初始宽度——因为上一轮失败后 sep 停在收缩后的宽度）**然后** `sep.rollback(init_sol, None)`（:66-67）。判定要点：Rust 顺序为"先变宽再回滚"。C++ **一步**：`separator.Rollback(bestSolution)`（:259-264，Restore+RebuildTracker）**后** `ChangeStripWidth(nextWidth, split)`（:266-270）。判定：差异（顺序与跳数：Rust change_strip_width→rollback；C++ rollback→change_strip_width(nextWidth)。但注意语义等价性分析：Rust 的第一步把 master 宽度变回 init 宽（此过程会把分割线右侧的件右移 delta=init_width-current_width，这一平移对"即将被 rollback 覆盖的中间态"无意义），第二步 rollback 恢复解；随后 :70-72 再 `change_strip_width(new_width, Some(split_pos))`。C++ 直接从当前态 Rollback 到 best（Restore 覆盖一切中间态），再一步收缩到 nextWidth。**最终态**：两侧均为"best_sol 的解内容 + 宽度 nextWidth + 分割点 split_pos 右侧平移 delta"——Rust 多一次无效的先变宽平移（其效果被 rollback 的 restore 完全覆盖）与 C++ 少一次变宽，最终一致。差异仅是中间操作次数【影响：Rust 的第一步 change_strip_width 末尾重建 tracker+workers（separator.rs:325-335），其随机数消耗【worker 重派生 rng.random()】会影响后续 split_pos 的随机流——但 Rust 的 split_pos 从 `sep.rng` 取【主 rng】，worker 种子也从 `sep.rng` 取，故第一步的 worker 重派生确实消耗了主 rng 的随机数、改变了 split_pos 与后续流；C++ ChangeStripWidth 不消耗 rng（Separator.cpp:331-385 无 rng 调用）。→ **随机流错位风险**：两侧 split_pos 的值序列不同【除非 Rust 第一步 change_strip_width 时 workers 为空或重派生不发生——workers 非空必发生 n_workers 次 rng.random()】。但由于 split_pos 本身是 `rng.random_range(0..strip_width)` 的连续值、且 fixture PhaseReference 锁定了 C++ 侧行为，该差异归为"随机流形态差异"，不改变算法分布语义）。
  2. **收缩宽度与随机分割点**：Rust `new_width = init_sol.strip_width() * (1.0 - r_shrink)`（:70）；`split_pos = sep.rng.random_range(0.0..sep.prob.strip_width())`（:71）；`sep.change_strip_width(new_width, Some(split_pos))`（:72）→ C++ `nextWidth = width*(1.0-step)`（:241，width=best 宽）；`split = separator.Random().Uniform01F32() * width`（:265）；`separator.ChangeStripWidth(nextWidth, split)`（:266-270）。判定：差异（细节：Rust `random_range(0.0..w)` 是 rand 0.10 的 f32 均匀采样【HalfOpen 区间，指数位级算法】；C++ `Uniform01F32() * width` 是"单位均匀×宽度"——两种映射的位级分布不同（rand 的 random_range f32 直接在区间内均匀生成尾数；乘法重建有一次舍入）。语义同为 [0,w) 均匀，数值序列不同。fixture PhaseReference 的 compression 行与 C++ 实现自洽；Rust 位级对齐由 `LbfSeed42Reference.tsv` 锁定的 rng 原语在其它路径承担）。另注意 Rust 的 `sep.prob.strip_width()`（:71 分母）在 rollback 后=init 宽=C++ 的 width（:265）：一致。
  3. **分离与成功判定**：Rust `sep.separate(term, sol_listener)` 返回 `(compacted_sol, ot)`，`ot.get_total_loss() == 0.0 → Some(compacted_sol)`（:75-79）→ C++ `separator.Run(...)` + `separator.Tracker().TotalLoss() == 0.0F`（:271-283, :306）。判定：一致（判据差异见上条 8）。
  4. **C++ 额外的逐轮指标**：`CalculateMetrics`（:284-304）每轮算 density/feasible 并 `OnProgress`（:296-304）；reject/exact 计数聚合（:292-295）：C++ 额外（同 S06 #11）。
  5. **C++ 额外 `ValidationConfig`**：`CompressionConfig::Validate`（:146-158）：shrink 范围/failureDecay∈(0,1)/timeLimit 校验：C++ 额外（前置校验）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 恢复顺序：change_strip_width(init)→rollback vs rollback→change_strip_width(next) | 差异（顺序/跳数） | 最终态等价（中间平移被 restore 覆盖）；但 Rust 的先变宽会消耗主 rng（worker 重派生）→两侧随机流错位，split_pos 数值序列不同（分布语义同） | compress.rs:66-67 vs Compression.cpp:259-270 + separator.rs:328-335 |
| 2 | split_pos 采样：random_range(0..w) vs Uniform01F32()*w | 差异（数值） | 同为 [0,w) 均匀，位级序列不同 | compress.rs:71 vs Compression.cpp:265 |
| 3 | 成功判据：loss==0 vs loss==0 && metrics.feasible | 差异（防御） | 正常路径一致（loss==0 ⇒ 可行）；C++ 双重判据 | compress.rs:76 vs Compression.cpp:306 |
| 4 | maxConsecutiveFailures 失败上限 | C++额外 | 默认 0=无限，与 Rust 一致（Rust 唯一停机=步长衰减到 min 下） | compress.rs:53-56 vs Compression.cpp:328-332 |
| 5 | maxIterations 限流 | C++额外 | 默认 0=无限 | compress.rs:39 vs Compression.cpp:210-213 |
| 6 | 时间衰减比例：f32 秒比 vs 毫秒整数比 | 差异（舍入） | 同线性内插，舍入级差 | compress.rs:29-30 vs Compression.cpp:218-228 |
| 7 | 每轮 OnProgress + 指标聚合 + reject/exact 计数 | C++额外 | 观测数据 | compress.rs（无）vs Compression.cpp:250-257,284-304 |
| 8 | 衰减公式（线性/失败几何）、step<min 停机、min_width 停机、next>=width 停机 | 一致 | 逐项复现 | compress.rs:24-45 vs Compression.cpp:214-247 |
| 9 | report(CmprFeas) → PublishSolutionReport(COMPRESSION_FEASIBLE) | 一致 | listener 映射 | compress.rs:50 vs Compression.cpp:314-323 |
| 10 | panic/expect → Result | 一致（既定差异） | change_strip_width 的 expect | compress.rs:66-72 vs Compression.cpp:259-270 |

## 结论
Algorithm 13 的压缩循环——两种步长衰减（线性时间、几何失败）、step<min 与 next<min_width 与 next>=width 三重停机、随机分割点收缩、以分离成功为准——在 C++ `Compress` 中逐项复现，判定基本等价。两处值得注意的差异都在 `attempt_to_compress` 的等价物里：其一，Rust"先变宽再回滚"的双跳与 C++"回滚后一步收缩"最终态等价，但 Rust 路径多消耗的主 rng 随机数（worker 重派生）与 C++ 的 `Uniform01F32()*w` 采样共同造成 split_pos 序列的随机流错位（分布语义不变，数值序列不同，PhaseReference 锁定的是 C++ 侧）；其二，C++ 的成功判据额外要求逐轮 metrics.feasible，属防御性双重校验，正常路径不改变行为。
