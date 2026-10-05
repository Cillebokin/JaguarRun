# S24. tui/ 逐文件逻辑比对（归属判定）
**Rust**: `tui.rs`（96 行）+ `tui/dashboard.rs`（736 行）+ `tui/runtime.rs`（287 行）+ `tui/logging.rs`（52 行）+ `tui/dashboard/tests.rs`（66 行）+ `tui/runtime/tests.rs`（16 行），合计约 1253 行
**C++**: `SparrowCore/Demo/`（App.js 59 行 + Index.html 27 行 + Style.css 102 行，浏览器 WASM Demo）+ `Tools/SparrowWasm/SparrowWasm.cpp`（162 行，C++↔JS 边界）；NestLab GUI 归属判定
**判定**: 排除（TUI 各职责在 C++ 生态的归属：优化循环驱动=库本体已有；仪表盘渲染/日志/live 可视化=范围外，归浏览器 Demo 与 NestLab GUI；Demo 当前仅覆盖"最终布局可视化+结果 JSON"一项，TUI 的实时监控/进度/交互控制均无覆盖）

> 比对方式说明：本记录不做逐行比对，按 TUI 三大职责（仪表盘渲染、优化循环驱动、日志）逐项判定"范围外（归 NestLab/Demo）"或"缺失"，并列出 Demo 页面的实际覆盖面。

## 职责归属逐条判定

### 1. 优化循环驱动（tui.rs:23-68 `run()` 编排 + runtime.rs:35-152）

- **Rust 形态**：`run()` 读 CLI→配置→后台线程跑 `optimize()`（runtime.rs:68-90 `start_optimizer`，独立"optimizer"线程），主线程跑 ratatui 事件循环轮询信道（updates/logs）、驱动渲染、处理按键；worker 结束后 `export_final_solution`（runtime.rs:154-174）写 final SVG/JSON。
- **C++ 对应**：库本体 `sparrow::Optimize`（SparrowOptimizer.h:15-27）同步调用；异步/驱动编排在 C++ 生态归宿主——浏览器 Demo 由 JS 事件循环（App.js:37-58 点击→`SparrowWasm.optimizeJson`）承担；NestLab GUI（Qt）预期用线程+`OptimizationListener` 回调驱动。
- **判定**：范围外（归属正确，无缺失）。库的同步 API + 虚监听器（S21）+ `RequestStop`（S20）正是为宿主驱动设计的接口面。
- **差异点**：Rust TUI 的 `TuiListener`（runtime.rs:176-228）经 mpsc 信道把解事件推给 UI 线程（含 100ms 快照节流，runtime.rs:33 `SNAPSHOT_INTERVAL`）；C++ 的 `OptimizationListener` 虚回调在优化线程上同步触发——宿主需自行做线程封送（Qt 需 queued signal）。既有监听器事件面（S21）已覆盖 TUIListener 所需的全部五类事件，无需库侧改动。

### 2. 仪表盘渲染（dashboard.rs 736 行）

- **Rust 形态**：ratatui 终端 UI——汇总区（状态机 FINISHED/FEASIBLE/NESTING/INFEASIBLE/STARTING、相位、宽度/密度、吞吐率 evals/s、分离迭代号、viewer 链接、ASCII 麻雀 logo）、三根 Gauge 进度条（时间预算/探索 attempt 或压缩 shrink step/碰撞损失收敛）、带滚动/换行/按级别着色的日志窗格（`log_style` 按消息模式匹配成功/失败/完成着色，dashboard.rs:578-603）、帮助栏与活动帧动画。
- **C++ 对应**：**无对应实现**。Demo（App.js/Index.html）只有：文件选择→Run 按钮→状态行→`<img>` 最终 SVG 预览→`<pre>` 结果 JSON。
- **判定**：范围外（仪表盘属终端专属呈现，归 NestLab GUI 承接同类信息）。数据面判定如下：
  - Dashboard 消费的每个数据字段在 C++ 侧都有等价来源：`SeparationProgress`（S21 一致）、`SeparationResult`（含 evals/moves/iterations→吞吐率，S21 一致）、`ReportType` 五类（S21 一致）、`shrink_step`（S21 一致）、相位（S21 四值超集）、时间预算（SearchBudget 由 config 推导——`total_duration`/`max_attempts`/`shrink_range`/`exploration_boundary` 全部可从 `SparrowConfig` 字段推得，dashboard.rs:37-52）。
  - 唯一数据缺口：`SearchBudget.exploration_boundary` 的时间分界可视化（dashboard.rs:433-463 在时间条上画"│"分界标记）依赖相位 timeLimit 比例——C++ `StartPhase` 不暴露相位截止时刻（S20 差异 4），宿主只能用传入 config 推算，行为可复刻。
- **Demo 页面当前覆盖的 TUI 功能**：仅一项——"最终布局可视化"（App.js:46-51 调 `optimizeSvg` 得最终 SVG 塞进 `<img>`；对应 Rust TUI 结束时的 final SVG 输出 runtime.rs:158-163）。以下 TUI 功能 Demo **未覆盖**：实时解流（live SVG 100ms 节流）、相位/状态指示、三进度条（时间/attempt-shrink/损失收敛）、吞吐率指标、日志面板、结果统计摘要行（Rust `Finished at width/density` println，tui.rs:62-66——Demo 的 JSON dump 粗略对应）。

### 3. 日志（logging.rs 52 行 + dashboard 日志窗格）

- **Rust 形态**：`fern::Dispatch` 双通道——同步信道（512 缓冲 `try_send`，满即丢，logging.rs:23-26）给 TUI 窗格 + 文件 `output/log.txt`；格式 `[LEVEL][H:M:S] message`（EPOCH 相对时间）。
- **C++ 对应**：**缺失**（库无日志框架，S02 差异 4/S22 差异 3）。`[EXPL] feasible solution found!`/`[CMPR] success at` 等 Rust 日志消息在 C++ 侧以 `OptimizationReportType` 事件（S21）承载语义，但文本日志流与按模式着色（dashboard.rs:578-603）无对应。
- **判定**：缺失（若 NestLab 需要等价日志面板，需宿主自建 logger + 监听器事件转日志；库侧现状无障碍但无现成组件）。

### 4. 交互控制（runtime.rs:130-151 按键：q/Esc 退出、Ctrl-C 跳相位、↑↓/PgUp/PgDn/Home/End 滚日志）

- **Rust 形态**：`TuiSignals`（quit/interrupt_phase 两个 `Arc<AtomicBool>`）+ `TuiTerminator`（quit 持久、interrupt 换相位重置，runtime/tests.rs:4-16 锁定该语义）。
- **C++ 对应**：`OptimizationTerminator::RequestStop()`（quit 等价物，S20）；**"跳过当前相位"（interrupt_phase）无公共入口**（S20 差异 3——`StartPhase` 不重置停止标志）；日志滚动属 UI 自理。
- **判定**：跳相位功能缺失（库接口层）；其余范围外归宿主。WASM 单线程边界下交互控制本就受限（长任务会阻塞 JS）。

### 5. 入口/配置（tui.rs:70-96 `configure()`，与 cli.rs 同构）

- 判定：范围外（应用层配置，语义同 S23 的 CLI 差异：-t/-e/-c 分摊、早停、seed/workers 覆盖；C++ Demo 用固定预算——SparrowWasm.cpp:72-88 硬编码 seed=0/128 样本/小迭代上限，浏览器桥接防失控，注释明言 native 默认经 SparrowConfig 仍可用）。

### 6. 测试（dashboard/tests.rs 3 用例：attempt 分组/滚动保持/换行；runtime/tests.rs 1 用例：信号重置语义）

- 判定：范围外（TUI 专属行为的单元测试；对应行为在 C++ 生态不存在，无需移植）。SparrowCore 侧对应物是 tests/ 下 RustReference 差分测试体系（另见 README 汇总）。

## 差异汇总表（按归属判定）
| # | TUI 职责 | 判定 | C++ 归属/现状 | 证据 |
|---|---|---|---|---|
| 1 | 优化循环后台驱动 + 信道推送 | 范围外（归属正确） | 库同步 `Optimize`+监听器；Demo=JS 事件循环；NestLab=宿主线程 | runtime.rs:35-152 vs App.js:37-58、SparrowOptimizer.h:15 |
| 2 | 仪表盘渲染（状态/指标/进度条/logo） | 范围外（归 NestLab GUI）；数据面无缺口 | Demo 仅最终 SVG 预览；实时面板未实现 | dashboard.rs:212-543 vs Demo/App.js:46-51 |
| 3 | 日志面板+文件双通道 | 缺失 | 库无日志框架；NestLab 需自建（监听器事件可转日志） | logging.rs:17-52 vs（无） |
| 4 | Ctrl-C 跳过当前相位 | 缺失（库接口层） | `StartPhase` 不重置停止标志，无相位级中断入口 | runtime.rs:139-141/253-256 vs OptimizationTerminator.cpp:32-39 |
| 5 | live SVG（100ms 节流→live_viewer.html） | 缺失（归 NestLab/Demo 可实现） | 监听器 OnSolution 已提供数据源，无现成导出组件 | runtime.rs:31/176-211 vs（无） |

## 结论
TUI 的三大职责在 C++ 生态的归属判定清晰：优化循环驱动是库本体的同步 API + 监听器/RequestStop 接口面（归属正确、无缺失）；仪表盘与交互渲染范围外归浏览器 Demo 与 NestLab GUI，且监听器事件面（S21）已提供仪表盘所需的全部数据字段；真正缺失的是三项——日志双通道组件、live SVG 导出组件、"跳过当前相位"的库级入口（S20 差异 3）。Demo 页面当前只覆盖了 TUI 的"最终布局可视化"一项，实时监控类功能全部未实现，NestLab GUI 落地时按上表缺口清单补齐即可。

### 补充审计（2026-09-29）
1. **【核对确认：文件清单与行数】** tui.rs 96 + dashboard.rs 736 + runtime.rs 287 + logging.rs 52 + dashboard/tests.rs 66 + runtime/tests.rs 16 = 1253 行，与主文声称一致；tui/ 目录无其他文件（现场 ls 复验）。
2. **【核对确认：归属判定关键事实】** a) TuiListener 五事件全实写经 mpsc 推送（runtime.rs:193-227，现场复验）——S21 补充审计第 3 条已确认 C++ 虚方法面对应无缺口；b) TuiTerminator 的 interrupt_phase 语义（kill = timeout || quit || interrupt_phase；new_timeout 重置 interrupt_phase 但**不重置 quit**——runtime.rs:244-256）与 S20 差异 3 的"StartPhase 不重置 m_StopRequested（quit 语义）"形成**单向错位**：Rust 的相位中断标志可重置而 C++ 停止标志不可重置——S24 差异 #4 的"跳相位缺失"判定维持且更精确：C++ 连"quit 与 skip"的分离都没有。c) Demo/App.js 现场复验仅 3 个交互点（文件选择 :37-45、Run 按钮 :47-51、SVG blob 预览 + JSON dump），无进度/状态/日志渲染——主文覆盖面判定维持。
3. **【浅覆盖补全：tui.rs 与 cli.rs 的 configure 同构】** tui.rs:70-96 的 configure 与 cli.rs:26-58 的 run 前半（时间预算三分支/早停/种子）同构（复用同一 MainCli），主文条目 5 已记"语义同 S23"，现场确认无 TUI 专属配置项遗漏（仅输出路径固定 LIVE_DIR/output 同名）。
