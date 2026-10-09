# S05. worker.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/worker.rs`（118 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/SeparatorWorker.h`（45 行）+ `Source/Optimizer/SeparatorWorker.cpp`（309 行）+ `Source/Optimizer/SeparatorWorkerTrace.h`（37 行，计 45+309；trace 头并入本组）
**判定**: 基本等价（候选收集与洗牌、逐项"仍碰撞才动"、参考位形纳入候选、五相搜索序、move 时序逐项复现；差异集中在 batch 语义的拆分【Rust move_items 整批 vs C++ FindBestSeparatorMove 单项 + Separator.cpp 编排】、Rust 对 best_sample 的 expect 不变量在 C++ 变为可空回退、以及 C++ 额外的统计计数与防重扫过滤）

说明：Rust 的 `SeparatorWorker` 是有状态对象（`move_items` 一口气处理所有碰撞项）；C++ 没有常驻 worker 类，对应物是**自由函数** `FindBestSeparatorMove`（单次"为一个碰撞项找最佳移动"）+ Separator.cpp 的 `RunWorkerRound`（把逐项循环编排起来，S04 已比）。本记录以 worker.rs 的函数为纲比对这两个 C++ 件。

## 函数逐条比对

### `SeparatorWorker` 结构体（worker.rs:18-23）
- C++ 对应：`FindBestSeparatorMove` 的参数组（SeparatorWorker.h:39-44：`layout, tracker, movingHandle, config, rng`）。
- 判定：差异（结构重组：常驻有状态 worker → 无状态函数 + 调用方编排）。
- 证据与说明：Rust 字段 `prob/ct/rng/sample_config`（worker.rs:19-22）；C++ 把 problem 操作（remove/place）上移到 `RunWorkerRound`（Separator.cpp:253-283），`FindBestSeparatorMove` 只读 layout+tracker 找变换。`sample_config → SeparatorConfig{50/25/3}`（SeparatorWorker.h:17-28，S01/S04 已比默认值一致）。

### `load`（worker.rs:26-31）
- C++ 对应：`RunWorkerRound` 前半（Separator.cpp:174-207：SPProblem::Create + Restore(masterSolution) + CollisionTracker::Create + Restore(masterTrackerSnapshot)）。
- 判定：一致（语义级）。
- 证据与说明：Rust `load(sol, ct)` = `prob.restore(sol)` + `ct = ct.clone()`（worker.rs:28-30，`debug_assert!(sol.strip_width() == self.prob.strip_width())` 为防御断言）；C++ 从序列化快照全量重建，语义同为"worker 状态对齐 master"（S04 差异 #1 详述）。

### `move_items`（worker.rs:34-66）—— Algorithm 5
- C++ 对应：`RunWorkerRound` 的候选循环（Separator.cpp:209-283）+ `FindBestSeparatorMove`（SeparatorWorker.cpp:128-284）。
- 判定：基本等价（候选收集/洗牌/重扫过滤一致；batch 语义拆分与统计口径差异如下）。
- 证据与说明：
  1. **候选收集**：Rust `layout.placed_items.keys().filter(|pk| ct.get_loss(*pk) > 0.0).collect_vec()`（worker.rs:36-38）→ C++ `for (index < tracker.ItemCount()) if (tracker.ItemLoss(index) > 0.0F) candidates.push_back(index)`（Separator.cpp:209-217）。判定：一致（loss>0 谓词逐字面；Rust 按 slotmap key、C++ 按 tracker 索引——`IndexForHandle` 建立同构映射，SeparatorWorker.cpp:141-146）。
  2. **洗牌**：Rust `.tap_mut(|v| v.shuffle(&mut self.rng))`（worker.rs:39，rand 0.10 `SliceRandom::shuffle`）→ C++ `Shuffle(candidates, rng)`（Separator.cpp:218，手写 Fisher-Yates 两通道，:66-145，见 S04 条目）。判定：实现目标对应；固定 separator trace 覆盖特定候选集和 seed 下的后续结果，但不能据此证明所有候选数的 shuffle 随机消耗逐项相同。`SparrowUniformIndexRustReference.tsv` 只验证指定整数 bound/seed 的抽样结果。
  3. **"仍碰撞才动"过滤**：Rust 循环体 `if self.ct.get_loss(pk) > 0.0`（worker.rs:47）→ C++ `if (index >= tracker.ItemCount() || tracker.ItemLoss(index) <= 0.0F) continue`（Separator.cpp:226-229）。判定：一致（本轮早先的移动可能已消除后续项的碰撞，两侧都重查）。
  4. **搜索单项**：Rust `SeparationEvaluator::new(&layout, item, pk, &ct)` + `search_placement(&layout, item, Some(pk), evaluator, sample_config, &mut rng)`（worker.rs:51-55）→ C++ `FindBestSeparatorMove(layout, tracker, previousHandle, config, rng)`（Separator.cpp:237-238），其内部 `SeparationEvaluator(layout, movingItem.GetItem(), movingHandle, tracker)`（SeparatorWorker.cpp:154-155）+ 五相搜索（见下条）。判定：一致（`Some(pk)` = 有参考位形 → C++ 显式 ReferencePlacement 相位；evaluator 本体 S11 已比）。
  5. **best_sample 不变量**：Rust `best_sample.expect("search_placement should always return a sample")`（worker.rs:57）——因为 `ref_pk=Some` 时参考位形必被 report 进 best_samples（search.rs:256-262），永不空。C++ `move.Value().transform` 可空：`if (!move.Value().transform.has_value()) continue`（Separator.cpp:247-250）——**跳过该项且不计 move**。判定：差异（表示层：Rust panic 不变量 → C++ 防御性跳过；正常路径【参考位形必然存在】两侧都不触发。C++ 的可空源于 FindBestSeparatorMoveImpl 内部多处防御失败路径【handle 不在 tracker、快照不一致等】返回无 transform 的 Result 值，SeparatorWorker.cpp:141-151）。
  6. **移动执行**：Rust `self.move_item(pk, new_dt)`（worker.rs:60）→ C++ `problem.RemoveItem(previousHandle)` + `problem.PlaceItem({removed.itemIndex, *transform})` + `tracker.UpdateMovedItem(layout, previousHandle, placed)`（Separator.cpp:253-283）。判定：一致（时序=worker.rs:77-82 的 remove/place/register_item_move；C++ 额外在每次移动后 `layout.Restore(updatedSolution.layoutSnapshot)` 刷新 layout 视图（:268-275），因为 layout 是值快照而非活引用——形态差异）。
  7. **统计口径**：Rust `total_moves += 1; total_evals += n_evals`（worker.rs:61-62，n_evals 是**本轮搜索**的评估数）；C++ `evaluationCount += move.Value().evaluationCount`（SeparatorWorker.cpp:246，= evaluator.EvaluationCount()）+ `++moveCount`（Separator.cpp:251），另加 `surrogateRejectCount/exactCollisionCount`（:244-245，C++ 额外观测计数，来自 S11 evaluator）。判定：一致（口径：跳过的项不计 move，其搜索 evals 仍计入）。
  8. **trace 钩子**：Rust 无（worker.rs 无观察者）；C++ `FindBestSeparatorMoveWithTrace` + `TracingEvaluator`（SeparatorWorker.cpp:55-104,298-308）对应 search.rs 的 oracle-trace `CallbackSearchObserver`（search.rs:133-175）——供 fixture 生成。判定：C++ 额外（测试设施，五相枚举 SeparatorSearchTracePhase 对齐 search.rs 的 SearchPhase，SeparatorWorkerTrace.h）。

### `move_items` 的搜索内部：Rust `search_placement(ref_pk=Some)`（search.rs:253-309）vs `FindBestSeparatorMoveImpl`（SeparatorWorker.cpp:128-284）
- 判定：基本等价（五相序与 BestSamples 参数一致；3 处差异）。
- 证据与说明：
  1. **参考位形先行**：Rust 将当前位形 `(dt, eval)` 评估并 report 进 best_samples（search.rs:256-262，UpperBound 传入）→ C++ `tracingEvaluator.Evaluate(movingItem.Transform(), std::nullopt)` + `report(...)`（SeparatorWorker.cpp:167-186）。判定：差异（细节：Rust 传 `Some(best_samples.upper_bound())` 给该次评估、C++ 传 `std::nullopt`——对 SeparationEvaluator 的 upper_bound 只影响早停阈值，首个评估时 best_samples 为空、upper_bound=Invalid（对应 f32::INFINITY 早停阈值，sep_evaluator.rs:57-61），nullopt 同为 INFINITY：语义一致）。
  2. **focused sampler**：Rust `UniformBBoxSampler::new(pi_bbox, item, container_bbox)`（search.rs:264-266，围绕当前位形 bbox）+ `n_focussed_samples` 次（:274-282）→ C++ `UniformRectSampler::Create(movingItem.CollisionShape().BoundingBox(), movingItem.GetItem(), bounds)` + `config.nFocussedSamples` 次（SeparatorWorker.cpp:188-208）。判定：一致（25 默认，S01）。
  3. **container sampler**：Rust 同一构造换 container bbox（search.rs:270-271,286-293）→ C++ `UniformRectSampler::Create(bounds, ...)` + `nContainerSamples` 次（SeparatorWorker.cpp:210-222）。判定：一致（50 默认）。Rust 对 sampler=None（放不下）跳过该段（search.rs:286 `if let`）；C++ `if (containerSampler.HasValue())` 同（:210）。
  4. **BestSamples**：`BestSamples(max(1, nCoordinateDescents), minDim*0.05)`（SeparatorWorker.cpp:164-166）vs Rust `BestSamples::new(n_coord_descents, item_min_dim * UNIQUE_SAMPLE_THRESHOLD)`（search.rs:242-245，0.05）：一致（max(1,·) 为 C++ 防御；Rust 容量为 0 时 `upper_bound` 在 Debug 下溢 panic，Release wrapping 到 `usize::MAX` 后返回 Invalid；正常配置不会传 0）。
  5. **pre-refine**：每个 best start 跑 `RefineCoordinateDescent(start, ..., preConfig, rng)` 并 report 结果（SeparatorWorker.cpp:224-236）vs Rust `refine_coord_desc(start, ...)` + report（search.rs:298-303）：一致。CD 配置 `MakeRefinementConfig(item, false/true)`（SeparatorWorker.cpp:21-53）= LbfSearch.cpp:31-63 同一函数副本：0.25/0.02/5°/1° 与 0.01/0.001/0.5°/0.05°，与 Rust consts.rs:17-26 一致（S14 详比 CD 内部）。
  6. **final-refine 与结果选择（差异）**：Rust `best_samples.best().map(|s| refine_coord_desc(s, ..., finalConfig, rng))`——返回的是 **refine 后的样本对**，且 refine 的结果**不再 report 回 best_samples**（search.rs:305-309，返回 `(cd.pos, cd.eval)`）。C++ 同样不把 final-refine 结果 report 回 best：`finalSample = refined.Value()`（:239-251），结果选择 `if (finalSample.has_value()) 用之; else if (refinedBest = best.Best()) 用之`（:253-267）。判定：一致（含回退——Rust 侧 `map` 在 best 为 None 时返回 None（最终 expect panic），C++ 的 refinedBest 回退对应 Rust best 必非 None 的不变式，refine 失败回退两路径都存在【Rust refine 永成功，C++ RefineCoordinateDescent 可失败】——同 S03 差异 #4 的防御形态）。
  7. **item min dim 校验**：C++ 额外 `isfinite/<=0` 报错（SeparatorWorker.cpp:157-163）。
  8. **fixture 覆盖**：`SeparatorSearchRustTrace.tsv`（`# generator=Sparrow Rust search_placement with SeparationEvaluator`，seed=42，16 container/8 focused/2 CD）按事件类型、阶段、accepted、retained 数量与 evaluation count 等离散字段精确比较；样本 kind 精确比较，transform 容差 2e-5、非 Invalid loss 容差 2e-3（TestSeparator.cpp:782-799,901-958）。它支持此输入与配置下的五相搜索轨迹，不证明任意输入下完整搜索等价。`SamplingCoordinateDescentRustReference.tsv` 的 8 个 sample、54 个 coordinate 和 1 个 result 按 transform 1e-6/loss 1e-5 容差比较，部分计数字段精确；不验证 shuffle 序。

### `move_item`（worker.rs:68-91）
- C++ 对应：并入 `RunWorkerRound` 的 remove/place/UpdateMovedItem（Separator.cpp:253-283，S04 条目 6 已比）。
- 判定：一致（语义级）。
- 证据与说明：时序与返回值语义双侧一致。Rust 的三处 `debug_assert!`（tracker_matches_layout、old_l>0、`new_w_l <= old_w_l * 1.001`，worker.rs:69-88）在 C++ 无对应（debug 层；其中"weighted loss 不增"不变式在 C++ 无断言——2026-09-23 计划以测试间接覆盖）。`debug!` 打印（:86）无对应（观测层）。

### `SepStats` + `Sum`/`AddAssign`（worker.rs:94-118）
- C++ 对应：`WorkerRoundResult` 的四个计数字段（Separator.cpp:147-156）+ Run 内逐 worker 累加（Separator.cpp:566-569，S04 条目 5）。
- 判定：一致（聚合语义等价；C++ 多 surrogateRejectCount/exactCollisionCount 两字段）。
- 证据与说明：Rust rayon `.sum()` 要求 `Sum` impl（worker.rs:99-111）；C++ 标量累加。`total_moves/total_evals → moveCount/evaluationCount`（Separator.cpp:152-153）。

### `SeparatorConfig::Validate`（SeparatorWorker.cpp:109-124）—— C++ 额外
- Rust 对应：无。
- 判定：C++ 额外（前置校验：nWorkers>0、采样预算非零；`noImprovementLimit=0` 允许，与 Rust 的零迭代配置一致）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | batch 拆分：move_items 整批有状态 vs FindBestSeparatorMove 单项函数 + Separator.cpp 编排 | 差异（结构重组） | 逐项行为等价；problem 写操作上移，FindBestSeparatorMove 可独立测试（fixture 生成器即用它） | worker.rs:34-66 vs SeparatorWorker.cpp:128-284 + Separator.cpp:209-283 |
| 2 | best_sample 必存在的不变量：Rust expect(panic) vs C++ 可空+continue | 差异 | 正常路径（有参考位形）两侧都不触发；C++ 把 panic 变为跳过该项 | worker.rs:57 vs Separator.cpp:247-250, SeparatorWorker.cpp:141-151 |
| 3 | 首评估 upper_bound：Some(upper_bound())【首时=Invalid→∞】 vs std::nullopt【→∞】 | 一致（语义） | 均为无穷早停阈值，首个评估不会早停 | search.rs:258 vs SeparatorWorker.cpp:168-169 |
| 4 | 每次移动后 layout 视图全量 Restore | C++额外 | C++ layout 是值快照需刷新；Rust 活引用零成本。形态差异，无行为差 | Separator.cpp:268-275 vs worker.rs:60 |
| 5 | surrogateRejectCount/exactCollisionCount 统计 | C++额外 | 观测计数，进 SeparatorResult/最终 LbfMetrics | worker.rs:94-97 vs SeparatorWorker.h:30-37 + Separator.cpp:244-245 |
| 6 | FindBestSeparatorMoveWithTrace + SeparatorWorkerTrace 五相 | C++额外 | 测试设施（fixture 生成），对齐 search.rs oracle-trace | SeparatorWorker.cpp:298-308 vs worker.rs（无） |
| 7 | debug_assert 三连（tracker 一致、old_l>0、wl 不增）与 debug 日志 | C++缺失 | debug 层不变式无 C++ 断言 | worker.rs:69-88 vs SeparatorWorker.cpp（无） |
| 8 | 候选收集/洗牌/重扫/搜索五相/CD 参数/统计口径 | 源码对应，固定路径有 Rust trace | trace 核对指定输入下的阶段/离散事件和容差浮点值；没有任意输入或通用 shuffle 消耗序证明 | worker.rs:36-63 vs Separator.cpp:209-251 + SeparatorWorker.cpp:128-284 |
| 9 | panic→Result（handle 不在 tracker、快照不一致等防御） | 一致（既定差异） | 错误通道统一 | worker.rs:57,73-74 vs SeparatorWorker.cpp:141-151 |

## 结论
Algorithm 5 的候选筛选、仍碰撞项重查、单项五相搜索和移动更新在 C++ 侧由自由函数与 Separator 编排承接，源码流程可对应。固定 Rust search trace 验证一个 seed/config 下的五相事件、报告和保留列表；阶段/离散字段精确比较，变换和 loss 使用容差。固定 round traces 再覆盖指定 worker 数、输入与 seed 的若干整轮结果；它们没有证明所有候选集的 shuffle 消耗序或任意输入下完全等价。C++ 将 Rust `expect` 不变量改成可空结果并跳过失败 move，还以值快照 Restore 支撑无状态编排；surrogate/exact 计数和 trace 钩子是 C++ 额外观测设施。

### 补充审计（2026-09-29）
1. **【核对确认：118 行逐项覆盖】** worker.rs 全量复核：结构体 4 字段、load、move_items、move_item、SepStats+Sum+AddAssign——主文 5 小节全覆盖无遗漏。
2. **【浅覆盖补全：SepStats 的 Sum 聚合与 rayon 的结合】** 主文 SepStats 条目记了 rayon `.sum()`；补记：`Sum for SepStats`（worker.rs:99-111）是 rayon 并行归约的 folds+reduce 载体——**f32 无关**（纯 usize 累加，无浮点结合序问题），C++ 标量累加（Separator.cpp:566-569）等价无舍入差。判定维持。
3. **【浅覆盖补全：候选过滤的 loss>0 vs ItemLoss 语义】** Rust `ct.get_loss(*pk) > 0.0`（worker.rs:37/47）与 C++ `tracker.ItemLoss(index) > 0.0F`（Separator.cpp:209-217/226-229）——注意两侧 ItemLoss/get_loss 均为 container+pair 行和（S18 已比），且 C++ SafeLoss 过滤非有限值（S18 差异 #7）在此处叠加：若行和含 NaN（正常不可能），C++ 判不碰撞跳过、Rust 比较 false 同跳过——等价。判定维持【一致】。
4. **【核对确认：move_items 中 evaluator 的生命周期】** Rust 每个候选项新建 SeparationEvaluator（worker.rs:51，含 CDE hazard 预注册），C++ FindBestSeparatorMove 内部同样逐项新建（SeparatorWorker.cpp:154-155）——两侧"每项一 evaluator"粒度一致（主文条目 4 隐含但未明说，此处显式化）。
