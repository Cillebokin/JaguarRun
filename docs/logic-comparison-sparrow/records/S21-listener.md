# S21. util/listener.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/listener.rs`（62 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationListener.h`（121 行）+ `Source/Optimizer/OptimizationListener.cpp`（34 行）
**判定**: 基本等价，完成回调边界已对齐（Rust/C++ 均在压缩结束后只发布 FINAL 解报告；C++ 仍保留构造/搜索进度、OnImprovement 和历史记录扩展）

## 逐条比对

### `trait SolutionListener`（Rust 4-14：`report` + 4 个默认空实现的方法）
- C++ 对应：`class OptimizationListener`（OptimizationListener.h:98-110，虚基类）+ 默认空实现（OptimizationListener.cpp:5-33）。
- 判定：一致（形态映射）。
- 证据与说明：Rust trait 的默认空方法体 ↔ C++ 虚函数空默认体（cpp:7-32）逐一对应：
  - `report(&mut self, ReportType, &SPSolution)` → `OnSolution(const OptimizationSolutionReport&)`（h:106）
  - `report_phase(OptimizationPhase)` → `OnPhase(OptimizationPhase)`（h:103）
  - `report_separation_progress(SeparationProgress)` → `OnSeparationProgress(const SeparationProgress&)`（h:107）
  - `report_separation_result(SeparationResult)` → `OnSeparationResult(const SeparationResult&)`（h:108）
  - `report_compression_progress(f32)` → `OnCompressionProgress(double)`（h:109）
  差异点：Rust `report` 的解参数是 `(ReportType, &SPSolution)` 二元组，C++ 打包为 `OptimizationSolutionReport` 结构（含 type/phase/iteration/solution/stripWidth/density/collisionLoss/isFeasible 八字段，h:40-52）——信息超集，见下条。

### `ReportType` 枚举（Rust 42-53）
- C++ 对应：`enum class OptimizationReportType`（h:31-38）。
- 判定：一致（五个值逐一对应）。
- 证据与说明：`ExplFeas→EXPLORATION_FEASIBLE`、`ExplInfeas→EXPLORATION_INFEASIBLE`、`ExplImproving→EXPLORATION_IMPROVING`、`CmprFeas→COMPRESSION_FEASIBLE`、`Final→FINAL`——语义与注释（feasible/infeasible/improving/final）逐项一致。

### `OptimizationPhase` 枚举（Rust 17-20）
- C++ 对应：`enum class OptimizationPhase`（h:12-18）。
- 判定：差异（C++ 超集）。
- 证据与说明：Rust 两值 `Exploration/Compression`；C++ 枚举有四值 `CONSTRUCTION/EXPLORATION/COMPRESSION/COMPLETE`。C++ `OnPhase` 只上报探索与压缩，与 Rust `report_phase` 的阶段序列一致；`CONSTRUCTION` 用于额外的构造进度，`COMPLETE` 保留为最终解报告的 `phase` 元数据，不再触发完成阶段 `OnProgress`（`SparrowOptimizer.cpp::OptimizeWithTerminator`）。

### `SeparationProgress` 结构体（Rust 24-30）
- C++ 对应：`struct SeparationProgress`（h:58-64）。
- 判定：一致。
- 证据与说明：`strip_width→stripWidth`、`density→density`、`iteration→iteration`、`min_loss→minimumLoss`，四字段全量对应（f32→double 既定差异）；Rust 文档注释"Iteration zero describes the initial layout"的语义由 S06/S04 侧保证。

### `SeparationResult` 结构体（Rust 32-39）
- C++ 对应：`struct SeparationResult`（h:66-73）。
- 判定：一致。
- 证据与说明：`success/elapsed_seconds→elapsedSeconds/total_evals→totalEvaluations/total_moves→totalMoves/iterations`，五字段全量对应。

### `DummySolListener`（Rust 56-62：空实现）
- C++ 对应：无需传监听器即可（`Optimize` 的 `listener` 参数默认 `nullptr`，SparrowOptimizer.h:22-27；`PublishSolutionReport` 对 nullptr 判空，h:112-120）。
- 判定：差异（机制不同，语义等价）。
- 证据与说明：Rust bench 用 `DummySolListener` 占位（bench.rs:98/105）；C++ 以空指针默认参数表达"无监听"，无需 dummy 类型。

### `OptimizationProgress` + `OnProgress`/`OnImprovement`（h:20-29/104-105）
- 判定：C++ 额外。
- 证据与说明：C++ 增设带 `phase/iteration/stripWidth/density/collisionLoss/isFeasible` 的进度事件。构造成功后额外上报构造进度（`SparrowOptimizer.cpp`）；探索循环在初始解与每轮分离后上报（`Exploration.cpp:954-961,1004-1011`），压缩循环在尝试开始与指标计算后上报（`Compression.cpp:281-288,339-343`）；成功改进时另发 `OnImprovement`（`Exploration.cpp:1020-1027`；`Compression.cpp:349-351`）。Rust 侧通过 `report(ExplImproving, ...)` 与分离/压缩进度方法公开状态，没有相同的独立 `OnProgress`/`OnImprovement` 双通道。C++ 不再在最终报告前另发 `OnProgress(COMPLETE)`；完成边界以 `OnSolution(FINAL)` 表达。其他扩展仍为 Demo/NestLab 提供更细的 UI 数据源（S24）。

### `OptimizationHistory` + `MakeSolutionReport` + `PublishSolutionReport`（h:54/75-120）
- 判定：C++ 额外。
- 证据与说明：C++ 把每个解报告同时落进 `std::vector<OptimizationSolutionReport>` 历史（`PublishSolutionReport` 先 push_back 再回调，h:112-120），随 `SparrowOptimizationResult::history` 返回（SparrowOptimizer.h:11-14）——Rust 侧监听器自行决定是否留存（SvgExporter 落盘、TUI 推信道），`optimize()` 不返回历史。这是 C++ 把"解的留痕"从监听器职责上移到返回值的设计。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `report(ReportType, &SPSolution)` 参数形态 | 差异 | C++ 打包为 `OptimizationSolutionReport`（八字段超集：多 phase/iteration/stripWidth/density/collisionLoss/isFeasible），监听端免于自查解 | listener.rs:5 vs h:40-52 |
| 2 | `OptimizationPhase` 两值 vs 四值 | API 枚举差异 | C++ 多出 CONSTRUCTION/COMPLETE；CONSTRUCTION 用于额外进度，COMPLETE 只作最终报告元数据；`OnPhase` 的探索/压缩序列与 Rust 一致 | listener.rs:17-20 vs h:12-18；SparrowOptimizer.cpp |
| 3 | `DummySolListener` | 差异（机制） | C++ 用 `listener=nullptr` 默认参数替代 dummy 占位对象 | listener.rs:56-62 vs SparrowOptimizer.h:22 |
| 4 | `OnProgress`/`OnImprovement` 进度通道 + `OptimizationHistory` 留存 | C++额外 | 给宿主 UI（Demo/NestLab）与调用方提供更细进度和解历史；Rust 无对应独立通道。完成时不再产生额外进度回调 | h:20-29/54/104-105、SparrowOptimizer.cpp vs（Rust 无） |

## 结论
监听器的 Rust 原生事件（五类解报告、探索/压缩相位、分离进度/结果、压缩步长）在 C++ 均有对应，默认空实现也一致。`optimizer_exit_reference.rs` / `TestImmediateStopOptimizeMatchesRustReference` 覆盖探索初始可行报告回调中的 phase stop，并核对后续阶段事件。完成边界由 `CodeResource/sparrow/src/optimizer/mod.rs` 的 `report(Final)` 与 C++ `PublishSolutionReport(FINAL)` 对齐：C++ 不再在 FINAL 前调用 `OnProgress(COMPLETE)`，但 FINAL 报告仍带 `phase=COMPLETE`、`iteration=0`。`TestZeroTimeoutOptimizeMatchesRustReference` 现在核对完成事件顺序、FINAL 元数据与返回解，并覆盖 FINAL 回调请求全局 stop 后仍返回结果，以及 FINAL 回调抛异常时优化调用传播异常、不返回结果。C++ 构造/搜索 `OnProgress`、`OnImprovement` 和 `OptimizationHistory` 仍属额外 API；其他进度回调触发时点和异常出口未穷尽，不能据此判定所有监听器行为均已完全等价。

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
