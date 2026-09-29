# S03. lbf.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/lbf.rs`（143 行）；损失公式参考 `eval/lbf_evaluator.rs:9-10,48`（X/Y 乘数），搜索算法参考 `sample/search.rs:191-343`
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/LbfLoss.h`（21 行）+ `Source/Optimizer/LbfLoss.cpp`（57 行）+ `Include/SparrowCore/Optimizer/LbfSearch.h`（34 行）+ `Source/Optimizer/LbfSearch.cpp`（296 行）+ `Source/Optimizer/LbfSearchTrace.h`（45 行）；构建流程对应 `SparrowLbfBuilder.cpp`（归 S08 详比）
**判定**: 基本等价（lbf.rs 的三个职责在 C++ 被拆为 LbfLoss【数值包装】、LbfSearch【搜索】、SparrowLbfBuilder【构建循环】三件；搜索流程与损失公式与 Rust 参考公式逐项一致，但损失计算时机、kind 语义、追溯历史三处存在结构差异）

说明：Rust sparrow 没有独立的 "LbfSearch" 模块——`lbf.rs::LBFBuilder` 是唯一入口，它把 `LBFEvaluator`（eval/lbf_evaluator.rs）+ `search_placement`（sample/search.rs，S13 已比）组合起来完成构造。C++ 侧把这一组合的"搜索"半边抽成自由函数 `SearchLbf`，"构造循环"半边抽成 `ConstructSparrowInitialSolution`（SparrowLbfBuilder.cpp，S08 组内比对）。因此本记录以 lbf.rs 的函数为纲，LbfLoss/LbfSearch 给出与 Rust 侧对应片段的比对。

## 函数逐条比对

### `ConstructionError`（lbf.rs:15-30）
- C++ 对应：无独立结构；错误以 `jaguar::ErrorCode::Infeasible` + 消息字符串返回（SparrowLbfBuilder.cpp:26-30 `InfeasibleConstruction`，:169/:176 "width growth limit exceeded"/"width ceiling exceeded"）。
- 判定：一致（既定差异：Rust panic/Error 型 → C++ Result/错误码）。
- 证据与说明：Rust `ConstructionError { item_idx }` 携带失败项索引（lbf.rs:17），C++ 消息不携带项索引，只报原因；错误信息保真度略降，行为方向等价。

### `LBFBuilder::new`（lbf.rs:40-50）
- C++ 对应：`SparrowLbfBuilder.cpp:141-146`（`jaguar::SPProblem::Create(instance)`）+ `SparrowRng rng(config.seed)`（:147）+ `MakeSearchConfig`（:32-44）。
- 判定：一致。
- 证据与说明：Rust `SPProblem::new(instance.clone()).expect(...)`（lbf.rs:41-42）→ C++ `SPProblem::Create` 走 Result。Rust 把 `rng` 和 `sample_config` 存入 builder；C++ 在 `ConstructSparrowInitialSolution` 内用局部 `SparrowRng rng(config.seed)` 与 `LbfConfig searchConfig`（由 `SparrowConstructorConfig` 映射：`samplesPerItem = nContainerSamples`、`nCoordinateDescents`、`widthGrowthRatio`、`maxWidthGrowthAttempts`，SparrowLbfBuilder.cpp:32-44）。Rust 侧 `LBF_SAMPLE_CONFIG = { n_container_samples: 1000, n_focussed_samples: 0, n_coord_descents: 3 }`（consts.rs:51-55），C++ `SparrowConstructorConfig` 默认 `nContainerSamples = 1000`、`nFocussedSamples = 0`、`nCoordinateDescents = 3`（LbfConfig.h:68-76）：默认值一致。

### `LBFBuilder::construct`（lbf.rs:55-86）——排序与构造循环
- C++ 对应：`ConstructSparrowInitialSolution`（SparrowLbfBuilder.cpp:93-229）。
- 判定：基本等价（排序键与展开语义一致；宽度上限与尝试次数上限两处收敛条件不同）。
- 证据与说明：
  1. **排序键**：Rust `(0..n_items).sorted_by_cached_key(|id| Reverse(OrderedFloat(convex_hull_area * diameter)))`（lbf.rs:58-64）——按 `ch_area * diameter` **降序**。C++ `itemOrder` push `{index, key=ConvexHullArea()*Diameter()}`（:119-124），`std::sort` 比较器 `left.key > right.key`、并列时 `left.index < right.index`（:127-134）——同为降序、并列按索引升序（Rust `sorted_by_cached_key` 为稳定排序，等价于索引升序 tie-break）。判定：一致。
  2. **需求展开**：Rust `.flat_map(|id| iter::repeat_n(id, missing_qty))`（lbf.rs:65-68）→ 每个实例项重复 demand 次成排序序列。C++ 不展开列表，而是外层 for 遍历 `itemOrder`、内层 `while (problem.RemainingDemand(ordered.index) > 0U)`（:152-154）。由于 `place_item` 每轮消耗 1 个需求，展开序 = while 序。判定：一致（实现形态不同，序列等价）。
  3. **宽度上限**：Rust `width_limit = 2.0 * Σ(diameter * qty)`（lbf.rs:104-110），`next_width >= width_limit` 即报错。C++ `widthCeiling = Σ(diameter*demand) * config.widthCeilingFactor`（:125，:135），`widthCeilingFactor` 默认 2.0（LbfConfig.h:76）。判定：一致（常量 2.0 → 可配置字段，默认相同）。注意 C++ 的直径/面积在 f64 域累加（`double`，:114-125），Rust 在 f32 域（`sum::<f32>()`，lbf.rs:109）——边界实例的舍入差属既定 f32/f64 差异。
  4. **尝试次数上限**：C++ 额外 `widthAttempts >= config.maxWidthGrowthAttempts`（默认 32，LbfConfig.h:73）先于宽度上限触发（SparrowLbfBuilder.cpp:166-170）；Rust **无此计数**，只看 `next_width >= width_limit`（lbf.rs:111）。判定：C++ 额外。影响：极端实例（宽增长比例极小）下 C++ 可能早于 Rust 终止并报 Infeasible；默认 `widthGrowthRatio=1.2` 下 32 次后宽度达初始 1.2^32≈342 倍，几乎必然先触发 2×Σ 上限，实际路径不改变。
  5. **fit_strip**：Rust `self.prob.fit_strip().expect(...)`（lbf.rs:77-79）→ C++ `problem.FitStrip()`（SparrowLbfBuilder.cpp:197-201）：一致。

### `LBFBuilder::place_item`（lbf.rs:88-122）——增长循环
- C++ 对应：SparrowLbfBuilder.cpp:152-195 的 while 体。
- 判定：基本等价（增长比例 1.2 一致；增长动作在"搜索失败"时的循环结构重组）。
- 证据与说明：
  - Rust：`place_item` 内 `loop { find_placement → place / else next_width = strip_width*1.2; if next_width >= width_limit {Err}; change_strip_width }`（lbf.rs:89-121）。
  - C++：`FindPlacement` 返回 `nullopt` 时执行 `nextWidth = problem.StripWidth() * config.widthGrowthRatio`（:172，`widthGrowthRatio` 默认 1.2，LbfConfig.h:74；Rust 硬编码 1.2 于 lbf.rs:102），检查 `nextWidth > widthCeiling`（:173）后 `ChangeStripWidth` + `++widthAttempts` + `continue`（:178-184）。增长比 1.2、上限判定方向（Rust `>=` vs C++ `>`，因 C++ 另有 `IsFiniteWidth` 检查非有限值）语义一致。
  - Rust 每次失败后 `change_strip_width` 后**在原 loop 内重试同一 item**（含新宽下重新搜索）；C++ `continue` 回到 `while (RemainingDemand > 0)` 重调 `FindPlacement`，同样重试同一 item：一致。

### `LBFBuilder::find_placement`（lbf.rs:124-142）
- C++ 对应：`FindPlacement`（SparrowLbfBuilder.cpp:51-88）+ `SearchLbf`（LbfSearch.cpp:122-273）。
- 判定：基本等价（判定条件 SampleEval::Clear → C++ 采样 kind 语义见下）。
- 证据与说明：Rust 侧 `search_placement(layout, item, None, evaluator, sample_config, rng)`，取 `Some((d_transf, SampleEval::Clear{..}))` 为成功、其余（含 `Invalid`、`Some((_, Collision))`——LBFEvaluator 永不返回 Collision）为失败（lbf.rs:138-141）。C++ `FindPlacement` 调 `SearchLbf`，`nullopt`/无值分别对应失败/错误（SparrowLbfBuilder.cpp:76-87）。C++ `SearchLbf` 成功时返回 `SearchResult{transform, loss, samples, tighteningHistory}`，失败（bestSample 空）返回 `FromValue(std::nullopt)`（LbfSearch.cpp:239-241）。
  - `LBFEvaluator::new(layout, item)`（lbf.rs:127）→ C++ `LbfEvaluator(layout, item, filter)` + 最小质量 HazardFilter（SparrowLbfBuilder.cpp:66-75，Rust 侧用 `NoFilter`——jagua-rs 的 SP 实例未含 InferiorQualityZone 语义，C++ 通过 filter 保留该过滤，属 C++ 额外的质量过滤钩子，evaluator 本体见 S09）。
  - `search_placement` 的完整两阶段（focused/container + 两轮 CD）比对见 S13（sampling/LocalSearchSampler——Rust `ref_pk=None` 时 focused 段整段跳过，search.rs:253-269/274-282）。

### 损失公式：Rust `LBFEvaluator::evaluate_sample`（lbf_evaluator.rs:48）vs `LbfLoss`
- C++ 对应：`LbfLoss`（LbfLoss.h:9-18）+ `LbfEvaluator.cpp:89-108 CalculateLbfLoss`。
- 判定：基本等价（公式一致；聚合形态从"单一 f32"改为"(xMax,yMax) 结构 + Cost()"）。
- 证据与说明：
  - Rust：`loss = X_MULTIPLIER * (poi.0 + bbox_corner.0) + Y_MULTIPLIER * (poi.1 + bbox_corner.1)`，`X_MULTIPLIER=10.0, Y_MULTIPLIER=1.0`（lbf_evaluator.rs:9-10,48）。jagua-rs 的 `SPolygon::bbox.corners()[0]` 是 bbox 左下角，但代码实际用 `poi.0 + bbox_corner.0`…（注意：jagua-rs 的 `corners()[0]` 即 `x_max` 侧——见 C++ 实现取 `bbox.xMax`，LbfEvaluator.cpp:95-96 `bboxX = bbox.xMax`、`bboxY = bbox.yMax`，并经 `SparrowLbfRustReference.tsv`/`LbfSearchRustTrace.tsv` fixture 锁定逐位一致，loss_f32_bits 列）。C++ 公式 `10.0F * (poiX + bboxX) + (poiY + bboxY)`（LbfEvaluator.cpp:102）：数值与乘数一致。
  - C++ `LbfLoss::FromBoundingBox` 只存 `{xMax, yMax}`（LbfLoss.cpp:14-17），`Cost() = xMax*10 + yMax`（:19-26，含非有限→+inf 防御），`operator<` 按 Cost 比较并含 NaN 防御（:43-56）。Rust 侧是裸 f32 + `SampleEval` 的 Ord；C++ 的 NaN/inf 防御为 C++ 额外（防御式，不改变正常路径）。
  - `TightenSampleBBox`（LbfLoss.h:17）：用 `Cost()/10` 收紧样本 bbox 上界，供 UpperBound 早停使用——Rust 侧对应 `best_samples.upper_bound()` 的 SampleEval 比较链（S15），此函数是 C++ 侧的表示适配。判定：C++ 额外（表示层）。

### `SearchLbf` / `SearchLbfImpl`（LbfSearch.cpp:122-273）vs Rust `search_placement`（search.rs:191-319，ref_pk=None 路径）
- C++ 对应：`SearchLbf`/`SearchLbfWithTrace`（LbfSearch.cpp:276-295）→ `SearchLbfImpl`。
- 判定：基本等价（阶段序、BestSamples 参数、两轮 CD 配置逐项一致；差异 3 条如下）。
- 证据与说明：
  1. **阶段集**：Rust `SearchPhase` 五相（ReferencePlacement/FocusedSample/ContainerSample/PreRefine/FinalRefine，search.rs:22-29）；LBF 场景 `ref_pk=None` 只走 ContainerSample→PreRefine→FinalRefine 三相。C++ `LbfSearchTracePhase` 只定义三相（LbfSearchTrace.h:11-16，ContainerSample/PreRefine/FinalRefine），LbfSearch.cpp:215-218 注释明确"no reference placement"。判定：一致（LBF 专用搜索器省去不走的相位；完整五相在 S13/S05 的 SeparatorWorker 侧）。
  2. **容器采样数**：Rust `sample_config.n_container_samples`（=1000）；C++ `config.samplesPerItem`（默认 5000，LbfConfig.h:13）。判定：差异（默认值不同：LbfConfig 是通用求解器配置，默认 5000；经 SparrowLbfBuilder 路径被 `MakeSearchConfig` 覆写为 `nContainerSamples=1000`（SparrowLbfBuilder.cpp:37），故构造路径行为一致；直接调 `SolveSppLbf`（S08）用 5000——对应 jagua-rs 老版 lbf 的默认，fixture `SppLbfCppReference.tsv` 记录 samples=738 即该路径）。
  3. **BestSamples 构造**：`BestSamples::new(max(1, n_coord_descents), item_min_dim * 0.05)`（LbfSearch.cpp:170-172）vs Rust `BestSamples::new(n_coord_descents, item_min_dim * UNIQUE_SAMPLE_THRESHOLD)`（search.rs:242-245，`UNIQUE_SAMPLE_THRESHOLD=0.05`，consts.rs:29）：一致（`max(1,·)` 是 C++ 对 0 的防御，Rust 0 时 upper_bound 恒取 index-1 的逻辑由 config 默认 3 规避）。
  4. **预/终精调参数**：C++ `MakeRefinementConfig(item, false/true)`（LbfSearch.cpp:31-63）：pre = `min_dim*0.25 / min_dim*0.02 / 5° / 1°`，final = `min_dim*0.01 / min_dim*0.001 / 0.5° / 0.05°`，`wiggle = Continuous`。Rust `prerefine_cd_config`/`final_refine_cd_config`（search.rs:321-343）+ consts.rs:17-26（PRE_REFINE_CD_TL_RATIOS=(0.25,0.02)、PRE_REFINE_CD_R_STEPS=(5°,1°)、SND_REFINE_CD_TL_RATIOS=(0.01,0.001)、SND_REFINE_CD_R_STEPS=(0.5°,0.05°)）：逐项一致（角度用 `* π/180` 展开而非 `to_radians`，浮点常量等价）。
  5. **CD 后报告**：Rust pre-refine 对每个 start 下降后 `best_samples.report(descended)`（search.rs:299-303）；C++ `report(refined.Value().first, refined.Value().second)`（LbfSearch.cpp:230-236）：一致。Rust final-refine 结果直接作为返回（search.rs:307-309 `best().map(refine…)`——注意 Rust 无条件 refine best 并返回结果，失败无回退）；C++ `final.HasValue() ? final.Value() : *bestSample`（:246-248）回退到未精调 best。判定：差异（回退路径 C++ 额外；Rust 侧 refine_coord_desc 必返回样本所以无此分支，C++ RefineCoordinateDescent 可失败——防御性，正常路径一致）。
  6. **tighteningHistory（C++ 额外）**：C++ `recordTightening` 在每次被接受的 clear 样本上计算 `TransformedRustF32` 的 bbox→LbfLoss，若优于历史最优则 push（LbfSearch.cpp:176-193，:249）；`SearchResult.tighteningHistory`（LbfSearch.h:25）。Rust 无任何等价物（`search_placement` 只返回 `(final_sample, n_evals)`）。判定：C++ 额外（观测/追溯数据，不影响搜索行为本身——它只读不改 BestSamples）。
  7. **item_min_dim 检查**：Rust 直接 `f32::min(width, height)`（search.rs:240）无校验；C++ 校验非有限/≤0 报错（LbfSearch.cpp:163-168）：C++ 额外（防御）。
  8. **UniformBBoxSampler 失败**：Rust `UniformBBoxSampler::new` 返回 None→`container_sampler=None`→跳过整段采样（search.rs:270-271, 286-293），LbfSearch.cpp:152-160 显式注释了这一对应并返回 `FromValue(std::nullopt)`（区别于 Rust 仍会继续走 CD refine 既有 best——Rust 此时 best_samples 为空、最终返回 None）。判定：差异（边缘路径：容器放不下 item 时 Rust 会继续跑 0 个采样 + 0 个 CD 然后返回 None，C++ 直接返回 nullopt；两者最终都向调用方报"无可行放置"，触发上层宽度增长，行为等价）。
  9. **fixture 锁定**：`LbfSearchRustTrace.tsv`（`# generator=Sparrow Rust search_placement with LBFEvaluator`，seed=42，n_container_samples=16/n_focussed_samples=0/n_coord_descents=2，逐 eval/report keep 事件的 f32 位级 trace）与 `LbfSearchReference.tsv`、`LbfSeed42Reference.tsv`（Rust rand 0.10 Xoshiro256++ 位级）锁定该路径的采样序与 loss 位级等价。

### `TracingEvaluator`（LbfSearch.cpp:70-115）vs Rust `ObservedEvaluator`（search.rs:62-99）
- 判定：一致（trace 观察者包装，C++ 以 `LbfSearchTraceCallback` 对应 Rust `oracle-trace` feature 的 `SearchTraceEvent`，search.rs:101-175；事件三型 Evaluation/Report/Result 对应 LbfSearchTrace.h:19-34）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 模块拆分：lbf.rs 单体 → LbfLoss + LbfSearch + SparrowLbfBuilder 三件 | 差异（结构重组） | 职责等价但 C++ 把搜索做成可复用自由函数（SeparatorWorker/SppLbf/BppLbf 共用） | lbf.rs:32-143 vs LbfSearch.h:28-34 + SparrowLbfBuilder.cpp:93 |
| 2 | 宽度增长尝试计数 `maxWidthGrowthAttempts=32` | C++额外 | 极端配置下早停为 Infeasible；默认 1.2^32 远超 2×Σ 上限，不触发 | lbf.rs:102-111（无计数） vs SparrowLbfBuilder.cpp:166-170 + LbfConfig.h:73 |
| 3 | `samplesPerItem` 默认 5000 vs Rust `n_container_samples=1000` | 差异 | 构造路径经 MakeSearchConfig 覆写为 1000 后等价；直接用 LbfConfig 的 SolveSppLbf/BppLbf 路径默认采样数不同 | consts.rs:51-55 vs LbfConfig.h:13 + SparrowLbfBuilder.cpp:37 |
| 4 | final-refine 失败回退到未精调 best | C++额外 | 仅在 RefineCoordinateDescent 报错时生效；Rust refine 必成功 | search.rs:307-309 vs LbfSearch.cpp:246-248 |
| 5 | `tighteningHistory` 收紧历史 | C++额外 | 纯观测数据（fixture LbfSearchRustTrace 的 keep 事件对应物），不影响搜索决策 | lbf.rs（无）vs LbfSearch.cpp:176-193,249 + LbfSearch.h:25 |
| 6 | 损失表示：裸 f32 → LbfLoss{xMax,yMax}+Cost()+NaN/inf 防御 | 差异（表示层） | 正常路径数值一致（fixture 位级锁定）；异常路径 C++ 有序化防御 | lbf_evaluator.rs:48 vs LbfLoss.cpp:19-26,43-56 |
| 7 | `LocalSearchFraction` 等 LbfConfig 字段 | C++额外 | 保留源兼容但当前搜索不用（LbfSearch.cpp:215-217 注释自认）；SolveSppLbf 只用它算 sampleCount 统计（SppLbfOptimizer.cpp:121-124） | LbfConfig.h:18-25 vs lbf.rs（无） |
| 8 | 容器放不下 item 时直接 nullopt vs Rust 跑空循环后 None | 差异（边缘） | 两者均触发上层宽度增长，终态等价 | search.rs:270-293 vs LbfSearch.cpp:152-160 |
| 9 | 最小质量 HazardFilter（InferiorQualityZone） | C++额外 | Rust 用 NoFilter；C++ 在构造搜索时过滤低质量区（SparrowLbfBuilder.cpp:66-75） | lbf.rs:124-136 vs SparrowLbfBuilder.cpp:66-75 |
| 10 | panic→Result、expect→错误码 | 一致（既定差异） | lbf.rs:41/77/118 三处 expect | lbf.rs:42,79,120 vs SparrowLbfBuilder.cpp:141,197,178 |

## 结论
LBF 构造器的核心行为——`ch_area*diameter` 降序、逐需求放置、1.2 倍宽度增长、2×Σ直径上限、1000 容器采样 + 3 候选两阶段 CD（0.25/0.02/5°/1° → 0.01/0.001/0.5°/0.05°）、损失公式 `10*(poi.x+bbox.xMax)+(poi.y+bbox.yMax)`——在 C++ 侧逐项复现且被 `LbfSearchRustTrace.tsv`/`SparrowLbfRustReference.tsv` 位级锁定，判定基本等价。真正的差异全部是结构性的：C++ 把搜索半边抽成可复用的 `SearchLbf`（带 tighteningHistory 观测与防御性回退），把 1.2/2.0/尝试上限等 Rust 硬编码提为配置字段，并新增 32 次增长尝试上限与质量区过滤；`samplesPerItem` 默认 5000 与 Rust 1000 的出入只在绕过 SparrowLbfBuilder 的求解器入口（S08）可见。
