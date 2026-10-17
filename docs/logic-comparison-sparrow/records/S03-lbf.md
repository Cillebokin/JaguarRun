# S03. lbf.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/lbf.rs`（143 行）；损失公式参考 `eval/lbf_evaluator.rs:9-10,48`（X/Y 乘数），搜索算法参考 `sample/search.rs:191-343`
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/LbfLoss.h`（21 行）+ `Source/Optimizer/LbfLoss.cpp`（57 行）+ `Include/SparrowCore/Optimizer/LbfSearch.h`（34 行）+ `Source/Optimizer/LbfSearch.cpp`（296 行）+ `Source/Optimizer/LbfSearchTrace.h`（45 行）；构建流程对应 `SparrowLbfBuilder.cpp`（归 S08 详比）
**判定**: 基本等价（固定参考矩阵已完成：构造排序键、宽度上限求值域和等号停止条件已对齐 Rust；零样本、构造失败、超过 32 次扩宽、多实例、默认 `1000/0/3` 完整 builder 输出及 `2.0f` 前一/精确/后一 ULP 初始宽度均有 Rust/C++ reference。默认 Sparrow 构造路径不设 Rust 无对应的尝试次数上限；显式正数上限保留为 C++ 扩展。通用 `LbfConfig` 的直接 `SolveSppLbf` 路径仍默认 cap=32，属于单独的 C++ API 行为）

说明：Rust sparrow 没有独立的 "LbfSearch" 模块——`lbf.rs::LBFBuilder` 是唯一入口，它把 `LBFEvaluator`（eval/lbf_evaluator.rs）+ `search_placement`（sample/search.rs，S13 已比）组合起来完成构造。C++ 侧把这一组合的"搜索"半边抽成自由函数 `SearchLbf`，"构造循环"半边抽成 `ConstructSparrowInitialSolution`（SparrowLbfBuilder.cpp，S08 组内比对）。因此本记录以 lbf.rs 的函数为纲，LbfLoss/LbfSearch 给出与 Rust 侧对应片段的比对。

## 函数逐条比对

### `ConstructionError`（lbf.rs:15-30）
- C++ 对应：无独立结构；失败以 `jaguar::ErrorCode::Infeasible` + 消息字符串返回（SparrowLbfBuilder.cpp:27-31 `InfeasibleConstruction`，:173/:183）。
- 判定：失败项与说明文本已对齐；错误类型仍有 API 形态差异。
- 证据与说明：Rust `ConstructionError { item_idx }` 携带失败项索引（lbf.rs:17），其 Display 为 `could not construct an initial placement for item N`。C++ 在默认宽度增长/上限失败分支现返回相同文本并保留索引，错误码为 `Infeasible`。`RustSppConstructionFailure.json` / `OptimizeConstructionFailureRustReference.tsv` 用单件 2×2 方件、条带高 1.9 验证 item 0 和零 listener 回调；该错误表示 LBF 启发式到达宽度限制，不证明数学不可行。Rust 的 `ConstructionError` 类型与 C++ 的通用 `Result<..., ErrorCode::Infeasible>` 仍不相同。

### `LBFBuilder::new`（lbf.rs:40-50）
- C++ 对应：`SparrowLbfBuilder.cpp:145-150`（`jaguar::SPProblem::Create(instance)`）+ `SparrowRng rng(config.seed)`（:151）+ `MakeSearchConfig`（:34-46）。
- 判定：一致。
- 证据与说明：Rust `SPProblem::new(instance.clone()).expect(...)`（lbf.rs:41-42）→ C++ `SPProblem::Create` 走 Result。Rust 把 `rng` 和 `sample_config` 存入 builder；C++ 在 `ConstructSparrowInitialSolution` 内用局部 `SparrowRng rng(config.seed)` 与 `LbfConfig searchConfig`（由 `SparrowConstructorConfig` 映射：`samplesPerItem = nContainerSamples`、`nCoordinateDescents`、`widthGrowthRatio`、`maxWidthGrowthAttempts`，SparrowLbfBuilder.cpp:34-46）。Rust 侧 `LBF_SAMPLE_CONFIG = { n_container_samples: 1000, n_focussed_samples: 0, n_coord_descents: 3 }`（consts.rs:51-55），C++ `SparrowConstructorConfig` 默认 `nContainerSamples = 1000`、`nFocussedSamples = 0`、`nCoordinateDescents = 3`（LbfConfig.h:68-76）：默认值一致。但 builder 仅映射 `nContainerSamples` 和 `nCoordinateDescents`；`nFocussedSamples` 仍是无效字段：正数设置不增加采样。`nContainerSamples=0` 现作为零次容器采样通过校验并返回无候选，Rust `search_placement(ref_pk=None)` 对应零循环后返回 None；不能用未映射的 `nFocussedSamples` 代替 Rust LBF 配置。Rust LBF 默认 focused=0，因此默认路径不受影响；C++ 可配置 API 的 focused 字段行为仍与名称不符。

### `LBFBuilder::construct`（lbf.rs:55-86）——排序与构造循环
- C++ 对应：`ConstructSparrowInitialSolution`（SparrowLbfBuilder.cpp:95-238）。
- 判定：基本等价（构造排序键和宽度上限语义已按 Rust 对齐；需求展开等价；默认 Sparrow 构造器不设尝试次数上限，显式正数 cap 属于 C++ 扩展；通用 `SolveSppLbf` 入口仍保留 `LbfConfig` 的默认 cap=32）。
- 证据与说明：
  1. **排序键**：Rust `(0..n_items).sorted_by_cached_key(|id| Reverse(OrderedFloat(convex_hull_area * diameter)))`（lbf.rs:58-64）按 `ch_area * diameter` f32 **降序**，稳定排序；C++ 现将 surrogate area 与 diameter 转为 `float` 后以 f32 相乘（SparrowLbfBuilder.cpp:119-128），比较器按降序、键相等时按 index 升序（:130-137），对应 Rust 输入序 tie。2026-10-06 的定向输入使 item 0/1 的 Rust f32 键相同、C++ 原 double 键不同：Rust 构造先放 item 0；修复前 C++ 先放 item 1，修复后 C++ 保留 item 0。结论：本次已修复已覆盖的 f32 排序边界；更广泛几何域仍由类型路径和排序规则对齐支撑，不能外推为所有输入逐位证明。
  2. **需求展开**：Rust `.flat_map(|id| iter::repeat_n(id, missing_qty))`（lbf.rs:65-68）→ 每个实例项重复 demand 次成排序序列。C++ 不展开列表，而是外层 for 遍历 `itemOrder`、内层 `while (problem.RemainingDemand(ordered.index) > 0U)`（:156-158）。由于 `place_item` 每轮消耗 1 个需求，展开序 = while 序。判定：一致（实现形态不同，序列等价）。
  3. **宽度上限**：Rust `width_limit = 2.0 * Σ(diameter * qty)`（lbf.rs:104-110），`next_width`、乘法与求和均为 f32，并在 `next_width >= width_limit` 时返回构造错误；C++ 现将直径、demand、growth ratio 与 ceiling factor 转为 f32，并用 f32 求和/乘法及 `>=` 比较（SparrowLbfBuilder.cpp:110-194）。默认 factor/growth ratio 分别为 2.0/1.2，与 Rust 常量对应。C++ 仍允许调用方改这两个参数，Rust 构造器没有相同配置项，parity 用例必须使用默认值。
  4. **尝试次数上限（区分入口）**：Rust 只在 `next_width >= width_limit` 时返回构造错误（lbf.rs:102-111），没有尝试次数计数。Rust-compatible 的 `SparrowLbfBuilder` 路径使用 `SparrowConstructorConfig.maxWidthGrowthAttempts`，默认 0 表示不设次数上限；显式正数才由 `widthAttempts >= config.maxWidthGrowthAttempts` 在宽度上限前终止（SparrowCore/Include/SparrowCore/LbfConfig.h:70-73；SparrowLbfBuilder.cpp:171-177）。通用 `LbfConfig.maxWidthGrowthAttempts` 则仍默认 32（LbfConfig.h:23），直接 `SolveSppLbf` 无零值表示“无限”的分支，会在计数达到配置值时返回（SppLbfOptimizer.cpp:125-127）；这条较低层 API 的默认 cap 是尚存的 C++ 入口差异，不应由 Sparrow constructor 的默认值推及。全局计数跨 item 累加（SparrowLbfBuilder.cpp:153-154,171-194）。
  5. **fit_strip**：Rust `self.prob.fit_strip().expect(...)`（lbf.rs:77-79）→ C++ `problem.FitStrip()`（SparrowLbfBuilder.cpp:207-211）：一致。

### `LBFBuilder::place_item`（lbf.rs:88-122）——增长循环
- C++ 对应：SparrowLbfBuilder.cpp:156-195 的 item-placement 循环。
- 判定：基本等价（增长比例 1.2 一致；增长动作在"搜索失败"时的循环结构重组）。
- 证据与说明：
  - Rust：`place_item` 内 `loop { find_placement → place / else next_width = strip_width*1.2; if next_width >= width_limit {Err}; change_strip_width }`（lbf.rs:89-121）。
  - C++：`FindPlacement` 返回 `nullopt` 时将当前宽度和 growth ratio 转成 f32 相乘，检查有限性后以 `nextWidth >= widthCeiling` 判定上限，再把该 f32 值传给 `ChangeStripWidth`（SparrowLbfBuilder.cpp:177-194）。默认增长比 1.2 与 Rust 对应；显式有限性检查是 C++ 防御，默认不设尝试次数上限，正数 cap 仅为 C++ 扩展。定向边界测试将 growth ratio 设为使下一宽度恰等于上限，确认旧 `>` 允许继续、现行 `>=` 返回 Infeasible；冷启动错误 fixture 还核对 item index 与回调前置出口。
  - Rust 每次失败后 `change_strip_width` 后**在原 loop 内重试同一 item**（含新宽下重新搜索）；C++ `continue` 回到 `while (RemainingDemand > 0)` 重调 `FindPlacement`，同样重试同一 item：一致。

### `LBFBuilder::find_placement`（lbf.rs:124-142）
- C++ 对应：`FindPlacement`（SparrowLbfBuilder.cpp:53-90）+ `SearchLbf`（LbfSearch.cpp:122-273）。
- 判定：基本等价（判定条件 SampleEval::Clear → C++ 采样 kind 语义见下）。
- 证据与说明：Rust 侧 `search_placement(layout, item, None, evaluator, sample_config, rng)`，取 `Some((d_transf, SampleEval::Clear{..}))` 为成功、其余（含 `Invalid`、`Some((_, Collision))`——LBFEvaluator 永不返回 Collision）为失败（lbf.rs:138-141）。C++ `FindPlacement` 调 `SearchLbf`，`nullopt`/无值分别对应失败/错误（SparrowLbfBuilder.cpp:78-90）。C++ `SearchLbf` 成功时返回 `SearchResult{transform, loss, samples, tighteningHistory}`，失败（bestSample 空）返回 `FromValue(std::nullopt)`（LbfSearch.cpp:239-241）。
  - `LBFEvaluator::new(layout, item)`（lbf.rs:127）→ C++ `LbfEvaluator(layout, item, filter)` + 最小质量 HazardFilter（SparrowLbfBuilder.cpp:68-76，Rust 侧用 `NoFilter`——jagua-rs 的 SP 实例未含 InferiorQualityZone 语义，C++ 通过 filter 保留该过滤，属 C++ 额外的质量过滤钩子，evaluator 本体见 S09）。
  - `search_placement` 的完整两阶段（focused/container + 两轮 CD）比对见 S13（C++ 搜索编排位于 Optimizer/LbfSearch 与 Optimizer/SeparatorWorker；Rust `ref_pk=None` 时 focused 段整段跳过，search.rs:253-269/274-282）。

### 损失公式：Rust `LBFEvaluator::evaluate_sample`（lbf_evaluator.rs:48）vs `LbfLoss`
- C++ 对应：`LbfLoss`（LbfLoss.h:9-18）+ `LbfEvaluator.cpp:89-108 CalculateLbfLoss`。
- 判定：基本等价（公式一致；聚合形态从"单一 f32"改为"(xMax,yMax) 结构 + Cost()"）。
- 证据与说明：
  - Rust：`loss = X_MULTIPLIER * (poi.0 + bbox_corner.0) + Y_MULTIPLIER * (poi.1 + bbox_corner.1)`，`X_MULTIPLIER=10.0, Y_MULTIPLIER=1.0`（lbf_evaluator.rs:9-10,48）。jagua-rs 的 `SPolygon::bbox.corners()[0]` 实际是 `(x_max, y_max)`（见 rect.rs:161-169）；C++ 同样取 `bbox.xMax/yMax`（LbfEvaluator.cpp:95-96），公式为 `10.0F * (poiX + bboxX) + (poiY + bboxY)`（LbfEvaluator.cpp:102）。这是源码公式对应；相关 Rust fixtures 的 loss 由测试转成 float 后按容差比较，不是由 `loss_f32_bits` 文本逐位断言。
  - C++ `LbfLoss::FromBoundingBox` 只存 `{xMax, yMax}`（LbfLoss.cpp:14-17），`Cost() = xMax*10 + yMax`（:19-26，含非有限→+inf 防御），`operator<` 按 Cost 比较并含 NaN 防御（:43-56）。Rust 侧是裸 f32 + `SampleEval` 的 Ord；C++ 的 NaN/inf 防御为 C++ 额外（防御式，不改变正常路径）。
  - `TightenSampleBBox`（LbfLoss.h:17）：用 `Cost()/10` 收紧样本 bbox 上界，供 UpperBound 早停使用——Rust 侧对应 `best_samples.upper_bound()` 的 SampleEval 比较链（S15），此函数是 C++ 侧的表示适配。判定：C++ 额外（表示层）。

### `SearchLbf` / `SearchLbfImpl`（LbfSearch.cpp:122-273）vs Rust `search_placement`（search.rs:191-319，ref_pk=None 路径）
- C++ 对应：`SearchLbf`/`SearchLbfWithTrace`（LbfSearch.cpp:276-295）→ `SearchLbfImpl`。
- 判定：基本等价（阶段序、BestSamples 参数、两轮 CD 配置逐项一致；差异 3 条如下）。
- 证据与说明：
  1. **阶段集**：Rust `SearchPhase` 五相（ReferencePlacement/FocusedSample/ContainerSample/PreRefine/FinalRefine，search.rs:22-29）；LBF 场景 `ref_pk=None` 只走 ContainerSample→PreRefine→FinalRefine 三相。C++ `LbfSearchTracePhase` 只定义三相（LbfSearchTrace.h:11-16，ContainerSample/PreRefine/FinalRefine），LbfSearch.cpp:215-218 注释明确"no reference placement"。判定：一致（LBF 专用搜索器省去不走的相位；完整五相在 S13/S05 的 SeparatorWorker 侧）。
  2. **容器采样数**：Rust `sample_config.n_container_samples`（=1000）；C++ `config.samplesPerItem`（默认 5000，LbfConfig.h:13）。判定：差异（默认值不同：LbfConfig 是通用求解器配置，默认 5000；经 SparrowLbfBuilder 路径被 `MakeSearchConfig` 覆写为 `nContainerSamples=1000`（SparrowLbfBuilder.cpp:39），故构造路径行为一致；直接调 `SolveSppLbf`（S08）用 5000——对应 jagua-rs 老版 lbf 的默认，fixture `SppLbfCppReference.tsv` 记录 samples=738 即该路径）。
  3. **BestSamples 构造**：C++ 现为 `BestSamples(config.nCoordinateDescents, item_min_dim * 0.05)`（LbfSearch.cpp；SeparatorWorker.cpp）vs Rust `BestSamples::new(n_coord_descents, item_min_dim * UNIQUE_SAMPLE_THRESHOLD)`（search.rs:242-245，`UNIQUE_SAMPLE_THRESHOLD=0.05`，consts.rs:29）。传值与比例一致；当容量为 0 时两侧保留 Rust profile-specific 行为：Debug 在 upper_bound 的减法处失败（Rust panic/C++ underflow_error），Release 会因 wrapping 接受候选并可能突破容量。真实 Search 调用路径已在 Rust/C++ Debug/Release 测试中覆盖（详见 S13/S15）。
  4. **预/终精调参数**：C++ `MakeRefinementConfig(item, false/true)`（LbfSearch.cpp:31-63）：pre = `min_dim*0.25 / min_dim*0.02 / 5° / 1°`，final = `min_dim*0.01 / min_dim*0.001 / 0.5° / 0.05°`，`wiggle = Continuous`。Rust `prerefine_cd_config`/`final_refine_cd_config`（search.rs:321-343）+ consts.rs:17-26（PRE_REFINE_CD_TL_RATIOS=(0.25,0.02)、PRE_REFINE_CD_R_STEPS=(5°,1°)、SND_REFINE_CD_TL_RATIOS=(0.01,0.001)、SND_REFINE_CD_R_STEPS=(0.5°,0.05°)）：逐项一致（角度用 `* π/180` 展开而非 `to_radians`，浮点常量等价）。
  5. **CD 后报告**：Rust pre-refine 对每个 start 下降后 `best_samples.report(descended)`（search.rs:299-303）；C++ `report(refined.Value().first, refined.Value().second)`（LbfSearch.cpp:230-236）：一致。Rust final-refine 结果直接作为返回（search.rs:307-309 `best().map(refine…)`——注意 Rust 无条件 refine best 并返回结果，失败无回退）；C++ `final.HasValue() ? final.Value() : *bestSample`（:246-248）回退到未精调 best。判定：差异（回退路径 C++ 额外；Rust 侧 refine_coord_desc 必返回样本所以无此分支，C++ RefineCoordinateDescent 可失败——防御性，正常路径一致）。
  6. **tighteningHistory（C++ 额外）**：C++ `recordTightening` 在每次被接受的 clear 样本上计算 `TransformedRustF32` 的 bbox→LbfLoss，若优于历史最优则 push（LbfSearch.cpp:176-193，:249）；`SearchResult.tighteningHistory`（LbfSearch.h:25）。Rust 无任何等价物（`search_placement` 只返回 `(final_sample, n_evals)`）。判定：C++ 额外（观测/追溯数据，不影响搜索行为本身——它只读不改 BestSamples）。
  7. **item_min_dim 检查**：Rust 直接 `f32::min(width, height)`（search.rs:240）无校验；C++ 校验非有限/≤0 报错（LbfSearch.cpp:163-168）：C++ 额外（防御）。
  8. **UniformBBoxSampler 失败**：Rust `UniformBBoxSampler::new` 返回 None→`container_sampler=None`→跳过整段采样（search.rs:270-271, 286-293），LbfSearch.cpp:152-160 显式注释了这一对应并返回 `FromValue(std::nullopt)`（区别于 Rust 仍会继续走 CD refine 既有 best——Rust 此时 best_samples 为空、最终返回 None）。判定：差异（边缘路径：容器放不下 item 时 Rust 会继续跑 0 个采样 + 0 个 CD 然后返回 None，C++ 直接返回 nullopt；两者最终都向调用方报"无可行放置"，触发上层宽度增长，行为等价）。
  9. **fixture 覆盖**：`LbfSearchRustTrace.tsv` 的配置为 seed=42、16 个 container samples、0 focused samples、2 次 coordinate descent；TestSampling.cpp 对 transform/loss 分别用 2e-5/2e-3 容差，kind、事件与计数按测试断言精确比较。`SparrowLbfRustReference.tsv` 是 64/0/1 的单件 Rust reference。`SparrowLbfMultiInstanceRustReference.tsv` 对 seed=42、64/0/1 的四个输入（单件、三件相同方件、异型两件、三件需求边界）逐个比较 placed/remaining/feasible、width/density 和按 item index 对齐的 placements；width/density 容差 4e-3/1e-3，rotation 1e-6、translation 5e-5。新增 `SparrowLbfDefaultBuilderRustReference.tsv` 对 seed=42 的默认 `1000/0/3` 配置比较三件方件完整 builder 输出，并比较单件初始条带宽度 `2.0f` 前一、精确、后一 ULP 三例；配置、初始 width bits、placed/remaining/feasible、最终 width/density 和逐件 placements 均纳入断言，width/density 容差 4e-3/1e-3，rotation 1e-6、translation 5e-5。RustReference Debug/Release 直接运行当前 `LBFBuilder` 并核对 TSV；C++ `TestSparrowRustDefaultLbfBuilderParity` 执行对应 builder。`SppLbfCppReference.tsv` 是 C++ 自参考，不是 Rust 对照；这些固定 fixture 不证明其他输入域或完整 optimize→compress。

### `TracingEvaluator`（LbfSearch.cpp:70-115）vs Rust `ObservedEvaluator`（search.rs:62-99）
- 判定：一致（trace 观察者包装，C++ 以 `LbfSearchTraceCallback` 对应 Rust `oracle-trace` feature 的 `SearchTraceEvent`，search.rs:101-175；事件三型 Evaluation/Report/Result 对应 LbfSearchTrace.h:19-34）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 模块拆分：lbf.rs 单体 → LbfLoss + LbfSearch + SparrowLbfBuilder 三件 | 差异（结构重组） | 职责等价但 C++ 把搜索做成可复用自由函数（SeparatorWorker/SppLbf/BppLbf 共用） | lbf.rs:32-143 vs LbfSearch.h:28-34 + SparrowLbfBuilder.cpp:95 |
| 2 | Sparrow constructor 与通用 `SolveSppLbf` 的宽度增长尝试 cap | Sparrow constructor 默认与 Rust 一致；通用 SPP solver 默认有 C++ cap | `SparrowConstructorConfig.maxWidthGrowthAttempts=0` 表示无次数上限，显式正数是扩展；通用 `LbfConfig` 默认 32，`SolveSppLbf` 按计数硬性退出，0 不表示无限 | lbf.rs:102-111 vs LbfConfig.h:23,70-73; SparrowLbfBuilder.cpp:171-177; SppLbfOptimizer.cpp:125-127 |
| 3 | `samplesPerItem` 默认 5000 vs Rust `n_container_samples=1000` | 差异 | 构造路径经 MakeSearchConfig 覆写为 1000 后等价；直接用 LbfConfig 的 SolveSppLbf/BppLbf 路径默认采样数不同 | consts.rs:51-55 vs LbfConfig.h:13 + SparrowLbfBuilder.cpp:39 |
| 4 | final-refine 失败回退到未精调 best | C++额外 | 仅在 RefineCoordinateDescent 报错时生效；Rust refine 必成功 | search.rs:307-309 vs LbfSearch.cpp:246-248 |
| 5 | `tighteningHistory` 收紧历史 | C++额外 | 纯观测数据（fixture LbfSearchRustTrace 的 keep 事件对应物），不影响搜索决策 | lbf.rs（无）vs LbfSearch.cpp:176-193,249 + LbfSearch.h:25 |
| 6 | 损失表示：裸 f32 → LbfLoss{xMax,yMax}+Cost()+NaN/inf 防御 | 差异（表示层） | 正常路径公式对应；现有 trace 按容差比较，异常路径 C++ 有序化防御 | lbf_evaluator.rs:48 vs LbfLoss.cpp:19-26,43-56 |
| 7 | `LocalSearchFraction` 等 LbfConfig 字段 | C++额外 | 保留源兼容但当前搜索不用（LbfSearch.cpp:215-217 注释自认）；SolveSppLbf 只用它算 sampleCount 统计（SppLbfOptimizer.cpp:121-124） | LbfConfig.h:18-25 vs lbf.rs（无） |
| 8 | 容器放不下 item 时直接 nullopt vs Rust 跑空循环后 None | 差异（边缘） | 两者均触发上层宽度增长，终态等价 | search.rs:270-293 vs LbfSearch.cpp:152-160 |
| 9 | 最小质量 HazardFilter（InferiorQualityZone） | C++扩展；Sparrow SPP 输入不可达 | C++ 构造搜索保留质量区过滤钩子；Rust `SPInstance` 和 C++ Sparrow SPP 都不能表达该 hazard。不得把 BPP/其他入口合成 Rust parity 输入 | lbf.rs:124-136; SparrowLbfBuilder.cpp:68-76; 本记录补充核验 |
| 10 | panic→Result、expect→错误码 | 一致（既定差异） | lbf.rs:41/77/118 三处 expect | lbf.rs:42,79,120 vs SparrowLbfBuilder.cpp:148,207,236 |
| 11 | 排序键精度域：C++ 现转成 f32 相乘，平局按输入索引保留 | 已修复（Rust LBFBuilder 与 C++ builder 定向回归通过） | 固定 near-tie 输入下 Rust/C++ 均先处理 item 0；未由此证明任意几何输入的完整排料结果一致 | lbf.rs:58-68; `Tests/RustReference/tests/lbf_order_reference.rs`; SparrowLbfBuilder.cpp:110-137; Tests/TestSppLbf.cpp |
| 12 | 宽度上限：C++ 现以 f32 累加/增长并采用 Rust 的 `>=` | 已修复（等号边界回归通过） | 下一宽度恰等于上限时在扩宽前停止；测试用可配置 growth ratio 精确触发该分支 | lbf.rs:89-121; SparrowLbfBuilder.cpp:110-194; Tests/TestSppLbf.cpp |
| 13 | `SparrowConstructorConfig::nFocussedSamples` 未映射到实际 LBF 搜索 | 【C++ 配置缺陷/差异】 | Rust LBF 固定 focused=0；默认一致，但自定义 C++ 正数 focused 设置不增加采样。零 container samples 现在通过校验并按零循环返回无候选 | LbfConfig.h:68-85; SparrowLbfBuilder.cpp:34-46; LbfSearch.cpp vs consts.rs:51-55 |

## 结论
Sparrow LBF 构造器的排序键、宽度增长边界和默认扩宽终止条件已有定向核对；新增 reference 将固定矩阵扩至默认 `1000/0/3` 的三件方件完整构造，以及 `2.0f` 前一/精确/后一 ULP 的单件初始宽度。加上 64/0/1 四实例、多实例摆放序、零样本、item 0 构造失败、超过 32 次扩宽成功和等号上限边界，S03 按列明固定参考矩阵完成；width/density 与 transform 仍按上文容差比较。该结论不证明其他几何/实例分布或完整 optimize→compress。Sparrow constructor 默认不设增长次数 cap，但通用 `LbfConfig` 仍默认 32，直接 `SolveSppLbf` 的该路径尚未与 Rust 无次数上限语义对齐；显式正数 constructor cap、在 Sparrow SPP 输入域不可达的质量区过滤钩子、未映射的 focused 配置、最终精调失败回退与观测历史仍是 C++ 扩展/实现差异。`samplesPerItem` 默认 5000 与 Rust 1000 的出入也只在绕过 SparrowLbfBuilder 的求解器入口（S08）可见。

### 补充审计（2026-10-17：默认预算与相邻初始宽度）

新增 `SparrowLbfDefaultBuilderRustReference.tsv`，由 RustReference 直接调用 `LBFBuilder::construct` 生成。seed=42、默认 `SampleConfig {1000, 0, 3}` 的三件 2×2 方件用例比较初始宽度、完整摆放序、placed/remaining/feasible 和最终 width/density；另用单件 2×2 方件，分别将初始条带宽度设为 `0x3fffffff`、`0x40000000`、`0x40000001`，比较三条完整构造结果。Rust 与 C++ 对配置、初始宽度位模式、物品身份精确比较；width/density 容差分别为 `4e-3`/`1e-3`，rotation 为 `1e-6`，translation 为 `5e-5`。fixture SHA-256 为 `DF7AFB571333C59B62FC29CEAF23BE739219E75FD5AB6AE1917E0A4481E6CAAA`。RustReference Debug/Release 全量验证及 SparrowCore C++ Debug/Release 全量 runner 均通过。S03 按列明固定输入矩阵完成；不对其他几何、实例分布、平台或完整 `optimize → compress` 外推。

### 补充核验（2026-10-09）

1. **零采样与构造失败**：Rust Debug/Release `zero_container_samples_return_the_same_unplaced_item_error` 与 C++ `TestSparrowBuilderZeroContainerSamplesMatchesRustError` 对照 `nContainerSamples=0`。两侧均不放置唯一零件，返回 item index 0 的构造失败；C++ `Infeasible` 的显示文本与 Rust `ConstructionError` 文本一致。该配置验证零循环的出口，不代表默认 1000/0/3 的完整构造结果。
2. **focused 字段边界**：C++ `TestSparrowBuilderFocusedSamplesStayAtRustZero` 比较 `nFocussedSamples=0` 与正值设置，样本和结果不变，确认 Sparrow builder 未把此 C++ 配置字段映射到 Rust LBF 搜索。Rust LBF 的对应配置固定为 0；该字段应作为 C++ API 配置缺陷/扩展边界记录，不能作为 Rust 参数参与 parity。
3. **质量区范围**：Rust `SPInstance` 只持有 items 与 `Strip`，而 `Strip -> Container` 在 `CodeResource/jagua-rs/jagua-rs/src/probs/spp/entities/strip.rs:37-51` 明确传入空 quality-zone 列表；SparrowCore 的 `jaguar::Strip::CreateContainer` 同样构造无 quality zone 的条带容器。没有合法 Sparrow SPP 的 Rust `NoFilter` 对照触发点；BPP/其他 C++ 入口的质量区能力继续作为扩展，不声称存在跨语言逐例等价证据。
4. **扩宽超过 32 次**：共享输入 `SparrowLbfGrowthAttempt.json` 的 Rust seed=42、1/0/1 参考从初始宽度 1.0 出发，需要超过 32 次宽度扩张后才放得下唯一零件。旧 C++ `SparrowConstructorConfig` 默认 cap=32 在该用例提前返回 item 0 的构造失败；改为该配置默认 0 表示 Sparrow builder 不设次数上限后，Rust/C++ 均成功，`SparrowLbfGrowthAttemptRustReference.tsv` 精确比较最终宽度和位姿 f32 位模式。通用 `LbfConfig` 及直接 `SolveSppLbf` 的默认 cap=32 未改变，也未由此 fixture 覆盖。

### 补充审计（2026-10-08：多实例 LBF 成对结果）
`optimizer_multi_instance_lbf_reference.rs` 与 `TestSparrowRustMultiInstanceLbfParity` 共用 seed=42、64 container samples、0 focused samples、1 coordinate descent，按 item index 比较四个共享 JSON 输入的布局。RustReference 先由 Rust 当前源码直接生成 reference，C++ 测试再核对同 seed 的 builder 输出；所有四例均全部放置且可行，布局变换和 width/density 在记录容差内匹配。样本只覆盖 LBF 初始构造阶段，不能代表探索、压缩、自然时限或总体实例分布。

### 补充审计（2026-09-29）
1. **【核对确认：143 行逐项覆盖】** lbf.rs 全量复核：ConstructionError（结构体+Display+Error impl）、LBFBuilder::new、construct、place_item、find_placement——主文 5 小节全覆盖无遗漏；`debug!` 的 4 处调用（:71/:80-84/:92-98/:114-117）属观测层（S01 差异 #4 已记 C++ 无日志）。
2. **【2026-10-06 更正：宽度上限边界】** Rust 使用 `>=`、旧 C++ 使用 `>`；本轮已把 C++ 比较改为 `>=`，并以自定义增长比在实际 builder 中触发等号边界，确认旧代码继续搜索、修复后返回 Infeasible。
3. **【浅覆盖补全：f32/f64 求和域差异的位置】** Rust 的 `width_limit = 2.0 * Σ_f32(diameter * qty as f32)` 与排序键 `Reverse(OrderedFloat(convex_hull_area * diameter))` 均为 f32；本轮 C++ 已将排序键、上限累加、growth ratio 乘法改为 f32。RustReference 的近邻键实例验证 Rust 的 f32 tie 和输入顺序，C++ builder 回归验证相同 item 顺序；其余几何中间量的完整跨语言误差仍需端到端覆盖。
4. **【2026-10-06 执行记录：S03 数值边界修复】** `RustReference/tests/lbf_order_reference.rs` 用独立 Rust `LBFBuilder` 构造并核对 f32 排序键 tie；`TestSppLbf.cpp` 先在旧 C++ 排序键上复现 item 顺序失败，改成 f32 后通过。第二个测试让下一宽度精确等于 ceiling，旧比较符允许继续并放置成功，修复后按 Rust 的 `>=` 终止。C++ SparrowCore Debug 全量测试及 RustReference 四项 `cargo test --locked` 均通过；边界比较用例的 growth ratio 是专门构造的 C++ 测试输入，不表示 Rust 提供同名可配置项。
5. **【2026-10-07 执行记录：构造失败信息】** 默认 Sparrow 冷启动在单件 2×2 方件、条带高 1.9 下，两侧均在探索 phase 前退出且没有 listener 回调。Rust 报 `ConstructionError { item_idx: 0 }`；C++ 原先只报宽度 ceiling，现已改为相同 Display 文本并通过 Debug/Release SparrowCore 全量测试。错误码仍映射为 C++ `Infeasible`，且此 heuristic failure 不是不可行性证明。
