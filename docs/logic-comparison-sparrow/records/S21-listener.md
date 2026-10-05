# S21. util/listener.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/listener.rs`（62 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationListener.h`（121 行）+ `Source/Optimizer/OptimizationListener.cpp`（34 行）
**判定**: 基本等价（事件种类一一对应；C++ 把 trait 对象改为虚基类并增补 OnProgress/OnImprovement/历史记录三处扩展，报告类型/结构体字段全量对应）

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
- 证据与说明：Rust 两值 `Exploration/Compression`；C++ 四值 `CONSTRUCTION/EXPLORATION/COMPRESSION/COMPLETE`——多出的首尾相位对应 C++ 编排层显式上报构造阶段与完成时刻（SparrowOptimizer.cpp:126-133/209-216），Rust 的 `report_phase` 只在两相位切换时触发。

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
- 证据与说明：C++ 增设了带 `phase/iteration/stripWidth/density/collisionLoss/isFeasible` 的进度事件，在构造完成、最终完成时上报（SparrowOptimizer.cpp:126-133/209-216），并区分普通进度（OnProgress）与改进（OnImprovement）；Rust 的进度可见性走 `report(ExplImproving, ...)` 与 `report_separation_progress`，无独立"进度/改进"双通道。为 Demo/NestLab 提供更细的 UI 数据源（S24）。

### `OptimizationHistory` + `MakeSolutionReport` + `PublishSolutionReport`（h:54/75-120）
- 判定：C++ 额外。
- 证据与说明：C++ 把每个解报告同时落进 `std::vector<OptimizationSolutionReport>` 历史（`PublishSolutionReport` 先 push_back 再回调，h:112-120），随 `SparrowOptimizationResult::history` 返回（SparrowOptimizer.h:11-14）——Rrust 侧监听器自行决定是否留存（SvgExporter 落盘、TUI 推信道），`optimize()` 不返回历史。这是 C++ 把"解的留痕"从监听器职责上移到返回值的设计。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `report(ReportType, &SPSolution)` 参数形态 | 差异 | C++ 打包为 `OptimizationSolutionReport`（八字段超集：多 phase/iteration/stripWidth/density/collisionLoss/isFeasible），监听端免于自查解 | listener.rs:5 vs h:40-52 |
| 2 | `OptimizationPhase` 两值 vs 四值 | 差异 | C++ 多出 CONSTRUCTION/COMPLETE 相位事件；Rust 仅两相位切换上报 | listener.rs:17-20 vs h:12-18 |
| 3 | `DummySolListener` | 差异（机制） | C++ 用 `listener=nullptr` 默认参数替代 dummy 占位对象 | listener.rs:56-62 vs SparrowOptimizer.h:22 |
| 4 | `OnProgress`/`OnImprovement` 进度通道 + `OptimizationHistory` 留存 | C++额外 | 给宿主 UI（Demo/NestLab）与调用方提供解历史返回值；Rust 无对应通道 | h:20-29/54/104-105 vs（Rust 无） |

## 结论
监听器的事件面（解报告五类型、相位切换、分离进度/结果、压缩进度）与两个进度结构体字段在 C++ 侧一一对应且数值语义一致，默认空实现行为一致，判定基本等价。C++ 的扩展集中在工程形态上：trait→虚基类、二元参数→报告结构体、null 替代 dummy、并额外提供 OnProgress/OnImprovement 通道与解历史返回值，为浏览器 Demo 与 NestLab GUI 的展示需求服务（见 S24）；这些扩展不改变 Rust 侧既有事件流的语义。

### 补充审计（2026-09-29）
1. **【核对确认：文件覆盖完整性】** listener.rs 62 行全量复核：trait 5 方法（report + 4 默认空实现）、OptimizationPhase 2 值、SeparationProgress 4 字段、SeparationResult 5 字段、ReportType 5 值、DummySolListener——主文均有条目，无遗漏项。
2. **【遗漏补记：report 方法的必要性与 Dummy 的差异】** Rust trait 中 `report` **无默认实现**（强制实现者处理解事件，listener.rs:5）；其余 4 方法有默认空体。C++ 五个虚函数全部有空默认体（cpp:5-33）——即 C++ 允许全空监听器而 Rust 至少需实现 report（DummySolListener 提供空 report）。纯 API 形态差异，无行为影响，补记闭合。
3. **【浅覆盖补全：消费方覆盖验证】** 现场核验 Rust 侧全部实现者：SvgExporter（report 实写，svg_exporter.rs:39-65）、DummySolListener（bench.rs:98/105 占位）、TuiListener（runtime.rs:193-227，**五方法全部实写**——report/report_phase/report_separation_progress/report_separation_result/report_compression_progress 经 mpsc 信道推 UI）。C++ 侧消费方：CLI 不传（nullptr）、Bench 不传、Demo 经 WASM 结果 JSON。S24 的"事件面无缺口"判定据此成立（TuiListener 消费的 5 事件在 OptimizationListener 均有对应虚方法）。
