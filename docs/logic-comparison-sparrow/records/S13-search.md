# S13. search（采样搜索编排）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/search.rs`（343 行）
**C++**: `SparrowCore/Include/SparrowCore/sampling/LocalSearchSampler.h`（50 行）+ `SparrowCore/Source/Sampling/LocalSearchSampler.cpp`（206 行）；搜索编排本身在 `SparrowCore/Source/Optimizer/LbfSearch.cpp`（297 行）与 `SparrowCore/Source/Optimizer/SeparatorWorker.cpp`（FindBestSeparatorMoveImpl，128-280 行）
**判定**: 基本等价（列差异）

说明：Rust 的 `search_placement` 是一个"编排函数"，把 UniformBBoxSampler + BestSamples + refine_coord_desc + evaluator 串成 Algorithm 6。C++ 没有单一对应文件，而是拆成两处内联编排（`SearchLbfImpl` 对应 ref_pk=None 的 LBF 构造路径；`FindBestSeparatorMoveImpl` 对应 ref_pk=Some 的分离路径），另提供一个独立的 `LocalSearchSampler`（高斯局部搜索器，Rust sparrow 中不存在，属于 C++ 额外的公共 API，供外部调用方使用，见 S13-4）。两条编排路径分别被 fixture `LbfSearchRustTrace.tsv`（container/prerefine/finalrefine 三阶段 eval 158 条）与 `SeparatorSearchRustTrace.tsv`（reference/focused/container/prerefine/finalrefine 全五阶段，eval 158 条）锁定。

## 函数逐条比对

### `SampleConfig`（search.rs:15-20）
- C++ 对应：`SparrowCore/Include/SparrowCore/LbfConfig.h` 的 `LbfConfig`（samplesPerItem/nCoordinateDescents，见 S01）与 `SeparatorConfig`（nContainerSamples/nFocussedSamples/nCoordinateDescents，见 S04）。Rust 单一 `SampleConfig{n_container_samples, n_focussed_samples, n_coord_descents}` 按 LBF（n_focussed_samples=0，见 consts.rs:51-55 `LBF_SAMPLE_CONFIG`）与 Separator 两种配置拆为两个结构。
- 判定：【一致】（结构归并，字段语义一一对应）
- 证据：search.rs:16-20 `pub struct SampleConfig { n_container_samples, n_focussed_samples, n_coord_descents }`；LbfSearch.cpp:218 `config.samplesPerItem`、SeparatorWorker.cpp:201 `config.nFocussedSamples`、:217 `config.nContainerSamples`。

### `SearchPhase` 枚举（search.rs:22-29）
- C++ 对应：`LbfSearchTracePhase`（`LbfSearchTrace.h:28` 起）仅含 ContainerSample/PreRefine/FinalRefine 三值；`SeparatorSearchTracePhase` 含全部五值（ReferencePlacement/FocusedSample/ContainerSample/PreRefine/FinalRefine）。
- 判定：【一致】（LBF 路径合法地少两个阶段；trace 类型按路径拆分）
- 证据：Rust search.rs:23-29 五值枚举；C++ `SparrowCore/Source/Optimizer/LbfSearchTrace.h:28`、`SeparatorWorkerTrace.h`（SeparatorSearchTracePhase）。fixture `LbfSearchRustTrace.tsv` 头部 `n_focussed_samples=0` 且 eval 行只有 container/prerefine/finalrefine 三种 phase（`awk '$1=="eval"{print $2}'` 统计：16 container / 56 prerefine / 32 finalrefine），`SeparatorSearchRustTrace.tsv` 则五阶段齐全（3 reference / 32 focused / 64 container / 44 prerefine / 18 finalrefine）——C++ 拆分与 Rust 两条实际执行路径完全一致。

### `SearchObserver` trait + `NoopSearchObserver`（search.rs:31-60）
- C++ 对应：无 observer 抽象；trace 用裸函数指针 `const LbfSearchTraceCallback* callback`（LbfSearch.cpp:128）与 `const SeparatorSearchTraceCallback* callback`（SeparatorWorker.cpp:134），为空时跳过。
- 判定：【差异】（形式差异：trait 对象 vs 函数指针；行为等价——Rust 的 NoopSearchObserver 与 C++ `callback == nullptr` 都是空操作）
- 证据：search.rs:58-60 `struct NoopSearchObserver; impl SearchObserver for NoopSearchObserver {}`；LbfSearch.cpp:93 `if (m_Callback != nullptr)`。

### `ObservedEvaluator`（search.rs:62-99）
- C++ 对应：`TracingEvaluator`（LbfSearch.cpp:70-115，包 `LbfEvaluator`）与 SeparatorWorker.cpp 中的 `TracingEvaluator`（包 `SeparationEvaluator`，带 Phase()/SetPhase()）。
- 判定：【一致】（包装-委托模式逐字段对应：set_phase→SetPhase、evaluate_sample 转发→Evaluate 转发并回调、n_evals→EvaluationCount 透传）
- 证据：search.rs:85-99 `impl SampleEvaluator for ObservedEvaluator` 转发 `self.inner.evaluate_sample(...)` 后调 `self.observer.evaluated(...)`；LbfSearch.cpp:87-103 `Evaluate` 先 `m_Delegate.Evaluate(transform, upperBound)` 再 `(*m_Callback)(event)`；EvaluationCount 透传见 LbfSearch.cpp:105-108、search.rs:96-98。

### `search_placement` / `search_placement_with_trace`（search.rs:191-229）
- C++ 对应：`SearchLbf`/`SearchLbfWithTrace`（LbfSearch.cpp:276-295，无参照布局，对应 `ref_pk=None`）与 `FindBestSeparatorMove`/`FindBestSeparatorMoveWithTrace`（SeparatorWorker.cpp，有 movingHandle，对应 `ref_pk=Some`）。泛型 evaluator 参数变成具体类型（LbfEvaluator / SeparationEvaluator），由编排层固定。
- 判定：【差异】（公共 API 形状：Rust 一个入口接受任意 `impl SampleEvaluator`；C++ 按调用方拆成两个具体入口 + 一个独立 LocalSearchSampler 通用件。行为在已锁定的两条路径上等价）
- 证据：search.rs:191-208 签名 `evaluator: impl SampleEvaluator`；LbfSearch.h:41-46 `SearchLbf(layout, item, config, rng, filter)`；fixture LbfSearchRustTrace.tsv 头 `generator=Sparrow Rust search_placement with LBFEvaluator`，SeparatorSearchRustTrace.tsv 头 `generator=Sparrow Rust search_placement with SeparationEvaluator`——即 fixture 生成用的正是同一个 Rust 函数的两条实例化。

### `search_placement_impl` — 参照布局阶段（search.rs:253-269）
- C++ 对应：SeparatorWorker.cpp:167-186：先 `SetPhase(ReferencePlacement)`，`tracingEvaluator.Evaluate(movingItem.Transform(), std::nullopt)`，再 `report(...)`。注意 C++ 传 `std::nullopt` 作为 upper_bound，Rust 传 `Some(best_samples.upper_bound())`（此时 BestSamples 为空，`upper_bound()` 返回 `SampleEval::Invalid`，其 loss 无意义但对 ComparationEvaluator 的 loss_bound 解析为 `f32::INFINITY`，语义相同——见 S11-3 第 5 条）。
- 判定：【差异】（表面差异，语义等价：Invalid 上界与 nullopt 上界都解析为无穷损失界）
- 证据：search.rs:256-258 `evaluator.set_phase(SearchPhase::ReferencePlacement); let dt = l.placed_items[ref_pk].d_transf; let eval = evaluator.evaluate_sample(dt, Some(best_samples.upper_bound()));`；best_samples.rs:81-87 空 BestSamples 的 `upper_bound()` 返回 `SampleEval::Invalid`；sep_evaluator.rs:57-61 `_ => f32::INFINITY`；SeparatorWorker.cpp:168-169 `tracingEvaluator.Evaluate(movingItem.Transform(), std::nullopt)`；对应 SeparationEvaluator.cpp:176-180 `upperBound.has_value() && upperBound->kind == CLEAR → Invalid`、:188-193 仅 Collision 上界才比较 loss，nullopt 不触发任何提前拒绝。

### `search_placement_impl` — 聚焦采样（search.rs:253-267, 274-282）
- C++ 对应：SeparatorWorker.cpp:188-208：`UniformRectSampler::Create(movingItem.CollisionShape().BoundingBox(), movingItem.GetItem(), bounds)`（对应 Rust 用 `l.placed_items[ref_pk].shape.bbox` 作为 sample_bbox、container outer bbox 作为 container_bbox，search.rs:265-266），失败则跳过聚焦阶段（对应 Rust `focussed_sampler` 为 None 时 `if let Some` 直接跳过，search.rs:274）。
- 判定：【一致】
- 证据：search.rs:265-266 `UniformBBoxSampler::new(pi_bbox, item, l.container.outer_cd.bbox)`；SeparatorWorker.cpp:190-193 `UniformRectSampler::Create(movingItem.CollisionShape().BoundingBox(), movingItem.GetItem(), bounds)` + :196 `if (focusedSampler.HasValue())`；循环体 `sampler.Sample(rng)` + `report(candidate, tracingEvaluator.Evaluate(candidate, best.UpperBound()))`（:204-206）对应 search.rs:277-280。

### `search_placement_impl` — 容器采样（search.rs:285-293）
- C++ 对应：SeparatorWorker.cpp:210-222（Separator 路径）与 LbfSearch.cpp:218-222（LBF 路径，仅一段容器采样，对应 `n_focussed_samples=0`）。
- 判定：【一致】
- 证据：search.rs:287-291 循环 `container_sampler.sample(rng)` → `evaluate_sample(dt, Some(upper_bound))` → `best_samples.report(...)`；LbfSearch.cpp:218-222 `for (index < config.samplesPerItem) { candidate = sampler.Sample(rng); report(candidate, evaluator.Evaluate(candidate, best.UpperBound())); }`；SeparatorWorker.cpp:214-221 同构。Rust 的 `if let Some(container_sampler)`（search.rs:286，其实 new() 返回 Option）在 LBF 路径对应 LbfSearch.cpp:154-159——sampler 创建失败时返回 `FromValue(std::nullopt)`（对应 Rust `find_placement` 返回 None → 触发 strip 扩宽，lbf.rs:90-121）。

### `search_placement_impl` — 两段精化（search.rs:295-309）
- C++ 对应：SeparatorWorker.cpp:224-251 与 LbfSearch.cpp:224-248。关键点逐条核对：
  1. 预精化遍历"快照"的 best_samples：Rust `best_samples.samples.clone()`（search.rs:299）避免在遍历中修改容器；C++ `const auto starts = best.Samples()`（值拷贝，SeparatorWorker.cpp:224 / LbfSearch.cpp:224）。【一致】
  2. 精化结果 report 回 BestSamples：search.rs:301-302；SeparatorWorker.cpp:234 `report(refined.Value().first, refined.Value().second)`。【一致】
  3. 最终精化只取 `best_samples.best()`（search.rs:307-309）；C++ `best.Best()` + finalConfig（SeparatorWorker.cpp:238-251）。Rust 返回值直接是最终样本；C++ SeparatorWorker 在 `refined` 失败（HasValue()==false，即预算耗尽）时回退到 `best.Best()`（SeparatorWorker.cpp:253, 264-268），LBF 同样回退（LbfSearch.cpp:248 `final.HasValue() ? final.Value() : *bestSample`）——Rust 无此回退路径（refine_coord_desc 正常终止必返回样本，无预算参数）。【差异】（C++ 额外的防御性回退，仅在 maxEvaluations 预算被触发时偏离 Rust；正常路径等价）
  4. C++ 编排层回退样本没有再 report 进 BestSamples——与 Rust 一致（Rust 最终精化结果也不 report，search.rs:307-309 只 map 不 report）。
- 判定：【一致】（含一条 C++ 额外的预算耗尽回退）
- 证据：上述行号；fixture LbfSearchRustTrace.tsv 的 finalrefine eval 共 32 条（2 个 start × 各 16 eval），SeparatorSearchRustTrace.tsv 的 finalrefine eval 18 条 + prerefine 44 条，与两段精化的评估量结构吻合，C++ 侧 TestSampling/TestSeparator 逐位比对通过（TestSampling.cpp:1001 起 `ReadLbfSearchRustTrace`，TestSeparator.cpp:131 起）。

### `prerefine_cd_config` / `final_refine_cd_config`（search.rs:321-343）
- C++ 对应：两处编排各自的 `MakeRefinementConfig(item, finalRefinement)`（SeparatorWorker.cpp 内定义；LbfSearch.cpp:31-63）。数值核对：PRE_REFINE_CD_TL_RATIOS=(0.25,0.02)（consts.rs:17）↔ LbfSearch.cpp:53-54 `0.25F / 0.02F`；SND=(0.01,0.001)（consts.rs:23）↔ :44-45 `0.01F / 0.001F`；PRE_REFINE_CD_R_STEPS=(5°,1°) ↔ :55-58（`5.0F * π/180`、`1.0F * π/180`）；SND_REFINE_CD_R_STEPS=(0.5°,0.05°) ↔ :46-49；wiggle = `allowed_rotation == Continuous`（search.rs:322-323, 335-336）↔ LbfSearch.cpp:60-61 `rotationWiggle = item.AllowedRotation().mode == Continuous`。【一致】
- 唯一差别：item 最小维的来源。Rust `item.shape_cd.bbox`（search.rs:240, 322, 334——形状原点 bbox 的 width/height 取 min）；C++ `item.CollisionShape().BoundingBox()`（LbfSearch.cpp:34，SeparatorWorker.cpp:157-158 同）。这是 jagua-rs `shape_cd` ↔ JaguarCore `CollisionShape()` 的既有对应（S03 已记录），此处不再计差异。
- 判定：【一致】

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 编排入口形状：单一泛型 `search_placement` vs 按调用方拆分的 `SearchLbf` + `FindBestSeparatorMove` | 差异 | 无行为影响（fixture 分别锁定两条路径） | search.rs:191 vs LbfSearch.cpp:276 / SeparatorWorker.cpp:128 |
| 2 | `LocalSearchSampler`（高斯局部搜索器：ShiftMean/DecayStddev/Normal01 采样） | C++ 额外 | Rust sparrow 无此组件；为 C++ 公共 API 提供的独立采样器，不参与 search_placement 编排，仅 TestSampling.cpp:416 覆盖 | LocalSearchSampler.cpp:152-205 vs search.rs 全文无对应 |
| 3 | Rust panic/debug_assert → C++ Result 错误通道（容器 bounds、item 最小维、surrogate 缺失等前置校验） | 差异 | 既定形态差异，正常输入不触发 | LbfSearch.cpp:135-167 vs search.rs:239-250 |
| 4 | 参照阶段 upper_bound：Rust `Some(Invalid)` vs C++ `std::nullopt` | 差异 | 语义等价（两者都解析为无穷损失界，不影响判定） | search.rs:258 + best_samples.rs:81-87 vs SeparatorWorker.cpp:169 |
| 5 | 最终精化失败的回退（C++ 回退 `best.Best()`） | C++ 额外 | 仅当 maxEvaluations 预算触发时偏离 Rust；默认 max=SIZE_MAX 不触发 | SeparatorWorker.cpp:253,264-268; LbfSearch.cpp:248 vs search.rs:307-318 |
| 6 | Observer 形式：trait 对象 vs 函数指针 | 差异 | 无行为影响 | search.rs:31-60 vs LbfSearch.cpp:87-103 |
| 7 | Rust `debug!` 日志 → C++ 无日志 | 差异 | 无行为影响 | search.rs:260,311-315 |

## 结论

Rust `search_placement` 的五阶段编排（参照/聚焦/容器/预精化/最终精化）在 C++ 侧被拆为 `SearchLbfImpl`（无参照，三阶段）与 `FindBestSeparatorMoveImpl`（全五阶段）两个内联编排，配合 BestSamples/UniformRectSampler/RefineCoordinateDescent 三个独立组件复现；阶段顺序、上界传递、report 回写、两段精化参数（0.25/0.02、5°/1°、0.01/0.001、0.5°/0.05°）逐项一致，且被 `LbfSearchRustTrace.tsv` 与 `SeparatorSearchRustTrace.tsv` 两个 Rust 参考逐位锁定。C++ 额外提供的 `LocalSearchSampler`（高斯采样 + ShiftMean + DecayStddev）在 Rust sparrow 中没有对应物，属于超出 sparrow 的公共 API 扩展，不参与搜索编排；其余差异均为错误通道与防御性回退等既定形态差异。

### 补充审计（2026-09-29）
1. **【核对确认：343 行逐项覆盖】** search.rs 全量复核：SampleConfig、SearchPhase、SearchObserver trait（3 方法默认空体）、NoopSearchObserver、ObservedEvaluator（set_phase/report/evaluate 转发）、SearchTracePhase+SearchTraceEvent+CallbackSearchObserver+From（oracle-trace 门控）、search_placement、search_placement_with_trace、search_placement_impl（五阶段主体）、prerefine_cd_config、final_refine_cd_config——主文 8 小节 + 差异表全覆盖无遗漏。
2. **【浅覆盖补全：impl 形态与阶段枚举的调用方】** 主文条目 2 已对齐 SearchPhase 枚举；补记：`impl From<SearchPhase> for SearchTracePhase`（search.rs:166-181）把**私有枚举映射到 trace 公有枚举**——C++ 对应物是两个独立的 trace 枚举类型（LbfSearchTrace.h 三值/SeparatorSearchTrace.h 五值），**无 From 桥**（类型即阶段集）——形态差异已隐含在主文条目 2，判定维持。
3. **【浅覆盖补全：n_focussed_samples=0 时 focussed_sampler 仍构造】** Rust `ref_pk=Some` 时**无条件构造** focussed_sampler（search.rs:264-268，构造本身消耗计算但不耗 rng），随后 `for _ in 0..0` 零循环（:274-282）——即 LBF 路径 ref_pk=None 才跳过构造；C++ SeparatorWorker.cpp:188-196 同构（构造+HasValue 检查）。主文条目 3 的"失败则跳过聚焦阶段"指 sampler 构造失败（放不下），n=0 的零循环路径两侧一致。判定维持。
4. **【核对确认：final_refine 的结果不回 report】** 主文条目 4 已确认两侧一致；现场复核 C++ 位置（SeparatorWorker.cpp:253-267 的 refinedBest 回退链 + LbfSearch.cpp:246-248），行号有效。
