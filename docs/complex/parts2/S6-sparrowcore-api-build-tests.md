# SparrowCore 工程结构说明：公共接口、构建产物、外围工具与测试/Rust 差分体系

## 一、工程概览与构建产物

SparrowCore 根目录为 SparrowCore/，依赖同级的 JaguarCore，SparrowCore.pro 通过 INCLUDEPATH += $$PWD/../../JaguarCore/Include 引入其头文件。核心静态库由 SparrowCore/SparrowCore.pro 定义，使用 TEMPLATE = lib、CONFIG += staticlib c++17，并以 QT -= core gui 去掉 Qt 依赖，输出目录为 DESTDIR = $$SPARROWCORE_BUILD_DIR/bin。仓库同时存在 Include/ 与 include/ 两套目录，当前 .pro 使用大写 Include/ 路径且目标目录存在；跨平台构建失败尚未由此得到证明，实际维护风险是两套头文件副本可能漂移，需按 G1 比较内容并指定权威副本。

除静态库外，工程有四类外围产物。第一是命令行工具 `SparrowCli`，由 `Tools/SparrowCli/SparrowCli.pro` 定义，源文件为 `Main.cpp` 与 `OptimizationSignalHandler.cpp`，链接 `-lSparrowCore -lJaguarCore`。它读取 JSON 实例（`--input`），写出 JSON 解（`--output`），可选输出 SVG（`--svg`），并支持 `--seed`、`--samples`、`--time-ms`、`--early-termination` 等参数。其中 `--early-termination` 会把 `maxConsecutiveFailures` 设为 10，并选用 `SHRINK_DECAY_FAILURE_BASED`。退出码含义为：2 表示用法或参数错误，3 为读取失败，4 为导入失败，5 为优化失败，6 为外部格式转换失败，7 为序列化失败，8 为写出失败，9 为 SVG 失败，10 为 main 捕获的未处理异常；代码 10 的外部触发尚未验证。Ctrl-C 由 `OptimizationSignalHandler.cpp` 中的 `HandleCtrlC` 通过 `std::signal(SIGINT, ...)` 接收，`ScopedCtrlCHandler` 在构造时调用 `OptimizationTerminator::SetExternalPhaseStopFlag`，析构时恢复旧处理函数。

第二是 WASM 产物，位于 `Tools/SparrowWasm/`，包括 `SparrowWasm.pro`（staticlib）与 `CMakeLists.txt`。在 Emscripten 分支中，CMake 生成可执行产物，链接参数包括 `-sMODULARIZE=1`、`-sEXPORT_NAME=SparrowWasmModule` 以及 `-sEXPORTED_FUNCTIONS=['_SparrowOptimizeJson','_SparrowOptimizeSvg']`；在非 Emscripten 分支中则生成静态库，输出名为 `sparrow_wasm`。CMake 通过 `SPARROWCORE_LIBRARY` 与 `JAGUARCORE_LIBRARY` 两个变量接收依赖库路径。C 接口声明在 `SparrowWasm.h`，实现在 `SparrowWasm.cpp`。

第三是浏览器 Demo，位于 Demo/，由 Index.html、App.js、Style.css 组成。页面没有网络依赖，包含一个文件选择框、JSON Optimize 运行按钮与独立的 Export SVG preview 按钮、一个状态段落、一个预览图和一个输出块；浏览器端验收仍待完成。

第四是性能基准 `SparrowCoreBench`，由 `bench/SparrowCoreBench.pro` 定义，源文件只有 `BenchMain.cpp`，要求命令行参数必须为 `all`。它会跑 `lbf_spp_small`（读取 `tests/fixtures/LbfSppSmall.json`）和 `lbf_spp_large`（代码合成的 8 个物品、宽度 8.0 的实例），输出 CSV，列为 `sparrowcore, version, name, seed, sampleCount, placedItemCount, remainingDemand, stripWidth, density, elapsed_ms, final_valid_rate, surrogate_rejects, exact_collisions, exact_collision_layout, checksum`。其中 `checksum` 是对宽度、放置索引、handle、旋转和平移原始字节做的 FNV-1a 哈希，因此依赖平台和编译器的结构体布局，不能跨平台直接比对。

第五是测试可执行文件 `SparrowCoreTests`，见第三节。

## 二、公共接口与典型调用顺序

公共接口集中在 `include/SparrowCore/` 下。`optimizer/SparrowOptimizer.h` 定义了 `SparrowOptimizationResult`，成员为 `jaguar::SPSolution solution`、`LbfMetrics metrics` 与 `OptimizationHistory history`。`Optimize` 有两个重载：一个接收 `(instance, config, OptimizationListener*, OptimizationObserver*)`，另一个接收 `(instance, config, OptimizationTerminator&, listener, observer)`。`optimizer/SparrowConfig.h` 中的 `SparrowConfig` 包含可选的 `seed`、可选的 `warmStart`（`SPSolution`）、`constructor`、`exploration`、`compression` 与 `terminator`。`MakeRustDefaultImporterConfig()` 返回与 Rust 参考一致的导入参数，其中 quadtreeDepth 为 4，cdThreshold 为 64，poleLimits 为 `{{64,0.0},{16,0.8},{8,0.9}}`，simplifyTolerance 为 0.001。

典型调用顺序是：先由 JSON 字符串构造 `SPInstance`（经 `JsonCodec` 与 `ValidateSPSolution` 等辅助函数），再构造 `SparrowConfig`（CLI 与 WASM 都以 `MakeRustDefaultImporterConfig()` 为基础修改），然后调用 `sparrow::Optimize(instance, config)` 得到 `SparrowOptimizationResult`。最后通过 `SerializeExternalSPSolutionJson(external, {false})` 导出 JSON，或通过 `ExportLayoutSvg` 导出 SVG。WASM 的 `OptimizeInput` 固定 seed 为 0，`constructor.nContainerSamples=128`，`exploration.maxIterations=2`，`maxConsecutiveFailures=3`，`compression.maxIterations=2`，`terminator.maxIterations=64`。`SparrowOptimizer.cpp` 中的 `OptimizeWithTerminator` 按顺序派生阶段种子，依次调用 `StartPhase(exploration.timeLimit)` 与 `Explore`，再调用 `StartPhase(compression.timeLimit)` 与 `Compress`，最后通过 `SPProblem::Restore` 与 `Layout::Restore` 校验，失败时返回 `ErrorCode::Infeasible`。

WASM 当前有两个 C ABI 导出：SparrowOptimizeJson 与 SparrowOptimizeSvg。空输入返回 1；输入已接受后的解析、优化、恢复或序列化失败返回 2；输出缓冲区不足返回 3。两个导出入口都捕获 C++ 异常，并以代码 2 和 {"error":"internal error"} 负载返回；CLI main 也捕获异常并以退出码 10 返回。Deployment.md 已按此契约更新。异常路径的外部触发以及 CLI 退出码 10 的运行验证仍待完成。

## 三、测试体系的四层结构

测试入口是 `tests/TestSupport.cpp` 的 `main`（已核对）。若 `argc > 1`，它调用 `RunWindowsConsoleCtrlCProbe`；否则先检查 `sparrow::Version()` 与 `jaguar::Version()` 非空，然后按顺序运行 `RunRngTests`、`RunSamplingTests`、`RunLbfLossTests`、`RunSppLbfTests`、`RunBppLbfTests`、`RunQuantifyTests`、`RunSeparatorTests`、`RunExplorationTests`、`RunWasmTests`，任一失败即返回 1。工程文件 `tests/SparrowCoreTests.pro` 编译全部 `Test*.cpp`，并把 `Tools/SparrowCli/OptimizationSignalHandler.cpp` 与 `Tools/SparrowWasm/SparrowWasm.cpp` 一并编入，因此 WASM 的 C ABI 也能在原生环境直接测试。Debug 链接 `-lSparrowCore -lJaguarCored`，Release 链接 `-lJaguarCore`。

第一层是单元测试，覆盖 RNG（`TestRng.cpp`，7 个）、采样（`TestSampling.cpp`，21 个，使用 `FloatBits` 做逐位比较）、LBF 损失（`TestLbfLoss.cpp`，8 个）、SPP 与 BPP 的 LBF 构造（`TestSppLbf.cpp` 12 个，`TestBppLbf.cpp` 5 个）、量化（`TestQuantify.cpp`，31 个）。第二层是行为不变式测试，主要是 `TestSeparator.cpp`（8 个）与 `TestExploration.cpp`（66 个，约 10668 行）。第三层是 Rust 差分（oracle）测试，核心位于 `tests/RustReference/`。它是一个 Cargo 包 `sparrow-rust-reference`（edition 2024，rust-version 1.90），依赖 `jagua-rs 0.9.0`（git 来源，分支 `jg-explicit-rotations-v0.9`，feature `spp`）与 `rand 0.10` 等，并通过 `sparrow = { path = "../../../CodeResource/sparrow", features = ["oracle-trace"] }` 直接引用 Rust 参考实现。`rust-toolchain.toml` 锁定 1.98.1，目标为 `x86_64-pc-windows-msvc`。`tests/*.rs` 共 56 个文件、145 个 `#[test]`。`pair_matrix_reference.rs` 在 inline `mod quantify` 中使用 `#[path = "../../../../../CodeResource/sparrow/src/quantify/pair_matrix.rs"]`，层数正确；`best_samples_*` 在顶层使用 `#[path = "../../../../CodeResource/sparrow/src/..."]`，同样正确。`exploration_pool_sampling_trace_reference.rs` 使用 `include_str!("../../Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv")`。`src/main.rs` 接收多个路径参数，把 Rust 侧轨迹写成 TSV，默认回退路径为 `../Fixtures/...`。

第四层是固定种子回归，即 C++ 侧的 `*CppReference.tsv` 与 `PhaseReference.tsv` 基线，由 `tests/fixtures/` 中约 142 个文件提供，包括 `LbfSppSmall.json`、`LbfBppSmall.json`、`RustSppSingleItem.json`、`SeparatorRoundRustTrace.json`，以及大量 `*RustTrace.tsv` 与 `*RustReference.tsv`。本段列举的 fixture 路径已静态确认存在；整个目录的引用关系与孤立项尚未完成清点（见 E7）。

更新 fixture 的方法是：在 Rust 侧修改后，通过 `RustReference` 的 `main.rs` 重新生成 TSV，人工核对差异后替换 `tests/fixtures/` 中对应文件，并重新运行 `SparrowCoreTests.exe`。C++ 测试代码中没有自动写基线文件的逻辑（grep 未发现 ofstream 写基线），因此基线更新完全依赖人工操作。Rust 侧的脚本是 `tests/RustReference/verify-rust-reference.ps1`，它检查 `rustc -vV` 的 release 为 1.98.1、commit 为 `48a229ceaefd4985c50990b14116b6d856af0985`，然后依次执行 `cargo fmt --check`、`cargo test --locked --target x86_64-pc-windows-msvc` 与 `cargo test --locked --release`。该脚本不生成轨迹，也不运行 C++ 侧的比对。

Windows Ctrl-C 探针由 `tests/TestWindowsConsoleCtrlC.cpp`（仅 `_WIN32`）与 `tests/run-windows-console-ctrlc-probe.ps1` 组成。脚本创建命名事件，以 `--windows-console-ctrlc-probe <ready> <release> <observed>` 启动子进程，父进程调用 `SetConsoleCtrlHandler(NULL, true)`，发送 `GenerateConsoleCtrlEvent(0,0)`，要求子进程退出码为 0 并输出 `WINDOWS_CTRL_C_PROBE_PASS`，默认超时 15 秒。

## 四、许可证边界与覆盖声明

docs/SourceMap.md 记录 jagua-rs/lbf 与 sparrow 的来源、许可证和 C++ 文件映射。当前文档使用仓库内 CodeResource/jagua-rs 本地镜像说明，并注明 sparrow 工作源码由负责人本地指定；此前的 NestLab 绝对路径已清理。JaguarCore/tools/RustOracle 当前锁定 Git revision f5e2b5f，而本地镜像曾被记录为与该 revision 有差异；两者对应关系与 fixture 生成基线见移植工作项清单 D5/E4。

docs/Differential.md 记录 fixture 矩阵、字段容差及 2026-10-08/09 的重复性运行结果。矩阵中的旧运行记录应按其日期与生成配置理解；新的默认 cold-start fixture 及 10 分钟 compression phase deadline override 尚需记录到覆盖矩阵。当前运行入口和构建目录应以 SparrowCore 当前 Deployment.md、ReleaseChecklist.md 与 qmake 工程配置为准，并在记录中保留命令和平台。

这里要区分三类证据：FloatBits 断言可证明指定字段和用例逐位相同；固定 fixture 的字段容差比较只证明该输入集合满足容差；SHA-256 重复性记录只证明同平台重复运行的输出稳定，不能证明 Rust 与 C++ 逐位一致。Exploration、Separator、Compression 等大部分轨迹比对属于固定种子回归，不是通用 bit-exact 证明。Differential.md 中的相关表述应按字段与测试逐条理解；矩阵未覆盖的 Rust 参考文件也不能仅凭目录存在推断已纳入 C++ 对拍。

## 五、发布门禁与文档一致性问题

docs/ReleaseChecklist.md 现已列出 MSVC/Qt Release 构建、测试程序、同平台 checksum、CLI 冒烟及退出码、Windows Ctrl-C、C++/Rust 比较、WASM、Demo 与最终解可行性检查。Bench checksum 只适合同一平台和工具链内比较。verify-rust-reference.ps1 只验证 Rust 侧，不能单独证明 C++ parity；在 E4 fixture 基线复核完成前，C++/Rust 项应继续标记未运行或未验收。

当前仍需处理的事项如下。其一，SparrowCore 同时存在 Include/include、Tests/tests 两套大小写不同的目录；当前 .pro 所引用的大写目录存在，因此仅凭大小写差异不能断言 Linux 构建失败，G1 的真实风险是两套目录内容漂移。其二，TestWasm.cpp 仍保留依赖目录布局的 fixture 候选路径，需在目标构建目录验证。其三，E4 的 oracle 配置已统一到 Rust 1.98.1 与 jagua-rs f5e2b5f，但受影响 fixture 的重新生成与对拍尚无记录。其四，ReleaseChecklist 已列入 Ctrl-C 探针、Demo 与 C++/Rust 比较；用户记录中浏览器 Demo 未运行，CLI 退出码 10 也未外部触发。SourceMap 与 Deployment 的旧绝对路径已清理，根目录生成的 Makefile 已删除。

综上，SparrowCore 包含静态库、CLI、WASM、Demo、Bench 与测试体系。当前应先闭合已决范围内的 oracle/fixture 基线复核与差分覆盖，再处理 Include/include、Tests/tests 双目录的内容漂移、TestWasm.cpp 路径假设和浏览器 Demo 验收。10 月 16 日的 MSVC Debug/Release 全套与 CLI Release 冒烟已有用户执行记录；它们支持该次配置下的构建与测试结论，尚不能替代 E4 的 Rust 基线复核或全范围 parity 验收。
