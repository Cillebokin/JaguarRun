# S07. compress.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/compress.rs`（215 行，含 `oracle-trace` 时钟钩子）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/Compression.h`（40 行）+ `Source/Optimizer/Compression.cpp`（481 行）
**判定**: 主循环与两种衰减策略在正常正时限/合法步长输入下结构对应。FailureBased固定参考覆盖`powi(1)`临界、`powi(2)`阈值三邻值、`powi(3)`等值及三种衰减下`powi(4)`–`powi(7)`阈值三邻值，共40条轨迹；Rust Debug/Release与C++ LLVM-MinGW、VS2019 MSVC 14.29 Debug/Release已通过。TimeBased计算现按f32秒、比例、步长和`shrinkMin`比较；新增共享时钟用例锁定`2s+1ns`折算到阈值的attempt，旧double路径会提前停止，现已修复。TimeBased秒转换与局部计时起点已复核：Rust按整秒和余纳秒分别转f32，C++已按相同语义修复；三十三条共享时钟duration用例：五条覆盖60秒邻域舍入和中间比例，另二十八条分别验证等范围越过deadline仍attempt、timeLimit=2ns且elapsed=1ns时ratio=0.5、step=0.09375并执行attempt、timeLimit=3ns且elapsed=1ns时ratio=0x3eaaaaaa、step=0x3dd55556并执行attempt、timeLimit=4ns且elapsed=1ns时ratio=0.25（0x3e800000）、step=0x3de00000并执行attempt、timeLimit=5ns且elapsed=1ns时ratio=0x3e4ccccd、step=0x3de66666并执行attempt、timeLimit=6ns且elapsed=1ns时ratio=0x3e2aaaaa、step=0x3deaaaab并执行attempt、timeLimit=7ns且elapsed=1ns时ratio=0x3e124924、step=0x3dedb6dc并执行attempt、timeLimit=8ns且elapsed=1ns时ratio=0x3e000000、step=0x3df00000并执行attempt、timeLimit=9ns且elapsed=1ns时ratio=0x3de38e39、step=0x3df1c71c并执行attempt、timeLimit=10ns且elapsed=1ns时ratio=0x3dcccccd、step=0x3df33333并执行attempt、timeLimit=11ns且elapsed=1ns时ratio=0x3dba2e8b、step=0x3df45d17并执行attempt、timeLimit=12ns且elapsed=1ns时ratio=0x3daaaaaa、step=0x3df55555并执行attempt、timeLimit=13ns且elapsed=1ns时ratio=0x3d9d89d8、step=0x3df62762并执行attempt、timeLimit=14ns且elapsed=1ns时ratio=0x3d924924、step=0x3df6db6e并执行attempt、timeLimit=15ns且elapsed=1ns时ratio=0x3d888889、step=0x3df77777并执行attempt、timeLimit=1ns且elapsed=0时step等于shrinkMax并执行attempt、`shrinkMax=shrinkMin=1`时下一宽度归零并由`min_width`检查阻止attempt、`shrinkMax=shrinkMin=0`时step为0且宽度不变、由no-shrink guard退出、`shrinkMax=1/shrinkMin=0`在elapsed=0.5s时step=0.5并执行attempt、timeLimit=INT64_MAX且elapsed距deadline 1024s时step比shrinkMin高1ULP并attempt、timeLimit=1ns且elapsed=1ns时秒转换保持非零并按shrinkMin执行attempt、非零范围在两倍deadline时停机、min=0且elapsed等于deadline时因宽度不变而停止、timeLimit=1s+32ns/elapsed=1s+33ns舍入后step恰等于min仍attempt、timeLimit=0.5s+16ns/elapsed=0.5s+17ns子秒余数舍入跨至下一f32秒值后step低于min停机、timeLimit=1s+59ns/elapsed=1s+60ns、2s+119ns/2s+120ns及4s+238ns/4s+239ns跨到下一f32秒值后step低于min而停止，均用10s非零起点。新helper后MSVC Debug/Release自然时限复验均通过（Debug Explore 540032/540000ms、Compression 60046/60000ms；Release Explore 540002/540000ms、Compression 60003/60000ms）；Rust Release有既有自然通过记录，Rust Debug仍被已知∞>NaN worker断言阻断。自然用例不比较不同调度下的逐轮步长和布局。宽度更新及停机按f32并有min_width邻域、`2^-25` no-shrink reference。错误出口审计已覆盖配置/状态前置校验、C++指标适配层、Separator下层错误传播分类，并新增极端有限旋转失败出口和NaN质心split分支两个确定性fixture；C++ NaN分支已按Rust严格比较修复，Release轨迹一致，Rust Debug专属quadtree断言仍与C++ Debug存在差异。其余合法状态下的下层错误出口仍未穷尽，S07目前不能退出。

## 函数逐条比对

### `compression_phase`（compress.rs:128-191）—— Algorithm 13
- C++ 对应：`Compress`（Compression.cpp:245-481，主双状态重载：265-481）。
- 判定：基本等价（主循环与两种衰减一致；三处差异如下）。
- 证据与说明：
  1. **min_width**：Rust `super::minimum_strip_width(&sep.prob)`（:135）→ C++ `CalculateMinimumStripWidth(instance, separatorState)`（调用：:308；实现：Compression.cpp:130-197）。Explore 阶段另有实现（Exploration.cpp:61-144），不是逐行相同的副本：Explore 与 Compression 均按 Rust 的 f32/f64 运算次序计算；Compression 的单件方形临界阈值已由真实阶段 oracle 验证，Explore 的“所有离散角都不适配容器高度”正无穷边界已由 `ExplorationMinimumWidthInfinityRustReference.tsv` 与 C++ 实际 Explore 回归对齐；Compression的`+Inf`早停也由共享fixture对齐。合法离散角的几何转换失败现在由C++ `CalculateMinimumStripWidth`向上返回`InvalidInput`，与Rust在`minimum_width`计算时panic且尚未发出Compression progress的阶段边界一致；`CompressionDiscreteRotationWorkerError.json`固定0°/315°退化包围盒覆盖该路径，MSVC与Rust Debug/Release定向用例通过。另有item 911仅允许90°却按0°预放的“所有离散角都不适配容器高度”fixture，两侧得到`minimum_width=0x7f800000`、保留宽度20且不发progress；该非法初始角证据只锁定`+Inf`早停，不代表合法旋转布局或初始报告分类。两条路径的一般上游几何数值仍未被整体证明，详见 S06/S08。
  2. **初始 best_sol**：Rust `init_sol.clone()`（:19）→ C++ `bestSolution = initialSolution`（:307）：一致。
  3. **shrink_step_size（时间衰减）**：Rust以f32执行`elapsed.as_secs_f32() / time_limit.as_secs_f32()`及`shrinkMax + ratio * (shrinkMin - shrinkMax)`（:143-148，元组`(max,min)`）；C++现由`DurationAsRustSecondsF32`（Compression.cpp:78-89）分别将整秒、余纳秒转为f32，再按Rust顺序相加；随后在f32域计算ratio、范围差和step（Compression.cpp:333-345）。`CompressionMinimumWidthRustReference.tsv`新增共享时钟阈值记录：elapsed为`2s+1ns`时Rust秒值舍入为2.0，step恰等于`shrinkMin=0.0625`并执行第二次attempt；随后`2s+500ns`低于阈值并停止。旧C++ double路径在`2s+1ns`时提前停止，少一次attempt；改为f32后Rust/C++核对steps、final width和时钟消费一致。计时锚点语义一致：Rust在计算min_width并复制初始解后创建局部stopwatch（:135-137）；C++在指标、restore、Separator/worker RNG初始化、初始解副本和minimumWidth等压缩预处理后取start（Compression.cpp:280-312），两侧都在主循环前启动本阶段计时。共享时钟样例使用10s非零起点并验证以end-start计算elapsed；新增timeLimit=1s+32ns、elapsed=1s+33ns的纳秒余数边界，Rust分段转换下两者均为1.0，step恰等于min并执行一次attempt；timeLimit=1s+59ns、elapsed=1s+60ns则分别舍入到1.0和下一f32秒值，step低于min并停止；timeLimit=2s+119ns、elapsed=2s+120ns则分别舍入到2.0和0x40000001，step同样低于min并停止；timeLimit=4s+238ns、elapsed=4s+239ns则分别舍入到4.0和0x40800001，step为0x3d7ffffe并低于min。直接调用且`timeLimit=0`时，Rust得到非有限step并在进度回调/压缩尝试前退出；C++显式提前退出，零时限reference继续核对该状态。
  4. **shrink_step_size（失败衰减）**：Rust `shrink_range.0 * r.powi(n_failed_attempts)`（:149-151，f32运算）→ C++将`shrinkMax`、`failureDecay`转为f32，以float参数调用`std::pow`，再以f32相乘并提升回double（Compression.cpp:349-361）。锁定rustc 1.98.1 / x86_64-pc-windows-msvc下，`f32::powi(i32)`降为`powf(float,float)`；C++双精度`pow(double,double)`在新增指数4 fixture中使步长偏高1 ULP，故保留float重载。停机阈值按f32`shrinkMin`比较。`CompressionFailureDecayThresholdRustReference.tsv`固定`shrinkMax=0x3e4ccccd`、`shrinkMin=0x3e23d70b`、`failureDecay=0x3f4ccccd`，确认`step[1] == shrinkMin`时仍执行第二次失败尝试，之后停机。指数fixture覆盖`powi(2)`三邻值、`powi(3)`等值及三种因子下`powi(4)`–`powi(7)`三邻值。Rust Debug/Release与C++ LLVM-MinGW、VS2019 MSVC 14.29 Debug/Release固定轨迹匹配；其他指数、衰减因子、工具链和宽度边界仍不外推。
  5. **循环条件**：Rust `while !term.kill() && let step = shrink_step_size(...) && step >= config.shrink_range.1`（:155-159——step低于min即退出）→ C++ `while (!terminator.ShouldTerminate(iteration) && (maxIterations==0 || iteration<maxIterations))`，随后按f32阈值比较并退出（Compression.cpp:318-368）。FailureBased与TimeBased的`minimumStep`现均转为f32；新增TimeBased用例覆盖step恰等于min及随后低于min的停止边界。其他timeLimit/min配置组合尚未穷举；局部计时起点已用非零共享时钟样本核对。
  6. **宽度计算与停机**：Rust `width = best_sol.strip_width(); next_width = width*(1.0-step); if next_width < min_width || next_width >= width { break }`（:160-165，f32）→ C++ 现将 `width`、`step` 转为 f32，在 f32 域计算 `nextWidth` 并执行宽度/min_width 停机比较，再将宽度提升为 double 传入现有 API（Compression.cpp:368-375,388-408）。`CompressionMinimumWidthRustReference.tsv` 覆盖 min_width 邻域及 `step=0x33000000`（`2^-25`）时 `1.0F-step` 舍入为 1.0、两侧均不发出压缩 progress 的边界；旧 C++ double 路径会多发出一次 progress。其他宽度与 min_width 组合仍未穷尽。C++ 另有非有限/非正防御。
  7. **进度上报**：Rust `sol_listener.report_compression_progress(step)`（:46）→ C++ 原生 `listener->OnCompressionProgress(step)`（:268-269）；额外 `OnProgress` 仅通过显式传入的 C++ `OptimizationObserver` 提供（:270-277），不属于 Rust listener parity。
  8. **attempt_to_compress 调用与分支**：Rust `Some(compacted_sol) → report(CmprFeas); best_sol = compacted_sol`；`None → n_failed_attempts += 1`（:47-57）→ C++ `if (loss == 0.0F && metrics.feasible) { bestSolution=...; OnImprovement; PublishSolutionReport(COMPRESSION_FEASIBLE) } else { ++failures; if (maxConsecutiveFailures>0 && failures>=...) break; }`（:333-359）。判定：差异（两点：a) C++ 成功判据多一个 `metrics.feasible`【CalculateMetrics 的完整校验定义于 :91-129：remainingDemand==0 + ValidateSPSolution + layout.IsFeasible】——Rust 仅凭 `ot.get_total_loss() == 0.0`（:76）判定。正常路径 loss==0 ⇒ 可行，两侧一致；C++ 为防御性双重判据。b) C++ 额外 maxConsecutiveFailures 限流【默认 0 时不加失败次数限制】，Rust 无失败上限；两侧仍受终止器、步长和宽度停机条件约束）。
  9. **结束日志/返回**：Rust `info!` + `best_sol`（:59-60）→ C++ 无日志 + `CompressionResult{bestSolution, bestMetrics, history}`（:364-368）。判定：一致（返回形态差异同 S06 #1）。
  10. **fixture 覆盖**：`RustSppOptimizeCompressSingleItem.json` + `OptimizeCompressRustReference.tsv` 由 `optimizer_compress_reference.rs` 直接调用 Rust `optimize` 产生，C++ 由 `TestExploration.cpp` 消费。seed=3、单件 warm-start、separator `no_improvement_limit=0`、一 worker；比较阶段/报告顺序、两次 0.01 压缩步、最终宽度/密度/变换。该场景压缩第一次成功、第二次失败，且无需 worker move。新增 `RustSppCompressWorkerMove.json` + `CompressionWorkerMoveRustReference.tsv` 由 `compression_worker_move_reference.rs` 直接调用 Rust `compression_phase` 产生，C++ `TestCompressionWorkerMoveRetryMatchesRustReference` 消费：seed=42、两件同型矩形、warm-start 宽度 4.2、failure-based 步长 0.2/0.1、单 worker；两次压缩尝试均失败，但分离器每次都执行实际 moves，分别核对 438 evaluations/8 moves/5 iterations 与 362/6/4、progress/result 顺序、report 状态、loss/位置及最终 placements。Rust/C++ 的离散事件和计数精确匹配，几何/损失按 fixture 头部容差比较。此直接阶段用例不等同于完整 `optimize` 流程。`PhaseReference.tsv` 仍是 C++ 自回归基线，不能替代其他配置的 Rust 对照。

### `attempt_to_compress`（compress.rs:193-215）
- C++ 对应：`Compress` 循环体（Compression.cpp:388-470）。
- 判定：基本等价（收缩流程一致；宽度恢复顺序差异如下）。
- 证据与说明：
  1. **恢复到初始解与宽度**：Rust 每次压缩尝试先 `sep.change_strip_width(init_sol.strip_width(), None)`，再 `sep.rollback(init_sol, None)`（:201-202）；随后生成 split 并收缩（:205-207）。C++ 现在按相同次序执行 `ChangeStripWidth(width)`、`Rollback(bestSolution)`、split 抽样和 `ChangeStripWidth(nextWidth, split)`（Compression.cpp:388-408）。Rust `Separator::new` 构造时派生 N 个 worker seed；每次宽度变更立即重派生 N 个 seed。C++ 压缩入口通过 `ResetWorkerRngs(config.separator.nWorkers)` 对齐构造时消费（Compression.cpp:308），`ChangeStripWidth` 在 tracker 重建后立即按现有 worker 数重派生；本夹具的两次 split 与压缩接受/拒绝序列由 Rust/C++ 端到端结果核对。C++ 通用 Separator 创建仍采用延迟初始化，但压缩首次随机抽样前已显式初始化。
  2. **收缩宽度与随机分割点**：Rust `new_width = init_sol.strip_width() * (1.0 - r_shrink)`（:70），`split_pos = sep.rng.random_range(0.0..sep.prob.strip_width())`（:71）；C++ `nextWidth = width*(1.0-step)`，并以 `Uniform01F32() * static_cast<float>(width)` 计算 split。Rust 锁定的 rand 0.10.3 `random_range(f32)` 使用 23 位均匀值再乘 `high-low`；对 `low=0`，C++ 的 `Uniform01F32` 与 f32 宽度乘法采用相同映射。Rust RNG fixture 覆盖原语，端到端 fixture 覆盖指定两次 split 对应的几何分支；没有据此声称所有平台/所有浮点编译选项都 raw-bit 等价。
  3. **分离与成功判定**：Rust `sep.separate(term, sol_listener)` 返回 `(compacted_sol, ot)`，`ot.get_total_loss() == 0.0 → Some(compacted_sol)`（:75-79）→ C++ `separator.Run(...)`（:298-303）、`loss = separator.Tracker().TotalLoss()`（:310）及 `loss == 0 && metrics.feasible` 接受分支（:333-350）。判定：一致（额外可行性校验见上条 8）。
  4. **C++ 额外的逐轮指标**：每轮构造 progress 并通知 listener（:434-438）；loss/metrics 与 reject/exact 计数聚合见（:421-433）：C++ 额外（同 S06 #11）。
  5. **C++ 额外 `ValidationConfig`**：`CompressionConfig::Validate`（:231-244）：shrink 范围/failureDecay∈(0,1)/timeLimit 校验：C++ 额外（前置校验）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 每次压缩尝试的宽度恢复/rollback顺序与worker RNG重派生 | 已按Rust调整；指定fixture覆盖无move成功/失败与有move的失败重试 | Rust构造时及每次宽度变化时重派生；C++压缩入口预派生，恢复宽度和收缩宽度时即时重派生；只证明列明的seed/config | compress.rs:146,201-207; separator.rs:40-48,299-335 vs Compression.cpp:304,388-400; Separator.cpp:331-395; `CompressionWorkerMoveRustReference.tsv` |
| 2 | split_pos采样映射：rand `random_range(0..w)` vs `Uniform01F32()*f32(w)` | 指定Rust rand 0.10.3默认路径已匹配 | 都是23-bit `[0,1)`值乘f32宽度；锁定RNG原语并由e2e接受/拒绝序列间接验证；不声称其他Rust rand版本/平台编译设置的位级等价 | Cargo.lock; rand 0.10.3 `uniform_float.rs:159-196`; compress.rs:206; Compression.cpp:401-402 |
| 3 | 成功判据：loss==0 vs loss==0 && metrics.feasible | 差异（防御） | 正常路径一致（loss==0 ⇒ 可行）；C++双重判据 | compress.rs:211-214 vs Compression.cpp:421-461 |
| 4 | maxConsecutiveFailures失败上限 | C++额外 | 默认0时不增加失败次数上限；Rust没有该配置项，两侧仍受终止器、步长和宽度条件约束 | compress.rs:156-182 vs Compression.cpp:318-368,466-470 |
| 5 | maxIterations 限流 | C++额外 | 默认 0=无限 | compress.rs:156 vs Compression.cpp:318-320 |
| 6 | TimeBased 曲线：f32 秒比例、阈值比较与局部 stopwatch 起点 | Duration秒转换、f32曲线及局部计时起点均按Rust语义对齐 | 五点曲线、deadline收尾、共享时钟`shrinkMin`阈值和默认60秒自然阶段有E5 fixture；自然用例已由MSVC Debug/Release与Rust Release按阶段预算/可行终态验收，helper后MSVC两profile已复验；阈值fixture覆盖elapsed=`2s+1ns`舍入到min并执行attempt，随后低于min时停止；三十三条duration边界覆盖timeLimit=2ns且elapsed=1ns时ratio=0.5、step=0.09375并执行attempt、timeLimit=3ns且elapsed=1ns时ratio=0x3eaaaaaa、step=0x3dd55556并执行attempt、timeLimit=4ns且elapsed=1ns时ratio=0.25（0x3e800000）、step=0x3de00000并执行attempt、timeLimit=5ns且elapsed=1ns时ratio=0x3e4ccccd、step=0x3de66666并执行attempt、timeLimit=6ns且elapsed=1ns时ratio=0x3e2aaaaa、step=0x3deaaaab并执行attempt、timeLimit=7ns且elapsed=1ns时ratio=0x3e124924、step=0x3dedb6dc并执行attempt、timeLimit=8ns且elapsed=1ns时ratio=0x3e000000、step=0x3df00000并执行attempt、timeLimit=9–15ns且elapsed=1ns时对应等分步长并执行attempt、elapsed/timeLimit的60秒邻界舍入、非零起点，以及等范围越过deadline仍attempt、`shrinkMax=shrinkMin=1`时下一宽度归零并在`min_width`检查处停止、`shrinkMax=shrinkMin=0`时step为0且宽度不变并由no-shrink guard退出、`shrinkMax=1/shrinkMin=0`在elapsed=0.5s时step=0.5并执行attempt、timeLimit=INT64_MAX且elapsed距deadline 1024s时step比shrinkMin高1ULP并attempt、timeLimit=1ns且elapsed=1ns时秒转换保持非零并按shrinkMin执行attempt，以及elapsed=0时step等于shrinkMax并执行attempt、非零范围在两倍deadline时停机、min=0时step=0的宽度不变停机、1s+32ns/33ns舍入后step恰等于min并attempt，以及0.5s+16ns/17ns子秒余数舍入跨阈值后step低于min停机，以及1s、2s、4s附近纳秒跨到下一f32秒值后step低于min而停机；其他timeLimit/min组合及认证错误出口仍开放 | compress.rs:143-148 vs Compression.cpp:78-89,312,333-345; `CompressionMinimumWidthRustReference.tsv::time_based_threshold/time_based_duration` |
| 7 | 每轮 observer OnProgress + 指标聚合 + reject/exact 计数 | C++额外（observer 显式启用） | 扩展观察数据，不进入 Rust listener 轨迹 | compress.rs（无）vs Compression.cpp:421-429 |
| 8 | FailureBased 的 f32 `powi`/step/min-threshold 与 C++ 幂计算 | `powi(1)`临界、`powi(2)`三邻值、一个`powi(3)`等值及三种衰减下`powi(4)`–`powi(7)`三邻值，共40条轨迹；Rust Debug/Release、C++ LLVM-MinGW Debug/Release和VS2019 MSVC 14.29 Debug/Release均对齐。指定`powi(4)`的1 ULP差异已修复 | canonical因子`0x3f666666`下`powi(4)`–`powi(7)`阈值三邻值及实际停止轮次固定；其他指数/因子/平台和宽度比较仍开放 | `CompressionFailureDecayThresholdRustReference.tsv`; `CompressionFailureDecayExponentBoundaryRustReference.tsv`; `failure_based_exponents_match_the_rust_f32_threshold_reference`; `TestCompressionFailureDecayExponentBoundariesMatchRustReference`; compress.rs:149-159 vs Compression.cpp:349-361 |
| 9 | report(CmprFeas) → PublishSolutionReport(COMPRESSION_FEASIBLE) | 一致 | listener映射 | compress.rs:175 vs Compression.cpp:445-461 |
| 10 | panic/expect → Result | 一致（既定差异） | change_strip_width的expect | compress.rs:201-210 vs Compression.cpp:388-408 |
| 11 | 直接调用且 TimeBased `timeLimit=0`：Rust 不进入压缩；C++ 显式提前退出 | 已由 Rust/C++ fixture 对齐 | 两侧均不报告 compression progress、不改变初始解；仅覆盖直接阶段入口的零时限状态 | `CompressionMinimumWidthRustReference.tsv::zero_time_time_based`; `direct_time_based_zero_duration_skips_compression_attempts`; `TestCompressionMinimumWidthMatchesRustBoundary`; `Compression.cpp` |

## 结论
Algorithm 13 的主循环与正时限下曲线方向已有源码核对和Rust/C++ reference。seed=3 warm-start全流程覆盖一次成功和一次无move失败压缩；seed=42直接compression-phase fixture覆盖单/双worker失败重试及实际worker moves。TimeBased已有五点曲线斜率、callback越过deadline后的当前attempt收尾、直接零时限，以及共享时钟`shrinkMin`边界：固定2s limit下，elapsed=`2s+1ns`按Rust f32舍入后仍执行step=`0.0625`的第二次attempt；随后`2s+500ns`计算值低于min并停止。旧C++ double路径曾只发出一次progress、只消费3个时钟样本且宽度停在`0x40600000`；改为f32后MSVC Debug/Release与Rust reference对齐到两次attempt、4个样本、final width=`0x40520000`。FailureBased覆盖40条固定轨迹及`powi(4)`的1 ULP修复；宽度更新按f32并有min_width邻域和`2^-25`停机reference。RustReference该fixture组Debug/Release各6项通过；较早fixture矩阵下MSVC Release全量runner退出码0、MSVC Debug全量runner此前通过；第十至二十六条新增边界由Rust Debug/Release与MSVC Debug/Release定向验证。默认60秒自然TimeBased阶段已在固定E5 fixture上按预算/可行终态验收（C++ MSVC Debug/Release、Rust Release；Rust Debug受已知∞>NaN断言阻断），不作跨调度步长/布局对拍。其他timeLimit/min组合、其他认证宽度/错误出口仍开放；本轮未重跑十分钟自然时限验收；metrics.feasible与压缩限流差异按本表保留。

### 补充审计（2026-09-29）
1. **【核对确认：80 行逐项覆盖】** compress.rs 全量复核：compression_phase、闭包 shrink_step_size（两分支）、attempt_to_compress——主文 2 大节全覆盖无遗漏；`report_compression_progress(step)` 的 f32→double（S21 既定差异）已记。
2. **【遗漏补记：shrink_step_size 的 TimeBased 分支用 elapsed 而非剩余时间】** Rust `ratio = elapsed.as_secs_f32() / time_limit.as_secs_f32()`（compress.rs:145-148，**单调递增 0→1**，步长从 max 线性降至 min）；C++ `ratio = elapsed_ns / timeLimit_ns` 同向（Compression.cpp:316-328）。主文已记线性插值方向及数值域差，此处补记方向语义（无时间反向陷阱，判定不变）。
3. **【浅覆盖补全：循环条件 let 链的求值序】** Rust `while !term.kill() && let step = shrink_step_size(...) && step >= shrink_range.1`（compress.rs:155-159）——**每轮循环头部重算 step**；C++ 在循环体内先算 step 再检查 `step < shrinkMin → break`（Compression.cpp:306-349）。主文条目 5 已记循环体求 step；补记：timeLimit 到达后 Rust 侧终止条件先于本轮步长计算；step 本身可继续增大至 shrinkMin 之上——两侧均不因 step>max 停机（只因 <min 停机），判定维持。
4. **【历史分析，已被 2026-10-06 实测更正】** 早先按 rand 0.10.2 的 `Uniform::new` 溢出修正讨论了 `random_range(0.0..w)`，并误将 `sample_single` 的实现也归入 `new_bounded`；Rust 实际走 `UniformFloat::sample_single`，直接由 `u32 >> 9` 构造 23-bit `[0,1)` 值，再计算 `value0_1 * (high-low) + low`。当前 Cargo.lock 固定 rand 0.10.3；C++ split 已改为 `Uniform01F32() * static_cast<float>(width)`，与 low=0 的 Rust 表达式相同。
5. **【历史分析，已被 2026-10-06 实现与 fixture 取代】** 旧记录称 C++ 首个 split 在 worker seed 派生前生成。当前 C++ 压缩器在首个 split 前按 `config.separator.nWorkers` 调 `ResetWorkerRngs`；宽度恢复和收缩在 `ChangeStripWidth` 中立即重派生 worker RNG，并按 Rust 次序先恢复宽度、后 rollback、再抽 split。
6. **【验证边界（2026-10-07）】** optimizer_compress_reference.rs/TestExploration.cpp 对 seed=3 的单件 warm-start 全流程做跨语言比较；compression_worker_move_reference.rs/TestCompressionWorkerMoveRetryMatchesRustReference 对 seed=42 的两 worker 数配置分别核对两次失败压缩尝试，每次都含实际 move，并比较 separator 事件顺序、计数和 placements。optimizer_termination_handoff_reference.rs/TestTerminationHandoffMatchesRustReference 再核对一次 exploration 中断后以 separatorState 与 best solution 两状态启动压缩。S08 的 optimizer_cold_start_default_reference.rs/TestDefaultColdStartOptimizeMatchesRustReference 还在默认阶段配置下运行压缩至首个 separation result，核对配置、报告/事件、计数和布局；它不覆盖自然跑满 60 秒或完整 time-based 衰减曲线。其他终止时点、一般实例分布仍待参考。
7. **【稳定性复跑（2026-10-07）】** 双 worker 失败重试 Rust reference 在 Debug 下以独立测试进程连续运行 10 次均通过；只支持该固定 seed/夹具轨迹稳定，不扩展到其他 worker 数、运行时调度或一般实例。
8. **【2026-10-07 执行记录：正时限压缩到期】** `OptimizePositiveTimeoutRustReference.tsv` 固定 seed=3 warm-start、250 ms phase limit、failure-based 0.01 shrink。compression progress listener 等待 300 ms 后，当前循环已经通过 terminator 检查，Rust/C++ 仍改变 strip width 并调用 separator；deadline 使 separator 跳过 worker iteration，返回 success/0 eval/0 moves/0 iterations，压缩将 4.0 宽度的可行布局更新为 3.96。该轨迹比较事件/报告/placement 与阶段计数，不覆盖一般超时搜索长度。
9. **【2026-10-07 执行记录：正时限 TimeBased 曲线】** `CompressionTimeBasedPositiveTimeoutRustReference.tsv` 固定 seed=3 单件 warm-start，compression `time_limit=2s`、`shrink_range=(0.08, 0.02)`、TimeBased；Rust/C++ 记录五个 `report_compression_progress` 时刻与步长，前四次回调各等待 400 ms，第五次请求 phase stop。两侧首步接近 0.08，按相邻实测时间计算的斜率均约为 `(0.02-0.08)/2=-0.03/s`，容差 0.001/s；因此插值方向和斜率对齐。Rust 用 `as_secs_f32()`，C++ 将 elapsed 截断到毫秒后用 double 计算，差别是极小的时间量化；同序号步长会随两侧每次 separator 的实际耗时而不同。该测试在自然 deadline 前由 listener 停止，不代表完整 2 秒到期、默认 60 秒运行或一般排料结果完全一致。
10. **【2026-10-07 执行记录：正时限 TimeBased 到期与最终布局】** `OptimizeTimeBasedPositiveTimeoutRustReference.tsv`、`positive_time_based_timeout_finishes_current_attempt_and_matches_reference` 与 `TestPositiveTimeBasedTimeoutOptimizeMatchesRustReference` 固定 seed=3 单件 warm-start、探索时限 0、压缩时限 2 s、`shrink_range=(0.08, 0.02)`。首个 compression progress callback 等待 2100 ms，但不设置 phase-stop；由 2 s phase deadline 自然到期。首个 TimeBased step 接近 0.08；当前 attempt 仍完成，separator result 为 success/0 eval/0 move/0 iteration，最终 strip width 约 3.68，placement 可行且与压缩前相同。Rust 与 C++ 比较完整 phase/report/step/separation/placement trace，step 容差 `1e-6`、report width/density `1e-5`、placement rotation `1e-6`/translation `5e-5`。该固定 callback crossing 验证期限到期后的收尾，不代表未经等待的完整时限搜索。

### 补充审计（2026-10-18）
1. **【直接阶段 TimeBased 零时限对齐】** `compression_minimum_width_trace.rs::zero_duration_time_based_state` 使用单件 2×2、初始宽度 4、`shrink_range=(0.1,0.01)`、`Duration::ZERO`、TimeBased、单 worker 和零采样配置直接调用 Rust `compression_phase`；`compression_minimum_width_reference.rs::direct_time_based_zero_duration_skips_compression_attempts` 固定初始/最终宽度 raw bits 均为 `0x40800000` 且 progress 回调数为 0。TSV 新增 `zero_time_time_based 40800000 40800000 0`。C++ `TestCompressionMinimumWidthMatchesRustBoundary` 读取同一行并直接调用 `Compress`，比较初始/最终宽度位模式、零 progress 和 `SameSolution`。
2. **【差异与修复证据】** 修复前 C++ 测试红灯：宽度从 `0x40800000` 变为 `0x40666666`，并报告一次 progress；Rust 保持 `0x40800000`、progress 为 0。`Compression.cpp` 现于 TimeBased 分支检查零 `timeLimit` 并在 attempt/progress 前退出，对齐 Rust 可观察状态。FailureBased 零时限不受此分支影响。
3. **【2026-10-18 前一轮 TimeBased 零时限验证】** 锁定 Rust reference 验证脚本退出码 0；SparrowCore Debug 全量 runner 退出码 0。Release 全量 runner 首次在后续 effective-seed replay 断言失败；同一 Release runner 完整复跑退出码 0，且该断言位于当轮 S07 专项之后。该复跑不证明该独立 replay 波动已消失；本条只保留当时观测结果。
4. **【FailureBased 接近 `shrinkMin` 的停机轮次参考】** `compression_failure_decay_trace.rs` 从 Rust `compression_phase` 生成 `CompressionFailureDecayThresholdRustReference.tsv`，复用 seed=42 的两件矩形 warm-start。配置 raw bits 为 `shrinkMax=0x3e4ccccd`、`shrinkMin=0x3e23d70b`、`failureDecay=0x3f4ccccd`；轨迹步长为 `0x3e4ccccd`、`0x3e23d70b`，两次 separation 均失败，最终宽度保持 `0x40866666`。修复前 C++ 只报告第一步：double 乘积略低于 f32 `shrinkMin`，提前停机；C++ 改为按 f32 计算 FailureBased step 和阈值后，此 fixture 要求两次尝试并匹配 Rust。该结果只锁定此配置的临界轮次；其他失败指数下 `powi` 与 `std::pow` 的结果、宽度停机边界和 TimeBased 仍需独立覆盖。
5. **【上一轮验证（2026-10-18）】** `verify-rust-reference.ps1` 退出码 0；RustReference Debug/Release 各 160 个可运行集成测试通过，另有 2 个 ignored。SparrowCore Debug/Release 全量 runner 均退出码 0；FailureBased 临界比较器在两 profile 下均通过。修复前 Debug 红灯记录到 Rust 两次尝试、C++ 一次尝试；该红灯与修复后的双 profile 通过构成指定配置的差异和回归证据。
6. **【2026-10-18 FailureBased 指数阈值补齐】** Rust `failure_decay_exponent_boundary_trace_tsv` 从 `compression_phase` 生成四条 reference：`powi(2)` 的 `shrinkMin` 为 step 下邻/等值/上邻，对应 3/3/2 次失败尝试；`powi(3)` 在等值阈值运行 4 次失败尝试。Rust 全套验证脚本在 Debug/Release 均退出码 0，C++ `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 与原 `powi(1)` comparator 的定向 harness 在 Debug/Release 均退出码 0。当前 C++ 公式在这些配置下与 Rust trace 完全一致，无生产公式变更；对其他指数/衰减值的数值行为仍不作外推。

### 补充审计（2026-10-18，FailureBased 指数本轮）
1. **【扩展阈值矩阵】** 在 `CompressionFailureDecayExponentBoundaryRustReference.tsv` 增加 `failureDecay=0x3f66ace8`、`shrinkMax=0x3e800000` 的 `powi(4)` 下邻/等值/上邻配置。Rust reference 的 step[4] 为 `0x3e28c422`；下邻和等值各执行 5 次失败尝试，上邻执行 4 次。最终宽度 raw bits 均为 `0x40866666`，用于隔离步长和停机轮次。
2. **【红灯与原因】** 修改前定向 C++ 比较器报告 Rust step[4] `0x3e28c422`、C++ `0x3e28c423`，相差 1 ULP。锁定 rustc 的 x86_64-pc-windows-msvc 输出将 `f32::powi(i32)` 降为 `powf(float,float)`；C++ 原双精度 `std::pow(double,double)` 路径没有保持该中间舍入。
3. **【实现与绿灯】** `Compression.cpp` 改为 `std::pow(float,float)`，失败计数先按 Rust 的 i32 语义转换，再以 float 作为指数；C++ Debug/Release 的 S07 定向比较器现均退出码 0。RustReference 全量 `verify-rust-reference.ps1`（包括 Debug/Release 与格式检查）退出码 0。
4. **【适用范围】** 该修正锁定当前 Windows Rust/MSVC 参考组合和列明的 f32 bit patterns；不证明其他指数/衰减值、其他 Rust target/libm、宽度最小值边界、自然完整压缩时限或任意布局全等。S07 仍标为部分。

### 补充审计（2026-10-20，f32宽度舍入停机边界）
1. **【Rust oracle】** 在 `CompressionMinimumWidthRustReference.tsv` 新增 `step=0x33000000`（`2^-25`）用例。Rust 的 `1.0_f32-step` 以 ties-to-even 舍入为 `1.0f32`，所以 `next_width` 仍为 `4.0f32`，`next_width >= width` 成立，压缩 progress 数为 0。RustReference Debug/Release 全套验证均通过。
2. **【红灯】** C++ 定向入口 `--compression-minimum-width` 在旧实现下稳定报告该 probe 的 progress 为 1、Rust 为 0；原因是 `Compression.cpp` 把 f32 步长提升到 double 后以 double 计算和比较 `nextWidth`。
3. **【实现】** 将当前宽度、步长、`nextWidth` 以及 `min_width`/不变宽度停机比较放回 f32 域，再将宽度提升为 double 传给现有 Separator API。未改 Rust 算法、RNG 或压缩尝试顺序。
4. **【绿灯与范围】** LLVM-MinGW 标量回退下80个翻译单元 Debug/Release 均构建成功；`--compression-minimum-width` 定向测试和完整 SparrowCore runner 两配置均退出码0。证据仅闭合此停机舍入点及现有S07 runner矩阵；其他宽度/min_width组合、FailureBased其他指数/衰减值、TimeBased自然长时限和MSVC路径仍未证明，S07保持部分。

### 补充审计（2026-10-20，FailureBased `powi(5)` 阈值）
1. **【Rust oracle】** 将 `failure_decay_exponent_boundary_trace_tsv` 扩展为 `failureDecay=0x3f66ace8`、`shrinkMax=0x3e800000` 的 `powi(5)` 阈值三邻值。Rust `compression_phase` 实际给出的 step[5] 为 `0x3e181234`；`shrinkMin` 下邻 `0x3e181233`、等值 `0x3e181234`、上邻 `0x3e181235` 分别执行 6、6、5 次失败尝试，最终宽度均为 `0x40866666`。按前一步直接递乘得到的估值比实际 oracle 低 1 ULP，因此阈值以实测轨迹为准。
2. **【C++ 对拍与验证】** 更新 `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 的期望用例集合，C++ Debug/Release 均逐步长、separation结果和最终宽度匹配Rust。RustReference定向两测试通过，crate级 `cargo fmt --check` 通过；LLVM-MinGW标量回退80个翻译单元 Debug/Release构建及全量SparrowCore runner均退出码0。
3. **【适用范围】** 本轮只增加一个衰减因子下 `powi(5)` 的三邻值；不外推到其他失败指数、衰减值或Rust target/libm。其他宽度组合、TimeBased计时边界、自然完整时限与更多布局/错误出口仍未证，S07保持部分。

### 补充审计（2026-10-21，FailureBased `powi(6)` 阈值）
1. **【Rust oracle】** 将 `failure_decay_exponent_boundary_trace_tsv` 扩展为同一衰减因子 `0x3f66ace8` 与起始步长 `0x3e800000` 的 `powi(6)` 阈值三邻值。Rust `compression_phase` 实际 step[6] 为 `0x3e090711`；`shrinkMin` 下邻 `0x3e090710`、等值 `0x3e090711`、上邻 `0x3e090712` 分别生成7、7、6次失败尝试，最终宽度均为 `0x40866666`。按递乘得到的估值低1 ULP，阈值依据Rust实际轨迹锁定。
2. **【C++ 对拍与验证】** `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 将比较集合扩至13个case；Rust定向两测试与crate级格式检查通过。LLVM-MinGW标量回退80个翻译单元 Debug/Release构建及全量SparrowCore runner均退出码0，新增三组轨迹逐步长、结果序列和最终宽度匹配。
3. **【适用范围】** 本轮只增加该衰减因子下 `powi(6)` 的固定阈值矩阵；不外推到其他指数/衰减值、Rust target/libm、其他宽度组合、TimeBased计时边界、自然完整时限或任意布局。S07仍为部分。

### 补充审计（2026-10-21，FailureBased `powi(7)` 阈值）
1. **【Rust oracle】** 同一衰减因子 `0x3f66ace8`、起始步长 `0x3e800000` 下，Rust `compression_phase` 实际 step[7] 为 `0x3df6f1cc`。`shrinkMin` 下邻 `0x3df6f1cb`、等值 `0x3df6f1cc`、上邻 `0x3df6f1cd` 分别执行8、8、7次失败尝试，最终宽度均为 `0x40866666`。
2. **【对拍与验证】** RustReference `compression_failure_decay_reference` Debug/Release各2项通过，crate级格式检查通过；C++ LLVM-MinGW标量回退80个翻译单元Debug/Release构建成功，全量SparrowCore runner两配置退出码0。比较器现含16个FailureBased指数边界case，step、separation结果与最终宽度匹配。
3. **【适用范围】** 本轮只覆盖当前衰减因子下 `powi(7)` 的固定阈值矩阵；其他指数/衰减值、跨Rust target/libm数值、其他宽度组合、TimeBased计时边界、自然完整时限及任意布局仍未证明，S07保持部分。

### 补充审计（2026-10-21，替代衰减因子的 FailureBased `powi(4)` 阈值）
1. **【Rust oracle】** 新增 `failureDecay=0x3f4ccccd`、`shrinkMax=0x3e800000` 的 `powi(4)` 下邻/等值/上邻阈值。Rust `compression_phase` 的 step[4] 为 `0x3dd1b718`；`shrinkMin=0x3dd1b717`、`0x3dd1b718`、`0x3dd1b719` 分别产生5、5、4次失败尝试，最终宽度均为 `0x40866666`。
2. **【C++ 对拍与验证】** `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 比较器扩至19个case。Rust定向测试 Debug/Release各2项通过，crate级格式检查通过；LLVM-MinGW标量回退80个翻译单元 Debug/Release构建及完整SparrowCore runner两配置均退出码0。新增轨迹的步长、separation结果序列和最终宽度均匹配Rust。
3. **【适用范围】** 本轮只覆盖替代衰减因子 `0x3f4ccccd` 下 `powi(4)` 的固定阈值矩阵；不外推至该因子的其他指数、其他衰减值或Rust target/libm。其他宽度组合、TimeBased计时边界、自然完整时限及任意布局仍未证明，S07保持部分。

### 补充审计（2026-10-21，替代衰减因子的 FailureBased `powi(5)` 阈值）
1. **【Rust oracle】** 同一替代衰减因子 `failureDecay=0x3f4ccccd`、`shrinkMax=0x3e800000` 下，Rust `compression_phase` 实际 step[5] 为 `0x3da7c5ad`。`shrinkMin` 下邻 `0x3da7c5ac`、等值 `0x3da7c5ad`、上邻 `0x3da7c5ae` 分别产生6、6、5次失败尝试，最终宽度均为 `0x40866666`。
2. **【C++ 对拍与验证】** `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 比较器扩至22个case。Rust定向测试 Debug/Release各2项通过，crate级格式检查通过；LLVM-MinGW标量回退80个翻译单元 Debug/Release构建及完整SparrowCore runner两配置均退出码0。新增轨迹的步长、separation结果序列和最终宽度均匹配Rust。
3. **【适用范围】** 本轮只覆盖替代衰减因子 `0x3f4ccccd` 下 `powi(5)` 的固定阈值矩阵；不外推至该因子的其他指数、其他衰减值或Rust target/libm。其他宽度组合、TimeBased计时边界、自然完整时限及任意布局仍未证明，S07保持部分。

### 补充审计（2026-10-21，替代衰减因子的 FailureBased `powi(6)` 阈值）
1. **【Rust oracle】** 同一替代衰减因子 `failureDecay=0x3f4ccccd`、`shrinkMax=0x3e800000` 下，Rust `compression_phase` 实际 step[6] 为 `0x3d8637be`。`shrinkMin` 下邻 `0x3d8637bd`、等值 `0x3d8637be`、上邻 `0x3d8637bf` 分别产生7、7、6次失败尝试，最终宽度均为 `0x40866666`。
2. **【C++ 对拍与验证】** `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 比较器扩至25个case。Rust定向测试 Debug/Release各2项通过，crate级格式检查通过；LLVM-MinGW标量回退80个翻译单元 Debug/Release构建及完整SparrowCore runner两配置均退出码0。新增轨迹的步长、separation结果序列和最终宽度均匹配Rust。
3. **【适用范围】** 本轮只覆盖替代衰减因子 `0x3f4ccccd` 下 `powi(6)` 的固定阈值矩阵；不外推至该因子的其他指数、其他衰减值或Rust target/libm。其他宽度组合、TimeBased计时边界、自然完整时限及任意布局仍未证明，S07保持部分。

### 补充审计（2026-10-21，替代衰减因子的 FailureBased `powi(7)` 阈值）
1. **【Rust oracle】** 同一替代衰减因子 `failureDecay=0x3f4ccccd`、`shrinkMax=0x3e800000` 下，Rust `compression_phase` 实际 step[7] 为 `0x3d56bf96`。`shrinkMin` 下邻 `0x3d56bf95`、等值 `0x3d56bf96`、上邻 `0x3d56bf97` 分别产生8、8、7次失败尝试，最终宽度均为 `0x40866666`。
2. **【C++ 对拍与验证】** `TestCompressionFailureDecayExponentBoundariesMatchRustReference` 比较器扩至28个case。Rust定向测试 Debug/Release各2项通过，crate级格式检查通过；LLVM-MinGW标量回退80个翻译单元 Debug/Release构建及完整SparrowCore runner两配置均退出码0。新增轨迹的步长、separation结果序列和最终宽度均匹配Rust。
3. **【适用范围】** 本轮只覆盖替代衰减因子 `0x3f4ccccd` 下 `powi(7)` 的固定阈值矩阵；不外推至该因子的其他指数、其他衰减值或Rust target/libm。其他宽度组合、TimeBased计时边界、自然完整时限及任意布局仍未证明，S07保持部分。

### 退出审查裁决（2026-10-21；本轮后续补充）
1. **【原审查缺口】** 审查时FailureBased reference有28条固定轨迹，覆盖 `0x3f66ace8` 与 `0x3f4ccccd`，尚未覆盖CLI early-termination可达常量 `DEFAULT_FAIL_DECAY_RATIO_CMPR=0.9f32`（`0x3f666666`）。本轮已补齐该因子下 `powi(4)`–`powi(7)` 阈值三邻值，并由Rust/C++运行时对拍。
2. **【仍未闭合的P0】** canonical FailureBased矩阵已在VS2019 MSVC 14.29 Debug/Release全量runner中复核；TimeBased新增共享时钟用例已核对一处`shrinkMin`等值attempt及下一次低于min时停机。仍需评估其他有效duration转换/计时锚点组合，以及默认60秒自然阶段的完成/停止状态和已声明输入域内可达错误出口。自然时限结果按适当不变量验收，不要求不同调度的逐轮搜索轨迹相同。
3. **【范围边界】** 不要求穷举所有f32衰减位模式或所有几何布局，也不把未声明Rust target视为当前必验项；但若保留任意`FailureBased(f32)`有效载荷的支持，就不能用有限fixture宣称所有衰减值均已逐位等价。应以源语义证明或明确登记的认证输入域界定结论。其他宽度/min_width组合和错误出口只有在进入该认证输入域时才作为该域的验收项；在此之前保持显式未证，不阻止对固定子域给出有边界的结论。

### 补充审计（2026-10-21，canonical `0.9f32` FailureBased 阈值矩阵）
1. **【Rust oracle】** 按Rust `f32::powi`计算候选阈值，并扩展`failure_decay_exponent_boundary_trace_tsv`，使用Rust `compression_phase`生成实际轨迹。`failureDecay=0x3f666666`、`shrinkMax=0x3e800000`时，`powi(4)`–`powi(7)`阈值依次为`0x3e27f62a`、`0x3e172a59`、`0x3e080c83`、`0x3df4e352`；每个阈值均有raw f32下邻/等值/上邻。下邻与等值分别执行5、6、7、8次失败尝试；上邻分别执行4、5、6、7次。所有case最终宽度raw bits为`0x40866666`。
2. **【C++ 对拍与验证】** 将`CompressionFailureDecayExponentBoundaryRustReference.tsv`和`TestCompressionFailureDecayExponentBoundariesMatchRustReference`扩展到40个case。Rust `compression_failure_decay_reference` Debug/Release各2项通过，`cargo fmt --package sparrow-rust-reference -- --check`通过；C++ LLVM-MinGW标量回退80个translation units Debug/Release构建成功，全量`SparrowCoreTests.exe`两种配置退出码均为0。C++比较器逐case核对配置、压缩step、separation结果及最终宽度。
3. **【裁决与范围】** canonical `0.9f32`这一可达配置的FailureBased阈值P0已闭合；本节记录时MSVC Debug/Release尚未复核，后续审计已补齐该证据。本轮没有生产算法改动。TimeBased共享时钟下的临界停机/attempt收尾、自然默认60秒阶段及认证输入域内其他可达错误出口仍未闭合，S07继续标为部分。结果不外推到其他衰减位模式、Rust target/libm或未列明宽度组合。

### 补充审计（2026-10-21，VS2019 MSVC Debug/Release复核）
1. **【工具链与构建】** 使用Qt 6.5.3 `msvc2019_64` qmake、Visual Studio 2019 MSVC 14.29.30133和项目`.pro`正式构建链，在`SparrowCore/build/s07-canonical-decay-powi4-7/msvc2019/`隔离构建JaguarCore DLL、SparrowCore静态库及`SparrowCoreTests.exe` Debug/Release；JaguarCore按`.pro`默认AVX2配置构建。两种配置的三个目标均成功编译/链接。qmake发出既存`qt_build_config` feature缺失告警，但未阻断生成或构建。
2. **【运行验收】** 匹配Debug `JaguarCored.dll`与Release `JaguarCore.dll`目录分别加入`PATH`，从仓库根目录运行全量`SparrowCoreTests.exe`；Debug与Release退出码均为0，均经过`TestCompressionFailureDecayExponentBoundariesMatchRustReference`并对齐40条FailureBased固定轨迹。Debug全量runner耗时约8分钟，主要时间落在末尾的WASM受限优化用例；Release runner约21秒。
3. **【裁决与范围】** S07的canonical `0.9f32` FailureBased阈值矩阵已在Rust 1.98.1 Debug/Release、LLVM-MinGW Debug/Release和VS2019 MSVC 14.29 Debug/Release六种profile通过。该次审计关闭了MSVC缺口；当时TimeBased共享时钟边界尚未补充，后续验证见本记录末尾。结论不外推到其他MSVC工具集、AVX2以外配置或未列明数值域。

### 补充审计（2026-10-21，TimeBased共享时钟`shrinkMin`边界）
1. **【Rust reference】** 扩展`CompressionMinimumWidthRustReference.tsv`及`compression_minimum_width_trace.rs`：固定`timeLimit=2s`、`shrinkMax=0.125`、`shrinkMin=0.0625`，时钟样本为`[0, 0, 2s+1ns, 2s+500ns]`。Rust Debug/Release均观察到step bits `[0x3e000000,0x3d800000]`：`2s+1ns`经`Duration::as_secs_f32()`舍入为2秒，第二次step恰等于min并进入attempt；`2s+500ns`计算低于min后停止。最终宽度`0x40520000`，消费4个样本且未耗尽。
2. **【红绿对照与实现】** C++对拍在修改前失败：只报告`0x3e000000`一次、最终宽度`0x40600000`、只消费3个时钟样本。将TimeBased elapsed、timeLimit、ratio、范围差、step和min比较改为f32后，VS2019 MSVC 14.29 Debug/Release `--compression-minimum-width`均通过；Debug/Release全量runner退出码均为0。Rust `compression_minimum_width_reference` Debug/Release各5项通过，cargo fmt --check通过。
3. **【范围】** 本用例关闭列明秒边界下的f32舍入及`step == shrinkMin`包含条件差异；不证明其他duration转换、Rust/C++ stopwatch锚点、所有min组合或默认60秒自然阶段等价。S07仍部分。本轮生产变更仅修改TimeBased数值计算，没有更改压缩主循环和配置契约。

### 补充审计（2026-10-21，默认自然60秒TimeBased阶段复验）

1. **【入口与配置】**复用E5慢速真实时钟入口`--e5-natural-time`，输入为`RustSppCompressWorkerMove.json`、seed=42、width=4.2完整可行warm-start、默认3个separator worker。默认配置分别给Explore 9分钟、Compression 60秒，C++ `CompressionConfig`默认使用TimeBased收缩，范围为`0.0005 → 0.00001`；断言阶段顺序、两个阶段的真实耗时窗口、搜索/压缩进度、step有效范围和最终可行态，不比较跨实现的逐轮轨迹/布局。

2. **【MSVC Debug/Release】**在本轮f32计算修正后的二进制上重新运行，两个profile退出码均为0。Debug：Explore 540075ms、Compression 60029ms、总计600113ms，阶段separation results为67/5，reports=91，progress=12081，compression steps=5，最终可行。Release：Explore 540003ms、Compression 60002ms、总计600006ms，阶段separation results为591/60，reports=914，progress=362153，compression steps=60，最终可行。不同profile搜索速度与worker调度使计数不同，属于自然时限验收允许的变化。

3. **【Rust与范围】**Rust Release的同一E5自然fixture已有2026-10-19通过记录，阶段耗时540024ms/60011ms且返回可行状态；Rust源码及锁定revision未因本轮修改变化，本轮未重复其十分钟运行。Rust Debug此前在进入完整Explore/Compression之前命中已知`∞ > NaN` worker断言，因此自然Debug profile仍未闭合。此结果证明列明输入和profile的自然阶段到期/收尾，不证明step轨迹、不同worker配置或一般实例分布等价；其他duration转换、stopwatch锚点和认证域错误出口仍开放，S07保持部分。

### 补充审计（2026-10-21，TimeBased Duration转换与局部计时锚点）
1. **【Rust语义】** 锁定Rust `Duration::as_secs_f32()`的源码公式为`(secs as f32) + (nanos as f32)/(1_000_000_000 as f32)`。C++原先的`std::chrono::duration<float>(nanoseconds).count()`先把总纳秒转为float，MSVC 14.29实测旧转换在59,999,999,999ns得`0x426fffff`、60,000,000,000ns得`0x42700001`、30,000,000,000ns得`0x41f00001`；Rust分段转换分别为`0x42700000`、`0x42700000`、`0x41f00000`。生产路径新增`DurationAsRustSecondsF32`，对elapsed和timeLimit统一采用Rust分段转换。
2. **【共享时钟fixture】** `CompressionMinimumWidthRustReference.tsv::time_based_duration`现有8条真实`compression_phase`轨迹，均从10s非零计时起点开始。原5条覆盖elapsed/timeLimit在60s邻域舍入及30s/60s中间比例；`equal_range_above_deadline`令timeLimit=30s、elapsed=60s且max=min=0.0625，步长保持0.0625并attempt一次；`unequal_range_twice_deadline_stops`使用max=0.125、min=0.0625和elapsed/timeLimit=2，步长外推到min以下而停止；新增`zero_min_at_deadline_stops_on_unchanged_width`令timeLimit=elapsed=30s、max=0.125、min=0，公式步长为0，因next_width等于当前width而在进度回调前停止。三条边界用例均消费2个时钟样本且未耗尽。
3. **【计时锚点】** Rust在`minimum_strip_width`和初始解clone后创建局部stopwatch；C++在计算metrics、restore separator、初始化worker RNG、复制best solution并计算minimum width等预处理后取start。两者均在压缩主循环开始前启动阶段计时；本轮fixture确认C++按样本end-start计时。各自预处理的耗时不属于TimeBased曲线elapsed。
4. **【验证与范围】** RustReference `compression_minimum_width_reference` Debug/Release各6项通过，`cargo fmt --check`通过；VS2019 MSVC 14.29 Debug/Release `--compression-minimum-width`均退出码0，MSVC Release全量runner本轮退出码0。新helper后再次运行MSVC Release `--e5-natural-time`，退出码0；Explore为540002/540000ms、Compression为60003/60000ms、总计600006ms，separation results=385/59、reports=723、progress=238444、compression steps=59，最终可行。MSVC Debug自然fixture随后已在helper后复验通过（Explore 540032/540000ms、Compression 60046/60000ms）；MSVC Debug全量runner未在helper后重跑，此前全量runner通过且当前Debug定向用例通过。其余`timeLimit/min`组合、认证域内可达错误出口和更广泛自然搜索分布仍待核验，S07保持部分。

### 补充审计（2026-10-21，压缩阶段错误出口分类）
1. **【配置与入口状态】** `CompressionConfig::Validate`（Compression.cpp:232-244）拒绝非法收缩范围、衰减值、负时限及无效Separator配置；Rust `compression_phase`本身不做等价前置校验。`CalculateMetrics`、`SPProblem::Create/Restore`、`Separator::Create`及tracker构造还会拒绝无法创建的实例、无效/不匹配布局快照或不一致solution。它们是C++输入防线或Rust直接操作前提；非法配置、损坏快照不作为当前合法配置和内部生成状态的算法等价证据。
2. **【尝试内错误】** C++ `ChangeStripWidth`、`Rollback`和`CalculateMetrics`将移动/放置、strip/container重建、布局恢复、tracker构造及solution校验错误向上返回（Compression.cpp:389-427）。Rust对应调用为`change_strip_width`、`rollback`和`separate`，多数不返回`Result`，部分依赖内部断言或expect（compress.rs:193-215；separator.rs:258-335）。成功的压缩尝试及worker move/retry已有固定轨迹；当前审计没有构造一个在已声明合法输入域内、可稳定触发这些错误并能区分Rust结果的最小输入。下层几何/碰撞失败条件由JaguarCore和S11/S12等项管理，不能据此宣称不可达。
3. **【Separator错误传播】** `Separator::Run`可返回配置校验、worker `Result`错误、空worker结果及内部solution/tracker恢复错误（Separator.cpp:510-734）；Rust `separate`直接执行worker、更新tracker及恢复快照。配置错误属于入口约束；worker搜索/碰撞算法错误应由其下层验收项锁定；内部恢复错误表示状态不变量未成立。S07只记录这些调用点和传播边界，不重复替代下层用例。
4. **【裁决】** 本次源审计未发现一个已证明落在认证域内、且仅由Compression新增的错误分支，因此不新增虚构的compression error fixture；同时，仓库尚未给“合法实例、配置、初始解/Separator状态”定义能证明所有下层错误不可达的完整边界。错误出口继续作为跨模块未证项保留，S07不因这次分类而退出。验证：本轮仅作源与文档审计，未运行测试。

### 补充审计（2026-10-21，当前fixture认证边界）
1. **【生产调用状态来源】** Rust顶层`optimize`在Explore结束后，将`expl_separator.prob`交给新建的Compression `Separator`，并把`final_explore_sol`传给`compression_phase`（`CodeResource/sparrow/src/optimizer/mod.rs:54-75`）。C++对应地把`ExploreResult.separatorState`与`ExploreResult.solution`传入`Compress`（`SparrowOptimizer.cpp:179-191`）。这限定生产阶段状态来自同一次SPP实例的Explore handoff；`separatorState`与初始best solution可以是不同快照，但都必须可由该实例恢复。固定direct-compression fixtures也由同实例的`SPProblem`生成状态。
2. **【当前可声称的fixture域】** 结论限于本记录逐项列出的Rust reference矩阵：40条FailureBased指数阈值轨迹、TimeBased正时限/零时限/共享时钟曲线和33条duration边界、宽度与`min_width`舍入边界、seed=3 warm-start和seed=42 worker move/retry轨迹，以及列明E5自然时限实例。配置值、seed、终止方式、profile均按每条fixture记录；未测的`FailureBased(f32)`值、任意时限比例、宽度组合、几何/布局与自然调度不在结论内。
3. **【输入与错误界线】** 该域要求可构造的有限SPP实例、同实例生成且内部结构可恢复的`SPSolution`/separator状态，以及通过C++ `CompressionConfig::Validate`的配置；几何可行性和item completeness不作为一概前置条件，C++ metrics会显式记录feasible状态。每次separation得到非零loss是正常压缩失败（Rust `None`/失败计数与C++ failure计数），不是API错误。非法配置或损坏/不兼容快照的C++拒绝行为不计入Rust算法parity；支持域内所有其他下层错误仍未被证明不可达。S12已覆盖Hole/QZone在worker搜索、Separator及Optimize调用链上的Rust panic/C++异常传播，但其测试不经过Compression；Compression入口无catch的源码路径只证明异常会向上传播，不替代Compression专属fixture。
4. **【裁决】** 由此把“已证”等价严格限定为列出的fixture与调用 profile，不把它扩展成任意合法输入保证。下一步若继续收错误出口，应选择一个明确可由同实例Explore handoff产生、并在Compression attempt内触发的下层错误，先补Rust reference，再补C++调用链对拍；若不能构造，则保留为未知域，不把它转写成“不可达”。本轮仍为源/文档审计，未运行测试，S07保持部分。

### 补充审计（2026-10-21，TimeBased helper后的MSVC Debug自然时限复验）
1. **【构建与入口】** 使用`SparrowCore/build/s07-canonical-decay-powi4-7/msvc2019/tests-cfg0/debug/SparrowCoreTests.exe`，匹配`core-cfg0/Bin/JaguarCored.dll`，运行`--e5-natural-time`。测试二进制晚于Compression helper源码构建，入口沿用E5的`RustSppCompressWorkerMove.json`、seed=42、width=4.2完整可行warm-start、默认9分钟Explore和60秒Compression。
2. **【结果】** 退出码0；Explore=540032/540000ms，Compression=60046/60000ms，总运行=600088ms；阶段separation results=67/5，reports=98，progress=11993，compression steps=5，最终feasible=1。该自然profile按预算、阶段收尾和可行终态验收，不要求与Release搜索计数相同。
3. **【裁决】** TimeBased Duration helper后MSVC Debug/Release自然验收现均通过；Rust Release既有同fixture验收通过。Rust Debug仍在完整阶段前受已登记的`∞ > NaN`断言阻断。自然时限只认证列明fixture/profile，不外推一般输入；其他TimeBased组合、宽度/min_width组合和跨模块错误出口继续开放，S07保持部分。

### 补充审计（2026-10-21，TimeBased等范围超过deadline边界）
1. **【Rust reference】** 新增`time_based_duration::equal_range_above_deadline`：`timeLimit=30s`、计时从10s到70s（elapsed=60s）、`shrinkMax == shrinkMin == 0.0625`。虽然elapsed是timeLimit的两倍，Rust公式仍得到step=`0x3d800000`；`step >= shrinkMin`为真，执行一次压缩attempt，最终宽度=`0x40700000`，消费2个clock samples且未耗尽。
2. **【红绿与对拍】** 先将期望行加入Rust reference测试，旧生成器因缺少第六条轨迹失败；补入生成器和TSV后Rust `compression_minimum_width_reference` Debug/Release各6项通过，`cargo fmt --check`通过。使用Qt 6.5.3 `msvc2019_64` qmake既有配置，在VS2022 MSVC 14.44.35228.0下重建SparrowCoreTests Debug/Release；两profile的`--compression-minimum-width`均退出码0，step、progress、final width和clock消费与Rust一致。
3. **【裁决与范围】** 本轮只扩展reference fixture和C++比较器期望数，不修改生产算法。TimeBased duration矩阵由5条增至6条，锁定等范围条件下elapsed超过deadline仍可按共同步长执行attempt；其他`timeLimit/min`组合、一般计时调度和认证域内可达错误出口仍开放，S07保持部分。

### 补充审计（2026-10-21，TimeBased非零范围两倍deadline停机边界）
1. **【Rust reference】** 新增`time_based_duration::unequal_range_twice_deadline_stops`：`timeLimit=30s`、计时从10s到70s（elapsed=60s）、`shrinkMax=0.125`、`shrinkMin=0.0625`。elapsed/timeLimit为2时，f32线性公式得到步长0，低于`shrinkMin`，因此不进入压缩attempt；progress为0、最终宽度保持`0x40800000`，消费2个clock samples且未耗尽。该例与等范围case配对，分别锁定deadline外的恒定步长和非零范围外推停机行为。
2. **【红绿与对拍】** Rust期望先增加后，reference测试因生成器缺少第七条轨迹失败；补入轨迹和TSV后`compression_minimum_width_reference` Debug/Release各6项通过，`cargo fmt --check`通过。Qt 6.5.3 `msvc2019_64` qmake配置下使用VS2022 MSVC 14.44.35228.0重建SparrowCoreTests Debug/Release；两profile的`--compression-minimum-width`均退出码0，progress、最终宽度和clock消费与Rust reference一致。
3. **【裁决与范围】** 本轮仅扩展Rust/C++ reference fixture和比较器期望数，未修改生产算法。TimeBased duration矩阵由6条增至7条；仍未覆盖其他`timeLimit/min`组合、一般计时调度和认证域内可达错误出口，S07保持部分。

### 补充审计（2026-10-21，TimeBased零min到期时宽度不变边界）
1. **【Rust reference】** 新增`time_based_duration::zero_min_at_deadline_stops_on_unchanged_width`：`timeLimit=elapsed=30s`、`shrinkMax=0.125`、`shrinkMin=0`。公式步长为0，while条件允许它越过`step >= shrinkMin`，随后`next_width >= width`在进度回调和attempt之前成立；progress为0、最终宽度保持`0x40800000`，消费2个clock samples且未耗尽。该正时限配置不同于已有`timeLimit=0`早退用例，也不同于FailureBased零收缩范围。
2. **【红绿与对拍】** 先增加Rust期望后，测试因缺少第八条生成轨迹失败；补入生成器和TSV后`compression_minimum_width_reference` Debug/Release各6项通过，`cargo fmt --check`通过。使用VS2022 MSVC 14.44.35228.0重建SparrowCoreTests Debug/Release，`--compression-minimum-width`两profile均退出码0；progress、宽度和clock消费与Rust reference一致。
3. **【裁决与范围】** 本轮只增加Rust/C++ reference及比较器期望数，没有生产算法改动。TimeBased duration矩阵由7条增至9条；其他`timeLimit/min`组合、一般计时调度及认证域内可达错误出口仍开放，S07保持部分。

### 补充审计（2026-10-21，TimeBased一秒附近纳秒余数的min阈值）

1. **【Rust reference】** 新增one_second_fraction_rounds_to_min_attempt：timeLimit=1,000,000,032ns，共享时钟样本为[10,000,000,000ns, 11,000,000,033ns]，elapsed为1,000,000,033ns。Rust按Duration::as_secs_f32()分别转换秒与纳秒余数，两者都舍入为1.0f32，ratio=1，step恰为shrinkMin=0.0625（0x3d800000），执行一次attempt；最终宽度0x40700000，消费2个clock samples且未耗尽。
2. **【红绿与对拍】** 先加入reference期望行后，Rust reference测试因轨迹生成器缺少该case而按预期失败；补入真实compression_phase生成轨迹和Rust显式期望后，compression_minimum_width_reference Debug/Release各6项通过，cargo fmt --check通过。VS2022 MSVC 14.44.35228.0下重建SparrowCoreTests Debug/Release，--compression-minimum-width两profile均退出码0。
3. **【裁决与范围】** 本轮只增加Rust/C++ fixture、比较器期望及覆盖摘要，没有生产算法改动。TimeBased共享时钟duration矩阵由8条增至9条；此case只锁定1秒附近的纳秒余数舍入与step == shrinkMin包含行为，不证明其他时限组合或所有有效Duration。S07仍部分。

### 补充审计（2026-10-21，TimeBased一秒附近纳秒舍入跨阈值停机）

1. **Rust reference** 新增one_second_nanosecond_rounding_crosses_min：timeLimit=1,000,000,059ns、elapsed=1,000,000,060ns，绝对时钟从10s起。Rust分段Duration::as_secs_f32()转换后，timeLimit舍入为1.0f32，elapsed跨到0x3f800001；线性f32 step为0x3d7ffffe，低于shrinkMin=0x3d800000。因此不进入attempt，progress=0、最终宽度保持0x40800000，消费2个clock samples且未耗尽。

2. **红绿与对拍** 先加入reference期望行后，Rust测试因轨迹生成器缺少该case按预期失败；补入轨迹和显式期望后，compression_minimum_width_reference Debug/Release各6项通过，cargo fmt --check通过。VS2022 MSVC 14.44.35228.0下重建SparrowCoreTests Debug/Release，--compression-minimum-width两profile均退出码0。

3. **裁决与范围** 本轮只扩展Rust/C++ reference fixture、比较器期望和文档，没有生产算法改动。TimeBased duration矩阵由9条增至10条；本轮未运行C++全量runner。此case锁定一秒附近纳秒分段转换跨f32阈值后的低于min停机，不能外推其他timeLimit/min组合。S07保持部分。

### 补充审计（2026-10-21，TimeBased两秒附近纳秒舍入跨阈值停机）

1. **Rust reference** 新增two_second_nanosecond_rounding_crosses_min：timeLimit=2,000,000,119ns、elapsed=2,000,000,120ns，绝对时钟从10s起。分段Duration::as_secs_f32()转换后，timeLimit舍入为2.0f32（0x40000000），elapsed跨到2.000000238f32（0x40000001）；ratio为0x3f800001，线性step为0x3d7ffffe，低于shrinkMin=0x3d800000。因此不进入attempt，progress=0、最终宽度保持0x40800000，消费2个clock samples且未耗尽。

2. **红绿与对拍** 先加入reference期望行后，Rust reference因生成器缺少第十一条轨迹按预期失败；补入真实compression_phase轨迹和Rust显式期望后，compression_minimum_width_reference Debug/Release各6项通过，cargo fmt --check通过。VS2022 MSVC 14.44.35228.0重建SparrowCoreTests Debug/Release，--compression-minimum-width两profile均退出码0。

3. **裁决与范围** 本轮只扩展Rust/C++ reference fixture、比较器期望和文档，没有生产算法改动。TimeBased duration矩阵由10条增至11条；本轮未运行C++全量runner。此case验证2秒处纳秒舍入半ULP跨越后由attempt转为低于min停机，不能外推其他秒值及timeLimit/min组合。S07保持部分。

### 补充审计（2026-10-21，TimeBased四秒附近纳秒舍入跨阈值停机）

1. **Rust reference** 新增four_second_nanosecond_rounding_crosses_min：timeLimit=4,000,000,238ns、elapsed=4,000,000,239ns，绝对时钟从10s起。分段Duration::as_secs_f32()转换后，timeLimit舍入为4.0f32（0x40800000），elapsed跨到4.000000477f32（0x40800001）；ratio为0x3f800001，线性step为0x3d7ffffe，低于shrinkMin=0x3d800000。因此不进入attempt，progress=0、最终宽度保持0x40800000，消费2个clock samples且未耗尽。

2. **红绿与对拍** 先加入reference期望行后，Rust reference因生成器缺少第十二条轨迹按预期失败；补入真实compression_phase轨迹和Rust显式期望后，compression_minimum_width_reference Debug/Release各6项通过，cargo fmt --check通过。VS2022 MSVC 14.44.35228.0重建SparrowCoreTests Debug/Release，--compression-minimum-width两profile均退出码0。

3. **裁决与范围** 本轮只扩展Rust/C++ reference fixture、比较器期望和文档，没有生产算法改动。TimeBased duration矩阵由11条增至12条；本轮未运行C++全量runner。此case验证4秒处纳秒舍入半ULP跨越后step低于min，不能外推其他秒值及timeLimit/min组合。S07保持部分。
### 补充审计（2026-10-21，TimeBased零点五秒子秒纳秒舍入跨阈值停机）

1. **Rust reference** 新增`half_second_nanosecond_rounding_crosses_min`：timeLimit=500,000,016ns、elapsed=500,000,017ns，绝对时钟从10s起。两者整秒部分均为0，纳秒余数转f32并除以1e9后分别为0.5（0x3f000000）和0.50000006（0x3f000001）；ratio为0x3f800001，step为0x3d7ffffe，低于`shrinkMin=0x3d800000`，不执行attempt，progress=0、宽度保持4.0，消费2个clock samples且未耗尽。
2. **验证** Rust `compression_minimum_width_reference` Debug/Release各6项通过，`cargo fmt --check`通过；VS2022 MSVC 14.44.35228.0重建Debug/Release并运行`--compression-minimum-width`均退出码0。
3. **裁决与范围** TimeBased duration矩阵由12条增至13条。本例覆盖`Duration::as_secs_f32()`中整秒为零的子秒转换路径，与1、2、4秒邻域用例互补；不外推其他小数秒或`timeLimit/min`组合。本轮仅扩展reference fixture、断言和文档，没有生产算法改动，也未运行C++全量runner；S07保持部分。

### 补充审计（2026-10-21，同实例Explore handoff的Compression错误候选复筛）

1. **同实例状态来源** 顶层Rust `optimize`把Explore结束后的problem state和final solution交给`compression_phase`；C++ `SparrowOptimizer`相应传入`ExploreResult.separatorState`与`ExploreResult.solution`。本次筛查只考虑这类内部handoff，不以损坏快照或非法配置作为差分用例。
2. **尝试内错误面** C++ `Compress`可传播`ChangeStripWidth`、`Rollback`、`Separator::Run`和结果`CalculateMetrics`的下层错误。TimeBased在调用前检查step有限且不低于min，随后检查next width有限、为正、实际变窄且不越过minimum width；split来自有限width上的`Uniform01F32`。因此配置/时限数值本身没有给出一个合法输入下稳定触发`InvalidInput`的候选。Rust `compression_phase`返回`SPSolution`；Rust `SPProblem::place_item/change_strip_width/restore`及`Separator::separate`没有等价的`Result`返回通道，内部不变量失败表现为panic或断言。
3. **同实例候选筛查** C++尝试期间仍有布局重建、动态hazard注册、tracker重建及worker搜索错误通道，但这些失败要么表示几何/状态内部不变量失败，要么由Separator及S11/S12下层模块管理。本次源码复筛没有找到一个能由现有同实例Explore handoff稳定进入Compression attempt、且落在已声明认证输入域内的最小候选。S12的Hole/QZone panic用例没有经过Compression；不能把它们直接移作Compression证据，也不能据此证明其他输入下分支不可达。
4. **裁决** 不新增越界或损坏输入fixture，也不把“尚未构造出用例”改写成“不可达”。Compression错误出口继续作为跨模块未证项保留，S07仍部分。本轮仅复核Rust/C++调用链与输入条件并更新记录，未修改生产算法或运行测试。

### 补充审计（2026-10-21，Compression worker零容量profile退出路径）

1. **配置可达性** `CompressionConfig::Validate()`只委托`SeparatorConfig::Validate()`；后者只要求`nWorkers>0`，不会拒绝`nCoordinateDescents=0`。因此零容量是已接受的配置值，不属于非法配置。
2. **触发点** C++ `RunWorkerRound`仅对`ItemLoss>0`的候选调用`FindBestSeparatorMove`。该搜索先报告当前reference placement；Rust `search_placement(ref_pk=Some)`也先报告当前placement。`nCoordinateDescents=0`时两边都会进入`BestSamples`零容量上界：Rust Debug在`size-1`下溢处panic，C++ Debug显式抛`std::underflow_error`；Release两边均保留零容量的wrap/Invalid语义并允许样本超过容量。若Compression attempt确实形成正loss候选，该profile-specific退出路径因此可由同实例handoff触发。
3. **错误分类与证据边界** 这不是`Compress`的`Result`错误，而是S13/S15已有的共享Search/BestSamples profile行为；现有S13边界证据覆盖搜索入口，没有覆盖同实例`optimize → compression_phase`/`Compress`组合。`FindBestSeparatorMove`其余错误要求handle/layout不一致、非正或非有限item最小维，或sampler几何转换失败；普通“没有更好位置”以成功结果中的空transform表示。`Separator::Run`的“无worker结果”保护在正worker数且任务成功/错误/异常均被分类收集时不会独立触发。
4. **Compression入口runtime复核** 新增同实例顶层`Optimize → Compression`定向用例，复用`RustSppCompressWorkerMove.json`与同一warm start，仅将Compression的`nCoordinateDescents`设为0。Rust Debug与C++ Debug均确认Explore停止后进入Compression并发出compression step，随后在worker搜索中分别触发Rust下溢panic和C++ `std::underflow_error`；两边均未产生separation result。Rust Release与C++ Release均无异常，完成一个Compression separation round并返回Optimize结果。C++由VS2022 MSVC 14.44.35228.0分别重建Debug/Release后定向运行通过；Rust Debug/Release定向用例通过，`cargo fmt --check`通过。
5. **裁决** 该profile-specific入口路径已被同一fixture的Rust/C++ Debug与Release runtime用例覆盖，证实差异来自共享Search/BestSamples在Debug与Release的既有行为，并非`Compress`的`Result`错误。其他几何/状态类下层错误仍未证，S07继续保持部分；未改生产算法，也未新增损坏输入fixture。

### 补充审计（2026-10-21，Compression下层Result错误出口复筛）

1. **handoff前提** `Explore`返回的`solution`与`separatorState`都由同一个`SPProblem`/`Separator`保存，且`SparrowOptimizer`只把该结果传入`Compress`。二者可因Explore阶段末尾的下一次宽度收缩而有不同宽度；已有真实双worker Explore→Compression参考用例覆盖了此类交接，不能把“快照宽度不同”本身视作错误条件。
2. **Compress初始化出口** `CompressionConfig::Validate`、初始`CalculateMetrics`、`SPProblem::Create/Restore`和`Separator::Create`的错误分别要求非法配置、无法恢复的输入solution、无效instance或tracker/geometry构造失败。同一个`Optimize`调用已用这些对象完成Explore；返回的两个快照来自该次Explore内部，当前没有发现合法handoff能稳定使这些前置条件失效。
3. **attempt内出口** step/nextWidth不合法时C++直接结束循环；实际进入attempt前已检查有限step、有限且为正的nextWidth、宽度确实变小并满足minimumWidth，split由有限宽度乘有限`Uniform01F32`产生。`ChangeStripWidth`和`Rollback`剩余错误要求移动句柄失效、变换或新container/CDE无法重建；attempt从保存的Explore/当前Separator状态恢复，移动项和句柄都取自该状态。普通重叠或出界是分离算法要处理的状态，不会单独使这些操作返回错误。
4. **worker及结果出口** 正worker数由配置校验保证；每个异步任务最终被收集为成功结果、`Result`错误或异常，因此“无worker结果”保护在正常worker调度中不会独立发生。worker返回错误的条件集中于master snapshot/tracker配对不一致、stale handle、item最小维非法、旋转后几何转换失败或移动后tracker无法更新；metrics及最终restore错误也要求worker产物与同一instance/layout不一致。现有调用链的内部快照成对生成，没有找到合法输入下稳定破坏这些不变量的构造。
5. **保留项与裁决（当次源审计）** 当时仍未穷尽极端但有限的高长宽比几何是否会使`TransformedRustF32`或surrogate旋转转换失败，且没有可复现的认证域fixture；没有据此宣称该类不可达。Rust对应操作多数没有`Result`通道，失败会走panic/断言；listener/observer主动抛异常也属回调异常，不并入几何状态`Result`清单。该次审计没有新增运行用例或修改生产代码；其后已找到并验证合法几何触发点，证据见下节。该记录当时尚未完成顶层错误映射裁决；后续按顶层对照接受panic/`Result`语言级错误通道映射，见本记录末尾；S07其他状态及几何错误出口仍未穷尽。

### 补充审计（2026-10-21，合法极端有限几何在Compression旋转采样中的失败出口）

1. **输入与契约** 使用continuous-rotation simple polygon，顶点为`(999646.4375,-353.5569152832031)`、`(1000353.5625,353.54986572265625)`、`(1000353.5625,353.5569152832031)`、`(999646.4375,-353.54986572265625)`，长宽比约100,000，需求量为2。Rust默认Importer和C++ `MakeRustDefaultImporterConfig()`均接受该有限输入；C++碰撞包围盒为`(403210.875,98.50646973,403918,805.6203003)`，与Rust显示的f32包围盒一致。因而失败不是非法JSON或Importer拒绝造成。
2. **Compression入口** 两侧用同一几何、两个重叠放置副本、strip width 2,000,000、seed 123、1个worker、`nContainerSamples=1`、`nFocussedSamples=1`、`nCoordinateDescents=1`和0.01收缩步；Rust使用定向terminator、C++使用单次迭代上限，使二者都只进入首个Compression attempt。首个进度已上报，之后旋转采样构造退化包围盒`x_min == x_max == -285043.44`，未产生separation result。
3. **profile对照** jagua-rs锁定revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。Rust Debug/Release均在`simple_polygon.rs:141`对无效rectangle执行`unwrap()`并panic；MSVC C++ Debug/Release均从真实`Compress`调用路径返回`InvalidInput`，错误消息为`invalid rectangle`。两侧遇到相同几何退化条件，但失败出口不同：Rust panic，C++可传播错误值；不能据此称该输入路径已行为等价。
4. **验证与裁决** 临时Rust定向测试以`cargo test --locked --test s07_geometry_probe_20261021`分别运行Debug/Release，均捕获到worker panic且断言只有一个compression progress、没有separation result；临时C++探针分别链接MSVC Debug/Release库并确认返回上述错误。前一节“尚无可复现合法几何触发点”的结论由本节运行证据更新。没有修改生产算法；后续已将该行为固化为共享fixture和Rust/C++正式回归用例，并经顶层对照接受panic/`Result`语言级错误通道映射，见下节。S07其他状态及几何错误出口仍未穷尽。

### 补充审计（2026-10-21，极端有限旋转错误出口永久回归）

1. **共享输入** 新增`SparrowCore/Tests/Fixtures/CompressionExtremeRotationWorkerError.json`，由Rust reference与C++测试runner共同读取。Importer接受该连续旋转多边形；两侧确认碰撞包围盒f32值一致。该共享fixture将上节临时探针所用输入保留为可重复用例。
2. **Rust回归** 新增`SparrowCore/Tests/RustReference/tests/compression_extreme_rotation_error_reference.rs`，运行`valid_extreme_rotation_reaches_compression_worker_and_panics_on_invalid_bbox`。测试捕获预期的`invalid rectangle` worker panic，并断言Compression只上报一次`0.01`进度、分离初始进度一次且没有分离结果。锁定revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`下，Debug与Release定向测试均通过；`cargo fmt --check`通过。
3. **C++回归** `SparrowCore/Tests/TestExploration.cpp`中的`RunS07CompressionExtremeRotationError`使用同一fixture，进入真实`Compress`路径并断言返回`InvalidInput`、只上报一次Compression进度和分离初始进度且无分离结果；测试同时进入`RunExplorationTests()`，并可用`--s07-compression-extreme-rotation-error`单独运行。VS2022 MSVC 14.44.35228.0下重建当前SparrowCore Debug/Release库及测试runner后，两种配置的定向用例均通过。首次链接失败是因为既有静态库缺少当前源码中的`SampleLbfUniformForTrace`符号；重建库后链接和运行通过。编译另有TestExploration现存未引用局部变量警告。
4. **直接阶段裁决** 该永久用例稳定记录Rust worker panic与C++ `InvalidInput`的差异，不将其扩展成所有极端几何或任意下层错误出口的结论。后续新增顶层`Optimize`对照后，按路线图关于panic/Result语言机制映射的规则裁决：只要触发条件、失败类别、失败前可观察状态、回调及终止边界一致，可将C++结构化错误作为Rust panic的错误通道映射。本例需以顶层对照证据为准，见下节。

### 补充验收与裁决（2026-10-21，顶层Optimize失败映射）

1. **同一顶层输入** Rust与C++均从共享fixture导入相同continuous-rotation polygon，构造两个重叠放置项作为warm start，使用root seed=123、单worker和1/1/1搜索预算。两侧将Explore时限设为0，使Explore上报初始解后进入Compression；Compression使用固定0.01 FailureBased step。
2. **阶段与可观察状态** Rust Debug/Release与MSVC C++ Debug/Release均依序报告`Exploration → Compression`，只报告一个Explore初始可行解、一次0.01 Compression step和一次分离初始进度，没有separation result，也没有最终解报告。故障仍发生在首个Compression worker构造旋转碰撞包围盒时，错误矩形与直接`Compress`用例相同。
3. **顶层失败载体** Rust `optimize`在锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`下从worker panic并向调用方传播`invalid rectangle`；C++ `Optimize`返回`InvalidInput: invalid rectangle`。Rust/C++ Debug与Release测试均通过上述断言，含分离初始进度回调计数；`cargo fmt --check`通过。该结果证明两边均在同一Compression边界失败且不继续发布最终结果；本轮不推断未捕获panic与各类宿主/进程退出策略等价。
4. **兼容性裁决** 按[迁移路线图](../replication-roadmap.md)第2节所定“语言级失败机制可以不同，但按触发条件、失败类别、失败前后可观察状态、回调与继续执行行为比较”，本例接受C++ `InvalidInput`作为Rust invalid-rectangle panic的语言级错误通道映射。此裁决只覆盖本fixture、列明的profile及Optimize调用边界；不表示panic与Result载体相同，也不证明其他下层错误出口已映射。没有生产算法改动，S07仍部分，其他TimeBased组合及合法状态/几何错误出口继续开放。

### 补充审计（2026-10-21，SeparatorWorker错误出口可达性分类）

1. **handle、快照与tracker出口** `RunWorkerRound`从同一个`SPSolution`构造Problem、Layout和CollisionTracker；候选handle又从当前Layout快照选出。`FindBestSeparatorMoveImpl`中的“handle不在tracker”及“snapshot不一致”，以及移除/放置后的tracker更新错误，要求这组内部状态失配；本轮未找到由Importer接受的外部几何或配置单独触发它们的路径。`Separator::Run`恢复的solution和tracker snapshot也由同一worker结果成对产生，restore错误仍是内部不变量待证项。
2. **最小维度出口** 默认Importer将坐标规整到f32后调用`SimplePolygon::Create`；它要求有限坐标和超过面积容差的非零面积。对该输入契约，polygon的两个包围盒跨度均为正，`minimumDimension <= 0`是防御性检查，不能作为已认证Importer输入的独立错误候选。
3. **sampler出口** C++ `UniformRectSampler::Create`的无可行样本以成功值中的空`optional`返回，与Rust sampler的`None`相对应，不属于错误退出。剩余错误来自离散旋转非有限或`TransformedRustF32`失败；默认Importer校验离散旋转有限性，因此保留的合法输入风险仍是旋转几何转换失败。已固定的极端连续旋转fixture覆盖其中一个稳定触发点，不能外推为所有有效几何都已覆盖。
4. **worker聚合出口** 配置要求`nWorkers > 0`，`Separator::Run`会将worker任务中的Result错误先行传播、异常先行重抛；“没有worker结果”保护只有在正worker配置下worker收集器违反内部约束才会触发。Rust对应的非空worker `expect`同样尚无合法输入触发证据。
5. **裁决与范围** 本轮是生产源码及Importer契约审计，没有找到新的认证域稳定错误fixture；没有新增测试或改生产算法。`RemoveItem`/`PlaceItem`/几何转换和快照恢复的其他内部失败仍未被逐一运行验证，继续作为未证边界记录；S07保持部分。

### 补充验收（2026-10-21，Compression split的NaN质心分支）

1. **fixture与随机流** 新增共享fixture SparrowCore/Tests/Fixtures/CompressionStripShiftTransformError.json，默认Importer接受该有限SPP输入。顶层Optimize使用warm start宽度2^60、放置平移2^57、root seed=4、单worker和固定0.875 FailureBased收缩。变换后质心为NaN；Rust与C++的exploration/compression seed、两次worker seed及split原始u32=224259143一致，split比例约0.052214384，本例分歧不是随机流造成。
2. **差异与红绿** 锁定Rust源码按 centroid > split 筛选待移动项；NaN比较为false，因此该物件留在原位。C++原来用 centroid <= split 后 continue，NaN比较同样为false，反而继续移动物件，ChangeStripWidth在进入separation前返回 InvalidInput: invalid rectangle。新增的C++定向断言先在旧实现下按预期失败；将谓词改为 !(centroid > split) 后，探针确认缩窄成功且物件平移未变。
3. **顶层profile对照** 修复后，MSVC C++ Debug/Release都完成8次Compression attempt：step为8次0.875、16次separation progress、8个separation result、最终宽度2^36。锁定jagua-rs revision f5e2b5ffd14931f17a20ce283a2122e877953ceb的Rust Release同样完成8次attempt并到达2^36；Rust Debug在第二次attempt于qt_hazard.rs:113触发Debug断言，已回调一次separation result但不产生final report。该断言要求多象限活跃的Partial hazard至少有一个子象限收集到相交边；Rust Release会移除此`debug_assert!`并继续解析其余象限。C++ `QuadTree::Constrict`没有对应断言，会直接按邻居presence/质心回填剩余象限，因此此输入下C++ Debug与Rust Release继续执行；profile差异来自Debug诊断断言，不是Release算法分支差异。此次C++谓词修复对齐NaN split分支及Release轨迹，Rust Debug仍在该额外断言处提前终止。
4. **验证与范围** cargo fmt --check通过；compression_strip_shift_error_reference Rust Debug/Release各1项通过；VS2022 MSVC 14.44.35228.0重建SparrowCore库和runner后，--s07-compression-strip-shift-error Debug/Release均退出码0。C++测试先证明旧分支返回invalid rectangle，再覆盖修复后的直接ChangeStripWidth及顶层轨迹；本轮没有运行全量runner。该fixture只覆盖这一NaN质心/split组合，S07其余状态与几何错误出口仍开放。

5. **Debug断言静态裁决** 锁定Rust `qt_hazard.rs:93-113`先为四个象限收集相交边，再以`debug_assert!`检查至少一个子象限存在Partial边；检查失败后，Rust Debug panic，Rust Release则继续对缺少边的象限执行邻居presence或质心分类。C++ `Quadtree.cpp:356-414,416-459`具有相同的“先收集边、再分类未解析象限”主流程，但没有该Debug断言。保持C++现有Debug行为：这条诊断断言不改变Release产物，且跨语言panic/断言终止方式也不能用C++裸断言视为等价；将Rust Debug提前终止作为profile残差记录，不把它计作Release搜索算法差异。本裁决为源码静态核对，没有新增或运行测试。

### 补充验收（2026-10-21，TimeBased单位端点范围与零宽度停机）

1. **固定输入** 新增`time_based_duration::unit_equal_range_stops_before_zero_width`共享时钟轨迹：timeLimit=1s、`shrinkMax=shrinkMin=1.0f`、绝对时钟start=end=10s。Rust计算step=`0x3f800000`，下一宽度为0；由于单件方形fixture的`min_width`为`0x3fffffe8`，压缩在宽度下界检查处停止，不发Compression progress，最终宽度保持`0x40800000`（4.0），消耗2个时钟样本且未耗尽。
2. **Rust参考与C++对照** Rust trace generator和literal期望、共享TSV fixture均加入该行；C++测试从共享fixture读入同一timeLimit、range及clock样本，并断言progress为0、最终宽度和clock消耗匹配。C++只将duration记录数量期望从13更新为14，没有修改生产算法。
3. **验证范围** 锁定Rust reference Debug/Release定向测试各6项通过，`cargo fmt --check`通过；VS2022 MSVC 14.44.35228.0在链接当前`build_Kit_MSVC_2019_x64` SparrowCore/JaguarCore产物的独立测试目录中重建runner后，`--compression-minimum-width` Debug/Release均退出码0。编译仍出现TestExploration既有C4189未引用局部变量警告。本轮未运行C++全量runner。
4. **裁决** TimeBased共享clock duration/min矩阵由13条增至14条，新增配置为合法的单位收缩端点，并确认两侧均在实际尝试和progress回调前被`min_width`守卫拦截。未发现该分支差异；其他TimeBased配置组合、认证宽度和合法状态/几何错误出口仍未穷尽，S07保持部分。

### 补充验收（2026-10-21，TimeBased零步长参数端点）

1. **固定输入** 新增`time_based_duration::zero_equal_range_stops_on_unchanged_width`共享时钟轨迹：timeLimit=1s、`shrinkMax=shrinkMin=0.0f`、绝对时钟start=end=10s。公式步长为0，下一宽度仍为`0x40800000`（4.0）；no-shrink守卫在progress回调前退出，最终宽度不变，消费2个clock samples且未耗尽。
2. **Rust参考与C++对照** Rust trace generator、literal期望和共享TSV fixture均加入该行；C++比较器读取同一参数，并将duration记录数量期望从14更新为15。未修改生产算法。
3. **验证范围** Rust Debug/Release的`compression_minimum_width_reference`各6项通过，`cargo fmt --check`通过；VS2022 MSVC 14.44.35228.0使用当前`build_Kit_MSVC_2019_x64` SparrowCore/JaguarCore产物重建独立runner后，`--compression-minimum-width` Debug/Release均退出码0。编译有既存TestExploration C4189未引用局部变量警告；本轮未运行C++全量runner。
4. **裁决** TimeBased共享clock duration/min矩阵由14条增至15条；该零步长端点由两侧相同的宽度不变守卫终止，没有attempt或progress差异。其他TimeBased参数组合、认证宽度和合法状态/几何错误出口仍未穷尽，S07保持部分。

### 补充验收（2026-10-21，TimeBased全范围半时限步长）

1. **固定输入** 新增`time_based_duration::full_range_midpoint_uses_half_step`共享时钟轨迹：timeLimit=1s、`shrinkMax=1.0f`、`shrinkMin=0.0f`、绝对时钟start=10s/end=10.5s。Rust计算ratio=0.5、step raw bits=`0x3f000000`；候选宽度为2.0，高于fixture的`min_width=0x3fffffe8`，因此实际执行一次attempt并发出一次progress。attempt结束后最终宽度恢复为`0x40800000`（4.0），消耗2个clock samples且未耗尽。
2. **Rust参考与C++对照** Rust trace generator、literal期望和共享TSV fixture均加入该行；C++ comparison runner以同一配置和时钟序列核对step bits、progress数、最终宽度及clock消耗。MSVC Debug/Release均与Rust参考一致；C++ duration记录数期望由15更新为16。
3. **验证范围** Rust Debug/Release的`compression_minimum_width_reference`各6项通过，`cargo fmt --check`通过；VS2022 MSVC 14.44.35228.0使用当前`build_Kit_MSVC_2019_x64` SparrowCore/JaguarCore产物重建独立runner后，`--compression-minimum-width` Debug/Release均退出码0。编译有既存TestExploration C4189未引用局部变量警告；本轮未运行C++全量runner。
4. **裁决** TimeBased共享clock duration/min矩阵由15条增至16条；全范围端点在半时限下按f32插值产生0.5步长，候选宽度通过下界检查并进入attempt，两侧轨迹一致。其他TimeBased参数组合、认证宽度和合法状态/几何错误出口仍未穷尽，S07保持部分。

### 补充验收（2026-10-21，TimeBased最大正时限与大时长f32舍入）

1. **固定输入** 新增`time_based_duration::maximum_time_limit_one_f32_ulp_before_deadline`：`timeLimit=INT64_MAX=9223372036854775807ns`，`shrinkMax=0.125f`、`shrinkMin=0.0625f`，时钟从0走到`9223371012854775807ns`，即距deadline 1024s。Rust计算step raw bits=`0x3d800001`，比min高1ULP，执行一次attempt并保留宽度`0x40700000`（3.75），消费2个clock samples且未耗尽。
2. **边界意义与C++对照** 该时限是C++ `std::chrono::nanoseconds`的最大正值；elapsed与timeLimit秒数处于约`2^33`量级，f32步进为1024秒。两侧对同一大时长做秒转换后得到相同step、progress数、最终宽度和clock消耗，MSVC Debug/Release均通过。
3. **Rust验证** Rust Debug/Release的`compression_minimum_width_reference`各6项通过，`cargo fmt --check`通过。共享TSV与C++比较器均加入该最大正时限行，duration记录数期望从16更新为17；未改生产算法。
4. **范围** 本例只核对C++可表示的最大正纳秒时限及相邻f32量化点，不外推到更大的Rust `Duration`；其他TimeBased时限/min组合、认证宽度和合法状态/几何错误出口仍未穷尽，S07保持部分。

### 补充验收（2026-10-21，TimeBased最小正时限）

1. **固定输入** 新增`time_based_duration::one_nanosecond_deadline_keeps_f32_seconds_nonzero`：`timeLimit=1ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust把1ns转换为非零f32秒值，ratio=1，step raw bits=`0x3d800000`（等于shrinkMin），执行一次attempt并得到宽度`0x40700000`（3.75）；消费2个clock samples且未耗尽。
2. **C++对照** 该用例确认C++ `DurationAsRustSecondsF32(1ns)`没有变为零；MSVC Debug/Release均与Rust reference的step bits、progress数、最终宽度和clock消耗一致。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项通过，`cargo fmt --check`通过；VS2022 MSVC 14.44.35228.0使用当前`build_Kit_MSVC_2019_x64` SparrowCore/JaguarCore产物重建独立runner后，`--compression-minimum-width` Debug/Release均退出码0。共享fixture和C++比较器加入该行，duration记录数由17更新为18；未改生产算法。该结论仅覆盖1ns最小正时限，S07仍部分。
### 补充验收（2026-10-21，TimeBased最小正时限的起始采样）

1. **固定输入** 新增`time_based_duration::one_nanosecond_at_start_uses_maximum_step`：`timeLimit=1ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s开始且elapsed=0。Rust将elapsed转换为0秒、timeLimit保持非零，ratio=0，step raw bits=`0x3e000000`（等于shrinkMax），执行一次attempt并得到宽度`0x40600000`（3.5）；消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同1ns时限、零elapsed和计时样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗对齐Rust reference。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由19条增至20条；只扩展reference生成器、测试、fixture和记录，未修改生产算法。S07仍部分，其他TimeBased组合、宽度/min_width组合与认证错误出口继续开放。
### 补充验收（2026-10-21，TimeBased最小时限的一半elapsed）

1. **固定输入** 新增`time_based_duration::two_nanosecond_half_elapsed_uses_midpoint_step`：`timeLimit=2ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns与2ns分别转换为`0x3089705f`和`0x3109705f`，ratio精确为`0x3f000000`（0.5）；step为`0x3dc00000`（0.09375），执行一次attempt并得到宽度`0x40680000`（3.625），消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同2ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗对齐Rust reference。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由19条增至20条；只扩展reference生成器、测试、fixture和记录，未修改生产算法。该用例证明这组纳秒值的秒转换比例精确为0.5，不外推所有小Duration组合；S07仍部分。

### 补充验收（2026-10-21，TimeBased纳秒比例的f32舍入）

1. **固定输入** 新增`three_nanosecond_limit_one_nanosecond_elapsed_uses_rounded_third_step`：`timeLimit=3ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和3ns分别转换为`0x3089705f`、`0x314e288f`；二者相除得到`0x3eaaaaaa`，并非理想1/3对应的`0x3eaaaaab`。线性插值的step为`0x3dd55556`，执行一次attempt后final width为`0x40655555`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同3ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗对齐Rust reference。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由20条增至21条；只扩展reference生成器、测试、fixture和记录，未修改生产算法。该用例锁定3ns时限下Duration分段转f32后的比例与插值舍入，不外推其他小Duration组合；S07仍部分。

### 补充验收（2026-10-21，TimeBased四纳秒时限的四分之一elapsed）

1. **固定输入** 新增`four_nanosecond_limit_one_nanosecond_elapsed_uses_quarter_step`：`timeLimit=4ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将4ns转换为`0x3189705f`，与1ns的`0x3089705f`相除得到四分之一（`0x3e800000`）；step为`0x3de00000`（0.109375），执行一次attempt后final width为`0x40640000`（3.5625），消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同4ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗对齐Rust reference。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由21条增至22条；只扩展reference生成器、测试、fixture和记录，未修改生产算法。该用例锁定4ns时限下的四分之一进度，不外推其他Duration组合；S07仍部分。

### 补充验收（2026-10-21，TimeBased五纳秒时限的五分之一elapsed）

1. **固定输入** 新增`five_nanosecond_limit_one_nanosecond_elapsed_uses_fifth_step`：`timeLimit=5ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和5ns分别转换为`0x3089705f`、`0x31abcc77`；二者相除得到0.2（`0x3e4ccccd`），线性step为`0x3de66666`。执行一次attempt后final width为`0x40633333`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同5ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗对齐Rust reference。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由22条增至23条；只扩展reference生成器、测试、fixture和记录，未修改生产算法。该用例锁定5ns时限下的转换与五分之一进度，不外推其他Duration组合；S07仍部分。

### 补充验收（2026-10-21，TimeBased六纳秒时限的六分之一elapsed）

1. **固定输入** 新增`six_nanosecond_limit_one_nanosecond_elapsed_uses_sixth_step`：`timeLimit=6ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和6ns分别转换为`0x3089705f`、`0x31ce288f`；二者相除得到`0x3e2aaaaa`，比理想1/6舍入值低1 ULP。step为`0x3deaaaab`，执行一次attempt后final width为`0x4062aaab`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同6ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗对齐Rust reference。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由23条增至24条；只扩展reference生成器、测试、fixture和记录，未修改生产算法。该用例锁定6ns时限下的分段转换和六分之一进度，不外推其他Duration组合；S07仍部分。
### 补充验收（2026-10-21，TimeBased七纳秒时限的七分之一elapsed）

1. **固定输入** 新增`time_based_duration::seven_nanosecond_limit_one_nanosecond_elapsed_uses_seventh_step`：`timeLimit=7ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和7ns分别转换为`0x3089705f`、`0x31f084a7`；二者相除得到`0x3e124924`，step为`0x3dedb6dc`，执行一次attempt后final width为`0x40624924`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同7ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference一致。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由24条增至25条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定7ns时限下的转换及七分之一进度，S07仍部分。
### 补充验收（2026-10-21，TimeBased八纳秒时限的八分之一elapsed）

1. **固定输入** 新增`time_based_duration::eight_nanosecond_limit_one_nanosecond_elapsed_uses_eighth_step`：`timeLimit=8ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和8ns分别转换为`0x3089705f`、`0x3209705f`；二者相除得到精确的`0x3e000000`（1/8），step为`0x3df00000`，执行一次attempt后final width为`0x40620000`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同8ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference一致。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`各6项及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由25条增至26条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例锁定2的幂纳秒时限下的精确比例点，S07仍部分。

### 补充验收（2026-10-21，TimeBased九纳秒时限的九分之一elapsed）

1. **固定输入** 新增`time_based_duration::nine_nanosecond_limit_one_nanosecond_elapsed_uses_ninth_step`：`timeLimit=9ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和9ns分别转换为`0x3089705f`、`0x321a9e6b`；比例为`0x3de38e39`，插值step为`0x3df1c71c`，执行一次attempt后final width为`0x4061c71c`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同9ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由26条增至27条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定9ns时限下的秒转换比例与九分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，TimeBased十纳秒时限的十分之一elapsed）

1. **固定输入** 新增`time_based_duration::ten_nanosecond_limit_one_nanosecond_elapsed_uses_tenth_step`：`timeLimit=10ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和10ns分别转换为`0x3089705f`、`0x322bcc77`；比例为`0x3dcccccd`，插值step为`0x3df33333`，执行一次attempt后final width为`0x4061999a`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同10ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由27条增至28条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定10ns时限下的秒转换比例与十分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，TimeBased十一纳秒时限的十一分之一elapsed）

1. **固定输入** 新增`time_based_duration::eleven_nanosecond_limit_one_nanosecond_elapsed_uses_eleventh_step`：`timeLimit=11ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和11ns分别转换为`0x3089705f`、`0x323cfa83`；比例为`0x3dba2e8b`，插值step为`0x3df45d17`，执行一次attempt后final width为`0x4061745d`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同11ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由28条增至29条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定11ns时限下的秒转换比例与十一分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，TimeBased十二纳秒时限的十二分之一elapsed）

1. **固定输入** 新增`time_based_duration::twelve_nanosecond_limit_one_nanosecond_elapsed_uses_twelfth_step`：`timeLimit=12ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和12ns分别转换为`0x3089705f`、`0x324e288f`；比例为`0x3daaaaaa`，比理想1/12舍入值低1 ULP；插值step为`0x3df55555`，执行一次attempt后final width为`0x40615555`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同12ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由29条增至30条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定12ns时限下的秒转换比例与十二分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，TimeBased十三纳秒时限的十三分之一elapsed）

1. **固定输入** 新增`time_based_duration::thirteen_nanosecond_limit_one_nanosecond_elapsed_uses_thirteenth_step`：`timeLimit=13ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和13ns分别转换为`0x3089705f`、`0x325f569b`；比例为`0x3d9d89d8`，插值step为`0x3df62762`，执行一次attempt后final width为`0x40613b14`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同13ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由30条增至31条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定13ns时限下的秒转换比例与十三分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，TimeBased十四纳秒时限的十四分之一elapsed）

1. **固定输入** 新增`time_based_duration::fourteen_nanosecond_limit_one_nanosecond_elapsed_uses_fourteenth_step`：`timeLimit=14ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和14ns分别转换为`0x3089705f`、`0x327084a7`；比例为`0x3d924924`，step为`0x3df6db6e`，执行一次attempt后final width为`0x40612492`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同14ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由31条增至32条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定14ns时限下的秒转换比例与十四分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，TimeBased十五纳秒时限的十五分之一elapsed）

1. **固定输入** 新增`time_based_duration::fifteen_nanosecond_limit_one_nanosecond_elapsed_uses_fifteenth_step`：`timeLimit=15ns`、`shrinkMax=0.125f`、`shrinkMin=0.0625f`，绝对时钟从10s前进到10s+1ns。Rust `Duration::as_secs_f32()`将1ns和15ns分别转换为`0x3089705f`、`0x3280d959`；比例为`0x3d888889`，插值step为`0x3df77777`，执行一次attempt后final width为`0x40611111`，消费2个clock samples且未耗尽。
2. **C++对照** 共享fixture提供相同15ns时限、1ns elapsed和绝对时钟样本；C++ MSVC Debug/Release按step bits、progress数、最终宽度和clock消耗与Rust reference对齐。
3. **验证与裁决** Rust Debug/Release的`compression_minimum_width_reference`及`cargo fmt --check`通过；C++ `--compression-minimum-width` MSVC Debug/Release通过。TimeBased共享clock duration矩阵由32条增至33条；只扩展reference生成器、测试、fixture和记录，没有修改生产算法。该用例只锁定15ns时限下的秒转换比例与十五分之一elapsed插值舍入，S07仍部分。

### 补充验收（2026-10-21，离散角退化包围盒的min_width错误出口）

1. **共享输入** 新增`CompressionDiscreteRotationWorkerError.json`，沿用默认Importer接受的有限细长多边形，离散允许角设为0°和315°，warm start仍按合法的0°放置两个重叠副本。Importer两侧都接受该输入，原始碰撞包围盒f32值与现有continuous-rotation fixture一致。
2. **Rust观察** 锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。Rust `minimum_strip_width`在计算离散角315°变换时得到`y_min == y_max`的无效rectangle并panic；Compression progress与separation progress均为0。Rust Debug/Release正式回归捕获同一错误。
3. **C++差异与修复** C++旧逻辑会跳过失败的旋转变换并继续计算其他角，进而把失败延后到worker阶段，导致多发Compression progress。`CalculateMinimumStripWidth`现返回`Result<float>`并传播`TransformedRustF32`错误；`Compress`在发出Compression progress前返回`InvalidInput`。用于最小宽度reference的内部trace接口也改为Result，以保留并可核验该错误。
4. **验证与范围** C++ MSVC 14.44 Debug/Release的`--s07-compression-discrete-rotation-error`、既有`--s07-compression-extreme-rotation-error`及`--compression-minimum-width`均通过；Rust `compression_extreme_rotation_error_reference` Debug/Release各3项通过，`cargo fmt --check`通过。本例只覆盖该Importer接受的多边形与315°离散角；其他合法几何转换错误仍未穷尽，S07保持部分。

### 退出审查（2026-10-21，认证矩阵计数与残留范围）

1. **矩阵计数** 当前固定reference矩阵包含40条FailureBased轨迹、33条共享时钟TimeBased duration行、宽度与min_width舍入边界、seed=3 warm-start、seed=42 worker move/retry，以及记录的E5自然阶段样例。33条duration行是具体fixture计数，不表示覆盖所有Duration值；其中含2–15ns且elapsed=1ns的比例/步长轨迹及多秒附近纳秒余数舍入边界。
2. **已裁决差异** C++额外的限流/feasible防御保持为扩展；ERR-CMPR-DEBUG-QTREE的固定seed Debug差异已在C++ Debug加入对应不变量检查，并通过Rust/C++ Debug/Release对拍。本轮Rust Debug/Release reference各1项、C++ Debug/Release的strip-shift、连续旋转错误、离散旋转错误及`+Inf` min_width早停四项均通过。Rust Debug的∞ > NaN worker断言仍是单独登记的profile行为。自然时限验收不要求跨调度逐轮轨迹一致，Rust Debug自然运行仍有已记录的断言阻断。
3. **仍未满足退出条件** ERR-CMPR-ROTATION-TRANSFORM将极端连续旋转worker失败与离散角min_width失败分别固定在两个阶段；两者只认证列明触发点。其余合法几何/worker错误出口仍未完整映射或证明不可达；TimeBased其余配置域与任意FailureBased因子也未获普遍认证。依据迁移路线图完成定义，S07继续标为“部分”。
4. **退出裁决** 本轮完成S07有界退出审查。40条FailureBased轨迹、33条TimeBased duration行、宽度/min_width边界、seed=3/42状态轨迹、已记录的E5阶段样例、两条Importer接受的几何变换失败路径及固定seed quadtree Debug fixture构成当前认证集。S07工作项仍标“部分”，因为其他合法几何/worker错误未获全域证明；不再按相邻数值或任意seed扩样。`ERR-CONFIG`与不一致snapshot作为API契约残余继续登记。今后只有出现新的具体Rust/C++分歧、支持输入域改变或上游修改影响该算法时，才重开S07并新增fixture。

### 错误出口源码盘点（2026-10-21）

对照Rust `compression_phase`与C++ `Compression.cpp`的全部`Result`传播点，按是否处于S07声明有效输入域分类：

1. **配置和调用契约外状态** `CompressionConfig::Validate`拒绝非有限/越界shrink值、非法`failureDecay`和负时限，并继续调用`SeparatorConfig::Validate`；Rust `CompressionConfig`本身是无校验的值结构。这部分归`ERR-CONFIG`，S01已固定共享有效闭区间，S07不把额外拒绝行为算作算法等价。`CalculateMetrics`、`SPProblem::Create/Restore`及`Separator::Create`也会拒绝无效实例、初始solution或separator snapshot；本轮没有将故意不一致的直接API对象纳入有效输入集。
2. **Importer接受、算法可达的几何错误** `CalculateMinimumStripWidth`的离散角变换错误由`CompressionDiscreteRotationWorkerError`固定；连续角在最小宽度估算中跳过，之后worker移动的变换错误由`CompressionExtremeRotationWorkerError`固定。两条均是有效fixture，归`ERR-CMPR-ROTATION-TRANSFORM`。
3. **attempt内传播面** 恢复宽度、Rollback、按split缩宽、`Separator::Run`和结果指标重建的`Result`错误都会向`Compress`调用者传播。已知连续旋转fixture实际命中worker移动/Separator传播路径；本轮静态盘点没有找到第三个已证明可达的Importer接受输入触发点，但这些下层错误的全域不可达性也未由源码证明。
4. **裁决** 该盘点未产生新fixture或生产代码改动；有效几何/worker的其他触发点仍开放。配置与不一致snapshot属于另一类输入契约差异，不能用本轮静态分类宣称双侧行为等价。S07继续为“部分”。

### 有界退出裁决（2026-10-21）

1. **退出对象** 本次关闭的是S07主动扩展审查，不是把整个Compression算法宣布为普遍等价。现有固定矩阵和列明的profile对照作为后续工作使用的验收基线。
2. **保留残余** 其他合法几何/worker错误触发点、全域配置组合和`ERR-CONFIG`的入口映射仍未普遍认证；这些边界按错误分类表保留，不计作已通过证据。
3. **重开条件** 仅在新增的受支持输入出现可复现Rust/C++差异、Sparrow配置/算法支持域发生变化，或上游revision更新影响相关逻辑时重开；不因“还可以再加相邻值/seed”单独重开。
4. **顺序** S07退出审查完成，清单状态仍为“部分”；按S线顺序转入S08未闭合项。S08已完成顶层错误路线源码分层，但配置入口映射与一般阶段下层错误触发仍未逐项验证；未经回调等待的完整时限输入域仍开放。deadline同刻比较器已通过源码语义裁决。
