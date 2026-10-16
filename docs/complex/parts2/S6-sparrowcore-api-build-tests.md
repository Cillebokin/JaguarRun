# SparrowCore 工程结构说明：公共接口、构建产物、外围工具与测试/Rust 差分体系

## 一、工程概览与构建产物

SparrowCore 根目录为 `SparrowCore/`，依赖同级的 JaguarCore，`SparrowCore.pro` 通过 `INCLUDEPATH += $$PWD/../../JaguarCore/Include` 引入其头文件。核心静态库由 `SparrowCore/SparrowCore.pro` 定义，使用 `TEMPLATE = lib`、`CONFIG += staticlib c++17`，并以 `QT -= core gui` 去掉 Qt 依赖，输出目录为 `DESTDIR = $$SPARROWCORE_BUILD_DIR/bin`。注意该 `.pro` 中写的是 `$$PWD/Include/...`，而磁盘上的目录是小写的 `SparrowCore/include/`，源码 `#include` 也依赖大小写不敏感的文件系统（Windows 下可以工作，Linux 下会失败），这一点在第五节再次说明。

除静态库外，工程有四类外围产物。第一是命令行工具 `SparrowCli`，由 `Tools/SparrowCli/SparrowCli.pro` 定义，源文件为 `Main.cpp` 与 `OptimizationSignalHandler.cpp`，链接 `-lSparrowCore -lJaguarCore`。它读取 JSON 实例（`--input`），写出 JSON 解（`--output`），可选输出 SVG（`--svg`），并支持 `--seed`、`--samples`、`--time-ms`、`--early-termination` 等参数。其中 `--early-termination` 会把 `maxConsecutiveFailures` 设为 10，并选用 `SHRINK_DECAY_FAILURE_BASED`。退出码含义为：2 表示用法或参数错误，3 为读取失败，4 为导入失败，5 为优化失败，6 为外部格式转换失败，7 为序列化失败，8 为写出失败，9 为 SVG 失败。Ctrl-C 由 `OptimizationSignalHandler.cpp` 中的 `HandleCtrlC` 通过 `std::signal(SIGINT, ...)` 接收，`ScopedCtrlCHandler` 在构造时调用 `OptimizationTerminator::SetExternalPhaseStopFlag`，析构时恢复旧处理函数。

第二是 WASM 产物，位于 `Tools/SparrowWasm/`，包括 `SparrowWasm.pro`（staticlib）与 `CMakeLists.txt`。在 Emscripten 分支中，CMake 生成可执行产物，链接参数包括 `-sMODULARIZE=1`、`-sEXPORT_NAME=SparrowWasmModule` 以及 `-sEXPORTED_FUNCTIONS=['_SparrowOptimizeJson','_SparrowOptimizeSvg']`；在非 Emscripten 分支中则生成静态库，输出名为 `sparrow_wasm`。CMake 通过 `SPARROWCORE_LIBRARY` 与 `JAGUARCORE_LIBRARY` 两个变量接收依赖库路径。C 接口声明在 `SparrowWasm.h`，实现在 `SparrowWasm.cpp`。

第三是浏览器 Demo，位于 `Demo/`，由 `Index.html`、`App.js`、`Style.css` 组成。页面没有网络依赖，只有一个文件选择框、一个运行按钮、一个状态段落、一个预览图和一个输出块。

第四是性能基准 `SparrowCoreBench`，由 `bench/SparrowCoreBench.pro` 定义，源文件只有 `BenchMain.cpp`，要求命令行参数必须为 `all`。它会跑 `lbf_spp_small`（读取 `tests/fixtures/LbfSppSmall.json`）和 `lbf_spp_large`（代码合成的 8 个物品、宽度 8.0 的实例），输出 CSV，列为 `sparrowcore, version, name, seed, sampleCount, placedItemCount, remainingDemand, stripWidth, density, elapsed_ms, final_valid_rate, surrogate_rejects, exact_collisions, exact_collision_layout, checksum`。其中 `checksum` 是对宽度、放置索引、handle、旋转和平移原始字节做的 FNV-1a 哈希，因此依赖平台和编译器的结构体布局，不能跨平台直接比对。

第五是测试可执行文件 `SparrowCoreTests`，见第三节。

## 二、公共接口与典型调用顺序

公共接口集中在 `include/SparrowCore/` 下。`optimizer/SparrowOptimizer.h` 定义了 `SparrowOptimizationResult`，成员为 `jaguar::SPSolution solution`、`LbfMetrics metrics` 与 `OptimizationHistory history`。`Optimize` 有两个重载：一个接收 `(instance, config, OptimizationListener*, OptimizationObserver*)`，另一个接收 `(instance, config, OptimizationTerminator&, listener, observer)`。`optimizer/SparrowConfig.h` 中的 `SparrowConfig` 包含可选的 `seed`、可选的 `warmStart`（`SPSolution`）、`constructor`、`exploration`、`compression` 与 `terminator`。`MakeRustDefaultImporterConfig()` 返回与 Rust 参考一致的导入参数，其中 quadtreeDepth 为 4，cdThreshold 为 64，poleLimits 为 `{{64,0.0},{16,0.8},{8,0.9}}`，simplifyTolerance 为 0.001。

典型调用顺序是：先由 JSON 字符串构造 `SPInstance`（经 `JsonCodec` 与 `ValidateSPSolution` 等辅助函数），再构造 `SparrowConfig`（CLI 与 WASM 都以 `MakeRustDefaultImporterConfig()` 为基础修改），然后调用 `sparrow::Optimize(instance, config)` 得到 `SparrowOptimizationResult`。最后通过 `SerializeExternalSPSolutionJson(external, {false})` 导出 JSON，或通过 `ExportLayoutSvg` 导出 SVG。WASM 的 `OptimizeInput` 固定 seed 为 0，`constructor.nContainerSamples=128`，`exploration.maxIterations=2`，`maxConsecutiveFailures=3`，`compression.maxIterations=2`，`terminator.maxIterations=64`。`SparrowOptimizer.cpp` 中的 `OptimizeWithTerminator` 按顺序派生阶段种子，依次调用 `StartPhase(exploration.timeLimit)` 与 `Explore`，再调用 `StartPhase(compression.timeLimit)` 与 `Compress`，最后通过 `SPProblem::Restore` 与 `Layout::Restore` 校验，失败时返回 `ErrorCode::Infeasible`。

C ABI 的返回码定义如下：`SparrowOptimizeJson` 返回 0 表示成功，1 表示输入为空指针或大小为 0，2 表示优化或序列化失败，3 表示输出缓冲区不足（`CopyOutput` 在 `outputCapacity <= value.size()` 时返回 3）。`docs/Deployment.md` 把"格式错误的 JSON"写为返回 1，而代码中 JSON 解析失败会落入 2 分支，二者不一致。当前实现也没有 try/catch，异常会直接穿过 C ABI 边界。

## 三、测试体系的四层结构

测试入口是 `tests/TestSupport.cpp` 的 `main`（已核对）。若 `argc > 1`，它调用 `RunWindowsConsoleCtrlCProbe`；否则先检查 `sparrow::Version()` 与 `jaguar::Version()` 非空，然后按顺序运行 `RunRngTests`、`RunSamplingTests`、`RunLbfLossTests`、`RunSppLbfTests`、`RunBppLbfTests`、`RunQuantifyTests`、`RunSeparatorTests`、`RunExplorationTests`、`RunWasmTests`，任一失败即返回 1。工程文件 `tests/SparrowCoreTests.pro` 编译全部 `Test*.cpp`，并把 `Tools/SparrowCli/OptimizationSignalHandler.cpp` 与 `Tools/SparrowWasm/SparrowWasm.cpp` 一并编入，因此 WASM 的 C ABI 也能在原生环境直接测试。Debug 链接 `-lSparrowCore -lJaguarCored`，Release 链接 `-lJaguarCore`。

第一层是单元测试，覆盖 RNG（`TestRng.cpp`，7 个）、采样（`TestSampling.cpp`，21 个，使用 `FloatBits` 做逐位比较）、LBF 损失（`TestLbfLoss.cpp`，8 个）、SPP 与 BPP 的 LBF 构造（`TestSppLbf.cpp` 12 个，`TestBppLbf.cpp` 5 个）、量化（`TestQuantify.cpp`，31 个）。第二层是行为不变式测试，主要是 `TestSeparator.cpp`（8 个）与 `TestExploration.cpp`（66 个，约 10668 行）。第三层是 Rust 差分（oracle）测试，核心位于 `tests/RustReference/`。它是一个 Cargo 包 `sparrow-rust-reference`（edition 2024，rust-version 1.90），依赖 `jagua-rs 0.9.0`（git 来源，分支 `jg-explicit-rotations-v0.9`，feature `spp`）与 `rand 0.10` 等，并通过 `sparrow = { path = "../../../CodeResource/sparrow", features = ["oracle-trace"] }` 直接引用 Rust 参考实现。`rust-toolchain.toml` 锁定 1.98.1，目标为 `x86_64-pc-windows-msvc`。`tests/*.rs` 共 56 个文件、145 个 `#[test]`。`pair_matrix_reference.rs` 在 inline `mod quantify` 中使用 `#[path = "../../../../../CodeResource/sparrow/src/quantify/pair_matrix.rs"]`，层数正确；`best_samples_*` 在顶层使用 `#[path = "../../../../CodeResource/sparrow/src/..."]`，同样正确。`exploration_pool_sampling_trace_reference.rs` 使用 `include_str!("../../Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv")`。`src/main.rs` 接收多个路径参数，把 Rust 侧轨迹写成 TSV，默认回退路径为 `../Fixtures/...`。

第四层是固定种子回归，即 C++ 侧的 `*CppReference.tsv` 与 `PhaseReference.tsv` 基线，由 `tests/fixtures/` 中约 142 个文件提供，包括 `LbfSppSmall.json`、`LbfBppSmall.json`、`RustSppSingleItem.json`、`SeparatorRoundRustTrace.json`，以及大量 `*RustTrace.tsv` 与 `*RustReference.tsv`。文档中引用的 fixture 全部存在，且没有孤立 fixture。

更新 fixture 的方法是：在 Rust 侧修改后，通过 `RustReference` 的 `main.rs` 重新生成 TSV，人工核对差异后替换 `tests/fixtures/` 中对应文件，并重新运行 `SparrowCoreTests.exe`。C++ 测试代码中没有自动写基线文件的逻辑（grep 未发现 ofstream 写基线），因此基线更新完全依赖人工操作。Rust 侧的脚本是 `tests/RustReference/verify-rust-reference.ps1`，它检查 `rustc -vV` 的 release 为 1.98.1、commit 为 `48a229ceaefd4985c50990b14116b6d856af0985`，然后依次执行 `cargo fmt --check`、`cargo test --locked --target x86_64-pc-windows-msvc` 与 `cargo test --locked --release`。该脚本不生成轨迹，也不运行 C++ 侧的比对。

Windows Ctrl-C 探针由 `tests/TestWindowsConsoleCtrlC.cpp`（仅 `_WIN32`）与 `tests/run-windows-console-ctrlc-probe.ps1` 组成。脚本创建命名事件，以 `--windows-console-ctrlc-probe <ready> <release> <observed>` 启动子进程，父进程调用 `SetConsoleCtrlHandler(NULL, true)`，发送 `GenerateConsoleCtrlEvent(0,0)`，要求子进程退出码为 0 并输出 `WINDOWS_CTRL_C_PROBE_PASS`，默认超时 15 秒。

## 四、许可证边界与覆盖声明

`docs/SourceMap.md` 给出许可证表：jagua-rs 为 MPL-2.0，sparrow 为 MIT，JaguarCore 为本地代码。映射表把 `jagua-rs/lbf` 与 `sparrow` 的源文件对应到 `Source/` 下的文件，并声明没有移植 NestLab 的适配层。但该文档中的参考路径全部指向 `D:\MyFiles\Gits_Xj\NestLab\NestLab\...`，这是本机绝对路径，换机器或换目录后不可用，也与当前 `JaguarRun` 布局不符。

`docs/Differential.md` 包含约 70 行的 fixture 矩阵，并定义了验收容差：采样与 CDE 的变换 1e-6、损失 1e-5；LBF 宽度 4e-3、密度 1e-3；Exploration 1e-5；限制为 6 的 Explore 位姿 2e-5、包围盒 2e-4、损失 1e-3。文档还记录了 2026-10-08 与 2026-10-09 两次重复性运行，每个 profile 跑 5 个进程，并给出 SHA-256 黄金值。重跑方法为在 `build\build_Kit_MSVC_2019_x64-*` 目录下执行 `nmake /A /f Makefile.Debug` 与 `Makefile.Release`，并执行 `cargo test --locked --manifest-path SparrowCore/Tests/RustReference/Cargo.toml`。

这里必须区分两类证据。逐位（bitwise）证据只出现在采样、RNG 以及部分 CDE 的 `FloatBits` 比较中，以及 SHA-256 重复性记录中；Exploration、Separator、Compression 等大部分轨迹比对是容差比较，属于固定种子回归，而非逐位一致。`docs/Compatibility.md` 明确说明不声称完全对等或输出逐位一致，但 `Differential.md` 中出现的 "bit-exact" 字样需要读者仔细区分语境。另外，Differential 矩阵未覆盖约 40 个 Rust 参考文件，这些文件是否被测试，只能从 `tests/RustReference/tests` 目录本身得知。

## 五、发布门禁与文档一致性问题

`docs/ReleaseChecklist.md` 规定的门禁包括：使用 MSVC 与 Qt 的 Release 构建；运行 `SparrowCoreTests.exe`；连续两次运行 `SparrowCoreBench all` 并要求 checksum 相同；CLI 冒烟测试，包括 `--time-ms` 与 `--svg`；使用 Emscripten 构建 WASM 并测试缓冲区不足的情况；启动 Demo 页面；检查 `Include/SparrowCore` 路径；以及 `jaguar::ValidateSPSolution` 与 `Layout::IsFeasible` 校验。这里有几点问题。第一，Bench 的 checksum 依赖结构体布局与平台，同一台机器两次运行相同不代表跨平台可复现。第二，Windows Ctrl-C 探针没有列入门禁。第三，`verify-rust-reference.ps1` 不运行 C++ 比对，因此 Rust 差分通过并不意味着 C++ 侧一致。

工程中还存在若干需要处理的问题。其一是大小写不一致：`.pro` 与源码使用 `Include/`、`Tests/`、`Optimizer/`，磁盘实际为 `include/`、`tests/`、`Source/Optimizer/`，在区分大小写的系统上会直接失败。其二是硬编码路径：`SourceMap.md` 中的 `D:\MyFiles\Gits_Xj\NestLab\...`，`Deployment.md` 中的 `D:/build/...` 示例，以及 `TestWasm.cpp` 中的 `..\\SparrowCore\\Tests\\Fixtures\\LbfSppSmall.json` 等路径都依赖特定目录布局。其三是根目录的 `Makefile.Debug` 与 `Makefile.Release` 属于 JaguarCore 测试，硬编码了 `C:\openssl`、`D:\MyWorks\ProQt` 等路径，与 SparrowCore 构建无关，容易被误用。其四是文档与代码不一致：Deployment 中的错误码 1 与实际 JSON 解析失败的返回码 2 不符；Demo 的 `App.js`（第 38 至 47 行）直接依赖 `window.SparrowWasm.optimizeJson`，并先后调用 `optimizeJson` 与 `optimizeSvg`，即优化两次，且未检查 `{"error":...}` 形式的错误负载；仓库中也没有生成该桥接对象的代码。其五是异常边界：CLI 的 `Main.cpp` 与 WASM 的 C ABI 均无 try/catch（已 grep 确认），任何 `std::logic_error`（例如 `MakeWarmStart` 中的 `HazardEntity not found in collector`）都会直接终止进程或穿越 C 边界。

综上，SparrowCore 的结构是清晰的：一个静态库、四类外围产物，以及由单元、不变式、Rust 差分和固定种子回归组成的四层测试。需要优先修复的是大小写路径、文档中的错误码与硬编码路径，并把 Ctrl-C 探针与 C++ 侧比对纳入发布门禁。
