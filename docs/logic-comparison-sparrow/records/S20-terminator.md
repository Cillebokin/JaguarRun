# S20. util/terminator.rs + util/ctrlc_terminator.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/terminator.rs` + `CodeResource/sparrow/src/util/ctrlc_terminator.rs` + `util/mod.rs`
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationTerminator.h/.cpp`；CLI 信号适配见 `SparrowCore/Tools/SparrowCli/OptimizationSignalHandler.h/.cpp`
**判定**: 相位超时对应；C++ 现区分持久全局取消 `RequestStop()` 与在 `StartPhase()` 时清除的 `RequestPhaseStop()`。SparrowCli 已将 SIGINT/Ctrl-C 接到相位停止；原生宿主可通过外部 `OptimizationTerminator` 在线调用 `RequestStop()` 做跨阶段取消。核心库保持不注册进程级信号处理器；C++ 另提供全局迭代上限/总时长

## 逐条比对（terminator.rs）

### `trait Terminator`（Rust 5-14：`kill`/`new_timeout`/`timeout_at`）
- C++ 对应：`OptimizationTerminator` 类（OptimizationTerminator.h:23-50）。
- 判定：差异（trait→具体类；接口重命名）。
- 证据与说明：Rust 以 trait 抽象（`BasicTerminator`/`CtrlCTerminator`/TUI 的 `TuiTerminator` 三实现，多态注入 `optimize()`）；C++ 是单一具体类，无继承点——`optimize` 重载直接收 `OptimizationTerminator&`（SparrowOptimizer.h:24-27）。接口映射：`kill()`→`ShouldTerminate(iteration)`（h:40，多出迭代参数）；`new_timeout(d)`→`StartPhase(d)`（h:31）；`timeout_at()`→无对应（C++ 不回查相位截止时刻，`Elapsed()`（h:41）返回自 `Start()` 的总耗时，语义不同）。C++ 用 `m_StopRequested` 表示跨阶段持久取消、用 `m_PhaseStopRequested` 表示换相位即清除的 interrupt；Rust 在具体 terminator 中组合 timeout、ctrlc 与 quit 标志。

### `BasicTerminator::new()`（Rust 27-31：`timeout: None`）
- C++ 对应：构造函数 `OptimizationTerminator(config)`（cpp:16-21）+ `Start()`（cpp:23-33）。
- 判定：一致。
- 证据与说明：初始无相位超时（Rust `None`；C++ `m_PhaseDeadline` 为空且全局/相位停止标志均为 false），同时记录总起点 `m_StartTime`（Rust 无总起点概念）。

### `BasicTerminator::kill()`（Rust 34-36：`timeout.is_some_and(|t| Instant::now() > t)`）
- C++ 对应：`ShouldTerminate()` 的相位分支（cpp:86-90）。
- 判定：一致。
- 证据与说明：C++ 以可选 `m_PhaseDeadline` 区分未设置与已设置的 deadline；存在时以 `steady_clock::now() > deadline` 终止，与 Rust `Instant::now() > timeout` 使用相同严格比较。未设置 deadline 时不因相位时间终止，两侧一致（Rust `None` → false）。零时限将 deadline 设为 phase start 时刻；在时钟前进后的轮询会终止，零时限 Rust/C++ optimize reference 已验证该行为。`OptimizePositiveTimeoutRustReference.tsv` 用 250 ms phase limit 和 300 ms listener delay 验证 FailureBased 下 deadline crossing：探索回调延迟后跳过 separator；压缩回调延迟后当前尝试继续完成，separator result 为 success/0 eval/0 moves/0 iterations，最终宽度约 3.96。新增 `OptimizeTimeBasedPositiveTimeoutRustReference.tsv` 将探索时限设为 0、TimeBased 压缩时限设为 2 s；progress callback 等待 2100 ms 而不请求 stop，deadline 到期后当前 attempt 完成，最终可行宽度约 3.68。两条用例均比较 Rust/C++ 阶段、报告、分离和布局 trace；TimeBased step 与 report 浮点使用记录容差。此测试确定性地触发 deadline crossing，不表示未经等待的完整限时搜索。恰在 deadline 相等瞬间轮询仍未单独实测。

### `BasicTerminator::new_timeout(timeout: Duration)`（Rust 38-40：`Some(Instant::now() + timeout)`）
- C++ 对应：`StartPhase(phaseDuration)`（cpp:35-50）。
- 判定：一致。
- 证据与说明：Rust 写入 `Some(Instant::now() + timeout)`，C++ 写入 `m_PhaseDeadline = steady_clock::now() + phaseDuration`；零时长同样设置 phase-start deadline，时间推进后的轮询按严格 `>` 规则终止。Rust/C++ 零时限和正时限 reference 均覆盖 phase deadline 到期。C++ 直接传入负时长时清空 deadline（配置入口会先校验时长）；该负值扩展没有 Rust `Duration` 对应物。

### `BasicTerminator::timeout_at()`（Rust 42-44）
- C++ 对应：无。
- 判定：C++ 缺失。
- 证据与说明：Rust 用它读取相位截止时刻（调用方如 TUI/优化循环显示剩余时间）；C++ 公共 API 只暴露 `Elapsed()`（h:41），无相位截止查询。影响：宿主 UI 无法直接显示"相位剩余时间"（Demo/NestLab 可用 `StartPhase` 传入值自行推算）。

## 逐条比对（ctrlc_terminator.rs）

### `CtrlCTerminator::new()`（Rust 20-36：`ctrlc::set_handler` 全局信号处理器 + `Arc<AtomicBool>`）
- C++ 对应：SparrowCli 的 `cli::ScopedCtrlCHandler`（`Tools/SparrowCli/OptimizationSignalHandler.h/.cpp`）+ `OptimizationTerminator::SetExternalPhaseStopFlag()`。
- 判定：CLI 的 SIGINT 接线已实现；核心库不注册进程级处理器。
- 证据与说明：Rust 在非 wasm 目标注册 Ctrl-C handler 并置位 `Arc<AtomicBool>`（`ctrlc_terminator.rs:26-29`，wasm 目标下整模块编译排除，mod.rs:8-9）。SparrowCli 用 `std::signal(SIGINT, ...)` 注册作用域 handler；回调只写入静态、无锁 `atomic<bool>`，优化轮询通过终止器读取该 phase-stop 标志。handler 在退出时恢复之前的 SIGINT 处理器；SparrowCore 本体不接管进程信号。`raise(SIGINT)` 用例验证信号处理器与 Rust phase-stop trace；另有 Windows 同控制台子进程用 `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)` 验证控制台事件确实到达同一处理器，真实键盘路径由用户回报通过（见第12项）。

### `CtrlCTerminator::kill()`（Rust 39-42：超时 || ctrlc 标志）
- C++ 对应：`ShouldTerminate()` 首分支检查 `m_StopRequested`、`m_PhaseStopRequested` 与可选外部 phase-stop flag（cpp:69-77）。
- 判定：停止作用域对应；CLI 信号适配和原生宿主 API 的触发源分别接线。
- 证据与说明：Rust 的 Ctrl-C 外部中断源是 OS 信号；SparrowCli 的处理器将 SIGINT 写入终止器读取的外部 phase-stop flag，等价于当前相位 `RequestPhaseStop()`。原生宿主可在线程中调用 `RequestStop()` 做持久全局取消。C++ 原子使用 relaxed 顺序；测试分别覆盖 SIGINT 的 phase reset、宿主线程在优化运行中发起取消，以及全局 stop 跨阶段保留。外部 phase-stop flag 由调用方拥有，必须在绑定期间保持有效；绑定/解绑只应发生在优化轮询未运行时。

### `CtrlCTerminator::new_timeout()`（Rust 44-48：**先重置 ctrlc 标志**再设新超时）
- C++ 对应：`RequestPhaseStop()`/外部 phase-stop flag 在 `StartPhase()` 时清除；`RequestStop()` 持久保留到 `Start()`（cpp:35-67）。
- 判定：相位中断/reset 语义已对应，SparrowCli 信号源已接线。
- 证据与说明：Rust `CtrlCTerminator::new_timeout` 清除 Ctrl-C 标志后更新 timeout；C++ `RequestPhaseStop()` 与 `StartPhase()` 复现“一次中断当前相位、换相位后恢复”，CLI 外部 SIGINT flag 也在 `StartPhase()` 清除。Rust TUI `quit` 不随 `new_timeout` 重置；C++ `RequestStop()` 同样跨 phase 保留。终止交接 fixture 由 listener 在首个 `ExplInfeas` 请求 phase stop，phase 切换后压缩继续，并比较 separatorState/best 与最终结果（S08）。C++ `Start()` 清除两种 stop flag，适用于新优化任务。

### `util/mod.rs` 的 wasm 条件门（Rust 8-9）
- C++ 对应：信号适配位于 SparrowCli；WASM C ABI 不编译/注册 CLI 信号处理器。
- 判定：当前 API 的范围边界，不构成 Rust parity 缺口。
- 证据与说明：Rust 仅在 `not(target_arch = "wasm32")` 时导出 `ctrlc_terminator`；C++ WASM 当前只导出同步 `SparrowOptimizeJson`/`SparrowOptimizeSvg`，签名没有取消参数，CMake 也没有配置 pthread/shared-memory 通道。`SparrowWasm.cpp` 将 `terminator.maxIterations` 设为 `64U`，但这是入口内部配置，不是调用方可在执行中触发的取消。要新增该能力，需另行定义 ABI 与宿主并发契约；不属于当前按Rust支持行为做 parity 的范围。

## 逐条比对（C++ 侧独有）

### `OptimizationTerminatorConfig`（h:12-20：`maxIterations=0`/`maxDuration=0ms`）+ `Validate()`（cpp:5-14）
- 判定：C++ 额外。
- 证据与说明：全局迭代数上限与全局总时长上限是 C++ 新增的终止维度（`ShouldTerminate` 的 cpp:78-84 分支）；Rust 侧无全局终止概念（exploration/compression 各自的 `max_conseq_failed_attempts`/时间预算是相位级参数）。`maxDuration=0` 只关闭 C++ 额外的全局期限；相位 deadline 独立，`StartPhase(0ms)` 将 deadline 设为 phase start，时钟前进后的轮询会到期。CLI `--time-ms` 即消费 `maxDuration`（Main.cpp:163-164）。`Validate` 负时长校验为 Result 通道惯例。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `Terminator` trait（三实现多态） | 差异 | C++ 单一具体类，宿主定制需持引用外部组合；TuiTerminator 的扩展点无对应 | terminator.rs:5-14 vs OptimizationTerminator.h:23-50 |
| 2 | Rust CLI 的 ctrlc 信号源 | CLI 已接线；库仍保持信号无关 | SparrowCli 把 SIGINT 映射为当前相位停止；原生宿主应调用 `RequestPhaseStop()` 或 `RequestStop()`，不能直接终止进程后声称保留 Rust 收尾行为 | ctrlc_terminator.rs:26-29 vs Tools/SparrowCli/OptimizationSignalHandler.cpp; OptimizationTerminator.h |
| 3 | `new_timeout` 清除 Ctrl-C/interrupt_phase，但保留 TUI quit | 对应（由 C++ 两种请求表达） | `RequestPhaseStop()` 在 `StartPhase()` 时清除；`RequestStop()` 跨 phase 保留 | ctrlc_terminator.rs:44-47; tui/runtime.rs:244-256 vs OptimizationTerminator.cpp:35-60 |
| 4 | `timeout_at()` 相位截止查询 | C++缺失 | 宿主 UI 无法直接显示相位剩余时间（可自行推算） | terminator.rs:42-44 vs h:28-41 |
| 5 | 全局 `maxIterations`/`maxDuration` 终止维度 + Validate | C++额外 | 新增全局上限（默认 0=不启用，不改变默认行为）；CLI `--time-ms` 消费 | h:12-20 vs（Rust 无） |

## 结论
相位 deadline 与两种停止作用域已有明确映射：SparrowCli 收到 SIGINT/Ctrl-C 后触发 phase stop，优化器切换 phase 时 `StartPhase()` 清除该请求；原生宿主可在线程中调用 `RequestStop()`，全局取消跨 phase 保留到 `Start()`。固定 handoff reference 验证 exploration 失败后 phase stop/reset 与压缩继续；立即停止 reference 验证 phase reset 后压缩继续；零时限 reference 验证两阶段跳过 separator 并返回同一构造布局；250 ms FailureBased reference 验证 deadline crossing 后探索跳过分离、压缩完成当前 attempt，并核对最终 3.96 宽度布局。TimeBased 两秒用例在 callback 跨过 deadline 且未请求 stop 时，验证当前 attempt 收尾、separator 的零工作成功结果和约 3.68 宽度的最终可行布局；另一用例在 deadline 前主动停止并比较五点曲线斜率。RustReference 与 C++ 全量 Debug/Release runner 均通过。deadline crossing 测试使用受控 callback 等待，不代表自然运行完整时限搜索；Windows 子进程测试已通过 `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)` 验证控制台事件、终止器观察、phase reset 和 Rust 参考轨迹，Debug/Release 均通过；用户回报的 Release 可见控制台真实键盘冒烟也已通过（见第 12 项）。相等瞬间轮询仍未验证；全局迭代/总时长属于 C++ 额外能力，对 Rust parity 运行应关闭或显式映射。

### 2026-10-12 执行记录

10. **【SIGINT 与原生宿主取消接线】** SparrowCli 的 `ScopedCtrlCHandler` 注册 `SIGINT`，信号回调只设置静态无锁 `atomic<bool>`；`OptimizationTerminator::ShouldTerminate()` 读取外部 phase-stop flag，`Start()`/`StartPhase()` 清除它。`TestImmediateStopOptimizeMatchesRustReference` 以 `std::raise(SIGINT)` 触发初始 exploration feasible report，确认 compression 恢复且完整 phase/report/step/separation/placement trace 与 Rust Ctrl-C reference 相同。另一个运行中 listener 启动独立 host thread 调用 `RequestStop()`，确认 Optimize 返回可行结果且未进入 separator；这验证原生宿主共享 `OptimizationTerminator&` 的跨线程取消接线。Debug/Release 全量测试通过，SparrowCli Release 构建及单件输入 smoke test 通过。此项记录早于下一项 Windows 控制台子进程核验；关于同步WASM C ABI取消能力的范围裁决见2026-10-19执行记录。
11. **【Windows 控制台 Ctrl-C 子进程核验】** `TestWindowsConsoleCtrlC.cpp` 为现有 `SparrowCoreTests.exe` 增加仅测试用子命令；`run-windows-console-ctrlc-probe.ps1` 创建 ready/release/observed 命名事件，在子进程到达首个 exploration feasible 回调后调用 `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)`。子进程通过同一 `ScopedCtrlCHandler` 收到事件，确认终止器观察到 phase-stop flag，再比较完整优化事件与 `OptimizeImmediateStopRustReference.tsv`；探索不进入 separator，phase reset 后压缩继续，最终布局匹配。Debug/Release 子进程探测与全量 runner 均通过。启动器在子进程创建后才忽略自身 Ctrl-C，并要求与子进程共享同一控制台；这是 Windows 控制台事件自动化，不等同于人按键。人工 Release 控制台冒烟结果见第 12 项。记录时提出的“WASM宿主另行提供共享控制通道”建议已由2026-10-19范围裁决修正：当前Rust `wasm32` 不含Ctrl-C终止器，新增WASM中途取消不属于现有 parity 要求。
12. **【真实键盘 Ctrl-C 冒烟】** 2026-10-12，用户在可见 Windows PowerShell 中运行 Release `SparrowCoreTests.exe --windows-console-ctrlc-manual`，在 `READY: press Ctrl+C in this console now` 后按下键盘 Ctrl+C；回报输出 `WINDOWS_CTRL_C_SIGNAL_OBSERVED` 与 `WINDOWS_CTRL_C_PROBE_PASS`，命令正常返回 PowerShell 提示符。此证据确认真实控制台键盘事件经过 `ScopedCtrlCHandler`，终止器观察到 phase-stop flag，且现有 immediate-stop Rust reference 轨迹通过；仅覆盖该 Windows/MSVC Release 环境和固定夹具，不扩展为其他终端、构建配置或自然运行时限的证明。

### 2026-10-19 执行记录：S20范围裁决与Linux行为说明
13. **【WASM中途取消范围裁决】** Rust `util/mod.rs` 仅在非 `wasm32` 目标编译 `ctrlc_terminator`；C++ WASM ABI 的 JSON/SVG 导出为同步调用，没有取消参数或可并发共享控制入口。当前WASM配置将 `terminator.maxIterations` 设为 `64U`，这不等于调用方可触发的中途取消。故S20不新增WASM取消API；若未来产品要求此能力，应另立ABI与宿主并发设计及验收项。
14. **【Linux行为与证据边界】** `OptimizationSignalHandler.cpp` 使用 `std::signal(SIGINT, ...)`，处理函数只将无锁 `atomic<bool>` 置位；SparrowCli把该标志接到phase-stop，`StartPhase()` 清除相位中断。源码没有平台专属信号分支。当前部署文档声明的二进制包目标为Windows x64/MSVC2019，因此Linux结论仅为源码映射说明；本记录没有Linux构建或运行时验证，也不外推为Linux支持承诺。
15. **【S20退出判断】** 现有Windows Debug/Release子进程和全量runner证据，以及用户回报的Release真实键盘冒烟证据仍按原范围有效；本轮未重跑这些测试。结合WASM与Linux边界裁决，S20在当前平台/API范围内关闭。若认证范围加入Linux运行时，或产品要求WASM中途取消，应重新打开对应验收项。

### 补充审计（2026-09-29）
1. **【核对确认：文件覆盖完整性】** terminator.rs 44 行（trait 三方法 + BasicTerminator 三方法）与 ctrlc_terminator.rs 52 行（new/kill/new_timeout/timeout_at 四方法）逐方法比对——主文均有条目，无遗漏函数。
2. **【遗漏补记：`Default for CtrlCTerminator`/`BasicTerminator`（Rust 24-27/ctrlc:16-19）】** 双侧均 `Default::default() = Self::new()`——trait 对象默认构造语义，C++ 无对应（构造直接传 config）。无行为影响，补记闭合清单。
3. **【2026-10-07 修正：kill() 的 deadline 表示与比较符】** BasicTerminator.kill 用严格 `>`（`Instant::now() > timeout`，terminator.rs:35）。C++ 原实现用正时长门控且以 `>=` 比较；现改为可选 deadline，并以 `steady_clock::now() > deadline` 比较（OptimizationTerminator.cpp:86-88），零时限不再被解释成无限时限。相等瞬间的轮询仍未由可重复用例单独覆盖。
4. **【浅覆盖补全：TuiTerminator 的第三实现归 S24】** 主文记了三实现但 TuiTerminator 细节（quit 持久 + interrupt_phase 换相位重置，runtime.rs:244-256）归 S24 条目 4——两记录交叉引用一致，无缺口。
5. **【2026-10-07 执行记录：phase stop 的初始报告时点】** `optimizer_exit_reference.rs` 与 `TestImmediateStopOptimizeMatchesRustReference` 使用同一单件 cold-start 输入；Rust 使用真实 `CtrlCTerminator`，listener 在初始 `ExplFeas` 回调中设置其公开 Ctrl-C 标志，C++ listener 调用 `RequestPhaseStop()`，两边比较 phase/report/separation/placement 序列。探索 separation 数为 0；C++ `StartPhase()` 和 Rust `CtrlCTerminator::new_timeout()` 清除相位中断后，压缩各完成一次 0 eval/move/iteration 的成功 separation，再由最小宽度停止。该固定回调路径不是 timeout 测试，也不覆盖截止边界。
6. **【2026-10-07 执行记录：零时限 phase timeout】** `optimizer_exit_reference.rs` 使用 `BasicTerminator`，将探索和压缩的 `time_limit` 均设为 `Duration::ZERO`；`OptimizeZeroTimeoutRustReference.tsv` 与 `TestZeroTimeoutOptimizeMatchesRustReference` 比较同一单件 cold-start 的 phase/report/placement 序列，并断言 separation result 与压缩 step 均为 0。C++ `TestZeroPhaseTimeoutSemantics` 验证未启动 phase 时不超时，并在 `StartPhase(0ms)` 后等待 1 ms 再轮询，以避开严格 `>` 比较下的同刻时钟读数。此路径验证零时限到期，不覆盖同刻轮询；正时限到期证据见第 7 项。
7. **【2026-10-07 执行记录：正时限 FailureBased deadline 收尾】** `optimizer_exit_reference.rs` 与 `TestExploration.cpp::TestPositiveTimeoutOptimizeMatchesRustReference` 使用 seed=3 单件 warm-start，探索/压缩 phase limit 均为 250 ms；Rust/C++ listener 分别在 exploration 初始可行报告和 compression 首个 shrink-progress 回调内等待 300 ms，不设置 stop flag。reference 比较 phase/report/separation/step/final placement trace：探索没有 separation result；压缩已越过循环入口，等待后仍执行宽度变化与当前 attempt 收尾，separator result 为 success 且 eval/move/iteration 均为 0；final width 为 3.96、placement 保持可行。该测试验证固定回调触发点下 deadline 到期后的阶段行为，不覆盖自然运行时长下的迭代数量或 deadline 相等瞬间。
8. **【2026-10-07 执行记录：正时限 TimeBased 曲线采样】** `CompressionTimeBasedPositiveTimeoutRustReference.tsv`、`optimizer_exit_reference.rs::positive_time_based_compression_shrink_curve_matches_reference` 与 `TestExploration.cpp::TestPositiveTimeBasedCompressionCurveMatchesRustReference` 配置 2 秒 compression limit、TimeBased `max=0.08/min=0.02`，记录五个进度回调的相对时间和步长，前四个回调各等待 400 ms；第五个回调主动请求 phase stop。两侧从接近 max 开始，按实测时间的相邻步长斜率均匹配 `(min-max)/limit=-0.03/s`，容差 0.001/s。C++ 用毫秒截断比值，Rust 用 f32 秒；相同迭代编号对应的 step 还会受各自 separator 运行时长影响。这个测试不自然跑到 2 秒 deadline，也不代表两边在相同阶段预算下返回相同迭代数或最终布局。
9. **【2026-10-07 执行记录：TimeBased deadline 收尾与最终布局】** `OptimizeTimeBasedPositiveTimeoutRustReference.tsv`、`positive_time_based_timeout_finishes_current_attempt_and_matches_reference` 与 `TestPositiveTimeBasedTimeoutOptimizeMatchesRustReference` 使用 seed=3 单件 warm-start、探索时限 0、压缩时限 2 s、TimeBased `max=0.08/min=0.02`；首个 compression progress callback 等待 2100 ms，但不设置 stop flag。phase deadline 到期后当前 attempt 仍完成，separator result 为 success/0 eval/move/iteration；最终 strip width 约 3.68、placement 可行。Rust/C++ 比较 phase/report/step/separation/placement trace，step 容差 `1e-6`、report width/density `1e-5`、placement rotation `1e-6`/translation `5e-5`。受控等待仅用于确定 deadline crossing 时点，不代表未经等待的自然完整时限搜索或其迭代数。
