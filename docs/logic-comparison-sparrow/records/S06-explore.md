# S06. explore.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/explore.rs`（245 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/Exploration.h`（29 行）+ `Source/Optimizer/Exploration.cpp`（857 行）
**判定**: 基本等价（收缩-失败池-正态池抽选-扰动的主循环逐项复现，含 rand_distr ZIGNOR f64 正态的位级复刻；差异集中在返回形态【可行解集合 vs 单一最优解+指标】、失败池上限/迭代上限 C++ 额外限流、扰动交换的长变换链精度路径【FollowedBy f64 vs Rust f32 矩阵】、以及 contained-relocation 的集合收集口径）

> **2026-09-29 修复**：扰动搬迁的长变换链已改用 `InverseRustF32().FollowedByRustF32()`（4 处调用点，Exploration.cpp），与 jagua-rs f32 矩阵路径位级对应——本记录中"FollowedBy f64 精度路径"条目已消除。附带修复（同轮验证发现）：① Separator 平移（separator.rs:315 的 compose().translate()）经分析为位级等价无需改（double 精确加法后单次 f32 舍入 == f32 加法）；② SparrowCore `CollisionTracker`/`SeparationEvaluator` 的私有引擎 hazard 实体未带布局句柄/变换，2974bb6 引入的重复实体检查会误拒同物品多放置——已补齐实体唯一性（对齐 Rust pk 语义）；③ 三个 C++ 自引用 fixture（SppLbf/PhaseReference）为旧基线生成，按当前基线重生成（密度 ~1e-8 量级漂移，宽度/次数不变）；④ TestBppJsonRoundTrip 在 2974bb6 桩化 ImportBPSolution 后未同步——已改为断言 UnsupportedOperation + reparse 闭环；⑤ TestSampling 的空离散旋转断言层级错误（Rust 在 Importer 层拒绝，Item::new 接受）——已改为走 JSON 入口验证。Debug/Release 全量测试与六个 parity oracle 复验通过。
## 函数逐条比对

### `exploration_phase`（explore.rs:21-92）—— Algorithm 12
- C++ 对应：`Explore`（Exploration.cpp:637-856）。
- 判定：基本等价（主循环一致；返回值与限流差异如下）。
- 证据与说明：
  1. **min_width**：Rust `super::minimum_strip_width(&sep.prob)`（explore.rs:22）→ C++ `CalculateMinimumStripWidth(instance, initialSolution)`（Exploration.cpp:677-679，:57-131；公式=mod.rs:85-122，S08 组内比对）：一致。
  2. **初始可行集合**：Rust `feasible_sols = vec![sep.prob.save()]` 并 `report(ExplFeas)`（:26-28）→ C++ `bestSolution = initialSolution` + `PublishSolutionReport(EXPLORATION_FEASIBLE 或 INFEASIBLE 按 initialMetrics.feasible)`（:675-698）。判定：差异（细节：Rust 无条件报 ExplFeas（初始解按构造为可行）；C++ 按指标真值分支报 FEASIBLE/INFEASIBLE——更严谨，对合法输入同为 FEASIBLE）。C++ 额外 `OnProgress` 进度事件（:699-706）。
  3. **循环条件**：Rust `while !term.kill()`（:33）→ C++ `while (!terminator.ShouldTerminate(iteration) && (config.maxIterations == 0U || iteration < config.maxIterations))`（:708-710）。判定：差异（C++ 额外 maxIterations 限流，默认 0=无限，S01 已记；行为等价）。
  4. **separate**：Rust `sep.separate(term, sol_listener)` 返回 `(local_best.0, local_best.1)`（:35）→ C++ `separator.Run(config.separator, terminator, listener, &history, EXPLORATION)` 返回 SeparatorResult（:712-722）+ `separator.Tracker().TotalLoss()`（:723）。判定：一致（S04 已比 Run/separate 等价性；total_loss 从返回的 ct 快照【Rust :36 `local_best.1.get_total_loss()`】vs C++ 分隔器当前 tracker——C++ Run 尾部已回滚到 best，二者一致）。
  5. **成功分支（total_loss==0）**：Rust `if current_width < best_width` 则更新 best、push feasible_sols、`report(ExplFeas)`（:38-45）→ C++ 同条件更新 bestSolution/bestMetrics + `OnImprovement` + PublishSolutionReport(FEASIBLE)（:758-783）。判定：一致。
  6. **收缩**：Rust `next_width = current_width * (1.0 - shrink_step)`；`if next_width < min_width || next_width >= current_width { break }`（:47-51）→ C++ `nextWidth = currentWidth * (1.0 - config.shrinkStep)`；`if (!isfinite || <=0 || >= currentWidth || (minimumWidth > 0 && nextWidth < minimumWidth)) break`（:784-789）。判定：一致（C++ 补非有限/非正防御；Rust 的 `next_width < min_width` 在 C++ 为 `minimumWidth > 0 &&` 守卫——C++ 的 CalculateMinimumStripWidth 失败时返 0 表示"无下限"，Rust 无失败态）。
  7. **宽度变更与清池**：Rust `sep.change_strip_width(next_width, None)`（split 取默认中心，S04）、`infeas_sol_pool.clear()`（:53-55）→ C++ `separator.ChangeStripWidth(nextWidth)`（默认 split=nullopt）+ `infeasiblePool.clear()`（:791-798）。判定：一致。C++ 额外 `failures = 0U`（:799）——Rust 无显式失败计数（其上限用 `infeas_sol_pool.len()`，见下条），C++ 用独立 `failures` 计数器。
  8. **失败分支**：Rust `report(ExplInfeas)`（:58）→ C++ `PublishSolutionReport(EXPLORATION_INFEASIBLE)`（:738-748，在 loss≠0 时）。判定：一致（C++ 把上报放在 loss≠0 判定内、与 `totalLoss==0` 主分支并列，Rust 在 else 分支：同构）。
  9. **池插入（有序）**：Rust `binary_search_by(partial_cmp)` + `insert(idx, (sol, total_loss))`（:61-63，升序）→ C++ `std::lower_bound` 按 loss + `insert`（:803-812）。判定：一致（同为升序插入；Rust `partial_cmp().unwrap()` 对 NaN panic——既定差异）。
  10. **池上限（C++ 额外）**：C++ `if (config.infeasiblePoolSize > 0U && size > infeasiblePoolSize) pop_back()`（:813-817）。Rust **无上限**（池可无限增长；上限仅由 `max_conseq_failed_attempts` 的长度判定间接控制——该判定用 `infeas_sol_pool.len()`，explore.rs:65）。判定：C++ 额外（默认 `infeasiblePoolSize=0`=不启用，S01；启用时丢弃最差尾部解，会改变池抽选分布——形态差异，默认路径无影响）。
  11. **连续失败上限**：Rust `if infeas_sol_pool.len() >= config.max_conseq_failed_attempts.unwrap_or(usize::MAX) { break }`（:65-68，默认 None=无限）→ C++ `++failures; if (config.maxConsecutiveFailures > 0U && failures >= ...) break`（:818-823，默认 0=无限）。判定：一致（默认都不限；差异：Rust 用池长度计数【注意：池被 clear 的时机只在成功后，故池长度=连续失败数，语义相同】、C++ 用独立 failures 计数【且在成功分支被清零 :799——与池 clear 同点，等价】）。
  12. **池抽选（正态）**：Rust `Normal::new(0.0, stddev).unwrap().sample(&mut sep.rng).abs().min(0.999)` → `selected_idx = (sample * pool.len()) as usize`（:71-81）→ C++ `RustNormalF32(separator.Random(), stddev)` + `std::abs` + `min(0.999F)` → `selectedIndex = min(sample*size, size-1)`（:825-833，最后再 clamp 到 size-1——Rust 的 `as usize` 截断在 sample<0.999*size 下不会越界，clamp 为防御）。判定：一致。C++ 的 `RustStandardNormalF64`（:262-297）逐行复刻 rand_distr 0.6 的 ZIGNOR 表（tail=3.654152885361008796、area=0.00492867323399、257 项表、`u = 1..2 区间位级`、尾部对数拒绝环），注释明确"rand_distr 0.6 samples StandardNormal with ZIGNOR in f64, then casts to f32"（:213-215）——位级复刻意图明确，fixture `ExplorationPoolRustTrace.tsv`（weighted_pool 用例，pool_stddev=1.0）锁定抽选序。
  13. **回滚+扰动**：Rust `sep.rollback(selected_sol, None)`（重建 tracker）+ `disrupt_solution(sep, config)`（:83-85）→ C++ `separator.Rollback(infeasiblePool[selectedIndex].solution)`（重建 tracker，S04 差异 #5 的 None 分支等价）+ `DisruptSolution(separator, config)`（:834-846）。判定：一致。
  14. **迭代计数**：C++ `++iteration`（:848）为限流用；Rust `n_iter` 在 separator 内部（explore 侧无迭代计数）：C++ 额外（配合 maxIterations）。
  15. **返回**：Rust `feasible_sols`（**全部可行解集合**，:91；调用方 mod.rs:59 取 `.last()` 为最终解）→ C++ `ExplorationResult{bestSolution, bestMetrics, history}`（单一最优解 + 指标 + 历史，:854-856）。判定：差异（形态：Rust 返回集合【ExplImproving/ExplFeas 上报承担了流式语义】、C++ 返回最终最优+聚合指标 surrogateReject/exact 计数（:681-682,734-735,852-853）+历史）。行为等价（last()=bestWidth 对应解）。
  16. **结束日志**：Rust `info!`（:89）；C++ 无日志（观测层）。
  17. **fixture 锁定**：`ExplorationRustTrace.tsv`（repeated_shrink 用例：2 轮迭代限、收缩 0.1、best_width 5.399999619 等位级）+ `ExplorationPoolRustTrace.tsv`（失败池抽选）+ `PhaseReference.tsv`（exploration 行 seed=42 width/density）锁定主循环。

### `disrupt_solution`（explore.rs:94-221）vs `DisruptSolution`（Exploration.cpp:394-600）
- 判定：基本等价（四步流程一致；交换变换合法化、差异容忍 0.01、contained 集合与搬迁变换链的语义逐项对应；三处细节差异如下）。
- 证据与说明：
  1. **前置检查**：Rust `placed_items.len() < 2 → warn!+return`（:95-98）→ C++ `placements.size() < 2U → FromValue()`（:404-408，静默）。判定：一致（静默 vs warn，观测层）。C++ 额外 `disruptionCount==0 && containedRelocationCount==0 → 直接返回`（:398-402，默认 1/1，S01——形参化的重复次数，Rust 硬编码一次交换 + 一轮 contained 搬迁；默认值下行为一致）。
  2. **大件阈值（Step 1）**：Rust `total_ch_area = Σ(ch_area * qty)`（instance 层，:107-113）、`cutoff = total * percentile`（:115）、按 ch_area 降序累计 `ch_area*qty` 超过 cutoff 时取该项 ch_area 为 `ch_area_cutoff`（:118-140）→ C++ 同一算法（:410-452：totalConvexHullArea、cutoff、ordered 降序 + 索引 tie-break、cumulative、cutoffArea）。判定：一致（C++ sort 以 `(key 降序, index 升序)` 明确 tie-break，Rust `sorted_by_key(Reverse)` 稳定排序等价）。
  3. **大件候选（Step 2）**：Rust `placed_items.filter(pi.shape.surrogate().ch_area >= ch_area_cutoff)`（:144-145）→ C++ `surrogate->ConvexHullArea() >= cutoffArea`（:454-468）。判定：一致。C++ 额外 `if (large.empty()) large = all`（:469-472）——Rust `choose().expect(...)`（:148）在空集时 panic：差异（防御；正常路径不触发——total≥cutoff 保证 cutoffArea 存在且至少一项满足）。
  4. **选第一件**：Rust `large_items.choose(&mut sep.rng)`（:148，rand `IteratorRandom::choose`——**均匀遍历式抽选，每个元素消耗不等随机数**）→ C++ `BoundedIndex(separator.Random(), large.size())`（:474-476，拒绝采样）。判定：一致（语义=均匀；随机数消耗序由 fixture 锁定——ExplorationRustTrace 的用例已覆盖该路径）。
  5. **选第二件（差异容忍）**：Rust 过滤 `!approx_eq!(area, ε=area*0.01) && !approx_eq!(diameter, ε=diameter*0.01)`，`choose().or_else(|| 全体非 pk1 中 choose)`（:152-164）→ C++ `distinct` 集合用 `std::abs(a1-a2) > areaTol && std::abs(d1-d2) > diameterTol`（:477-498），`secondPool = distinct.empty() ? all : distinct`，再过滤 `!= firstIndex` 后 BoundedIndex（:499-514）。判定：差异（细节一：Rust 的 fallback 是"distinct 选不到 → 从**全体**（含可能与第一件相同的件，但 filter pk != pk1 排除同一件）选"；C++ 是"distinct 空 → 用 all（再排除 firstIndex）"——同构。细节二：`approx_eq!(f32, a, b, epsilon)` 语义为 `|a-b| < eps`【近相等】，Rust 保留"不近似相等"的件；C++ `>` 保留——边界 `==` 时两侧归类相反（浮点边界情形，行为级影响可忽略）。细节三：C++ 空第二池直接返回（:508-511），Rust `expect` panic——防御差异）。注意 Rust fallback `or_else` 的 `choose` 在空迭代时返回 None → `expect` panic；C++ `filteredSecondPool.empty()` 返回：同前。
  6. **交换变换合法化（Step 3）**：Rust `dt1_new = convert_sample_to_closest_feasible(dt2_old, &pi1.item)`（:172-173，把第二件旧变换的旋转吸附到第一件 item 的允许旋转集）→ C++ `LegalizeTransform(secondOld, firstItem)`（:520-523，Exploration.cpp:148-187）。判定：差异（细节：C++ `LegalizeTransform` 用 f64 的 `RotationDistance`（最短角距，:133-146）选最近允许角；Rust `convert_sample_to_closest_feasible`（uniform_sampler.rs:104-121）用 `normalize_rotation(dt - r).abs()`——**非最短角距**（先归一化差值再取绝对值，跨 ±π 边界的角可能选错方向）。对离散角集【常见 0/90/180/270】两者同选；对非常规角集 C++ 更"正确"但与 Rust 有偏离。归属 S16 详比的已知项，此处确认现状：Exploration 用的是 f64 最短角距版本，与 Rust 的 f32 归一化版本在边界角上有差）。
  7. **执行交换**：Rust `sep.move_item(pk1, dt1_new)` ×2（:177-178）→ C++ `separator.MoveItem(firstPlacement.handle, firstNew)` ×2（:525-536）。判定：一致。
  8. **contained 集合（practically_contained_items）**：Rust `layout.cde().collect_poly_collisions(&pi_c.shape)` → 收集 PlacedItem 危害 → 过滤 `poi.center` 被 `pi_c.shape.collides_with` 包含（:224-245）→ C++ `engine->CollectPolygonCollisionsUntil(containerShape, collector, filter【排除自身】, stop=false)`（:366-371）+ `ContainsRustF32(poi.center)`（:383-385）。判定：一致（排除自身：Rust 用 `*pk != pk_c` 后过滤 :238-243；C++ 用 filter.irrelevant 按 handle 排除 :360-365——注意 Rust 是"碰撞收集后再过滤自身"，C++ 是"收集时就跳过自身"，集合等价）。C++ 额外：`CollectContainedHandles` 重建 layout + collision engine（:331-358，无状态函数式，快照成本——形态差异）。
  9. **搬迁变换链（Step 4）——本组最重要的精度差异**：Rust `converting_transformation = dt1_new.compose().inverse().transform(&dt1_old.compose())`（explore.rs:187-188，**全程 f32 3×3 矩阵**：compose→inverse→transform，即 jagua-rs Transformation 的矩阵乘/逆，transformation.rs:70-84）；被搬件 `new_dt = c1_pi.d_transf.compose().transform(&converting).decompose()`（:193-196）。C++ `firstConversion = firstNew.Inverse().FollowedBy(firstOld)`（Exploration.cpp:538-539），被搬件 `LegalizeTransform(placement->Transform().FollowedBy(firstConversion), item)`（:558-562）。判定：**差异**。三层：
     - 组合语义：Rust `self.inverse().transform(&old)` = `old · inverse`（矩阵右乘）；C++ `Inverse().FollowedBy(firstOld)` = 先 Inverse 再被 firstOld 跟随（`FollowedBy` 定义为 `this` 后接 `next`，Transformation.cpp:66-69）。`A.FollowedBy(B)` 的语义是"先 A 后 B"= B∘A（矩阵 B·A）；Rust `inverse.transform(&old)` = old · inverse = "先 inverse 后 old"= old∘inverse = `inverse.FollowedBy(old)`。**语义一致**（均为"撤销新位形、恢复旧位形"的变换）。
     - 精度路径：Rust 全链 f32 矩阵（含 3×3 求逆的 f32 舍入）；C++ `Transform2d` 是 (double rot, double trans) 的**解析式**组合（FollowedBy = 角度相加 + 旋转平移向量，Transformation.cpp:66-91 的 f64 版），**不是** JaguarCore 提供的 `FollowedByRustF32`（f32 矩阵位级复刻版，Transformation.cpp:71-91）。即：Exploration 的长变换链走的是 f64 解析路径而非 Rust-f32 矩阵路径——与 jagua-rs 的逐位结果会有 ULP~更大量级的偏差（矩阵求逆 f32 误差 vs 解析 f64 精度）。归属判定：**行为级差异（数值）**，非语义错误；ExplorationRustTrace/ExplorationContainedRustTrace fixture 锁定的是离散角/轴对齐情形（此时两条路径数值一致或差异被 LegalizeTransform 吸附）。
     - Legalize 尾步：Rust 搬迁后 `convert_sample_to_closest_feasible(new_dt, item)`（:199）；C++ `LegalizeTransform`（:561）——同前述角距差异。
  10. **排除对侧件**：Rust `.filter(|c1_pk| *c1_pk != pk2)`（:190，搬迁第一件的包含物时排除 pk2）；C++ `if (handle == secondMoved.Value()) continue`（:546-549）。判定：一致（两侧同在两段分别排除对侧）。注意 Rust 用交换后的**新** pk2（move_item 返回值 :178），C++ 用 `secondMoved.Value()` 新 handle：一致。
  11. **重复次数**：C++ `disruptionCount/containedRelocationCount`（默认 1/1）只实现为"1 次与否"的开关（代码中无循环——1 为默认行为，0 直接短路 :398-402；>1 的取值不产生多次执行）。判定：差异（配置形参化不完整：>1 值无效果；Rust 无此参数。默认路径一致）。

### `practically_contained_items`（explore.rs:224-245）
- C++ 对应：`CollectContainedHandles`（Exploration.cpp:331-392）。
- 判定：一致（见上条 8；POI 包含判定的 `ContainsRustF32` 对应 `collides_with(poi.center)` 的点-多边形包含，归属 S05 的 JaguarCore 记录）。

### C++ 额外辅助件
- `CalculateMetrics`（Exploration.cpp:17-55）：每轮重建 problem+layout 校验可行性并聚合指标（placedItemCount/remainingDemand/density/feasible）——Rust 无此逐轮校验（解的可行性由 tracker loss==0 + 构造不变式保证）。判定：C++ 额外（防御+指标聚合）。
- `MakeProgress`/`PublishSolutionReport`（:602-614 + OptimizationListener.cpp）：对应 Rust `SolutionListener` 的 report/report_phase（listener.rs:5-14）；`ReportType::ExplFeas/ExplInfeas → OPTIMIZATION_REPORT_EXPLORATION_FEASIBLE/INFEASIBLE`（listener.rs:44-50 vs OptimizationListener.h:40-56）。判定：一致（S21 详比）。
- `ExplorationConfig::Validate`（:619-635）：C++ 额外前置校验。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 返回：可行解集合 vs 单一最优解+指标+history | 差异（形态） | mod.rs 取 last() 与 C++ bestSolution 等价；C++ 聚合 reject/exact 计数进 LbfMetrics | explore.rs:91 vs Exploration.cpp:854-856 |
| 2 | 失败池上限 infeasiblePoolSize（pop_back 丢最差） | C++额外 | 默认 0 不启用；启用时改变池抽选分布 | explore.rs:61-63（无上限）vs Exploration.cpp:813-817 |
| 3 | maxIterations 限流 + 独立 failures 计数 | C++额外 | 默认 0=无限；failures 语义=池长（池只在成功时清），等价 | explore.rs:33,65 vs Exploration.cpp:708-710,799,818-823 |
| 4 | ~~长变换链精度：f64 解析 Inverse/FollowedBy~~（2026-09-29 修复后）InverseRustF32/FollowedByRustF32 位级对应 | 一致（已修复） | 见"补充审计"第 1 条；fixture 锁定 | explore.rs:187-196 vs Exploration.cpp:538-541,563-565,572-575,596-599 |
| 5 | 旋转合法化：f32 归一化差值 vs f64 最短角距 | 差异（边界） | 非常规离散角集时选角可不同（±π 边界） | uniform_sampler.rs:104-121 vs Exploration.cpp:133-187 |
| 6 | 初始解无条件报 ExplFeas vs 按 metrics.feasible 分支 | 差异（防御） | 合法输入下同为 FEASIBLE | explore.rs:28 vs Exploration.cpp:687-698 |
| 7 | 正态池抽选的 ZIGNOR f64 复刻 | 一致 | 位级复刻 rand_distr 0.6；ExplorationPoolRustTrace.tsv 锁定 | explore.rs:73-76 vs Exploration.cpp:213-303 |
| 8 | 大件/第二件抽选用 BoundedIndex 拒绝采样 | 一致 | 均匀语义；随机消耗序由 fixture 锁定 | explore.rs:148,152-164 vs Exploration.cpp:474-514 |
| 9 | 空 large/distinct 池：panic vs 静默返回 | 一致（既定差异：panic→Result） | 正常路径不触发 | explore.rs:148,164 vs Exploration.cpp:469-472,508-511 |
| 10 | disruptionCount/containedRelocationCount 形参化（>1 无效果） | C++额外（不完整） | 默认 1/1 下行为与 Rust 硬编码一致 | explore.rs:94-221（硬编码一次）vs Exploration.cpp:398-402 |
| 11 | 逐轮 CalculateMetrics 可行性校验 | C++额外 | 防御+指标聚合；Rust 依赖不变式 | explore.rs（无）vs Exploration.cpp:17-55,724-731 |
| 12 | contained 排除自身：收集后过滤 vs 收集时 filter | 一致 | 集合等价 | explore.rs:238-243 vs Exploration.cpp:360-365 |

### 补充审计（2026-09-29）
1. **【遗漏：主文 2026-09-29 修复注记与差异表 #4/结论未同步】** 修复注记已声明"FollowedBy f64 精度路径条目已消除"，但上方差异汇总表 #4 仍写"f64 解析 Inverse/FollowedBy、未用 FollowedByRustF32"，结论段同称"是本组唯一可能产生可见数值偏差的点"——**已过时**。经现场核验（Exploration.cpp:538-541,563-565,572-575,596-599）：两段 contained-relocation 的 4 处调用点全部为 `InverseRustF32().FollowedByRustF32(...)`（firstConversion/secondConversion 各 1 处 + 被搬件 `Transform().FollowedByRustF32(conversion)` 各 1 处），且源文件带 538-539/572-573 的"用 RustF32 位级对应"注释。更正判定：**一致（位级对齐）**——`FollowedByRustF32`（Transformation.cpp:71-91）以 f32 sin/cos 矩阵乘法复刻 jagua-rs `dot_prod`（transformation.rs:172-195），`InverseRustF32`（Transformation.cpp:49-63）复刻 3×3 矩阵求逆（含 `cos²+sin²` 行列式与 `atan2` 重分解）。差异表 #4 应据此替换为"一致（RustF32 位级路径，fixture 锁定）"。
2. **【浅覆盖：第 6 条 Explore 内 LegalizeTransform 的角距公式描述滞后】** 主文第 6/9 条写 Exploration 用"RotationDistance（最短角距，f64，:133-146）"。现状核对（Exploration.cpp:133-146）：`NormalizeRotation`+`min(d, 2π-d)` 的最短角距仍在（与 uniform_sampler.rs:104-121 的 `normalize_rotation(delta).abs()` 非最短角差语义差在**跨 ±π 边界角集**可选不同角——主文已记为差异 #5，判定维持"差异（边界）"），但需注明该差异只影响非常规离散角集；常规 0/90/180/270 与连续旋转两路径同选。差异 #5 维持不变。
3. **【核对确认】** 失败池抽选（Exploration.cpp:832-839）`abs(RustNormalF32(sep.Random(), stddev))`、`min(0.999F)`、`selectedIndex = min(sample*len, len-1)` 与 explore.rs:74-76 逐项对应（Rust `as usize` 截断 + C++ clamp 的防御差异主文已记）；`RustStandardNormalF64`（:262-297）为 rand_distr 0.6 ZIGNOR 的 257 项表逐位复刻（tail=3.654152885361008796、area=0.00492867323399、`u∈[1,2)` 位级、尾部对数拒绝环），判定"一致"成立。
4. **【核对确认】** `0.98` 双阈值不在 explore.rs 也不在 Exploration.cpp（属 separator 内层，S04 范围）；Explore 主循环条件 `while (!terminator.ShouldTerminate(iteration) && (maxIterations==0 || iteration<max))`（:714-716）与 explore.rs:33 `while !term.kill()` 的差异（maxIterations 限流）主文 #3 已记。
5. **【核对确认】** 逐项复核记录的 C++ 行号基线：当前 Exploration.cpp 为 863 行（主文头部"857 行"含 2026-09-29 修复引入的 +6 行漂移，行号引用整体仍准确±6）。

## 结论
Algorithm 12 的探索主循环（分离→成功则收缩清池、失败则入有序池、连续失败上限、|N(0,σ)|·池长 抽选、回滚+扰动）连同扰动的四步交换流程在 C++ `Explore/DisruptSolution` 中逐项复现，正态抽选甚至位级复刻了 rand_distr 的 ZIGNOR 表，并被 Exploration(RustTrace/Pool/Contained) 与 PhaseReference fixture 锁定，判定基本等价。~~最重要的发现是差异 #4~~（**2026-09-29 更正**：差异 #4 的"f64 解析路径"已由 `InverseRustF32/FollowedByRustF32` 修复消除，长变换链现为 f32 矩阵位级对应，见补充审计第 1 条）；现存最重要的残余差异是 #5：Exploration 的 `LegalizeTransform` 用循环最短角距选角，而 Rust `convert_sample_to_closest_feasible` 用非循环归一化差值——非常规离散角集（跨 ±π 边界）时可选不同角；其余差异均为 C++ 的防御性限流、指标聚合与返回形态重组。
