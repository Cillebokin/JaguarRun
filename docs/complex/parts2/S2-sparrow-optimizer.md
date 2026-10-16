# sparrow 优化器（src/optimizer）代码逻辑说明

阅读范围：`sparrow/src/optimizer/` 下的 `mod.rs`、`lbf.rs`、`explore.rs`、`separator.rs`、`worker.rs`、`compress.rs`，并追到其直接依赖的 `config.rs`、`consts.rs`、`quantify/tracker.rs`、`sample/search.rs`、`eval/sep_evaluator.rs`、`eval/collision_loss.rs`、`util/ctrlc_terminator.rs`、`cli.rs`，以及 jagua-rs 中 `SPProblem`（`jagua-rs/jagua-rs/src/probs/spp/entities/problem.rs`）和 `DTransformation` 组合语义（`jagua-rs/jagua-rs/src/geometry/transformation.rs`）。文中路径均相对 `sparrow/`，行号以当前源码为准。凡是由代码推导而未经实际运行验证的结论，文中均已注明。

## 一、顶层调用顺序与阶段切换

入口是 `optimize`（`src/optimizer/mod.rs:28-81`），对应论文 Algorithm 11。它先用 `next_rng` 闭包（mod.rs:37）从主随机数发生器中依次取出 u64 作为子种子，然后按以下顺序执行。第一步，若没有给出初始解，则调用 `LBFBuilder` 构造初始布局（mod.rs:40-44）；若给出了 `initial_solution`，则用 `SPProblem::restore` 直接还原（mod.rs:45-51）。第二步，调用 `terminator.new_timeout(expl_config.time_limit)` 设置探索阶段的时限，并以 `Separator::new` 建立探索用的分离器 `expl_separator`，再进入 `exploration_phase`（mod.rs:54-59）。探索结束后取返回向量的最后一个元素作为 `final_explore_sol`（mod.rs:59）。第三步，重置时限为压缩阶段的 `cmpr_config.time_limit`，并把 `expl_separator.prob` 移入新建的 `cmpr_separator`，调用 `compression_phase`（mod.rs:62-75）。最后报告 `ReportType::Final` 并返回（mod.rs:77-80）。

阶段切换的条件非常朴素：探索阶段没有“成功判据”的显式切换，而是由 `exploration_phase` 内部的任一终止条件结束后直接进入压缩；这些条件包括时限或 Ctrl-C（`term.kill()`）、宽度降到 `min_width` 以下、以及可选的连续失败次数上限。阶段之间唯一传递的状态是 `expl_separator.prob`（布局本身）和 `final_explore_sol`（快照）。注意两者并不一致：压缩分离器的起始状态来自探索结束时的布局，它可能是不可行的，但压缩阶段第一次尝试时会先 `change_strip_width` 并 `rollback` 到 `final_explore_sol`，因此起始状态会被覆盖，不影响正确性（见 `compress.rs:84-85`）。

另一处需要注意的是时限的实际来源。`DEFAULT_SPARROW_CONFIG`（`src/config.rs:51-98`）中写的是探索 9 分钟、压缩 60 秒，但 `cli.rs:36-57` 在命令行未指定时会用 `600 秒 × 0.8 / 0.2` 覆盖，因此默认实际为探索约 480 秒、压缩约 120 秒。`-t` 参数同理按 0.8 和 0.2 切分。`--early-termination` 会把 `max_conseq_failed_attempts` 设为 10，并把压缩的收缩策略换成 `FailureBased(0.9)`（cli.rs:58-62）。

`minimum_strip_width`（mod.rs:83-122）为两阶段共用的宽度下界。它取三者的最大值：面积下界 `总面积 / 容器高度`（乘以 `1 - 1e-6`），以及每种离散旋转下可放入高度的物品的最小包围盒宽度，最后再加上容器内缩量 `width_inset`。连续旋转物品仅计入面积下界（mod.rs:101-102 的 ponytail 注释亦承认这一点）。注意 `item.shape_cd.area` 取的是碰撞检测形状而非原始形状，多边形简化与膨胀会使其略偏大，因此该下界可能比真实下界保守。

## 二、LBF 初始解构造

`LBFBuilder::construct`（`src/optimizer/lbf.rs:55-86`）先对物品排序，排序键为 `Reverse(凸包面积 × 直径)`（lbf.rs:58-64），即大而“宽”的物品先放。排序以物品类型为单位，同一类型的多个副本随后按数量连续展开（lbf.rs:65-69），使用 `sorted_by_cached_key`，相同键保持稳定顺序。这个键把面积与长度相乘，量纲并不统一，但只起启发式排序作用。

放置由 `place_item`（lbf.rs:88-122）完成。对每个待放物品，先调用 `find_placement`（lbf.rs:124-142）。它以 `LBF_SAMPLE_CONFIG`（`src/consts.rs:50-54`：容器内 1000 个均匀采样、无聚焦采样、3 次坐标下降）调用 `search_placement`，评估器为 `LBFEvaluator`（`src/eval/lbf_evaluator.rs:33-59`）。该评估器只返回 `Invalid` 或 `Clear`，`Clear` 的损失为 `10·x + y`（`X_MULTIPLIER=10`，`Y_MULTIPLIER=1`），因此近似实现“左优先、再下优先”的左下填充。只有最优样本是 `Clear` 时才接受放置。

若找不到可行放置，则进入宽度扩张：`next_width = 当前宽度 × 1.2`（lbf.rs:102），并以 `2 × Σ(直径 × 数量)` 作为上限 `width_limit`（lbf.rs:104-110）。若 `next_width ≥ width_limit` 则返回 `ConstructionError`（lbf.rs:111-113），`optimize` 因而以 `Err` 退出（mod.rs:42 的 `?`）。注释明确声明这不是不可行证明（lbf.rs:13-14, 103）。扩张使用的是 `SPProblem::change_strip_width`，它只改变右端位置、不平移已放物品（jagua-rs problem.rs:33-37），这与探索阶段使用的 `Separator::change_strip_width` 不同，后者会平移右侧物品。构造完成后调用 `fit_strip`（lbf.rs:77-79）把宽度收缩到最右物品的包围盒，这也是探索开始时的宽度。

这里有两个隐含假设。其一，LBF 的每个物品只有一次“采样 1000 点”的机会，采样稀疏时可能找不到狭缝中的可行位置，从而被迫扩张宽度，结果质量依赖随机种子，但不影响正确性。其二，`width_limit` 与实例规模线性相关，对小实例上限很宽松，对很长的实例可能产生非常宽的初始条带，这会增加探索阶段的收缩次数。

## 三、探索阶段（exploration_phase_impl）

`exploration_phase_impl`（`src/optimizer/explore.rs:153-299`）对应论文 Algorithm 12。它维护三个状态：`current_width`、`best_width`，以及 `feasible_sols`（已发现的可行解序列）；另有 `infeas_sol_pool`（按总损失升序排列的不可行解池，元素为 `(SPSolution, f32)`，见 explore.rs:173）。

初始化时 `feasible_sols` 被直接放入 `sep.prob.save()`（explore.rs:164），而不论其是否可行。随后进入主循环 `while !term.kill()`（explore.rs:177）。每轮先调用 `sep.separate(term, sol_listener)`（explore.rs:179）在当前宽度下尝试消除碰撞，并取回最小损失状态及其 CT 快照。

若 `total_loss == 0.0`（explore.rs:182），则记录可行解：仅当 `current_width < best_width` 时才压入 `feasible_sols`（explore.rs:184-193）。随后计算 `next_width = current_width × (1 - shrink_step)`（explore.rs:195），`shrink_step` 在默认配置中为 0.001（`src/config.rs:54`），即每次成功收缩 0.1%，且**不随时间或失败次数衰减**（衰减只存在于压缩阶段）。若 `next_width < min_width` 或 `next_width ≥ current_width` 则退出循环（explore.rs:196-199）。否则调用 `sep.change_strip_width(next_width, None)`（explore.rs:206），清空不可行池（explore.rs:208），进入下一轮。

若 `total_loss > 0`（explore.rs:210），则该结果被视为失败。失败的解按损失升序插入不可行池（explore.rs:220-227），插入位置由 `binary_search_by` 决定，并用 `partial_cmp().unwrap()`，遇到 NaN 会 panic。连续失败次数即为池的长度（池只在成功时清空），若达到 `max_conseq_failed_attempts`（默认 `None`，即 `usize::MAX`，见 `config.rs:56`）则终止（explore.rs:239-245）。

池的采样方式在 explore.rs:248-284。取 `raw = N(0, σ=0.25)`，再取绝对值并截断到 0.999，然后映射为 `floor(sample × 池长)`。因此该分布是半正态分布，`P(sample < 0.25) ≈ 68%`，即约 68% 的概率选中池中前四分之一的解，这体现了“越好的解被选中概率越大”的设计。被选中的解以 `sep.rollback(selected_sol, None)`（explore.rs:287）恢复。注意这里传入的快照为 `None`，因此 `rollback` 会重建 CT（separator.rs:259-262），所有 GLS 权重被重置为 1，这与 `separate` 内部回滚保留权重的行为不同（见下一节）。

恢复之后调用 `disrupt_solution`（explore.rs:288，实现于 explore.rs:315-514）。它的思路是选两个“大”物品交换位置。“大”的判定（explore.rs:333-372）是按凸包面积降序累加，直到累计面积超过全部物品凸包面积的 `large_item_ch_area_cutoff_percentile = 0.75`（config.rs:69），取此时的物品面积作为阈值。这个定义的含义并不是“前 75% 物品”，而是“覆盖约 75% 凸包面积的最大物品集合”。若实例中所有物品尺寸相同，则阈值会落在第一个物品上，所有物品都成为“大”物品，这一点在下文的疑点中还会提到。

第一个物品 `pk1` 从大物品集合中均匀随机选取（explore.rs:384-387）。第二个物品 `pk2` 优先从面积和直径都与 `pk1` 相差超过 1% 的大物品中选取（explore.rs:391-397）；若没有则退化为从所有其他已放物品中随机选取（explore.rs:398-402）。随后两物品的位姿互换（explore.rs:407-442），并用 `convert_sample_to_closest_feasible`（`src/sample/uniform_sampler.rs:115-135`）把旋转吸附到各自允许的离散集合。

互换之后是“包含物”搬迁（explore.rs:444-513）。对于 `pk1` 新位置的碰撞物中、其 POI（`shape.poi.center`）落在 `pk1` 形状内部的物品（`practically_contained_items`，explore.rs:517-539），计算一个刚体变换 `dt1_old ∘ dt1_new⁻¹`，把这些物品的位姿从新位置参考系搬到旧位置参考系（explore.rs:450、460-464），再吸附旋转后移动。`pk2` 对称处理（explore.rs:484-512）。这一步的意图是：大物品被放到小物品的位置后，原来包围小物品的若干物品会被大物品包住，搬迁它们到大物品原来的空位，可以减少立即产生的大面积重叠。这里 `a.transform(&b)` 在 jagua-rs 中的语义是先作用 `a` 再作用 `b`（transformation.rs:70-73），以上解读依据于此。该变换只保证相对位姿近似保持，旋转吸附后并不精确。

探索循环的其余细节包括：`best_width` 与 `current_width` 实际上同步变化（成功时两者同时变为新宽度），因此 `current_width < best_width` 在成功后恒为真，除非初始解不可行；这一点与下文的疑点有关。循环结束时返回 `feasible_sols`，`mod.rs:59` 取其最后一个作为探索结果。

## 四、Separator：在给定宽度下消除重叠

`Separator::separate`（`src/optimizer/separator.rs:75-202`）对应 Algorithm 9。它的外层是 strike 循环（separator.rs:107），内层是无改进迭代循环（separator.rs:116）。

内层每一轮调用 `move_items_multi`（separator.rs:119，实现于 205-248），然后读取 `loss` 与加权损失 `w_loss`。若 `loss == 0`，则记录并 `break 'outer`，即成功（separator.rs:137-146）。若 `loss < min_loss`，则更新最小损失快照 `min_loss_sol`，并且只有当 `loss < min_loss × 0.98` 时才把 `n_iter_no_improvement` 清零（separator.rs:147-160）。这意味着 2% 以内的微小改进不会延长内层耐心，但仍会更新最优快照。否则计数加一（separator.rs:161-164）。每轮结束后调用 `self.ct.update_weights()`（separator.rs:168）做 GLS 权重更新，`update_weights` 的规则见 `src/quantify/tracker.rs:124-155`：有碰撞的对乘以 `1.2 + 0.8 × loss/max_loss`，无碰撞的对以 0.95 衰减，下限为 1.0，**没有上限**。

内层退出后，strike 判定在 separator.rs:172-178。若 `initial_strike_loss × 0.98 ≤ min_loss`，即本次 strike 中没有达到 2% 的改进，则 `n_strikes += 1`，否则 `n_strikes` 清零。随后无论如何都会执行 `self.rollback(&min_loss_sol.0, Some(&min_loss_sol.1))`（separator.rs:179），回到本次 strike 中的最优状态，并且**保留 GLS 权重**（`restore_but_keep_weights`，tracker.rs:94-107）。成功（`break 'outer`）时直接跳过这次回滚，但此时布局本身即为可行状态。

`separate` 的返回值是 `(min_loss_sol.0, min_loss_sol.1)`（separator.rs:201），即全程最优状态，无论是否可行。调用方通过 `get_total_loss` 判断成败。

这里的限制条件可以归纳如下：strike 上限在探索中为 3、压缩中为 5（`src/config.rs:58-60, 75-85`）；内层无改进上限在探索中为 200、压缩中为 100（同处）。`n_strikes` 只在某次 strike 的 2% 改进不成立时增加，因此一个 `separate` 调用至少会跑 `3 × 200 = 600` 次 `move_items_multi`（探索）。`min_loss` 是跨 strike 的全局最优，因此 strike 的判定是相对“本次 strike 起点”而言的。

回滚的条件有三种。其一是 strike 结束时回滚到本次全局最优（separator.rs:179）。其二是成功时不回滚，因为当前即为可行解。其三是外部调用 `rollback`（separator.rs:250-264），若传入快照则保留权重，否则 `CollisionTracker::new` 重建，权重全部归 1。`rollback` 的 `None` 分支被探索的失败恢复（explore.rs:287）与压缩的每次尝试（compress.rs:85）调用，因此这两处每次都会重置 GLS 权重，这是一个重要的设计事实。

`move_items_multi`（separator.rs:205-248）是多 worker 并行的核心，在下一节展开。`change_strip_width`（separator.rs:299-337）用于收缩条带：它先把中线右侧（`split_position`，默认为宽度中点，压缩时为随机位置）的物品整体平移 `Δ`（separator.rs:305-318），然后调用 `SPProblem::change_strip_width`，最后重建 CT 并用 `self.rng` 重新生成所有 worker 的种子（separator.rs:325-335）。注意平移过程中的 `move_item` 是在旧宽度的容器中进行的，因此中间状态可能出现越界，但最终的 CT 是重建的，不会残留。

## 五、Worker 并行结构与随机数

每个 `Separator` 在构造时为 `n_workers` 个 worker 各自复制一份 `prob` 与 `ct`，并用主随机数生成器的 `rng.random()` 依次为每个 worker 派生一个种子（separator.rs:40-49）。随后创建专属的 rayon `ThreadPoolBuilder`，线程数等于 `n_workers`（separator.rs:51-62），wasm32 时退化为全局线程池。探索与压缩各自拥有一个 `Separator`，因此各有一个独立的线程池，且只要 `Separator` 存活线程池就存在。在 `optimize` 中，探索的分离器在压缩阶段开始后仍然存活到函数结束（mod.rs:57, 64），因此两组线程会共存，其中探索的一组在压缩期间空闲。这是资源上的小浪费，不影响正确性。

`move_items_multi` 的并行语义如下。每轮迭代开始时，主线程保存一份 `master_sol = self.prob.save()`（separator.rs:206）。随后在线程池中对 `workers.par_iter_mut()` 执行：每个 worker 通过 `load` 恢复到 `master_sol` 并复制一份主 CT（worker.rs:26-31），然后调用 `move_items`（worker.rs:34-80）。`move_items` 先收集所有仍有损失的物品，用 worker 自己的随机数打乱顺序（worker.rs:36-43），然后逐个重新搜索位置，搜索使用 `SeparationEvaluator`（sep_evaluator.rs:49-97）与 `search::search_placement`（sample/search.rs:231-318），并把当前位姿作为参考候选加入（search.rs:255 附近），因此每次移动不会比当前更差，这与 worker.rs:122-127 中 `new_w_l <= old_w_l × 1.001` 的 debug 断言一致。

并行结束后，主线程用 `min_by_key` 在所有 worker 中选择**加权**总损失最小者（separator.rs:236-241），把它的 `prob` 与 `ct` 恢复到主状态（separator.rs:244-245）。其余 worker 的工作全部被丢弃。注意这里的选择标准是加权损失，而 `separate` 的最优记录是非加权的 `get_total_loss`（separator.rs:147-160）。两者不一致意味着某一轮被选中的 worker 可能并不是非加权损失最小者，这是一个设计选择，但文档中并未说明。

随机数的分配可以归纳为一棵由单一种子展开的树。顶层种子经 `optimize` 的 `next_rng` 依次派生出 LBF 构造用的种子、探索分离器种子、压缩分离器种子（mod.rs:37-68）。每个分离器再用自身的 `rng` 为各 worker 派生种子（separator.rs:42-49）。每个 worker 使用自己的 `rng` 做打乱与采样（worker.rs:43, 64）。探索的池采样与交换选择使用 `sep.rng`（explore.rs:253, 386, 397），压缩的随机分割位置也使用 `sep.rng`（compress.rs:89）。每次 `change_strip_width` 都会用 `self.rng` 重新派生 worker 种子（separator.rs:328-334），因此种子序列依赖于宽度变化的次数，但仍是确定的。

由此可得到两条结论。第一，各 worker 之间没有共享的可变状态，`master_sol` 与 `self.ct` 只被只读借用，worker 的结果仅依赖于自己的状态与种子，`SepStats` 的求和与 `min_by_key` 的平局规则（取首个）也与线程调度无关，因此在相同种子下迭代序列是确定的，不存在因调度引起的数据竞争或结果漂移。第二，程序的非确定性只来自墙钟时间：`Terminator` 的超时（explore.rs 中的 `term.kill()`、compress.rs:28-30 中按时间衰减的步长）以及 `Instant` 相关的日志。因此“同一种子，结果一致”只在不触发时限的意义上成立。

## 六、压缩阶段（compression_phase）

`compression_phase`（`src/optimizer/compress.rs:11-74`）对应 Algorithm 13。它以 `final_explore_sol` 为初始 `best_sol`，循环尝试进一步收缩。每轮的步长由闭包 `shrink_step_size` 给出（compress.rs:24-36）。`ShrinkDecayStrategy::TimeBased` 时，步长随已用时间线性衰减，`step = shrink_range.0 + (elapsed/time_limit) × (shrink_range.1 - shrink_range.0)`，默认 `shrink_range = (0.0005, 0.00001)`（config.rs:72），即从 0.05% 线性降到 0.001%。`FailureBased(r)` 时，`step = shrink_range.0 × r^n_failed`，只随失败次数衰减，成功不会恢复步长。

循环条件为 `while !term.kill() && let step = ... && step >= shrink_range.1`（compress.rs:39-42）。因此 TimeBased 的终止点恰好是时限（此时步长降至 0.00001），FailureBased（r = 0.9）在约 38 次失败后因步长低于 0.00001 而终止，哪怕时间仍然充裕。此外 compress.rs:45-48 还要求 `next_width ≥ min_width` 且 `next_width < width`，否则退出。

每次尝试由 `attempt_to_compress`（compress.rs:76-98）完成。它先用 `change_strip_width(init_sol.strip_width())` 把宽度恢复到 `best_sol` 的宽度（compress.rs:84），再 `rollback(init_sol, None)`（compress.rs:85），此时权重被重置。然后在 `[0, 当前宽度)` 之间随机选一个分割点（compress.rs:89），把宽度收缩 `step` 比例（compress.rs:88, 90），中线右侧物品随之左移。随后调用 `sep.separate`（compress.rs:93），仅当总损失为 0 时返回可行解。成功时 `best_sol` 被替换，失败时 `n_failed_attempts` 加一（compress.rs:50-65）。

压缩阶段的每次尝试都是从 `best_sol` 出发的一次独立试验，失败不会修改 `best_sol`，因此压缩结果单调不劣于探索结果。压缩与探索的区别可概括为：探索是“宽度逐步下降，失败后从不可行池扰动”，压缩是“每次尝试都从最优可行解出发，一次性收缩一个随机位置的较大比例，失败则放弃该次尝试”，没有不可行池，也没有交换扰动。

## 七、疑点、隐含假设与风险

第一，可行性不被保证的路径。`feasible_sols` 在 explore.rs:164 无条件放入初始解。若用 `initial_solution` 热启动且初始解不可行，并且在探索收敛到任何可行解之前时限到期，则 `feasible_sols.last()` 仍是这个不可行解，mod.rs:59 会将其交给压缩，压缩若无一次成功，最终输出 `cmpr_sol` 即为不可行解。此外，若初始解不可行，但在当前宽度下随后变为可行，则 explore.rs:184 的 `current_width < best_width` 为假，这个可行状态不会被记录，只有在后续更窄的成功时才会入列。调用方（`cli.rs` 的导出，cli.rs:125-141）并不检查最终解的可行性。

第二，GLS 权重无上界，且存在溢出风险（按代码推导，未运行验证）。`update_weights` 对有碰撞的对每轮乘以 1.2 至 2.0 之间的系数（tracker.rs:141-153），f32 在 `2^128` 附近溢出。若一对始终是最严重碰撞，约 128 轮后权重即为 `inf`。若该对之后损失降为 0，则 `0.95 × inf` 仍为 `inf`，`weighted_loss = inf × 0.0 = NaN`，NaN 会传入 `get_total_weighted_loss`。在 debug 构建中，separator.rs:130-135 与 worker.rs:122-127 的断言会因 NaN 比较失败而 panic；在 release 构建中，`min_by_key` 配合 `OrderedFloat` 会把 NaN 视为最大值，从而使该 worker 几乎不会被选中。非加权的 `get_total_loss`（tracker.rs:199-214）不依赖权重，因此池的排序不受影响。由于权重在一次 `separate` 内跨 strike 保留（tracker.rs:94-107），在压缩的 5 个 strike 与 100 次无改进迭代的组合下，此风险并非遥远。即使每轮都取下界乘子 1.2，约 490 轮后也会溢出，因此长时间的单次 `separate` 本身就足以触发。

第三，内层循环没有时限检查。`separate` 的外层循环开头检查 `term.kill()`（separator.rs:107），但内层 `while n_iter_no_improvement < iter_no_imprv_limit`（separator.rs:116）不检查。因此时限到达后，当前 strike 仍需完整跑完，探索中最多多出一个 strike（约 200 次以上的 `move_items_multi`），压缩中最多多出约 100 次以上。对于大规模实例，单次 `move_items_multi` 可达秒级，这会使时限的实际超出不可忽略。此外，Ctrl-C 对探索阶段的中断也存在滞后。

第四，Ctrl-C 语义。`CtrlCTerminator::new_timeout` 会把 `ctrlc` 标志清零（util/ctrlc_terminator.rs:45-49），而 `optimize` 在探索结束后调用 `new_timeout`（mod.rs:63）。因此，探索期间按下 Ctrl-C 只会结束探索并自动进入完整的压缩阶段，而不是整个程序退出。这可能与用户预期不符，应当在使用说明中注明，或者改为仅在压缩阶段清零标志。

第五，`min_width` 的收敛性。探索的终止依赖 `next_width < min_width`（explore.rs:196），但 `min_width` 是面积下界，一般远不可达。因此探索的实际终止几乎只能靠时限或连续失败上限。默认配置中 `max_conseq_failed_attempts` 为 `None`（config.rs:56），因此在默认设置下探索会一直跑满时限。另外，连续旋转的物品不计入宽度下界（mod.rs:101-102），这同样使下界偏弱。

第六，不可行池的内存增长。池中每个元素都是完整的 `SPSolution`（含 CDE 快照），池大小只在成功时清零（explore.rs:208）。若探索长期失败，池会持续增长，且池的插入使用二分查找但没有容量上限。在默认时限下这通常不是问题，但若将 `max_conseq_failed_attempts` 设为 `None` 且失败频繁，内存占用会线性增长。

第七，交换扰动的退化。大物品的阈值（explore.rs:341-372）在实例中所有物品尺寸相同时会退化为“全部物品都是大物品”，此时第二个物品的过滤条件（面积与直径都相差超过 1%，explore.rs:391-397）总是为空，退化为随机选择任一其它物品。这意味着在同质实例中，交换的语义从“大小物品互换”变为“随机两物品互换”，搬迁逻辑对于大小相近的物品的意义也随之减弱。另外 `practically_contained_items` 的判定只看 POI 是否在形状内（explore.rs:533-537），对于形状高度凹陷的物品，POI 可能落在空隙中，导致判定不准确。

第八，并行与性能。并行结构本身没有数据竞争：worker 不共享可变状态，主线程的 `prob.restore` 与 `ct = best_ct.clone()` 只在 `pool.install` 返回后执行（separator.rs:236-245）。但每一轮都要对全部 worker 执行 `load`，即一次 `SPProblem::restore`（jagua-rs problem.rs:94-114）与一次 CT 克隆，以及对主状态的一次 `save`，这些都是 O(n) 的操作，在迭代次数很多时开销显著。`ReportType::ExplImproving` 的参数 `&self.prob.save()`（separator.rs:154）在报告前就会被计算，即使监听器不使用它，每次改进都要付出一次快照成本。`attempt_to_compress` 中的 `change_strip_width(init 宽度)`（compress.rs:84）在宽度相同时仍会对右半物品执行 `move_item`（delta 为 0 的空移动），随后又被 `rollback` 覆盖，属于可以省去的开销。

第九，`separate` 内 `loss == 0.0` 的判断依赖精确相等。由于每一对碰撞的损失为正（tracker.rs:81, 86 的 `assert!(loss > 0.0)`），总损失为零当且仅当无碰撞，因此该判断是安全的。但 `minimum_strip_width` 中的 `REL_TOL` 与 `shape_cd` 的膨胀仍可能让“看起来刚好能放下”的情形在数值上被判为碰撞，这属于几何精度问题而非逻辑错误。

第十，一致性的若干断言。`register_item_move` 通过复用 `idx` 槽位维持物品与 CT 下标的稳定对应（tracker.rs:113-121），`restore_but_keep_weights` 在同一 `separate` 调用内使用同源快照，因此权重与物品的对应关系不会错位。但若将来有人在不同 CT 谱系之间使用 `restore_but_keep_weights`，就会出现权重错配，此处的正确性依赖于调用约定而非类型系统保证。

## 八、收敛与终止情形小结

探索的正常终止有三种：时限或 Ctrl-C；宽度降到 `min_width` 以下；连续失败达到配置的上限（默认关闭）。压缩的正常终止有两种：时限（TimeBased 与时限重合）；步长降到 `shrink_range.1` 以下（FailureBased 的失败次数达到约 38 次）。`separate` 本身不会无限循环：strike 计数有上限，而每次“2% 改进”的重置只能发生有限次，因为损失有正的下界（由 `overlap_area_proxy` 中的 `epsilon²` 项保证，quantify/mod.rs:16-19 与 overlap_proxy.rs:15-18），且每次重置要求损失至少乘以 0.98。这两点合起来保证 `separate` 有限终止，但在大规模实例上，有限可能意味着很长。

需要注意的死循环形态有两种。第一种是探索中的“成功—失败交替”：在窄宽度附近，`separate` 可能反复失败，但由于池会不断被扰动，实际上会一直运行到时限，属于正常的工作状态而非死循环。第二种是在 `disrupt_solution` 的 `placed_items.len() < 2` 分支（explore.rs:321-324）中，扰动直接返回而不改变布局，若此时仍处于不可行状态，则下一轮 `separate` 将从相同状态重新开始，形成没有新信息的重复。该分支只在仅有一个物品时触发，因此在多物品实例中并不会出现。

## 九、建议的验证点

建议团队在阅读本文后，结合以下几点做进一步确认。其一，用热启动的不可行解跑一次短时限探索，检查输出是否可行，以验证第七节第一点的推断。其二，在 debug 构建中对一个高冲突实例长时间运行 `separate`，观察 GLS 权重是否出现 `inf` 与 NaN，以验证第二点的推断。其三，统计 `separate` 中内层循环在时限后的实际超出时长，以验证第三点。其四，在同质物品实例上对比“大物品交换”的日志，确认第七点中的退化是否发生。以上结论均为代码推导，尚未通过实际运行验证。
