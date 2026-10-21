# S03. lbf.rs 逐文件逻辑比对
**Rust**：`CodeResource/sparrow/src/optimizer/lbf.rs`（143 行）；搜索组合参考 `sample/search.rs:191-343`，评价器参考 `eval/lbf_evaluator.rs`
**C++**：Sparrow builder 的搜索组合位于 `SparrowLbfBuilder.cpp`，直接复用 `UniformRectSampler`、`LbfEvaluator`、`BestSamples` 和 `CoordinateDescent`；通用低层搜索另见 `LbfSearch.cpp`/`LbfSearchTrace.h`，二者不是 Sparrow builder 的委托实现
**判定**：固定参考矩阵通过；Sparrow builder 的实际搜索组合已改为对照 Rust `search_placement(ref_pk=None)` 的路径。当前 LLVM-MinGW 标量回退构建下，Sparrow LBF 定向测试组以及完整 SparrowCore 测试 runner 的 Debug/Release 均退出码 0。结论限于已列 fixture、当前平台与构建配置；不外推到任意几何或其他平台

说明：Rust `lbf.rs::LBFBuilder` 在 `find_placement` 中组合 `LBFEvaluator` 与 `search_placement`。C++ Sparrow builder 现直接组合已有 Sparrow sampler/evaluator/best-samples/coordinate-descent 组件；通用 `SearchLbf` 保留给低层通用 LBF 搜索调用，不再作为 Sparrow builder 的实现。下文区分这两条路径，避免将通用 LBF 搜索的配置或差异套到 Sparrow 构造路径上。

## 函数逐条比对

### `ConstructionError`（lbf.rs:15-30）
- C++ 对应：无独立结构；失败以 `jaguar::ErrorCode::Infeasible` + 消息字符串返回（SparrowLbfBuilder.cpp:27-31 `InfeasibleConstruction`，:173/:183）。
- 判定：失败项与说明文本已对齐；错误类型仍有 API 形态差异。
- 证据与说明：Rust `ConstructionError { item_idx }` 携带失败项索引（lbf.rs:17），其 Display 为 `could not construct an initial placement for item N`。C++ 在默认宽度增长/上限失败分支现返回相同文本并保留索引，错误码为 `Infeasible`。`RustSppConstructionFailure.json` / `OptimizeConstructionFailureRustReference.tsv` 用单件 2×2 方件、条带高 1.9 验证 item 0 和零 listener 回调；该错误表示 LBF 启发式到达宽度限制，不证明数学不可行。Rust 的 `ConstructionError` 类型与 C++ 的通用 `Result<..., ErrorCode::Infeasible>` 仍不相同。

### `LBFBuilder::new`（lbf.rs:40-50）
- C++ 对应：`ConstructSparrowInitialSolution` 中的 `jaguar::SPProblem::Create`、`SparrowRng rng(config.seed)` 与 `SparrowConstructorConfig`（`SparrowLbfBuilder.cpp`）。
- 判定：默认入口配置对应。
- 证据与说明：Rust 保存 seed 与 `SampleConfig`；C++ 用局部 `SparrowRng(config.seed)` 并直接读取 `nContainerSamples`、`nCoordinateDescents`。Rust 默认 `LBF_SAMPLE_CONFIG={1000,0,3}`；C++ `SparrowConstructorConfig` 默认值也为 `1000/0/3`。Rust LBF 走 `ref_pk=None`，不执行 focused 阶段；C++ `nFocussedSamples` 正数仍不增加采样，是保留字段的配置行为差异。此处不存在 `MakeSearchConfig` 或 `LbfConfig` 转换。

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
- C++ 对应：`FindPlacement`（`SparrowLbfBuilder.cpp`），不再调用通用 `SearchLbf`。
- 判定：固定输入路径与 Rust `search_placement(ref_pk=None)` 的阶段组合相同；测试矩阵没有证明任意几何、任意 seed 的位级等价。
- 证据与说明：C++ 从容器碰撞形状 bbox 创建 `UniformRectSampler`，然后对 `nContainerSamples` 个候选调用 `LbfEvaluator` 并报告给 `BestSamples`；随后对保留样本运行预精调、再次报告，再对当前最优样本运行终精调。配置对应 Rust 的 container sampling、`BestSamples(n_coord_descents, item_min_dim*0.05)`、`prerefine_cd_config`、`final_refine_cd_config`；`rotationWiggle` 仅在 Continuous 模式开启。Rust Sparrow SPP 输入的容器 bbox 对应当前使用的碰撞形状 bbox。无可行采样框或没有 clear best 时，C++ 返回空候选并触发上层宽度增长。
  - Rust `LBFEvaluator::new(layout, item)` → C++ `LbfEvaluator(layout, item, filter)`。质量区 `HazardFilter` 是 C++ 保留的扩展钩子；Rust Sparrow SPP 构造输入不含 `InferiorQualityZone`，因此该差异在此 parity 域不可达，evaluator 公式详见 S09。
  - Rust `ref_pk=None` 时跳过 focused 阶段；C++ `nFocussedSamples` 正数配置也不增加采样。默认值 0 对齐，但自定义正数仍是 C++ 配置差异。

### 损失公式：Rust `LBFEvaluator::evaluate_sample`（lbf_evaluator.rs:48）vs `LbfLoss`
- C++ 对应：`LbfLoss`（LbfLoss.h:9-18）+ `LbfEvaluator.cpp:89-108 CalculateLbfLoss`。
- 判定：基本等价（公式一致；聚合形态从"单一 f32"改为"(xMax,yMax) 结构 + Cost()"）。
- 证据与说明：
  - Rust：`loss = X_MULTIPLIER * (poi.0 + bbox_corner.0) + Y_MULTIPLIER * (poi.1 + bbox_corner.1)`，`X_MULTIPLIER=10.0, Y_MULTIPLIER=1.0`（lbf_evaluator.rs:9-10,48）。jagua-rs 的 `SPolygon::bbox.corners()[0]` 实际是 `(x_max, y_max)`（见 rect.rs:161-169）；C++ 同样取 `bbox.xMax/yMax`（LbfEvaluator.cpp:95-96），公式为 `10.0F * (poiX + bboxX) + (poiY + bboxY)`（LbfEvaluator.cpp:102）。这是源码公式对应；相关 Rust fixtures 的 loss 由测试转成 float 后按容差比较，不是由 `loss_f32_bits` 文本逐位断言。
  - C++ `LbfLoss::FromBoundingBox` 只存 `{xMax, yMax}`（LbfLoss.cpp:14-17），`Cost() = xMax*10 + yMax`（:19-26，含非有限→+inf 防御），`operator<` 按 Cost 比较并含 NaN 防御（:43-56）。Rust 侧是裸 f32 + `SampleEval` 的 Ord；C++ 的 NaN/inf 防御为 C++ 额外（防御式，不改变正常路径）。
  - `TightenSampleBBox`（LbfLoss.h:17）：用 `Cost()/10` 收紧样本 bbox 上界，供 UpperBound 早停使用——Rust 侧对应 `best_samples.upper_bound()` 的 SampleEval 比较链（S15），此函数是 C++ 侧的表示适配。判定：C++ 额外（表示层）。

### 通用 `SearchLbf` / `SearchLbfImpl`（LbfSearch.cpp）——独立低层路径

Sparrow builder 不调用此函数；其调用者与 pinned `lbf::search` 的预算、采样与损失对拍属于 L1/L2 记录。`SearchLbfWithTrace` 和 `LbfSearchTraceCallback` 仍用于通用搜索及轨迹诊断，不作为 `LBFBuilder::find_placement` 的实现证据。通用 `SearchLbf` 的 `LbfLoss`、local budget、采样框和错误通道差异继续按 L1/L2 对照表记录。早期本记录曾把它误列为 Sparrow builder 的搜索实现；本次更正归属，不改通用搜索路径。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 模块组织：Rust builder 调用 `search_placement`，C++ Sparrow builder 直接组合已有组件 | 结构差异 | 通用 `SearchLbf` 保留为独立低层路径，不是 Sparrow builder 的委托实现 | lbf.rs:124-142; SparrowLbfBuilder.cpp `FindPlacement` |
| 2 | Sparrow constructor 的宽度增长尝试 cap | 默认一致 | `SparrowConstructorConfig.maxWidthGrowthAttempts=0` 表示无次数上限；显式正数为 C++ 扩展。通用 `LbfConfig` 默认 32 属于低层 API | lbf.rs:102-111; LbfConfig.h; SparrowLbfBuilder.cpp |
| 3 | `nContainerSamples` 默认 1000、`nCoordinateDescents` 默认 3 | 默认一致 | builder 直接读取构造配置；通用 `LbfConfig` 默认 5000 不代表 builder 预算 | consts.rs:51-55; LbfConfig.h; SparrowLbfBuilder.cpp |
| 4 | `nFocussedSamples` 正数配置 | C++ 配置差异 | Rust LBF 使用 `ref_pk=None` 并跳过 focused 阶段；C++ 正数设置不增加采样，默认 0 对齐 | search.rs:253-282; SparrowLbfBuilder.cpp |
| 5 | Sparrow 搜索组合 | 已按 Rust 路径实现；固定矩阵定向组通过 | 直接组合 `UniformRectSampler`、`LbfEvaluator`、`BestSamples` 与两阶段 coordinate descent；不外推至全部输入 | search.rs:191-343; SparrowLbfBuilder.cpp; `--spp-lbf` |
| 6 | 最小质量 `HazardFilter` | C++ 扩展；Sparrow SPP 输入不可达 | Rust `SPInstance` 与 Sparrow SPP 容器不表达该 hazard | lbf.rs:124-136; SparrowLbfBuilder.cpp |
| 7 | 构造错误映射 | API 形态差异 | Rust `ConstructionError` 映射为 C++ `ErrorCode::Infeasible`；显示文本与 item index 对齐 | lbf.rs:15-30; SparrowLbfBuilder.cpp |
| 8 | 通用 `SearchLbf` 的 budget/loss/trace 差异 | 仅通用低层 API | 不适用于 Sparrow builder 的 `find_placement`；详细结论归 L1/L2 | LbfSearch.cpp; L1/L2 记录 |

## 结论

排序键、宽度上限、默认扩宽终止、零样本、构造失败、多实例、默认 `1000/0/3` 结果、初始宽度相邻 ULP 和超过 32 次扩宽均有 Rust/C++ 固定参考。现行 `FindPlacement` 已改为直接组合 Sparrow sampler/evaluator/best-samples/coordinate-descent，并在当前 LLVM-MinGW Debug/Release 的 `--spp-lbf` 定向 runner 中退出码 0，覆盖 SPP LBF 回归（含 `growth_limit_38` 位模式断言）。width/density 与 transform 仍按上文容差比较；该结论不证明任意几何、实例分布或完整 optimize→compress。显式正数 constructor cap、focused 正数配置、质量过滤钩子及通用低层 `SolveSppLbf` 默认 cap 等差异继续单独记录。

本轮追查完整 runner 曾报告的 Exploration 固定轨迹差异，确认差异来自 seed 10–14 测试 fixture 的初始位姿表达式：C++ 优化构建将乘法与平移合并为 FMA，造成一个 ULP 的偏差；Rust fixture 将 f32 乘法和加减分别舍入。将 C++ 测试 fixture 的乘法中间值显式拆开后，恢复一致。此为测试数据构造差异，没有修改生产算法。80 个翻译单元在 LLVM-MinGW 标量回退 Debug/Release 下重建成功，完整 SparrowCore runner 两个配置均退出码 0。此证据仅覆盖当前工具链和 runner 范围，不证明 MSVC、其他平台或任意输入逐位等价。
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
