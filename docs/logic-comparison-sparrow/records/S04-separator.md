# S04. separator.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/separator.rs`（338 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/Separator.h`（64 行）+ `Source/Optimizer/Separator.cpp`（709 行）
**判定**: 基本等价（separate 主循环的 strike/inner-iteration/0.98 阈值/GLS 权重更新/回滚链逐项复现，多 worker 选优语义一致；差异集中在 worker 状态管理形态【每轮重建 vs 常驻】、RNG 派生时序、零内层预算下不可观察的 strike 循环，以及 C++ 在 Run 尾部的一次幂等回滚。初始已分离且正内层预算时，C++ 已补齐 Rust 的一次空 worker round 与 progress 1 事件。终止后出现的 separatorState/bestSolution 分离发生在 exploration 的失败报告与扰动之间，详见 S08）

## 函数逐条比对

### `SeparatorConfig`（separator.rs:21-28）
- C++ 对应：`SeparatorConfig`（SeparatorWorker.h:17-28）。
- 判定：一致（字段级，S01 已比）。
- 证据与说明：`iter_no_imprv_limit→noImprovementLimit(200)`、`strike_limit→strikeLimit(3)`、`n_workers→nWorkers(3)`、`sample_config→nContainerSamples/nFocussedSamples/nCoordinateDescents(50/25/3)` 默认值全部一致；Rust 的 `log_level: Level` 无 C++ 对应（S01 差异 #4）。C++ 额外 `Validate()` 目前只要求 worker 数为正（SeparatorWorker.cpp:109-118）；container/focused 样本数为零时允许空采样循环，`FindBestSeparatorMoveImpl` 仍评估 reference placement，与 Rust 行为一致。零样本参考用例见 S13。

### `Separator` 结构体与 `new`（separator.rs:30-72）
- C++ 对应：`Separator::Separator`/`Separator::Create`（Separator.cpp:299-318）。
- 判定：基本等价（worker 持有形态不同）。
- 证据与说明：
  - Rust 构造即创建 `n_workers` 个**常驻** `SeparatorWorker`（各自 `prob.clone()`、`ct.clone()`、`Xoshiro256PlusPlus::seed_from_u64(rng.random())`，separator.rs:42-49）+ rayon 本地线程池（:51-62，wasm32 时 None）。
  - C++ 只存 `m_WorkerRngs`（每 worker 一个 `SparrowRng`，Separator.h:61）；worker 的 problem/tracker **每轮在 `RunWorkerRound` 内从 master 快照重建**（Separator.cpp:167-207：`SPProblem::Create(instance)` + `Restore(masterSolution)` + `CollisionTracker::Create` + `Restore(masterTrackerSnapshot)`）。
  - 判定：差异（结构重组）。Rust `worker.load(&master_sol, &self.ct)`（separator.rs:214）做增量同步（`prob.restore` + `ct.clone`），C++ 全量重建——语义等价（都把 worker 状态对齐 master 快照），C++ 路径成本更高但避免跨线程共享可变状态；2026-09-23 计划已将多 worker 轮改为该形态。
  - RNG 派生：Rust 在 `new` 时由主 rng 为每个 worker 派生种子（separator.rs:46）；C++ 普通 `Create` 先初始化 root RNG，worker RNG 在 `Run` 入口发现数量不匹配时才派生（Separator.cpp:473-493）。`ChangeStripWidth` 在 tracker 重建后立即按当前 worker RNG 数量重派生（:341-395），并非清空后延迟派生；若尚未执行过 `Run`，该数量仍为 0。压缩入口则在第一次 split 前显式按配置 worker 数预派生（Compression.cpp:227），与 Rust 构造 separator 时的消费顺序对应，S07 有固定端到端用例。worker 子流在各自轮次间连续，但普通 Separator 的延迟初始化与 Rust 构造时初始化仍是形态差异；有限 worker trace 不证明任意调用序的随机流一致。
  - C++ `Create` 从 `problem.Save()` 构建 tracker（Separator.cpp:308-318）；Rust `new` 用 `CollisionTracker::new(&prob.layout)`（:41）。等价。

### `separate`（separator.rs:75-202）vs `Separator::Run`（Separator.cpp:473-708）—— Algorithm 9
- 判定：基本等价（主循环逐项一致；3 处差异如下）。
- 证据与说明：
  1. **初始最优**：Rust `min_loss_sol = (prob.save(), ct.save())`、`min_loss = ct.get_total_loss()`（:80-81）→ C++ `bestSolution = m_Problem.Save()`、`bestTracker = m_Tracker.Save()`、`minLoss = m_Tracker.TotalLoss()`（:485-487）：一致。
  2. **外层条件与初始可行布局**：Rust `while n_strikes < strike_limit && !term.kill()`（:107）→ 当前 C++ `while (!terminator.ShouldTerminate(iteration) && strikes < strikeLimit)`（Separator.cpp:514-516），两侧均不在外层条件检查 `loss==0`。进入时已分离且 `noImprovementLimit>0`，两侧执行一次空 worker round、保留零 moves/零 evals，报告 progress 0 和 progress 1，再从 loss==0 分支退出；Rust reference `separator_empty_round_reference.rs` 与 C++ `TestInitiallySeparatedRunCompletesEmptyWorkerRound` 比较了报告序列、结果计数、布局、tracker 和 root RNG。`noImprovementLimit=0` 时，Rust 会在外层增加 strike 并回滚到初始 best，直到 strike limit/terminator；C++ 在内层跳过后由 `IsSeparated()` 退出。该零预算差异只改变不可观察的内部循环次数：没有 worker round、shuffle/sample RNG 或额外 progress，最终布局、计数和回调相同；worker RNG 初始化仍遵循各自既有生命周期。没有把它推广为源代码路径完全相同。
  3. **strike 内层**：Rust `while n_iter_no_improvement < iter_no_imprv_limit`（:116）→ 当前 C++ `while (!terminator.ShouldTerminate(iteration) && noImprovement < config.noImprovementLimit)`（:520-523）：C++ 在 worker round 之间也检查 terminator，Rust 只在外层检查；属于 S20 的终止器检查粒度差异。当前两侧在内层都不以 `IsSeparated()` 作为循环条件，`loss==0` 在轮后分支退出。
  4. **每轮统计**：Rust 记 `(loss_before, w_loss_before)` 供 debug 与 `debug_assert!(w_loss <= w_loss_before*1.001)`（:117-135）；C++ 不做该断言（仅累计 sep_stats）：差异（仅 debug 路径，Release 行为等价）。
  5. **loss==0 分支**：Rust 保存 `min_loss_sol`、`report_separation_progress(progress(n_iter+1, loss))`、`break 'outer`（:137-146）→ C++ `bestSolution/bestTracker` 保存 + `reportProgress(iteration+1, loss)` + break（:607-614）：一致。
  6. **改进分支**：Rust `report(ExplImproving)`、`loss < min_loss*0.98 → n_iter_no_improvement=0`、更新 min_loss_sol/min_loss（:147-160）→ C++ `PublishSolutionReport/OnSolution(EXPLORATION_IMPROVING)`（:624-636）、`loss < minLoss*0.98F → noImprovement=0`（:617-619）、更新 best（:621-623）：一致（0.98 阈值逐字面复现）。
  7. **无改进**：`n_iter_no_improvement += 1`（:163）→ `++noImprovement`（:639-642）：一致。
  8. **轮尾**：Rust `report_separation_progress(progress(n_iter+1, min_loss))` **然后** `ct.update_weights()`（:166-168）→ C++ **先** `m_Tracker.UpdateWeights()` **后** `reportProgress(iteration+1, minLoss)`（:644-645）。判定：差异（报告与权重更新顺序互换；progress 报告只携带 minLoss 与 iteration，不读权重，故对可观测行为无影响，但 listener 回调时序不同）。同时注意：loss==0 提前 break 的路径两侧都不执行 update_weights——一致。
  9. **strike 判定**：Rust `if initial_strike_loss * 0.98 <= min_loss { n_strikes += 1 } else { n_strikes = 0 }`（:172-178）→ C++ `if (initialStrikeLoss * 0.98F <= minLoss) ++strikes; else strikes = 0`（:653-660）：一致。
  10. **strike 后回滚**：Rust `rollback(&min_loss_sol.0, Some(&min_loss_sol.1))`（:179）→ C++ `Restore(bestSolution)` + `RestoreTrackerButKeepWeights(bestTracker, bestSolution)`（:662-673）：一致（restore_but_keep_weights 语义，S18 已比）。
  11. **Run 尾部额外回滚（C++ 形态差异）**：C++ 在 while 退出后再执行 `Restore(bestSolution)` + `RestoreTrackerButKeepWeights`（:676-687）。Rust `separate` 在每个 strike 结束时执行 `rollback(&min_loss_sol.0, Some(&min_loss_sol.1))`（separator.rs:179）；terminator 只控制外层循环，已开始的 strike 收尾时仍回滚 best。`loss==0` 路径中 master 本来就是本轮 best；`strike_limit=0` 时循环未开始且初始 master 就是 best。因此，对这些正常和终止出口，C++ 的尾部回滚是冗余/幂等步骤，不会造成 Rust best 快照与 separatorState 分离。终止后状态分离实际发生在 `exploration_phase`：它先报告 `ExplInfeas`，把候选加入池并在未达到失败上限时 rollback、disrupt；下一轮才检查 terminator 并退出（explore.rs:182-288）。这时返回的 best feasible solution 仍与分离器被扰动后的 problem 不同，详见 S08。C++ 现将这种 separatorState 从 `Explore` 单独返回并传给压缩；差异是实现形态而非 Run 语义。C++ 在 `loss==0` 时跳过 strike 判定与尾部循环内回滚；Rust 同样跳过 strike 判定，且该路径 master 已是 best：结果一致。
  12. **结果上报**：Rust `SeparationResult{success: ct.get_total_loss()==0.0, elapsed, total_evals, total_moves, iterations}`（:192-198）→ C++ `OnSeparationResult({m_Tracker.IsSeparated(), elapsed, totalEvaluationCount, totalMoveCount, iteration})`（:690-697）+ `SeparatorResult` 携带 solution/tracker 快照与 surrogateRejectCount/exactCollisionCount（:699-707，C++ 额外的两类计数，来自 S05 的 evaluator 统计）。判定：一致（C++ 额外计数为观测数据）。注意 C++ `success` 用 `m_Tracker.IsSeparated()` 且此时已回滚到 best 快照（tracker 与 best 解一致）；Rust 用回滚后的 `self.ct`（同样=best）：一致。
  13. **evals/s 日志**：Rust 结束时打统计日志（:181-191）；C++ 不打日志（无日志框架）：一致（观测层，S01）。

### `move_items_multi`（separator.rs:205-248）—— Algorithm 10
- C++ 对应：`Run` 内 worker fan-out 段（Separator.cpp:530-610）。
- 判定：基本等价（选优与统计聚合一致；执行载体与失败传播不同）。
- 证据与说明：
  1. **保存 master**：Rust `master_sol = self.prob.save()`（:206）→ C++ `masterSolution = m_Problem.Save()` + `masterTracker = m_Tracker.Save()`（:525-526）。Rust 不显式存 tracker 快照（worker 直接 `ct.clone()` 活对象）；C++ 序列化快照传给线程：差异（形态：Rust 共享引用克隆、C++ 快照恢复，含 GLS 权重——`tracker.Restore(masterTrackerSnapshot)` 恢复权重，Separator.cpp:201-207；Rust `ct.clone()` 完整克隆含权重）。
  2. **并行执行**：Rust `self.workers.par_iter_mut().map(|w| { w.load(&master_sol, &self.ct); w.move_items() }).sum()` 经 rayon 本地池（:209-225）→ C++ `std::async(std::launch::async)` 每 worker 一个任务（:537-557）；worker RNG 按值捕获（:537-538,541-552），结果中带回更新后的 RNG（:553-556），收集时写回 `m_WorkerRngs[task.index]`（:562-563）。判定：一致（既定差异：rayon 线程池 vs std::async；RNG 状态连续性两侧都保持——Rust worker 常驻自持、C++ 显式回写）。
  3. **worker 失败**：Rust worker 内 panic 会传播（move_items 的 expect）；C++ 任一 worker Result 失败 → 整个 Run 返回错误（:560-569）：一致（既定差异：panic→Result）。
  4. **选优**：Rust `min_by_key(OrderedFloat(opt.ct.get_total_weighted_loss()))` 取**加权损失最小**的 worker，`unwrap()` 保证至少一个（:236-241）→ C++ `if (!haveBestWorker || worker.weightedLoss < bestWorker->weightedLoss)`（:570-580），`weightedLoss = tracker.TotalWeightedLoss()` 在 worker 轮末保存（:285-292）。判定：一致（严格小于 ⇒ 并列时取**先完成收集的**worker；Rust `min_by_key` 并列取**首个**（迭代序=worker 0..n）；C++ 按 future 收集序=派发序=worker 0..n，两者同序。且两侧都取 `weighted loss` 而非 loss——复现）。
  5. **统计聚合**：Rust rayon `.sum()`（SepStats 的 Sum impl，worker.rs:99-111）→ C++ 逐 worker 累加 surrogateReject/exactCollision/evaluationCount/moveCount（:571-574）：一致（C++ 多两类计数）。
  6. **装载最优**：Rust `prob.restore(&best_sol); ct = best_ct.clone()`（:243-245）→ C++ `m_Problem.Restore(bestWorker->solution)` + `RestoreTracker(...)`（:591-600）：一致。Rust 丢掉其余 worker 的工作（注释 :243）；C++ 同。
  7. **w_o debug**：Rust 打各 worker 加权损失（:227-233）；C++ 无日志：一致（观测层）。

### `rollback`（separator.rs:250-264）
- C++ 对应：`Separator::Rollback`（Separator.cpp:397-405）。
- 判定：差异（签名与 tracker 恢复策略）。
- 证据与说明：Rust `rollback(sol, Some(ots))` 有两态：提供快照时 `ct.restore_but_keep_weights(ots, layout)`（保留当前 GLS 权重）；`None` 时**重建** `CollisionTracker::new(&layout)`（权重归 1）。C++ `Rollback(solution)` 只有一种：`m_Problem.Restore` + `RebuildTracker()`（全重建=权重归 1，即 Rust 的 None 分支）。Rust 侧 `Some(ots)` 分支只在 `separate` 的 strike 循环内部使用（:179——传的是 min_loss_sol 的 tracker 快照）；C++ 的对应位置用 `RestoreTrackerButKeepWeights`（:667-673，独立辅助函数，:53-64）实现了同一语义。判定：功能双侧都有，但 C++ 的公开 `Rollback` 只暴露重建分支，`restore_but_keep_weights` 是 Run 内部路径；explore/compress 调用 `sep.rollback(selected_sol, None)`（explore.rs:287、compress.rs:67 均传 None）→ C++ `Rollback` 行为与这些调用点一致。`debug_assert!(sol.strip_width() == self.prob.strip_width())`（:251）无 C++ 对应（防御断言）。

### `move_item`（separator.rs:266-297）
- C++ 对应：`Separator::MoveItem`（Separator.cpp:407-451）。
- 判定：一致（语义级；快照/回滚防御为 C++ 额外）。
- 证据与说明：Rust：读 old loss/weighted loss（debug 打印）→ `prob.remove_item(pk)` → `place_item(SPPlacement{d_transf, item_idx})` → `ct.register_item_move(&layout, pk, new_pk)`（:269-283）。C++：`RemoveItem(handle)`（:417-423）→ `PlaceItem({removed.itemIndex, transform})`（:424-430）→ `m_Tracker.UpdateMovedItem(layout, handle, placed)`（:441-449），两侧时序一致。差异：C++ 在每步前保存 `previous` 解（:417），PlaceItem/建 layout/更新 tracker 任一失败即 `m_Problem.Restore(previous)` + 必要时 `RebuildTracker()`（:427-449）——Rust 依赖内部不变式（register 失败即 panic）。判定：C++ 额外（错误恢复路径）。C++ 入口 `transform.IsFinite()` 校验（:411-416）对应 Rust 无校验。`tracker_matches_layout` debug_assert（:267/294）无对应（debug 层）。
  - 注意一个 API 形态差异：Rust `move_item` 返回 new_pk 供 explore 的 contained-relocation 循环使用（explore.rs:421-442）；C++ `MoveItem` 返回 `Result<PlacedItemHandle>`（Separator.h:45-47）同样供 Exploration.cpp:728-755 使用：一致。

### `change_strip_width`（separator.rs:299-337）
- C++ 对应：`Separator::ChangeStripWidth`（Separator.cpp:341-395）。
- 判定：基本等价（分界点/平移/重建一致；遍历快照 vs 活布局、失败回滚为差异）。
- 证据与说明：
  - 分界点：Rust `split_position.unwrap_or(strip_width/2.0)`（:301）→ C++ `split = splitPosition.value_or(oldWidth * 0.5)`（:345-347）：一致。
  - 平移集合：Rust 遍历 `layout.placed_items` 过滤 `pi.shape.centroid().0 > split_position`（:305-312）→ C++ 遍历 `previous.layoutSnapshot.placements` 过滤 `CollisionShape().Centroid().x <= split → continue`（:357-362）：同一谓词（> vs ≤ 取反）：一致。
  - 平移量：`delta = new_width - strip_width`，`new_transf = existing.compose().translate((delta, 0.0)).decompose()`（:302,314-317）→ C++ `shifted = placement.placedItem.Transform(); shifted.translation.x += delta`（:364-365）。Rust `translate` 左乘平移矩阵（jagua-rs `Transformation::translate`：`transl_m(delta) * self.matrix`），因此这是世界坐标 x 方向平移；C++ 对最终 translation.x 加 delta 在刚体变换语义上对应。矩阵 f32 运算与直接加法仍可能有数值舍入差；不应把表达式误写成右乘平移。
  - MoveItem 循环：Rust `self.move_item(pik, new_transf)`（:317）→ C++ `MoveItem(placement.handle, shifted)`（:366）：一致。
  - 失败回滚：C++ MoveItem 失败时 `Restore(previous)` + `RebuildTracker()` 再返回错误（:367-375）；ChangeStripWidth 本身失败同样回滚（:378-387）。Rust `change_strip_width` 里的 `.expect(...)`（:320-322）→ Result：既定差异。
  - 重建：Rust `ct = CollisionTracker::new(&layout)`（:325）+ **重建全部 workers**（含重新 `seed_from_u64(rng.random())`，:328-335）→ C++ `RebuildTracker()` + `ResetWorkerRngs(m_WorkerRngs.size())`（:388-394），同一宽度变更调用内立即重派生现有数量的 worker RNG。压缩器在第一次 split 前显式以配置 worker 数初始化 RNG（S07）；普通 `Separator::Create` 仍在首次 `Run` 时惰性初始化。固定压缩 reference 覆盖指定生命周期，其他直接创建后先改宽度再抽取主 RNG 的调用序仍需单独核对。
  - C++ 额外 `isfinite` 校验（:345-355）。

### `Shuffle`/`BoundedIndex`（Separator.cpp:66-145）—— C++ 额外（辅助）
- Rust 对应：`candidates.shuffle(&mut self.rng)`（worker.rs:39，rand 0.10 的 `SliceRandom::shuffle`）。
- 判定：C++ 额外（实现件）。
- 证据与说明：C++ 手写 Fisher-Yates，含两个通道——小数组用"乘积分块"压缩随机数消耗（:91-136），大数组用 `BoundedIndex` 拒绝采样（:139-144）。`SparrowUniformIndexRustReference.tsv` 对比若干指定 seed/bound 下的 `UniformIndex` 整数结果；它验证该整数映射样本，不验证 shuffle 的任意数组/长度消耗序。`SeparatorRoundRustTrace` 系列核对固定输入和 worker 数下的整轮报告、计数、布局及 tracker 状态，浮点项按容差比较；这些固定轮次结果与该洗牌路径相符，但不构成通用 shuffle 消耗序证明。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | worker 形态：常驻 SeparatorWorker+load 增量同步 vs 每轮 RunWorkerRound 全量重建 | 差异 | 源码都从 master 状态开始；固定双 worker 轨迹有 Rust 对照，离散计数精确核对、浮点状态按容差比较，不覆盖任意调度 | separator.rs:42-49,214 vs Separator.cpp:167-207,538-563 |
| 2 | worker RNG 派生时点：Rust 构造时派生；C++ 普通创建时惰性初始化，宽度变化时按当前数量立即重派生；压缩入口显式预初始化 | 形态差异，压缩调用序已由 S07 固定用例验证 | 普通 Separator 的直接调用仍需结合首次 Run/宽度变化顺序核对 | separator.rs:40-48,328-335 vs Separator.cpp:331-339,341-395,473-493; Compression.cpp:227 |
| 3 | Run 尾部对 best 的额外回滚 | C++形态差异 | Rust 每个 strike 结束时已回滚 best；C++ Run 尾部再回滚一次，固定退出路径下幂等。separatorState 与 best 的分离来自 exploration 报告后的池选择/扰动，见 S08 与 handoff fixture | separator.rs:107-179 vs Separator.cpp:514-687; explore.rs:182-288 |
| 4 | 进度报告与 update_weights 顺序互换 | 差异 | 仅 listener 回调时序（报告内容不含权重），无行为影响 | separator.rs:166-168 vs Separator.cpp:644-645 |
| 5 | 公开 Rollback 只有"重建 tracker"分支（无 ots 参数） | 差异 | Rust rollback(sol,None) 调用点（explore/compress）与 C++ Rollback 一致；keep-weights 路径在 Run 内部以 RestoreTrackerButKeepWeights 存在 | separator.rs:250-264 vs Separator.cpp:397-405,53-64 |
| 6 | 平移实现：矩阵 compose/decompose vs 直接加 delta | 差异 | 数学等价，可有 ULP 级差；正常路径一致 | separator.rs:314-317 vs Separator.cpp:364-365 |
| 7 | MoveItem/ChangeStripWidth 的失败回滚 + isfinite 校验 | C++额外 | 错误恢复路径，正常路径一致 | Separator.cpp:341-451 vs separator.rs:266-337 |
| 8 | w_loss 不增 debug_assert、evals/s 日志 | C++缺失 | debug/观测层 | separator.rs:130-135,181-191 vs Separator.cpp（无） |
| 9 | rayon 线程池 vs std::async、panic vs Result | 一致（既定差异） | 并行载体与错误通道 | separator.rs:209-225 vs Separator.cpp:536-587 |
| 10 | 选优并列 tie-break（首个 worker） | 一致 | 两侧同按派发序取首个 | separator.rs:236-241 vs Separator.cpp:575-580 |
| 11 | SeparatorResult 额外携带 surrogateRejectCount/exactCollisionCount | C++额外 | 观测计数，供 Exploration/Compression 指标聚合 | separator.rs:75-79 vs Separator.h:16-26 |
| 12 | 初始布局已分离时的执行路径 | 正内层预算已对齐；零预算仅内部循环次数不同 | 正 `iter_no_imprv_limit` 下，两侧均完成一次空 worker round，报告 progress `[0,1]` 且结果迭代/evals/moves 为 0；Rust/C++ reference 还核对空布局、tracker 和 root RNG。`limit=0` 时 Rust 累计内部 strike，C++ 提前短路；两侧无 worker round、shuffle/sample RNG 或额外进度，worker stream 初始化仍按各自既有生命周期，最终可观察结果相同 | `separator_empty_round_reference.rs`; `TestInitiallySeparatedRunCompletesEmptyWorkerRound`; separator.rs:90,107-146,172-180,205-225 vs Separator.cpp:514-516,520-523,649-652 |

## 结论
算法 9/10 的主控制流——strike 与内层无改进计数、0.98 双阈值、GLS 权重更新、strike 后保留权重的回滚、多 worker 选取及淘汰——可从 Rust 与 C++ 源码逐项对应。单 worker、双 worker、三 worker/另一 seed 的 Rust trace 对固定配置核对报告/计数/结果；另有单/双 worker压缩失败重试参考。离散字段精确比较，loss、weight、transform 等浮点量使用各 fixture 容差。初始已分离且正内层预算的空 round 已有 Rust/C++ 专用测试，核对 progress `[0,1]`、零统计、布局、tracker 与 root RNG；零内层预算的 strike 循环次数差异不改变当前可观察结果。压缩中的 worker RNG 初始化与宽度变更重派生顺序另由 S07 fixture 验证。普通 `Separator::Create` 仍惰性初始化 worker streams；`PhaseReference` 是 C++ 自回归数据，不能作为 Rust 对照。现有夹具不证明任意 worker 调度或 RNG shuffle 消耗序。Run 尾部回滚是额外幂等操作；阶段间分离状态来自 exploration 在 infeasible report 后的 pool/disrupt 路径，已由 S08 的 Rust 参考用例验证。其他结构差异包括 C++ 每轮从快照重建 worker、公开 Rollback 仅提供重建分支；报告/权重更新时序与平移表示也不同。

### 补充审计（2026-09-29）
本审计对 separator.rs 338 行做逐行覆盖复核（每个函数/常量/断言均已有条目），并对 C++ 侧现场重验，结果如下：

1. **【2026-10-09 更正：初始已分离的空 worker round】** 旧记录把 `!IsSeparated()` 写作 C++ 外/内层循环条件，且据此判为跳过空 round；该描述已被本轮修复取代。当前 C++ 循环只检查 terminator/strike/no-improvement 条件，正内层预算会运行一次零候选 worker round。Rust `separator_empty_round_reference.rs` 与 C++ `TestInitiallySeparatedRunCompletesEmptyWorkerRound` 固定验证 progress `[0,1]`、零统计、布局/tracker 与 root RNG；零预算下 Rust/C++ 仍有不可观察的内部 strike 次数差异，见主文条目 2 与汇总 #12。
2. **【遗漏补记：loss==0 早退路径的 strike 判定跳过】** 主文条目 11 已提到"C++ 在 loss==0 break 时不执行 strike 判定/回滚而直接双层 break"。现场确认 C++ 在内层 break 后于外层检查 `if (m_Tracker.IsSeparated()) break;`（:649-652）**跳过了 strike 判定与 rollback** 直接退出外层——而 Rust `break 'outer`（separator.rs:146）同样跳过 strike 判定但**随后执行** Run 尾部回滚（C++ :676-687 对应）。两侧均以 keep-weights 回滚收尾（C++ 尾部统一回滚覆盖该路径），行为一致，主文条目 11 的分析成立，此处补记内层 break→外层 IsSeparated 短路的中间步骤（Separator.cpp:649-652，主文未显式记录该行）。
3. **【浅覆盖补全：worker RNG 回写时点】** 主文差异 #2 记了派生时点，但未记"回写"。现场确认：worker 任务按值捕获 `workerRng = initialWorkerRng`（:535-536），future 收集时 `m_WorkerRngs[task.index] = task.rng`（:555-558）——即每个 worker 的随机流跨轮**连续**（等价 Rust 常驻 worker 自持 rng），而非每轮重置。该时序保证与 separator.rs:46（构造时一次派生、worker 永久持有）在流语义上对齐（差异仅派生触发时点），补记为差异 #2 的细化。
4. **【历史更正，2026-10-06 后续修复见文末】** 早先检查时 C++ 在 `ChangeStripWidth` 后清空 worker RNG 缓存，后续 `Run` 才派生，因此当压缩先抽 split 时会错位。当前 C++ 已在宽度变更内立即调用 `ResetWorkerRngs`，压缩入口也会在第一次 split 前按配置预初始化；S07 的 `seed=3` warm-start e2e fixture 核对了该路径。早先关于“只清空、延迟派生”的结论已被当前实现取代。
5. **【核对确认：SeparatorResult 内容】** 现场（Separator.h:16-26 + Separator.cpp:690-707）确认 Result 携带 solution/trackerSnapshot/elapsed/surrogateReject/exactCollision/evals/moves/iterations 八字段，主文条目 12/差异 #11 判定维持。
6. **【核对确认：行号漂移】** 当前 Separator.cpp 为 709 行、Separator.h 为 64 行；主文头部行数与关键引用按当前源码更新。
7. **【2026-10-07 修正：Run 中断后的内部状态】** Rust `separate` 的外层 terminator 检查发生在 strike 之间；每个已运行 strike 都会先 rollback 到 `min_loss_sol`。因此“最后 worker 轮状态直接泄漏出 `separate`”不是实际分离点。S08 新增的 `OptimizerTerminationHandoffRustReference.tsv` 证明状态分离发生在 `exploration_phase` 收到 `ExplInfeas` 后继续执行 pool 选择和 disruption、再于下一轮检查终止。C++ 的 `ExplorationResult.separatorState` 与 `solution` 分开返回，`Optimize` 将两者分别交给 `Compress`；固定 handoff trace 已通过。
