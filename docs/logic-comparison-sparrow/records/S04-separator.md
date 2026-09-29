# S04. separator.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/separator.rs`（338 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/Separator.h`（63 行）+ `Source/Optimizer/Separator.cpp`（704 行）
**判定**: 基本等价（separate 主循环的 strike/inner-iteration/0.98 阈值/GLS 权重更新/回滚链逐项复现，多 worker 选优语义一致；差异集中在 worker 状态管理形态【每轮重建 vs 常驻】、RNG 派生时序、以及 C++ 在 Run 尾部的一次额外回滚）

## 函数逐条比对

### `SeparatorConfig`（separator.rs:21-28）
- C++ 对应：`SeparatorConfig`（SeparatorWorker.h:17-28）。
- 判定：一致（字段级，S01 已比）。
- 证据与说明：`iter_no_imprv_limit→noImprovementLimit(200)`、`strike_limit→strikeLimit(3)`、`n_workers→nWorkers(3)`、`sample_config→nContainerSamples/nFocussedSamples/nCoordinateDescents(50/25/3)` 默认值全部一致；Rust 的 `log_level: Level` 无 C++ 对应（S01 差异 #4）。C++ 额外 `Validate()`（SeparatorWorker.cpp:109-124：noImprovementLimit/nWorkers 必须为正、采样预算非零）。

### `Separator` 结构体与 `new`（separator.rs:30-72）
- C++ 对应：`Separator::Separator`/`Separator::Create`（Separator.cpp:299-318）。
- 判定：基本等价（worker 持有形态不同）。
- 证据与说明：
  - Rust 构造即创建 `n_workers` 个**常驻** `SeparatorWorker`（各自 `prob.clone()`、`ct.clone()`、`Xoshiro256PlusPlus::seed_from_u64(rng.random())`，separator.rs:42-49）+ rayon 本地线程池（:51-62，wasm32 时 None）。
  - C++ 只存 `m_WorkerRngs`（每 worker 一个 `SparrowRng`，Separator.h:61）；worker 的 problem/tracker **每轮在 `RunWorkerRound` 内从 master 快照重建**（Separator.cpp:167-207：`SPProblem::Create(instance)` + `Restore(masterSolution)` + `CollisionTracker::Create` + `Restore(masterTrackerSnapshot)`）。
  - 判定：差异（结构重组）。Rust `worker.load(&master_sol, &self.ct)`（separator.rs:214）做增量同步（`prob.restore` + `ct.clone`），C++ 全量重建——语义等价（都把 worker 状态对齐 master 快照），C++ 路径成本更高但避免跨线程共享可变状态；2026-09-23 计划已将多 worker 轮改为该形态。
  - RNG 派生：Rust 在 `new` 时从主 rng `rng.random()` 逐个派生 worker 种子（separator.rs:46）；C++ 在 `Run` 入口 `m_WorkerRngs[i] = SparrowRng(m_Rng.NextU64())`（Separator.cpp:475-483，且仅当 `size != config.nWorkers` 时重建）。判定：差异（时点不同：Rust 构造时派生一次、worker 永久持有；C++ 首次 Run 派生、`ChangeStripWidth` 后 `m_WorkerRngs.clear()`（:383）强制下次 Run 重派生——对应 Rust `change_strip_width` 重建 worker 时重新 `seed_from_u64(self.rng.random())`（separator.rs:328-335））。种子流不同 → worker 随机序不同，但 fixture `SeparatorMultiWorkerRoundRustTrace.tsv`/`SeparatorThreeWorkerSeed43RoundRustTrace.tsv`（2/3 worker、strike=1/iter=1 的轮级 trace）已锁定 C++ 重建序与 Rust 一致。
  - C++ `Create` 从 `problem.Save()` 构建 tracker（Separator.cpp:308-318）；Rust `new` 用 `CollisionTracker::new(&prob.layout)`（:41）。等价。

### `separate`（separator.rs:75-202）vs `Separator::Run`（Separator.cpp:463-703）—— Algorithm 9
- 判定：基本等价（主循环逐项一致；3 处差异如下）。
- 证据与说明：
  1. **初始最优**：Rust `min_loss_sol = (prob.save(), ct.save())`、`min_loss = ct.get_total_loss()`（:80-81）→ C++ `bestSolution = m_Problem.Save()`、`bestTracker = m_Tracker.Save()`、`minLoss = m_Tracker.TotalLoss()`（:485-487）：一致。
  2. **外层条件**：Rust `while n_strikes < strike_limit && !term.kill()`（:107）→ C++ `while (!terminator.ShouldTerminate(iteration) && !m_Tracker.IsSeparated() && strikes < strikeLimit)`（:509-511）。C++ 额外 `IsSeparated()`（=TotalLoss==0）作为外层条件：Rust 靠内层 `loss == 0.0 → break 'outer`（:137-146）退出，C++ 把同一条件同时放内层（:517）与外层（:510）判定：行为等价（0-loss 时内层 break，或 strike 循环首轮后外层检出）。
  3. **strike 内层**：Rust `while n_iter_no_improvement < iter_no_imprv_limit`（:116）→ C++ 加 terminator/IsSeparated 守卫（:516-518）：一致（C++ 的 term 检查更细粒度，Rust `move_items_multi` 内不查 term——既定终止器语义差异，S20）。
  4. **每轮统计**：Rust 记 `(loss_before, w_loss_before)` 供 debug 与 `debug_assert!(w_loss <= w_loss_before*1.001)`（:117-135）；C++ 不做该断言（仅累计 sep_stats）：差异（仅 debug 路径，Release 行为等价）。
  5. **loss==0 分支**：Rust 保存 `min_loss_sol`、`report_separation_progress(progress(n_iter+1, loss))`、`break 'outer`（:137-146）→ C++ `bestSolution/bestTracker` 保存 + `reportProgress(iteration+1, loss)` + break（:603-609）：一致。
  6. **改进分支**：Rust `report(ExplImproving)`、`loss < min_loss*0.98 → n_iter_no_improvement=0`、更新 min_loss_sol/min_loss（:147-160）→ C++ `PublishSolutionReport/OnSolution(EXPLORATION_IMPROVING)`（:619-632）、`loss < minLoss*0.98F → noImprovement=0`（:612-614）、更新 best（:616-618）：一致（0.98 阈值逐字面复现）。
  7. **无改进**：`n_iter_no_improvement += 1`（:163）→ `++noImprovement`（:636-637）：一致。
  8. **轮尾**：Rust `report_separation_progress(progress(n_iter+1, min_loss))` **然后** `ct.update_weights()`（:166-168）→ C++ **先** `m_Tracker.UpdateWeights()` **后** `reportProgress(iteration+1, minLoss)`（:639-640）。判定：差异（报告与权重更新顺序互换；progress 报告只携带 minLoss 与 iteration，不读权重，故对可观测行为无影响，但 listener 回调时序不同）。同时注意：loss==0 提前 break 的路径两侧都不执行 update_weights——一致。
  9. **strike 判定**：Rust `if initial_strike_loss * 0.98 <= min_loss { n_strikes += 1 } else { n_strikes = 0 }`（:172-178）→ C++ `if (initialStrikeLoss * 0.98F <= minLoss) ++strikes; else strikes = 0`（:648-655）：一致。
  10. **strike 后回滚**：Rust `rollback(&min_loss_sol.0, Some(&min_loss_sol.1))`（:179）→ C++ `Restore(bestSolution)` + `RestoreTrackerButKeepWeights(bestTracker, bestSolution)`（:657-668）：一致（restore_but_keep_weights 语义，S18 已比）。
  11. **Run 尾部额外回滚（C++ 差异）**：C++ 在 while 循环正常退出后**再执行一次** `Restore(bestSolution)` + `RestoreTrackerButKeepWeights`（:671-682）。Rust 的 `separate` 在 `loss==0 → break 'outer` 路径后直接返回 `min_loss_sol`（此时 master 已是该解，因为改进时同步保存但 master 当前态可能是更差的轮末态）——细看 Rust：改进时更新 `min_loss_sol` 快照但**不回滚 master**，strike 到限时循环退出后返回快照；C++ 循环退出后统一回滚到 best。对 Rust 的退出路径：a) loss==0 break：master 即最优（当轮刚保存且 master 状态就是它）——注意 Rust 在该分支保存快照后立即 break，master=快照；C++ 此时也已回滚过（:605 保存后 break 出两层 → 尾部再回滚一次，幂等）。b) strike 耗尽：末次 strike 循环已 rollback 过（:179），master=best；C++ strike 循环尾部回滚后外层再检条件退出 → 尾部再回滚一次，幂等。c) term.kill() 退出：**Rust master 停在最后一轮 worker 结果（可能差于 best），返回的 min_loss_sol 是快照**——调用方拿到快照解；C++ 尾部回滚使 master==best 并返回 best。判定：差异（终止器打断时 C++ 保证返回态=最优快照；Rust 返回值同为最优快照（元组第一项），但 `sep.prob`/`sep.ct` 的**内状态**在 Rust 停留在轮末态。影响：Rust 调用方（explore/compress）只用返回值 `local_best.0`，不用 sep 内态 → 下轮 `separate` 前必有 rollback/change_strip_width 重置，故下游行为等价；C++ 把不变式显式化）。此外 C++ 在 loss==0 break 时（:603-609）**不执行** strike 判定/回滚而直接双层 break，Rust `break 'outer` 同样跳过：一致。
  12. **结果上报**：Rust `SeparationResult{success: ct.get_total_loss()==0.0, elapsed, total_evals, total_moves, iterations}`（:192-198）→ C++ `OnSeparationResult({m_Tracker.IsSeparated(), elapsed, totalEvaluationCount, totalMoveCount, iteration})`（:685-693）+ `SeparatorResult` 携带 solution/tracker 快照与 surrogateRejectCount/exactCollisionCount（:694-702，C++ 额外的两类计数，来自 S05 的 evaluator 统计）。判定：一致（C++ 额外计数为观测数据）。注意 C++ `success` 用 `m_Tracker.IsSeparated()` 且此时已回滚到 best 快照（tracker 与 best 解一致）；Rust 用回滚后的 `self.ct`（同样=best）：一致。
  13. **evals/s 日志**：Rust 结束时打统计日志（:181-191）；C++ 不打日志（无日志框架）：一致（观测层，S01）。

### `move_items_multi`（separator.rs:205-248）—— Algorithm 10
- C++ 对应：`Run` 内 worker fan-out 段（Separator.cpp:520-600）。
- 判定：基本等价（选优与统计聚合一致；执行载体与失败传播不同）。
- 证据与说明：
  1. **保存 master**：Rust `master_sol = self.prob.save()`（:206）→ C++ `masterSolution = m_Problem.Save()` + `masterTracker = m_Tracker.Save()`（:520-521）。Rust 不显式存 tracker 快照（worker 直接 `ct.clone()` 活对象）；C++ 序列化快照传给线程：差异（形态：Rust 共享引用克隆、C++ 快照恢复，含 GLS 权重——`tracker.Restore(masterTrackerSnapshot)` 恢复权重，Separator.cpp:201-207；Rust `ct.clone()` 完整克隆含权重）。
  2. **并行执行**：Rust `self.workers.par_iter_mut().map(|w| { w.load(&master_sol, &self.ct); w.move_items() }).sum()` 经 rayon 本地池（:209-225）→ C++ `std::async(std::launch::async)` 每 worker 一个任务（:528-553），`workerRng` 按值捕获、任务结束后回写 `m_WorkerRngs[task.index] = task.rng`（:555-558）。判定：一致（既定差异：rayon 线程池 vs std::async；RNG 状态连续性两侧都保持——Rust worker 常驻自持、C++ 显式回写）。
  3. **worker 失败**：Rust worker 内 panic 会传播（move_items 的 expect）；C++ 任一 worker Result 失败 → 整个 Run 返回错误（:560-564）：一致（既定差异：panic→Result）。
  4. **选优**：Rust `min_by_key(OrderedFloat(opt.ct.get_total_weighted_loss()))` 取**加权损失最小**的 worker，`unwrap()` 保证至少一个（:236-241）→ C++ `if (!haveBestWorker || worker.weightedLoss < bestWorker->weightedLoss)`（:570-576），`weightedLoss = tracker.TotalWeightedLoss()` 在 worker 轮末保存（:285-292）。判定：一致（严格小于 ⇒ 并列时取**先完成收集的**worker；Rust `min_by_key` 并列取**首个**（迭代序=worker 0..n）；C++ 按 future 收集序=派发序=worker 0..n，两者同序。且两侧都取 `weighted loss` 而非 loss——复现）。
  5. **统计聚合**：Rust rayon `.sum()`（SepStats 的 Sum impl，worker.rs:99-111）→ C++ 逐 worker 累加 surrogateReject/exactCollision/evaluationCount/moveCount（:566-569）：一致（C++ 多两类计数）。
  6. **装载最优**：Rust `prob.restore(&best_sol); ct = best_ct.clone()`（:243-245）→ C++ `m_Problem.Restore(bestWorker->solution)` + `RestoreTracker(...)`（:586-600）：一致。Rust 丢掉其余 worker 的工作（注释 :243）；C++ 同。
  7. **w_o debug**：Rust 打各 worker 加权损失（:227-233）；C++ 无日志：一致（观测层）。

### `rollback`（separator.rs:250-264）
- C++ 对应：`Separator::Rollback`（Separator.cpp:387-395）。
- 判定：差异（签名与 tracker 恢复策略）。
- 证据与说明：Rust `rollback(sol, Some(ots))` 有两态：提供快照时 `ct.restore_but_keep_weights(ots, layout)`（保留当前 GLS 权重）；`None` 时**重建** `CollisionTracker::new(&layout)`（权重归 1）。C++ `Rollback(solution)` 只有一种：`m_Problem.Restore` + `RebuildTracker()`（全重建=权重归 1，即 Rust 的 None 分支）。Rust 侧 `Some(ots)` 分支只在 `separate` 的 strike 循环内部使用（:179——传的是 min_loss_sol 的 tracker 快照）；C++ 的对应位置用 `RestoreTrackerButKeepWeights`（:662-668，独立辅助函数，:53-64）实现了同一语义。判定：功能双侧都有，但 C++ 的公开 `Rollback` 只暴露重建分支，`restore_but_keep_weights` 是 Run 内部路径；explore/compress 调用 `sep.rollback(selected_sol, None)`（explore.rs:84、compress.rs:67 均传 None）→ C++ `Rollback` 行为与这些调用点一致。`debug_assert!(sol.strip_width() == self.prob.strip_width())`（:251）无 C++ 对应（防御断言）。

### `move_item`（separator.rs:266-297）
- C++ 对应：`Separator::MoveItem`（Separator.cpp:397-441）。
- 判定：一致（语义级；快照/回滚防御为 C++ 额外）。
- 证据与说明：Rust：读 old loss/weighted loss（debug 打印）→ `prob.remove_item(pk)` → `place_item(SPPlacement{d_transf, item_idx})` → `ct.register_item_move(&layout, pk, new_pk)`（:269-283）。C++：`RemoveItem(handle)` → `PlaceItem({removed.itemIndex, transform})` → `m_Tracker.UpdateMovedItem(layout, handle, placed)`（:407-439），两侧时序一致。差异：C++ 在每步前保存 `previous` 解、PlaceItem/建 layout/更新 tracker 任一失败即 `m_Problem.Restore(previous)` + `RebuildTracker()`（:414-439）——Rust 依赖内部不变式（register 失败即 panic）。判定：C++ 额外（错误恢复路径）。C++ 入口 `transform.IsFinite()` 校验（:401-406）对应 Rust 无校验。`tracker_matches_layout` debug_assert（:267/294）无对应（debug 层）。
  - 注意一个 API 形态差异：Rust `move_item` 返回 new_pk 供 explore 的 contained-relocation 循环使用（explore.rs:177-178）；C++ `MoveItem` 返回 `Result<PlacedItemHandle>`（Separator.h:45-47）同样供 Exploration.cpp:525-536 使用：一致。

### `change_strip_width`（separator.rs:299-337）
- C++ 对应：`Separator::ChangeStripWidth`（Separator.cpp:331-385）。
- 判定：基本等价（分界点/平移/重建一致；遍历快照 vs 活布局、失败回滚为差异）。
- 证据与说明：
  - 分界点：Rust `split_position.unwrap_or(strip_width/2.0)`（:301）→ C++ `split = splitPosition.value_or(oldWidth * 0.5)`（:337）：一致。
  - 平移集合：Rust 遍历 `layout.placed_items` 过滤 `pi.shape.centroid().0 > split_position`（:305-312）→ C++ 遍历 `previous.layoutSnapshot.placements` 过滤 `CollisionShape().Centroid().x <= split → continue`（:347-351）：同一谓词（> vs ≤ 取反）：一致。
  - 平移量：`delta = new_width - strip_width`，`new_transf = existing.compose().translate((delta, 0.0)).decompose()`（:302,314-317）→ C++ `shifted = placement.placedItem.Transform(); shifted.translation.x += delta`（:354-355）。判定：差异（数学等价：Rust 走矩阵 compose→translate→decompose，C++ 直接在 (rot,trans) 表示上平移。对纯旋转+平移的 compose 表示，`T·translate(delta)` 等价于 translation 加 delta（旋转在 translate 之前应用于自身矩阵——jagua-rs `translate` 是右乘平移矩阵 `dot_prod(&transl_m(delta), &self.matrix)`，作用于已 compose 的刚体变换等价于给最终平移加 delta），f32 矩阵路径与直接加法可能有 ULP 差，但 explore/compress 传的都是对称收缩场景，行为一致）。
  - MoveItem 循环：Rust `self.move_item(pik, new_transf)`（:317）→ C++ `MoveItem(placement.handle, shifted)`（:356）：一致。
  - 失败回滚：C++ MoveItem 失败时 `Restore(previous)` + `RebuildTracker()` 再返回错误（:357-365）；ChangeStripWidth 本身失败同样回滚（:368-377）。Rust `change_strip_width` 里的 `.expect(...)`（:320-322）→ Result：既定差异。
  - 重建：Rust `ct = CollisionTracker::new(&layout)`（:325）+ **重建全部 workers**（含重新 `seed_from_u64(rng.random())`，:328-335）→ C++ `RebuildTracker()`（:378-382）+ `m_WorkerRngs.clear()`（:383，下次 Run 重派生）：一致（对应 worker 重建，见 `new` 条目差异讨论）。
  - C++ 额外 `isfinite` 校验（:339-345）。

### `Shuffle`/`BoundedIndex`（Separator.cpp:66-145）—— C++ 额外（辅助）
- Rust 对应：`candidates.shuffle(&mut self.rng)`（worker.rs:39，rand 0.10 的 `SliceRandom::shuffle`）。
- 判定：C++ 额外（实现件）。
- 证据与说明：C++ 手写 Fisher-Yates，含两个通道——小数组用"乘积分块"压缩随机数消耗（:91-136），大数组用 `BoundedIndex` 拒绝采样（:139-144，对应 rand 的 `random_range` 无偏整数采样，fixture `SparrowUniformIndexRustReference.tsv` 锁定该映射）。这些是 `RunWorkerRound` 内候选洗牌（Separator.cpp:218 `Shuffle(candidates, rng)`）的支撑件，行为对标 rand 0.10 的 shuffle 消耗序（`SeparatorRoundRustTrace` 系列锁定）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | worker 形态：常驻 SeparatorWorker+load 增量同步 vs 每轮 RunWorkerRound 全量重建 | 差异 | 语义等价（对齐 master 快照），C++ 成本更高；多 worker 轮行为被 SeparatorMultiWorkerRoundRustTrace.tsv 锁定 | separator.rs:42-49,214 vs Separator.cpp:167-207,528-553 |
| 2 | worker RNG 派生时点：构造时一次 vs 首次 Run/宽度变更后重派生 | 差异 | 随机流不同时点分叉；ChangeStripWidth 后两侧都强制重派生（Rust 重建 worker / C++ clear m_WorkerRngs） | separator.rs:46,328-335 vs Separator.cpp:475-483,383 |
| 3 | Run 尾部对 best 的额外回滚（终止器打断路径） | C++额外 | Rust 返回快照解但 sep 内态停在轮末；C++ 显式回滚 master=best。下游（只用返回值）行为等价 | separator.rs:107,201 vs Separator.cpp:671-682 |
| 4 | 进度报告与 update_weights 顺序互换 | 差异 | 仅 listener 回调时序（报告内容不含权重），无行为影响 | separator.rs:166-168 vs Separator.cpp:639-640 |
| 5 | 公开 Rollback 只有"重建 tracker"分支（无 ots 参数） | 差异 | Rust rollback(sol,None) 调用点（explore/compress）与 C++ Rollback 一致；keep-weights 路径在 Run 内部以 RestoreTrackerButKeepWeights 存在 | separator.rs:250-264 vs Separator.cpp:387-395,53-64 |
| 6 | 平移实现：矩阵 compose/decompose vs 直接加 delta | 差异 | 数学等价，可有 ULP 级差；正常路径一致 | separator.rs:314-317 vs Separator.cpp:354-355 |
| 7 | MoveItem/ChangeStripWidth 的失败回滚 + isfinite 校验 | C++额外 | 错误恢复路径，正常路径一致 | separator.cpp:331-441 vs separator.rs:266-337 |
| 8 | w_loss 不增 debug_assert、evals/s 日志 | C++缺失 | debug/观测层 | separator.rs:130-135,181-191 vs Separator.cpp（无） |
| 9 | rayon 线程池 vs std::async、panic vs Result | 一致（既定差异） | 并行载体与错误通道 | separator.rs:209-225 vs Separator.cpp:526-577 |
| 10 | 选优并列 tie-break（首个 worker） | 一致 | 两侧同按派发序取首个 | separator.rs:236-241 vs Separator.cpp:570-576 |
| 11 | SeparatorResult 额外携带 surrogateRejectCount/exactCollisionCount | C++额外 | 观测计数，供 Exploration/Compression 指标聚合 | separator.rs:75-79 vs Separator.h:16-26 |

## 结论
Algorithm 9/10 的控制流——strike×内层无改进计数、0.98 双阈值（改进重置、strike 判定）、GLS 权重每轮更新、strike 后 keep-weights 回滚、多 worker 取加权损失最小、丢弃其余工作——在 C++ `Separator::Run` 中逐项复现，并被 Separator(Round/MultiWorker/ThreeWorker) 系列与 PhaseReference fixture 锁定，判定基本等价。核心差异是结构性的：C++ 放弃常驻 worker（每轮从快照重建，RNG 按轮回写）、在 Run 尾部显式回滚最优解、公开 Rollback 只暴露重建分支；另有报告/权重更新顺序互换与平移的表示级差异，均不改变算法行为。
