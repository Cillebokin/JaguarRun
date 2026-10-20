# S24. tui/ 逐文件逻辑比对（归属判定）
**Rust**: `tui.rs`（96 行）+ `tui/dashboard.rs`（736 行）+ `tui/runtime.rs`（287 行）+ `tui/logging.rs`（52 行）+ `tui/dashboard/tests.rs`（66 行）+ `tui/runtime/tests.rs`（16 行），合计约 1253 行
**C++**: `SparrowCore/Demo/`（App.js、Index.html、Style.css、SparrowWorker.js）+ `Tools/SparrowWasm/SparrowWasm.cpp`（WASM C ABI 与listener事件）
**判定**: 部分（Emscripten构建和Chrome浏览器验收已通过代表性单件fixture；Demo在后台Worker单次调用Optimize并同时取得最终JSON/SVG，监听器事件驱动阶段、分离进度、压缩步长、结果摘要、日志和限频live SVG。Cancel通过终止Worker实现，属于宿主硬中止，不等价于Rust TUI的合作式phase interrupt；Rust fern文件日志、精确Gauge/滚动行为仍未复刻）

> 比对方式说明：本记录不做逐行比对，按 TUI 三大职责（仪表盘渲染、优化循环驱动、日志）逐项判定"范围外（归 NestLab/Demo）"或"缺失"，并列出 Demo 页面的实际覆盖面。

## 职责归属逐条判定

### 1. 优化循环驱动（tui.rs:23-68 `run()` 编排 + runtime.rs:35-152）

- **Rust 形态**：`run()` 读 CLI→配置→后台线程跑 `optimize()`（runtime.rs:68-90 `start_optimizer`，独立"optimizer"线程），主线程跑 ratatui 事件循环轮询信道（updates/logs）、驱动渲染、处理按键；worker 结束后 `export_final_solution`（runtime.rs:154-174）写 final SVG/JSON。
- **C++ 对应**：库本体 `sparrow::Optimize`（SparrowOptimizer.h:15-27）同步调用；浏览器Demo由`SparrowWorker.js`在Web Worker中调用新增`SparrowOptimizeWithSvg`，主线程接收listener消息并更新界面。一次调用返回JSON和SVG，不改变既有固定seed/样本/迭代配置；Qt原生宿主仍可用`OptimizationListener`及`RequestStop()`/`RequestPhaseStop()`。Demo的Cancel通过`Worker.terminate()`硬中止，不经过C++ terminator。
- **判定**：部分。Demo已用Web Worker承担后台驱动并消费现有listener回调；取消仅能终止Worker，未接入Rust式合作停止。原生Qt宿主仍需自行做线程封送。
- **差异点**：Rust TUI 的 `TuiListener`（runtime.rs:176-228）经 mpsc 信道把解事件推给 UI 线程（含 100ms 快照节流，runtime.rs:33 `SNAPSHOT_INTERVAL`）；C++ 的 `OptimizationListener` 虚回调在优化线程上同步触发——宿主需自行做线程封送（Qt 需 queued signal）。既有监听器事件面（S21）已覆盖 TUIListener 所需的全部五类事件，无需库侧改动。

### 2. 仪表盘渲染（dashboard.rs 736 行）

- **Rust 形态**：ratatui 终端 UI——汇总区（状态机 FINISHED/FEASIBLE/NESTING/INFEASIBLE/STARTING、相位、宽度/密度、吞吐率 evals/s、分离迭代号、viewer 链接、ASCII 麻雀 logo）、三根 Gauge 进度条（时间预算/探索 attempt 或压缩 shrink step/碰撞损失收敛）、带滚动/换行/按级别着色的日志窗格（`log_style` 按消息模式匹配成功/失败/完成着色，dashboard.rs:578-603）、帮助栏与活动帧动画。
- **C++ 对应**：Demo已提供阶段名、耗时、宽度/密度/碰撞损失、分离迭代和eval/s、压缩步长、事件日志、live SVG及最终JSON。新增`SparrowOptimizeWithSvg`在一次`OptimizeInput`后同时序列化JSON与SVG；live SVG取自同一listener报告，不重新运行算法。固定WASM seed与迭代预算保持不变。
- **判定**：部分。C++ listener提供主要数据源，Demo已显示阶段时限消耗、解宽度/密度、分离结果、压缩步长与live SVG；没有复刻Rust的attempt分组、损失收敛Gauge、精确phase时间边界、viewer链接或ASCII动画。Demo时间预算使用WASM入口已知的默认Explore/Compression时限；原生Qt宿主可从传入config推算。
- **Demo 页面当前覆盖的 TUI 功能**：listener提供阶段/状态、解宽度和密度、separation evaluation/move/iteration、压缩shrink step；解报告和进度事件限频至约100ms，解报告生成live SVG；最终JSON和SVG共用一次Optimize。宿主日志由这些结构化事件生成，不是Rust `fern`文本日志，也不写`output/log.txt`。目前没有精确复制Rust的三根Gauge算法、事件文本着色、滚动键位或100ms dashboard snapshot调度。

### 3. 日志（logging.rs 52 行 + dashboard 日志窗格）

- **Rust 形态**：`fern::Dispatch` 双通道——同步信道（512 缓冲 `try_send`，满即丢，logging.rs:23-26）给 TUI 窗格 + 文件 `output/log.txt`；格式 `[LEVEL][H:M:S] message`（EPOCH 相对时间）。
- **C++ 对应**：SparrowCore库仍无通用日志框架；Demo把`OptimizationReportType`、phase、SeparationProgress/Result和compression step事件转为UI日志，不写`output/log.txt`，也不复制Rust fern格式与按模式着色。
- **判定**：部分（Demo侧事件日志已接入；持久文件日志和Rust文本/颜色规则仍缺）。

### 4. 交互控制（runtime.rs:130-151 按键：q/Esc 退出、Ctrl-C 跳相位、↑↓/PgUp/PgDn/Home/End 滚日志）

- **Rust 形态**：`TuiSignals`（quit/interrupt_phase 两个 `Arc<AtomicBool>`）+ `TuiTerminator`（quit 持久、interrupt 换相位重置，runtime/tests.rs:4-16 锁定该语义）。
- **C++ 对应**：`OptimizationTerminator::RequestStop()` 对应持久 quit；`RequestPhaseStop()` 请求当前 phase 中断，随后 `StartPhase()` 清除该标志（S20）；OS 键盘事件如何映射到两者由宿主负责。日志滚动属 UI 自理。
- **判定**：原生库接口有两种停止作用域；Qt宿主可接线。浏览器Cancel目前直接终止Worker，用户可停止当前任务，但没有partial final state，也没有触发`RequestPhaseStop()`/`RequestStop()`；这是硬中止，不是Rust TUI中断语义等价。日志滚动属UI自理。

### 5. 入口/配置（tui.rs:70-96 `configure()`，与 cli.rs 同构）

- 判定：范围外（应用层配置，语义同 S23 的 CLI 差异：-t/-e/-c 分摊、早停、seed/workers 覆盖；C++ Demo 用固定预算——SparrowWasm.cpp:72-88 硬编码 seed=0/128 样本/小迭代上限，浏览器桥接防失控，注释明言 native 默认经 SparrowConfig 仍可用）。

### 6. 测试（dashboard/tests.rs 3 用例：attempt 分组/滚动保持/换行；runtime/tests.rs 1 用例：信号重置语义）

- 判定：范围外（TUI 专属行为的单元测试；对应行为在 C++ 生态不存在，无需移植）。SparrowCore 侧对应物是 tests/ 下 RustReference 差分测试体系（另见 README 汇总）。

## 差异汇总表（按归属判定）
| # | TUI 职责 | 判定 | C++ 归属/现状 | 证据 |
|---|---|---|---|---|
| 1 | 优化循环后台驱动 + 信道推送 | 部分 | Demo Web Worker运行一次`SparrowOptimizeWithSvg`并接收listener消息；Cancel只终止Worker | runtime.rs:35-152 vs SparrowWorker.js、SparrowWasm.cpp |
| 2 | 仪表盘渲染（状态/指标/进度条/logo） | 部分 | Demo显示阶段、耗时、分离/压缩进展和解指标；不复制ratatui的Gauge与绘制 | dashboard.rs:212-543 vs App.js/Index.html |
| 3 | 日志面板+文件双通道 | 部分 | Demo将listener事件转换为限长UI日志；未实现`output/log.txt`和Rust fern文本/颜色规则 | logging.rs:17-52 vs App.js |
| 4 | Ctrl-C 跳过当前相位 | 部分 | 原生库和CLI接口已有；Demo Cancel硬终止Worker，不返回partial solution，不映射到合作式phase stop | runtime.rs:139-141/253-256 vs OptimizationTerminator.h/.cpp、App.js |
| 5 | live SVG（100ms 节流→live_viewer.html） | 部分 | 通过listener解报告生成限频live SVG；不实现Rust文件路径和外部live_viewer.html | runtime.rs:31/176-211 vs WasmOptimizationListener |

## 结论
SparrowCore库仍不复刻ratatui；浏览器Demo已承接主要可观察状态，并以一次Optimize返回最终JSON/SVG。结构化listener日志与限频live SVG已经接入。取消采用Worker硬终止，未复现TUI合作式phase interrupt及partial final state；Rust `fern`文件日志、精确Gauge/键位仍未闭合。代表性Chrome运行证据已通过，但不外推为完整TUI语义等价。整体保持部分状态。

### 补充审计（2026-09-29）
1. **【核对确认：文件清单与行数】** tui.rs 96 + dashboard.rs 736 + runtime.rs 287 + logging.rs 52 + dashboard/tests.rs 66 + runtime/tests.rs 16 = 1253 行，与主文声称一致；tui/ 目录无其他文件（现场 ls 复验）。
2. **【2026-10-07 更新：停止作用域接口】** TuiTerminator 的 `interrupt_phase` 在 `new_timeout` 重置而 `quit` 保留（runtime.rs:244-256）；C++ 现用 `RequestPhaseStop()` 与 `RequestStop()` 分开表达，`StartPhase()` 只清除 phase stop。S20 handoff fixture 验证了当前相位中断后压缩阶段继续。SparrowCli 的 SIGINT 已接入 phase stop；交互式 GUI/浏览器宿主的输入事件仍由各自 UI 接线；Demo 当前没有进度/状态/日志交互。
3. **【浅覆盖补全：tui.rs 与 cli.rs 的 configure 同构】** tui.rs:70-96 的 configure 与 cli.rs:26-58 的 run 前半（时间预算三分支/早停/种子）同构（复用同一 MainCli），主文条目 5 已记"语义同 S23"，现场确认无 TUI 专属配置项遗漏（仅输出路径固定 LIVE_DIR/output 同名）。

### 2026-10-19 执行记录：开始Demo宿主接线

新增`SparrowOptimizeWithSvg`，一次WASM调用完成Optimize、JSON序列化与SVG导出，并经`SparrowFreeOutput`释放`JSON\0SVG\0`结果缓冲区；原固定缓冲区导出仍保留。Demo新增`SparrowWorker.js`，将同步优化移出UI线程；C++ listener事件在Worker内通过`postMessage`传送阶段、解报告、分离进度/统计和压缩步长。主页面据此显示阶段、耗时、宽度/密度/损失、eval/s、限长日志和约100ms限频live SVG，最终JSON与SVG来自同一次Optimize。取消通过`Worker.terminate()`硬中止，不能返回partial result，也不等价于`RequestStop()`/`RequestPhaseStop()`。该条记录的是开始接线时的状态：当时环境未找到Emscripten命令，尚未构建或浏览器验收；后续状态见下方“WASM构建与浏览器Demo验收”。

### 2026-10-19 执行记录：WASM构建与浏览器Demo验收

Emscripten 6.0.12 Release构建通过；CMake默认从仓库内编译SparrowCore/JaguarCore，启用`-pthread`并预热3个Pthread worker，JaguarCore使用portable AVX2 fallback。Node 24.19.0 API冒烟通过：有效合并调用输出371字节JSON与2722字节SVG且仅一个FINAL报告；空/非法JSON状态1/2；固定缓冲区JSON/SVG成功，过小缓冲区状态3。

Chrome 155.0.8059.82浏览器验收通过；HTTP服务返回COOP/COEP隔离头、JavaScript `text/javascript`及WASM `application/wasm`。`RustSppSingleItem.json`完成单次JSON+SVG输出，阶段/进度/日志/live SVG、SVG图像加载均通过；第二次用`LbfSppSmall.json`在Exploration期间真实运行后取消，Worker被终止，JSON及live SVG被清空，取消后无迟到结果；非法JSON能显示错误并恢复控件。页面错误、console错误和失败请求均为0。

浏览器验收修复并锁定三项集成缺陷：Emscripten复用`SparrowWorker.js`启动Pthread时保留其`onmessage`协议处理器；`TextDecoder`解码共享WASM内存前先复制`HEAPU8`切片；取消时撤销并隐藏已显示的live SVG；同时为CSS显式覆盖`.preview[hidden]`，避免`display:block`使空预览仍可见。截图见`.obj/s24-wasm/browser-acceptance.png`。S24仍为部分：Cancel依旧是宿主硬中止，未复现Rust合作式phase interrupt；文件日志、精确Gauge与滚动行为尚未实现。上述浏览器证据仅适用于记录的Chrome版本与fixture，不代表跨浏览器或全配置认证。
