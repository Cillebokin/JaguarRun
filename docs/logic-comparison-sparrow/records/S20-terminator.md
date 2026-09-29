# S20. util/terminator.rs + util/ctrlc_terminator.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/terminator.rs`（44 行）+ `CodeResource/sparrow/src/util/ctrlc_terminator.rs`（52 行）+ `util/mod.rs`（9 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationTerminator.h`（40 行）+ `SparrowCore/Source/Optimizer/OptimizationTerminator.cpp`（76 行）
**判定**: 基本等价（超时终止语义一致；ctrlc 全局信号终止器 C++ 缺失，由 `RequestStop()` 协作式停止替代；C++ 额外全局迭代上限/总时长/相位时长三类终止条件）

## 逐条比对（terminator.rs）

### `trait Terminator`（Rust 5-14：`kill`/`new_timeout`/`timeout_at`）
- C++ 对应：`OptimizationTerminator` 类（OptimizationTerminator.h:22-39）。
- 判定：差异（trait→具体类；接口重命名）。
- 证据与说明：Rust 以 trait 抽象（`BasicTerminator`/`CtrlCTerminator`/TUI 的 `TuiTerminator` 三实现，多态注入 `optimize()`）；C++ 是单一具体类，无继承点——`optimize` 重载直接收 `OptimizationTerminator&`（SparrowOptimizer.h:24-27）。接口映射：`kill()`→`ShouldTerminate(iteration)`（h:30，多出迭代参数）；`new_timeout(d)`→`StartPhase(d)`（h:28）；`timeout_at()`→无对应（C++ 不回查相位截止时刻，`Elapsed()`（h:31）返回自 `Start()` 的总耗时，语义不同）。C++ 侧把"相位超时"与"全局停止"合进一个对象（`m_StopRequested` 原子标志），Rust 用组合（timeout 字段+ctrlc 标志）由具体实现混合。

### `BasicTerminator::new()`（Rust 27-31：`timeout: None`）
- C++ 对应：构造函数 `OptimizationTerminator(config)`（cpp:16-22）+ `Start()`（cpp:24-30）。
- 判定：一致。
- 证据与说明：初始无超时（Rust `None`；C++ `m_PhaseDuration{0}` 且 `m_StopRequested=false`），同时记录总起点 `m_StartTime`（Rust 无总起点概念）。

### `BasicTerminator::kill()`（Rust 34-36：`timeout.is_some_and(|t| Instant::now() > t)`）
- C++ 对应：`ShouldTerminate()` 的相位分支（cpp:60-66）。
- 判定：一致。
- 证据与说明：`m_PhaseDuration > 0` 时 `now - m_PhaseStartTime >= m_PhaseDuration` 即终止——与 Rust"当前时刻超过相位截止点"等价（`>=` vs `>` 的边界差仅在恰好相等的瞬时，单调时钟下不可区分）。未设相位超时则不因时间终止，两侧一致（Rust `None` → false）。

### `BasicTerminator::new_timeout(timeout: Duration)`（Rust 38-40：`Some(Instant::now() + timeout)`）
- C++ 对应：`StartPhase(phaseDuration)`（cpp:32-39）。
- 判定：一致。
- 证据与说明：两侧均"记录相位起点+时长"（Rust 记截止点、C++ 记起点+时长，等价）；C++ 额外把负时长钳为 0（cpp:36-38）。

### `BasicTerminator::timeout_at()`（Rust 42-44）
- C++ 对应：无。
- 判定：C++ 缺失。
- 证据与说明：Rust 用它读取相位截止时刻（调用方如 TUI/优化循环显示剩余时间）；C++ 公共 API 只暴露 `Elapsed()`，无相位截止查询。影响：宿主 UI 无法直接显示"相位剩余时间"（Demo/NestLab 可用 `StartPhase` 传入值自行推算）。

## 逐条比对（ctrlc_terminator.rs）

### `CtrlCTerminator::new()`（Rust 20-36：`ctrlc::set_handler` 全局信号处理器 + `Arc<AtomicBool>`）
- C++ 对应：无。
- 判定：C++ 缺失。
- 证据与说明：Rust 在进程内注册全局 SIGINT 处理器（`ctrlc` crate，`ctrlc_terminator.rs:26-29`，wasm 目标下整模块编译排除，mod.rs:8-9）；C++ 侧经检索 SparrowCore 全部源码无 `signal`/`SIGINT`/`SetConsoleCtrlHandler` 调用——库不适合装全局信号处理器（与多宿主嵌入冲突），判定符合预期。影响评估：**低**——CLI 场景下用户 Ctrl-C 直接杀进程（无需优雅停止，最终结果本就只在结束时写盘）；需要优雅停止的宿主（NestLab GUI/浏览器 Demo）用 `RequestStop()`（h:29）从自身事件循环协作触发，SparrowWasm 的 JS 边界同理。

### `CtrlCTerminator::kill()`（Rust 39-42：超时 || ctrlc 标志）
- C++ 对应：`ShouldTerminate()` 首分支 `m_StopRequested`（cpp:48-50）。
- 判定：差异（触发源不同，机制等价）。
- 证据与说明：Rust 的外部中断源是 OS 信号；C++ 的外部中断源是 `RequestStop()` 线程安全调用（`std::atomic<bool>::store`，cpp:41-44，relaxed 序与 Rust `Ordering::SeqCst` 略弱但对单布尔标志+终态判定足够）。信号语义→协作式停止的映射是 C++ 嵌入式库的标准取舍。

### `CtrlCTerminator::new_timeout()`（Rust 44-48：**先重置 ctrlc 标志**再设新超时）
- C++ 对应：`StartPhase()` **不重置** `m_StopRequested`；重置在 `Start()`（cpp:29）。
- 判定：差异（相位切换时外部停止标志的持久性）。
- 证据与说明：Rust 语义=一次 Ctrl-C 只中断当前相位（exploration 被 Ctrl-C 后换相位即恢复运行，除非再按）；C++ `StartPhase` 保留 `m_StopRequested=true`（一旦请求停止，后续相位 `ShouldTerminate` 恒真），只有完整 `Start()` 才清除。这是 TUI"skip phase"（tui/runtime.rs:139-141 Ctrl-C=跳过相位）与 C++"整体停止"的语义分叉：C++ 无"跳过单相位"的公共入口。影响：宿主若要复刻"跳过当前相位"，需自带相位级停止逻辑（现有 Demo 未暴露该功能，见 S24）。

### `util/mod.rs` 的 wasm 条件门（Rust 8-9）
- C++ 对应：无（C++ 无需条件编译，信号模块整体不存在）。
- 判定：C++ 缺失（随 ctrlc 一并缺失）。

## 逐条比对（C++ 侧独有）

### `OptimizationTerminatorConfig`（h:12-20：`maxIterations=0`/`maxDuration=0ms`）+ `Validate()`（cpp:5-14）
- 判定：C++ 额外。
- 证据与说明：全局迭代数上限与全局总时长上限是 C++ 新增的终止维度（`ShouldTerminate` 的 cpp:52-59 分支）；Rust 侧无全局终止概念（exploration/compression 各自的 `max_conseq_failed_attempts`/时间预算是相位级参数）。注释（h:14-15）明言 0 值对齐 `BasicTerminator` 的"仅相位超时"行为。CLI `--time-ms` 即消费 `maxDuration`（Main.cpp:157-158）。`Validate` 负时长校验为 Result 通道惯例。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `Terminator` trait（三实现多态） | 差异 | C++ 单一具体类，宿主定制需持引用外部组合；TuiTerminator 的扩展点无对应 | terminator.rs:5-14 vs OptimizationTerminator.h:22-39 |
| 2 | ctrlc 全局信号处理器 | C++缺失 | 影响 低：CLI 直接杀进程即可；优雅停止由 `RequestStop()` 协作替代 | ctrlc_terminator.rs:26-29 vs（全库无 signal 调用） |
| 3 | `new_timeout` 重置中断标志（单相位中断→换相位恢复） | 差异 | C++ 一旦 RequestStop 则全程终止，无"跳过当前相位"语义 | ctrlc_terminator.rs:44-47 vs cpp:32-39/29 |
| 4 | `timeout_at()` 相位截止查询 | C++缺失 | 宿主 UI 无法直接显示相位剩余时间（可自行推算） | terminator.rs:42-44 vs h:27-31 |
| 5 | 全局 `maxIterations`/`maxDuration` 终止维度 + Validate | C++额外 | 新增全局上限（默认 0=不启用，不改变默认行为）；CLI `--time-ms` 消费 | h:12-20 vs（Rust 无） |

## 结论
相位超时的核心语义（设超时→超时即杀）与默认"仅相位超时限制"的行为两侧一致，判定基本等价。Rust 的 trait 抽象与三个实现（Basic/CtrlC/Tui）在 C++ 收敛为一个自带原子停止标志的具体类：OS 信号入口按预期缺失（影响低，CLI 场景可接受），其替代物 `RequestStop()` 的语义比 Ctrl-C 更强（不可被相位切换重置），导致"跳过单相位"功能无公共入口；C++ 额外引入全局迭代/总时长上限，默认关闭不影响等价性。
