# S21. util/listener.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/listener.rs`（62 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationListener.h` + `OptimizationObserver.h` + `Source/Optimizer/OptimizationListener.cpp`
**判定**: Rust 原生监听器回调的源码触发点已逐项映射；默认 cold-start 顺序/计数、分离进度代表字段、异常截断与 FINAL 边界已有跨语言证据。C++ 独有的 `OnProgress`/`OnImprovement` 已从 `OptimizationListener` 移至显式可选的 `OptimizationObserver`；不传 observer 时，listener 只暴露 Rust 原生事件。传入 observer 会启用 C++ 扩展回调，这条路径仍可能改变停止/异常结果。`OptimizationHistory` 继续作为 C++ 返回值扩展。

## 逐条比对

### `trait SolutionListener`（Rust 4-14：`report` + 4 个默认空实现的方法）
- C++ 对应：`class OptimizationListener`（OptimizationListener.h，虚基类）+ 默认空实现（OptimizationListener.cpp）。
- 判定：必需事件与默认 no-op 契约对应。
- 证据与说明：Rust `report` 必须实现 ↔ C++ `OnSolution` 现为纯虚函数；Rust 其余四个方法有默认空实现 ↔ C++ 其余四个虚函数提供空默认实现（cpp:7-19）。Rust `DummySolListener` 与 C++ `listener=nullptr` 或显式 no-op 子类构成实际 no-op 对照：
  - `report(&mut self, ReportType, &SPSolution)` → `OnSolution(const OptimizationSolutionReport&)`（h:94）
  - `report_phase(OptimizationPhase)` → `OnPhase(OptimizationPhase)`（h:93）
  - `report_separation_progress(SeparationProgress)` → `OnSeparationProgress(const SeparationProgress&)`（h:95）
  - `report_separation_result(SeparationResult)` → `OnSeparationResult(const SeparationResult&)`（h:96）
  - `report_compression_progress(f32)` → `OnCompressionProgress(double)`（h:97）
  差异点：Rust `report` 的解参数是 `(ReportType, &SPSolution)` 二元组，C++ 打包为 `OptimizationSolutionReport` 结构（含 type/phase/iteration/solution/stripWidth/density/collisionLoss/isFeasible 八字段，h:29-41）——信息超集，见下条。

### `ReportType` 枚举（Rust 42-53）
- C++ 对应：`enum class OptimizationReportType`（h:20-27）。
- 判定：一致（五个值逐一对应）。
- 证据与说明：`ExplFeas→EXPLORATION_FEASIBLE`、`ExplInfeas→EXPLORATION_INFEASIBLE`、`ExplImproving→EXPLORATION_IMPROVING`、`CmprFeas→COMPRESSION_FEASIBLE`、`Final→FINAL`——语义与注释（feasible/infeasible/improving/final）逐项一致。

### `OptimizationPhase` 枚举（Rust 17-20）
- C++ 对应：`enum class OptimizationPhase`（h:12-18）。
- 判定：差异（C++ 超集元数据）。
- 证据与说明：Rust 两值 `Exploration/Compression`；C++ 枚举有四值 `CONSTRUCTION/EXPLORATION/COMPRESSION/COMPLETE`。C++ `OnPhase` 只上报探索与压缩，与 Rust `report_phase` 的阶段序列一致；`CONSTRUCTION` 只供可选 `OptimizationObserver` 进度事件使用，`COMPLETE` 保留为最终解报告的 `phase` 元数据，不触发完成阶段回调。

### `SeparationProgress` 结构体（Rust 24-30）
- C++ 对应：`struct SeparationProgress`（h:48-54）。
- 判定：一致。
- 证据与说明：`strip_width→stripWidth`、`density→density`、`iteration→iteration`、`min_loss→minimumLoss`，四字段全量对应（f32→double 既定差异）；Rust 文档注释"Iteration zero describes the initial layout"的语义由 S06/S04 侧保证。

### `SeparationResult` 结构体（Rust 32-39）
- C++ 对应：`struct SeparationResult`（h:56-63）。
- 判定：一致。
- 证据与说明：`success/elapsed_seconds→elapsedSeconds/total_evals→totalEvaluations/total_moves→totalMoves/iterations`，五字段全量对应。

### `DummySolListener`（Rust 56-62：空实现）
- C++ 对应：无需传监听器即可（`Optimize` 的 `listener` 参数默认 `nullptr`，SparrowOptimizer.h:20-24；`PublishSolutionReport` 对 nullptr 判空，h:100-108）。
- 判定：差异（机制不同，语义等价）。
- 证据与说明：Rust bench 用 `DummySolListener` 占位（bench.rs:98/105）；C++ 以空指针默认参数表达"无监听"，无需 dummy 类型。

### C++ 扩展观察器 `OptimizationObserver`（`OptimizationObserver.h`）
- 判定：C++ 额外。
- 证据与说明：C++ 增设带 `phase/iteration/stripWidth/density/collisionLoss/isFeasible` 的进度事件。构造成功后额外上报构造进度（`SparrowOptimizer.cpp`）；Explore 在初始报告后和每次 separator 返回后上报，成功改进时发 `OnImprovement`（`Exploration.cpp`）；Compression 在尝试开始、separator 返回后上报，成功尝试时发 `OnImprovement`（`Compression.cpp`）。Rust 没有这两个独立回调。它们现在由独立接口 `OptimizationObserver` 接收，通过 `Optimize`、`Explore` 或 `Compress` 的最后一个可选参数显式启用；`OptimizationListener` 本身不再含这两个方法。调用 observer 时，其同步 stop/异常影响仍属于 C++ 扩展语义，严格 Rust 对照必须不传 observer。

### `OptimizationHistory` + `MakeSolutionReport` + `PublishSolutionReport`（h:43/65-108）
- 判定：C++ 额外。
- 证据与说明：C++ 把每个解报告同时落进 `std::vector<OptimizationSolutionReport>` 历史（`PublishSolutionReport` 先 push_back 再回调，h:100-108），随 `SparrowOptimizationResult::history` 返回（SparrowOptimizer.h:13-18）——Rust 侧监听器自行决定是否留存（SvgExporter 落盘、TUI 推信道），`optimize()` 不返回历史。这是 C++ 把"解的留痕"从监听器职责上移到返回值的设计。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `report(ReportType, &SPSolution)` 参数形态 | 差异 | C++ 打包为 `OptimizationSolutionReport`（八字段超集：多 phase/iteration/stripWidth/density/collisionLoss/isFeasible），监听端免于自查解 | listener.rs:5 vs h:29-41 |
| 2 | `OptimizationPhase` 两值 vs 四值 | API 枚举差异 | C++ 多出 CONSTRUCTION/COMPLETE；CONSTRUCTION 仅供可选 observer 进度，COMPLETE 只作最终报告元数据；`OnPhase` 的探索/压缩序列与 Rust 一致 | listener.rs:17-20 vs h:12-18；SparrowOptimizer.cpp |
| 3 | `DummySolListener` | 差异（机制） | C++ 用 `listener=nullptr` 默认参数替代 dummy 占位对象 | listener.rs:56-62 vs SparrowOptimizer.h:20-24 |
| 4 | `OptimizationObserver` 进度扩展 + `OptimizationHistory` 留存 | C++额外（已隔离） | 进度/改进只通过单独可选 observer 提供；Rust parity 路径不传 observer。history 仅是额外返回数据 | `OptimizationObserver.h`、`SparrowOptimizer.h`、`OptimizationHistory` vs（Rust 无） |

## 结论
Rust 原生事件类型及完成阶段边界已有实现映射和定向跨语言回归。`OptimizationListener` 不再暴露额外的 `OnProgress`/`OnImprovement`；在不传 `OptimizationObserver` 的 parity 路径中，C++ 原生 listener 事件序列不含 Rust 不存在的额外回调。显式传入 observer 后启用的是独立 C++ 扩展路径，其同步 stop/异常仍可能改变运行结果。`OptimizationHistory` 是不参与 listener 调用序列的 C++ 返回值扩展。

### 完成阶段回调边界（2026-10-10）

- Rust 在压缩阶段返回后直接调用 `SolutionListener::report(ReportType::Final, &cmpr_sol)`，随后返回最终解（`CodeResource/sparrow/src/optimizer/mod.rs:77-80`）；此处没有独立完成进度或完成 phase 回调。
- C++ 对应序列为 `PublishSolutionReport(... FINAL, phase=COMPLETE, iteration=0, ...)` 后返回（`SparrowCore/Source/Optimizer/SparrowOptimizer.cpp::OptimizeWithTerminator`）。`PublishSolutionReport` 先把报告加入本次调用的 history，再同步调用 `OnSolution`（`OptimizationListener.h::PublishSolutionReport`）。已删除此前位于 FINAL 报告之前的 `OnProgress(COMPLETE)`；`COMPLETE` 枚举值和最终报告元数据保留。
- FINAL 回调请求 `RequestStop()` 时，优化主流程已完成后续算法工作，仍返回最终结果；FINAL 回调抛出的 C++ 异常继续向调用方传播，调用不会返回结果。Rust 对应回调 panic 会中止 `optimize()` 的正常返回。停止与异常的语言机制不同，但该最终回调边界相同。
- 跨语言回归：`TestZeroTimeoutOptimizeMatchesRustReference` 使用已有零时限 Rust trace，断言完成边界只出现 FINAL 解报告，检查 `phase=COMPLETE`、`iteration=0`、可行性与最终布局指标，并验证 stop/exception 出口。该用例固定了零时限终止条件；其他终止原因下的最终报告仍由共用 `OptimizeWithTerminator` 路径保证，尚未逐种重跑。

### 补充审计（2026-09-29）
1. **【核对确认：文件覆盖完整性】** listener.rs 62 行全量复核：trait 5 方法（report + 4 默认空实现）、OptimizationPhase 2 值、SeparationProgress 4 字段、SeparationResult 5 字段、ReportType 5 值、DummySolListener——主文均有条目，无遗漏项。
2. **【遗漏补记：report 方法的必要性与 Dummy 的差异】** Rust trait 中 `report` **无默认实现**（强制实现者处理解事件，listener.rs:5）；其余 4 方法有默认空体。C++ 五个虚函数全部有空默认体（cpp:5-33）——即 C++ 允许全空监听器而 Rust 至少需实现 report（DummySolListener 提供空 report）。纯 API 形态差异，无行为影响，补记闭合。
3. **【浅覆盖补全：消费方覆盖验证】** 现场核验 Rust 侧全部实现者：SvgExporter（report 实写，svg_exporter.rs:39-65）、DummySolListener（bench.rs:98/105 占位）、TuiListener（runtime.rs:193-227，**五方法全部实写**——report/report_phase/report_separation_progress/report_separation_result/report_compression_progress 经 mpsc 信道推 UI）。C++ 侧消费方：CLI 不传（nullptr）、Bench 不传、Demo 经 WASM 结果 JSON。S24 的"事件面无缺口"判定据此成立（TuiListener 消费的 5 事件在 OptimizationListener 均有对应虚方法）。
4. **【2026-10-07 执行记录：回调触发 phase stop】** Rust 与 C++ 完整 cold-start 用例在初始 exploration feasible 报告回调里请求 phase stop；Rust 使用 `CtrlCTerminator` 并设置其 Ctrl-C 标志，C++ 使用 `RequestPhaseStop()`。事件 fixture 确认停止发生在首个 separation 之前，随后 compression phase 正常开始。该断言覆盖一个 listener→terminator 控制路径，不证明其他报告类型、progress callback 或错误回调的停止/异常行为。

### 剩余边界审计（2026-10-12）

本节以当前 `CodeResource/sparrow` 与 `SparrowCore` 源码为基线，区分源码路径审计和 fixture 实际覆盖。Rust 原生 callback 的触发语义如下：

| Rust 原生回调 | 触发条件/次数 | SparrowCore 对应与差异 |
|---|---|---|
| `report_phase(Exploration/Compression)` | 构造成功后，在进入两个阶段前各一次；没有完成阶段回调 | `OnPhase(EXPLORATION/COMPRESSION)` 同序；`COMPLETE` 只留在 FINAL 报告元数据中，不触发 `OnPhase`/完成进度 |
| `report(ExplFeas)` | Explore 开始时报告初始可行快照一次；之后仅在更窄宽度的分离结果可行时报告 | `OnSolution(EXPLORATION_FEASIBLE)` 对应；报告中的 phase、iteration、指标是 C++ 附加字段 |
| `report(ExplInfeas)` | 每次 Explore 分离返回非零 loss 后一次 | `OnSolution(EXPLORATION_INFEASIBLE)` 对应；在分离结果之后、Explore 池处理之前 |
| `report(ExplImproving)` | Separator 每次找到严格低于历史最小值的 loss 时一次；即使本轮最终仍不可行也可触发 | `OnSolution(EXPLORATION_IMPROVING)` 对应；由 Separator 的改进分支发出，不等同于 C++ 独立 `OnImprovement` |
| `report(CmprFeas)` | 每个成功压缩尝试结束后一次 | `OnSolution(COMPRESSION_FEASIBLE)` 对应 |
| `report(Final)` | 压缩正常返回后一次；随后优化返回 | `OnSolution(FINAL)` 对应；C++ 在回调前额外 Restore/验证最终快照，失败时不会发 FINAL（该快照错误出口归 S08）；FINAL 回调请求 stop 不撤销已完成结果，回调异常则中断正常返回 |
| `report_separation_progress` | 每次 `separate` 开始先发 iteration 0；每个完成搜索迭代再发一次。分离成功时，额外在返回前发 `n_iter + 1`、loss=0 的末次进度 | `OnSeparationProgress` 对应；包含宽度、密度、iteration 和 minimum loss。退出/失败仍可能有初始进度，但如果回调本身抛出则不会有 result 回调 |
| `report_separation_result` | `separate` 正常收尾时一次；含 success、elapsed、evaluations、moves、iterations | `OnSeparationResult` 对应；elapsed 是运行时量，不作为跨进程逐值相等断言 |
| `report_compression_progress(step)` | 每次压缩尝试通过 step、最小收缩和最小宽度检查后、进入尝试前一次；没进入尝试则无事件 | `OnCompressionProgress(double)` 对应。Rust 的 step 是 `f32`；C++ 在回调边界先 round 到 `float` 再 widen 到 `double`，算法内部仍使用其配置路径 |

可选 `OptimizationObserver` 继续承载 C++ 独有的 `OnProgress`/`OnImprovement`：构造成功后各一次；Explore 初始报告后及每次 separator 返回后发 `OnProgress`，更窄可行解时发 `OnImprovement`；Compression 每次尝试前（紧随 `OnCompressionProgress`）及 separator 返回后发 `OnProgress`，成功尝试时发 `OnImprovement`。Rust 没有这两个独立 callback。observer 回调为同步调用；显式启用后，stop/异常效果属于扩展路径，不计入 Rust 原生 listener parity。SparrowCore `OptimizationHistory` 仍会在 `OnSolution` 前追加报告，而 Rust `optimize()` 不返回 history。

**线程与异常边界。** Rust listener 借用留在主 optimizer/separator 调用路径；C++ worker future 不捕获 listener，父线程收集 worker 结果后才触发原生 listener。原生 listener callback 没有 catch/exception adapter：C++ exception 直接向调用方传播，Rust panic 沿 Rust unwind 传播。已配对的 `OnSeparationProgress` 抛出/panic 用例确认发生在首个分离进度之后、`SeparationResult` 与 Compression 之前；已有 FINAL 回调 stop/throw 用例确认正常返回/异常退出边界。可选 observer 同样在父线程同步调用，异常传播只适用于显式启用扩展的路径。源码审计覆盖全部原生回调调用点，但没有为每一种 callback 类型分别构造 throw fixture，另有一个双 worker unsupported-hazard 夹具覆盖 Explore 初始回调后的 phase cutoff；其他 callback 类型、worker 数和错误源仍未穷尽（见 S12）。

**验证证据与边界。** `optimizer_cold_start_default_reference.rs` 与 `TestDefaultColdStartOptimizeMatchesRustReference` 对默认配置 cold-start 的 phase、solution report、compression step、separation result 和 progress 汇总做 Rust/C++ 对照；探索分离进度为 2 次、压缩分离进度为 647 次，并比较每段的首/末 iteration、loss、width、density。parity 用例只向 `Optimize` 传 `OptimizationListener`，不传 observer。显式 observer 的独立 C++ 入口由 `RecordingObserver` 覆盖，验证进度和改进事件只通过单独扩展接口收到。该用例在每阶段首个 `OnSeparationResult` 后请求 phase stop，证明当前 separator 先完成、下一阶段开始前 phase stop 被重置。运行时 elapsed、所有中间 progress payload 和任意实例的频率不作为已证明的 parity。`optimizer_exit_reference.rs` 与 C++ `TestListenerExceptionDuringSeparationProgressMatchesRust` 对原生 listener 异常截断做配对；固定步长 callback 的 `f32` 位值另由 `optimizer_compress_reference.rs` / `TestOptimizeCompressionMatchesRustReference` 检查。

本次修正还把 C++ cold-start 测试的步长范围上界改为 `double(float(0.0005))`，避免把合法的 Rust `f32(0.0005)` widening 值 `0.00050000002374872565` 错判为大于未量化的 `double 0.0005`。实现上的 `OnCompressionProgress` 转换保持 Rust f32 callback 语义，Debug/Release runner 均已验证。

**闭合结论。** Rust 原生 Listener 的触发点、默认 cold-start 的代表性顺序/计数/字段、阶段 stop/reset、已测分离进度 panic/exception 截断及 FINAL 返回边界已闭合到本记录列出的 profile。原 strict-parity 缺口已通过 API 隔离收口：`OptimizationListener` 只承载 Rust 原生回调，`OnProgress`/`OnImprovement` 放在必须显式传入的 C++ `OptimizationObserver`；Rust 对照运行不启用 observer。历史仍是 C++ 结果扩展。S20 的 SparrowCli SIGINT 和原生宿主跨线程取消接线已完成；Windows `CTRL_C_EVENT` 子进程自动化已按 Rust trace 在 Debug/Release 通过，真实键盘 Release 冒烟也已由用户回报通过；S12 中固定双 worker unsupported-hazard 的 Separator/Optimize 传播边界已闭合；WASM 同步 ABI 中途取消仍开放。
