# S11. sep_evaluator（分离评估器）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/sep_evaluator.rs`（103 行）
**C++**: `SparrowCore/Include/SparrowCore/eval/SeparationEvaluator.h`（44 行）+ `Source/Eval/SeparationEvaluator.cpp`（212 行）
**判定**: 基本等价（列差异）

评估语义被 `SeparatorSearchRustTrace.tsv`（158 条 eval 的 kind/loss 位模式 + report/keep 序列，TestSeparator.cpp:131 消费）与 `SeparatorRoundRustTrace`/`SeparatorMultiWorkerRoundRustTrace`（round 级聚合，见 S04/S05）锁定。

## 函数逐条比对

### `SeparationEvaluator::new`（sep_evaluator.rs:21-47）
- C++ 对应：构造函数（SeparationEvaluator.cpp:63-114）。逐项：
  1. **当前 hazard 识别**：Rust :28-35 `layout.cde().haz_key_from_pi_key(current_pk).expect(...)` 取移动件在 CDE 中的 hazard key 与 entity；C++ :77-101 从布局快照逐放置注册 hazard，`placement.handle == movingHandle` 时记 `m_MovingHazardHandle`；找不到则引擎置空（后续 Evaluate 全部 INVALID）。【差异】（实现路径：直接查 CDE 注册表 vs 快照重建 + 匹配 handle；语义等价——移动件的 hazard 都被识别）
  2. **collector 预置**：Rust :40 `BasicHazardCollector::with_capacity(placed_items.len()+1)`；C++ 用 `jaguar::HazardCollector collisions`（cpp:137）且把"跳过移动件自身"编码进 filter（`irrelevantHandle = (handle == movingHazardHandle)`，cpp:105-110），对应 Rust 在 evaluate 里 `collector.insert(current_hazard)`（:67）令遍历跳过自身。两处机制不同但语义一致：碰撞遍历忽略移动件旧位置。【一致】
  3. **loss 评估器**：Rust :42 `CollisionLossEvaluator::new(layout, ct, current_pk)`；C++ 持 `m_Tracker` 指针 + `m_MovingIndex`（tracker.IndexForHandle，cpp:71），损失计算走 `m_Tracker->EvaluateCandidateLoss(...)`（cpp:182-183，见 S12）。
  4. **shape_buff 克隆**：Rust :43 复用缓冲多次变换；C++ 每次 Evaluate 调 `TransformedRustF32` 新建（cpp:149）。性能形式差异，无行为影响。【差异】（缓冲复用 vs 逐次构造）
- 判定：【一致】（形式差异如上）
- 证据：sep_evaluator.rs:21-47 vs SeparationEvaluator.cpp:63-114。

### `evaluate_sample`（sep_evaluator.rs:52-98）— 上界解析
- Rust :57-61：`loss_bound = match upper_bound { Some(Collision{loss}) => loss, Some(Clear{..}) => 0.0, _ => f32::INFINITY }`。
- C++（cpp:176-193）：先检测碰撞收集，**之后**才看上界：(a) `upperBound` 是 CLEAR → 直接 INVALID（cpp:176-180）；(b) 无碰撞 → CLEAR{0.0}（cpp:172-175，在检查上界**之前**——注意顺序：cpp:172 先判 collisions.Empty() 返回 CLEAR，然后 cpp:176 才处理上界，所以 CLEAR 上界不会误杀 CLEAR 样本）✓；(c) 是 COLLISION → 先 `EvaluateCandidateLoss` 算完整 loss，再 `loss > upperBound->loss` 才 INVALID（cpp:188-193）。**与 Rust 的关键语义差别**：Rust 把 loss_bound 下传给 CollisionLossEvaluator，在逐 hazard 累加时**提前终止**（`add` 返回 true 即停，:71-77）；C++ 在 `EvaluateCandidateLoss`（tracker 整体计算，见 S12）之后才比较上界——即 C++ 不做"逐 hazard 的上界早停"，只做"算完后一次性比较"。**后果差异**：(1) Rust `Some(Clear{..})` 上界时 loss_bound=0.0，任何累积 loss>0 都会触发 early-stop 返回 Invalid——即 Clear 上界下**带碰撞的样本**直接 Invalid（数学上：任何 Collision loss>0 ≥ bound=0 都被拒）。C++ cpp:176-180 显式实现同一语义（上界 CLEAR 且有碰撞 → INVALID）✓ 等价。(2) Collision 上界时 Rust 是"累加一旦 > bound 停止并 Invalid"（::79 短路），C++ 是"全量累加后比较"。**最终接受性判定相同**（loss>bound 都拒），但 Rust 的早停意味着它可能少检测后续 hazard——这属于性能语义而非结果语义；且 hazard 收集集合会不同（Rust 短路后 collector 不再增长，但那时直接返回 Invalid，不再使用 collector）。**一个真正的边界差异**：Rust 早停条件是 `self.loss > self.loss_bound`（collision_loss.rs:55，累加**每个** hazard 后检查，含等于：loss==bound 不停），C++ 是 `loss.Value() > upperBound->loss`（cpp:191）同样严格大于 ✓。综合判定：接受性结果等价，短路时机不同。【差异】（早停粒度：Rust 逐 hazard 短路 vs C++ 算完比较；接受/拒绝结果等价）
- 判定：【一致】（接受性等价；短路粒度差异见差异表 #3）
- 证据：sep_evaluator.rs:57-61,71-77 vs SeparationEvaluator.cpp:172-193。

### `evaluate_sample` — 变换与两段碰撞收集
- Rust :62-67：`shape = shape_buff.transform_from(item.shape_cd, &dt.compose())`——注意 `dt.compose()`（DTransformation 的 f32 旋转+平移合成为完整 Transformation，含可能的缩放？此处为旋转平移）；`collector.clear()` + 预置当前 hazard（:65-67）。
- C++ cpp:126-137：`LegalizeTransform`（旋转合法化，见 S09 差异#1，Rust 无此防御——SepEvaluator 的调用方 search 产合法旋转）→ 取 surrogate → `CollectSurrogateCollisionsUntil(..., [](entity){return false;})`（**should_stop 谓词恒 false**，即 surrogate 阶段不短路，全量收集，cpp:138-147）→ `TransformedRustF32`（cpp:149）→ `CollectPolygonCollisionsUntil`（同样恒 false 谓词，cpp:158-163）→ 合并两个 collector（cpp:168-171）。
- 与 Rust 的结构对照：Rust 的 `collect_surrogate_collisions_until(shape, ..., should_stop)` 与 `collect_poly_collisions_until(...)` 的 should_stop 是 `|hazard| loss_evaluator.add(hazard, shape)`（:71）——**每发现一个碰撞 hazard 就累加 loss 并检查短路**。C++ 两段收集的谓词恒 false（不短路、不累加），收集完后 cpp:172 判空 → CLEAR；非空再 cpp:182 整体调 `EvaluateCandidateLoss`。
- **行为差异分析**：(a) `stopped_during_surrogate_check == true → Invalid`（Rust :78-79）对应"surrogate 阶段累加 loss 已超上界"；C++ 对应路径是收集后统一判定（若最终 loss > 上界 → INVALID）——结果一致，但 Rust 的 early-stop Invalid 与"最终 loss 超界"的 INVALID 无法从外部区分（都是 Invalid）✓。(b) `collector.len() == n_ignored_hazards → Clear{0.0}`（Rust :89-90，除自身外零碰撞）↔ C++ `collisions.Empty()`（cpp:172，filter 已排除自身）✓。(c) 其余 → `Collision{loss_evaluator.loss()}`（Rust :92-93）↔ C++ `COLLISION, std::max(loss, 1.0e-6F)`（cpp:195）——**注意 C++ 把 loss 钳到 ≥1e-6**：Rust `loss()` 可为极小正数甚至 0.0（理论上一对碰撞件的量化可为 0？quantify 对重叠面积 >0 返回 >0，但 f32 下溢可能得 0），Rust 直接返回原值。若 Rust 侧出现 0.0 loss 的 Collision，C++ 会放大为 1e-6。此外 C++ 的 `std::max(loss, 1e-6)` 只在 loss 有值时到达（EvaluateCandidateLoss 返回 Result）。【差异】（loss 下限钳制 1e-6 为 C++ 额外；正常量化值远大于 1e-6，fixture loss 位模式如 `40afead0`≈5.5 均匹配）
- 判定：【一致】（短路粒度与下限钳制两条差异，见差异表）
- 证据：sep_evaluator.rs:62-97 vs SeparationEvaluator.cpp:137-195。

### `n_evals`（sep_evaluator.rs:100-102）
- C++ 对应：`EvaluationCount()`（cpp:198-201）。【一致】

### 【C++ 额外】统计计数器 SurrogateRejectCount / ExactCollisionCount（h:29-30, cpp:203-211）
- C++ 额外维护 surrogate 命中数与精确碰撞数两个诊断计数器（SeparatorWorkerResult 上报，见 S05），Rust 无对应物（SepStats 只有 total_moves/total_evals，worker.rs:88-91）。纯诊断信息，不回灌算法。
- 判定：【C++ 额外】

### 【Rust→C++ 缺失】BasicHazardCollector 的容量语义
- Rust :40 `with_capacity(placed_items.len()+1)` 预分配；C++ HazardCollector 默认构造。无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 早停粒度：Rust 逐 hazard 累加并短路（CollisionLossEvaluator.add），C++ 全量收集后一次性算 loss 再比较上界 | 差异 | 接受/拒绝结果等价；C++ 无逐 hazard 提前终止（性能差异），且 surrogate 阶段不短路 | sep_evaluator.rs:71-77 vs cpp:138-163,182-193 |
| 2 | loss 下限钳制：C++ `std::max(loss, 1e-6)`，Rust 原值返回 | C++ 额外 | 仅 loss < 1e-6 时偏离（f32 下溢场景）；fixture 无此样本 | cpp:195 vs sep_evaluator.rs:93 |
| 3 | LegalizeTransform 防御（旋转合法化） | C++ 额外 | 正常路径恒等（调用方采样器产合法旋转） | cpp:126-130,15-58 vs sep_evaluator.rs:52-64 无 |
| 4 | 自身排除机制：Rust collector 预置当前 hazard vs C++ filter 的 irrelevantHandle | 差异 | 实现形式差异，语义等价（遍历都跳过移动件旧位置） | sep_evaluator.rs:65-67 vs cpp:105-110 |
| 5 | hazard 注册：Rust 查 cde 注册表（expect panic）vs C++ 快照重建 + handle 匹配（失败置空引擎→全 INVALID） | 差异 | 既定 panic→Result 形态差异；正常输入不触发 | sep_evaluator.rs:28-31 vs cpp:77-101 |
| 6 | shape 缓冲复用 vs 逐次 TransformedRustF32 构造 | 差异 | 性能形式差异，无行为影响 | sep_evaluator.rs:62-64 vs cpp:149 |
| 7 | 诊断计数器 SurrogateRejectCount/ExactCollisionCount | C++ 额外 | 纯诊断，不回灌算法 | h:29-30, cpp:156-157,164-167 |
| 8 | Rust Clear 上界时 loss_bound=0.0 逐 hazard 短路 vs C++ 上界 CLEAR 直接 INVALID | 一致（殊途同归） | 两种写法都实现"有 Clear 样本在手时任何碰撞样本都拒收" | sep_evaluator.rs:59 + collision_loss.rs:55 vs cpp:176-180 |

## 结论

SeparationEvaluator 的评估骨架——上界解析（Clear 上界拒绝一切碰撞、Collision 上界拒绝更劣 loss）、surrogate/精确两段收集、跳过移动件自身、零碰撞 Clear{0} / 有碰撞 Collision{loss}——在 C++ 中语义复刻，接受性判定与 Rust 逐 hazard 短路版本结果一致，并被 SeparatorSearchRustTrace.tsv 的 158 条 eval 位模式锁定。实质差异是 C++ 把"边收集边累加早停"改为"全量收集后调 CollisionTracker 整体算 loss"（结果等价、少一层短路），以及 loss ≥1e-6 下限钳制、LegalizeTransform 防御等 C++ 额外稳健性；精细的逐 hazard 权重累加语义归 S12。
