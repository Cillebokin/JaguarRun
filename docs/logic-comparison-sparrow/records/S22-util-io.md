# S22. util/io.rs + util/svg_exporter.rs + util/assertions.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/io.rs`（149 行）+ `util/svg_exporter.rs`（65 行）+ `util/assertions.rs`（132 行）
**C++**: 无集中对应模块——IO/JSON/SVG 全部委托 JaguarCore 公共 API（`JaguarCore/IO/Importer.h`、`JsonCodec.h`、`SvgExport.h`，JaguarCore 记录 33-36 已覆盖其内部语义），SparrowCore 侧接线分布在 `Tools/SparrowCli/Main.cpp`、`Tools/SparrowWasm/SparrowWasm.cpp`、`bench/BenchMain.cpp`；断言侧委托 `JaguarCore/Assertions.h`（JaguarCore 记录 39）
**判定**: 基本等价（委托 JaguarCore；实例导入/解导出/SVG 接线一致；三处差异：CLI 双格式输入回退解析缺失、SvgExporter 三通道监听器形态缺失、tracker_matches_layout 断言集缺失）

## 逐条比对（io.rs）

### `MainCli` 结构 + `apply_config_overrides`（Rust 15-58）
- 归属：CLI 选项语义，见 S23（本记录只登记存在性）。
- 判定：见 S23。

### `ExtSPOutput { #[serde(flatten)] instance, solution }`（Rust 60-65）
- C++ 对应：`jaguar::ExternalSPSolutionFromSolution` + `jaguar::SerializeExternalSPSolutionJson(external, {pretty})`（Main.cpp:167-176；SparrowWasm.cpp:116-123）。
- 判定：差异（输出形态）。
- 证据与说明：Rust 输出"实例+解"扁平合并的单 JSON（serde flatten）；C++ CLI/WASM 导出的是**仅解**（ExternalSPSolution 序列化，`{true}`=pretty/`{false}`=紧凑，Main.cpp:176 vs SparrowWasm.cpp:123）——实例部分不回写。语义=可再次输入的 warm-start 文件：Rust 的输出文件可原样再喂给 `-i`；C++ 的输出能否再输入取决于 JaguarCore `DeserializeExternalSPSolutionJson` 是否被调用方接线（SparrowCore 现有两个入口都只 `ImportSPInstanceJson`，不回读解）。影响：C++ 侧 warm-start 闭环（读回自己导出的解）未在 CLI 接线，`config.warmStart` 只能由 API 调用方以 `jaguar::SPSolution` 对象传入（SparrowOptimizer.cpp:116-118 的 `MakeWarmStart` 路径）。

### `init_logger(level_filter, log_file_path)`（Rust 67-103：fern，双通道 stdout+文件，[LEVEL][H:M:S]<thread> 前缀）
- C++ 对应：无（SparrowCore 库无日志框架）。
- 判定：C++ 缺失（应用层，归 CLI/Demo/NestLab）。
- 证据与说明：Main.cpp/BenchMain.cpp 均用 `std::cout/std::cerr` 直写，无分级/落盘日志；EPOCH 时间前缀与线程名格式化属 Rust 应用设施（与 S02 差异 4 呼应）。

### `write_svg(document, path, log_lvl)`（Rust 106-120：建父目录→svg::save→日志）
- C++ 对应：CLI `ExportLayoutSvg` + `std::ofstream` 写盘（Main.cpp:196-229）。
- 判定：一致（接线等价，细节差异见 DRAW_OPTIONS——S02 差异 3）。
- 证据与说明：两侧均为"序列化 SVG→写文件"；C++ 不自动建父目录（Rust `create_dir_all`，io.rs:108-110）——CLI 调用方需保证目录存在（`--svg` 路径无父目录创建逻辑，Main.cpp:217-228）。SVG 内容生成在 JaguarCore 记录 36。

### `write_json(json, path, log_lvl)`（Rust 122-133：pretty JSON 落盘）
- C++ 对应：`SerializeExternalSPSolutionJson(external, {true})` + ofstream（Main.cpp:175-194）。
- 判定：一致。
- 证据与说明：`{true}`=pretty 对应 `serde_json::to_writer_pretty`；序列化字段语义在 JaguarCore 记录 33（ext-repr）。

### `read_spp_input(path) -> (ExtSPInstance, Option<ExtSPSolution>)`（Rust 135-149）
- C++ 对应：`jaguar::Importer::ImportSPInstanceJson`，CLI/WASM/Bench 分别使用 `sparrow::MakeRustDefaultImporterConfig()` 构造 importer（Main.cpp:133-134、SparrowWasm.cpp:63-65、BenchMain.cpp:204-205；配置 helper 在 `SparrowConfig.h:80-96`）。该 helper 显式映射 Rust `DEFAULT_SPARROW_CONFIG` 的 CDE 和形状修改默认参数。
- 判定：差异（双格式回退解析缺失）。
- 证据与说明：Rust 先试"instance+solution 完整输出"（`ExtSPOutput`），失败再退回"仅 instance"——这让 `-i` 既可接新实例也可接 warm-start 解文件（cli.rs:76-83 据此把 `ext_solution` 传入 `optimize`）。C++ 三个入口全部只调 `ImportSPInstanceJson`，无"先试解文件"的回退：喂给 CLI 一个含解的 JSON，其 `solution` 字段不用于 warm start。实例几何导入的默认 CDE/形状修改参数已由 `MakeRustDefaultImporterConfig()` 映射并接入这三个入口；这不补足解文件回退，也不让 `SparrowConfig` 承载自定义 importer 参数。影响：warm-start 的 CLI 入口缺失，与上一条共同构成 C++ 侧"解回读"闭环缺口；库级 `config.warmStart` API 可接收解对象，`MakeWarmStart` 使用 Restore 处理结构并将剩余需求/解验证/碰撞可行性写入 metrics，不把这些质量条件作为 warm-start 拒绝条件；三条已测 Restore 边界见 S06。该库级入口仍不能补上 CLI 对导出解文件的回读。

## 逐条比对（svg_exporter.rs）

### `SvgExporter::new`（Rust 19-37：三路径构造 + 清空中间目录旧 svg）
- C++ 对应：无。
- 判定：C++ 缺失（监听器形态的导出器）。
- 证据与说明：Rust 把 SVG 导出做成 `SolutionListener` 实现，`report()` 按 `ReportType` 分流三通道：live（每报都写 `.live_solution.svg` 覆盖，供 live_viewer.html 轮询）、intermediate（除 ExplImproving 外逐个写 `{counter}_{width:.3}_{suffix}.svg`）、final（仅 Final 报告时写）。C++ 侧解事件走 `OptimizationListener`（S21），但 CLI 只在**结束时**导出一次最终 SVG（Main.cpp:196-229）；中间解/live 导出无实现——库不落盘，宿主（NestLab GUI/Demo）可自行实现 `OnSolution` 监听获取中间解。影响：live/intermediate 可视化在 C++ 生态归 NestLab/Demo（S24），当前 Demo 只展示最终布局。

### `SvgExporter::report` 的文件命名 `{counter}_{width:.3}_{suffix}`（Rust 48）
- 判定：C++ 缺失（随上一条）。
- 证据与说明：C++ CLI 的 SVG 文件名由 `--svg` 参数一次性指定（Main.cpp:23）。

## 逐条比对（assertions.rs）

### `tracker_matches_layout(ct, l) -> bool`（Rust 12-132）
- C++ 对应：无逐项对应。
- 判定：C++ 缺失（debug 断言集）。
- 证据与说明：该函数是 CollisionTracker 与 Layout CDE 的交叉验证（pk_idx_map 覆盖性、quadtree 一致性、pair loss 与 CDE 检测结果双向对照、container loss 逐项、含非对称碰撞的宽限分支 27-73/104-117），Rust 在 tracker/worker/separator 的 7 处 `debug_assert!` 调用（tracker.rs:42/94/108、worker.rs:69/88、separator.rs:267/294）——仅 debug 构建生效。C++ 侧：(a) `CollisionTracker`（S18）有自己的快照/恢复一致性机制，但无"与 CDE 双向对照"的调试自检；(b) Optimize 返回前的 `SPProblem::Restore` 与 `Layout::Restore` 只复核最终快照结构（SparrowOptimizer.cpp:188-210），并非 `ValidateSPSolution` + `Layout::IsFeasible` 终态重验，也不替代 tracker/CDE 双向断言。影响：低——Rust 断言是开发期回归哨兵，Rust release 构建同样不执行；C++ 当前未复刻该断言集。S18 中的 Rust trace 仅对有限输入下的 tracker 字段按容差提供差分证据，不能作为通用数值一致性证明。

### 其余（无）
- Rust util/assertions.rs 全文即这一个函数（132 行），无其他条目。

### JaguarCore 侧断言集的归属
- 判定：不适用（分层）。
- 证据与说明：Rust 侧另有 jagua-rs 的 `util/assertions.rs`/quadtree/probs 断言，对应 JaguarCore `Assertions.cpp`（JaguarCore 记录 21/28/39 已逐条覆盖）；SparrowCore 只消费 `ValidateSPSolution`（SparrowOptimizer.cpp:57/187）。本记录不重复其内容。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `read_spp_input` 双格式回退（instance+solution → instance） | C++缺失 | CLI/WASM/Bench 均只解析实例；warm-start 解文件不能直接作 `-i` 输入（库级 warmStart API 存在且校验更严） | io.rs:135-149 vs Main.cpp:133-134/SparrowWasm.cpp:63-65 |
| 2 | `ExtSPOutput`（实例+解扁平输出） | 差异 | C++ 只导出解（`SerializeExternalSPSolutionJson`），实例不回写；与差异 1 合成回读闭环缺口 | io.rs:60-65 vs Main.cpp:167-176 |
| 3 | `init_logger`（fern 双通道+时间前缀） | C++缺失 | 应用层日志设施归 CLI/Demo/NestLab；C++ 入口仅 cout/cerr | io.rs:67-103 vs Main.cpp（无） |
| 4 | `SvgExporter` 监听器（live/intermediate/final 三通道） | C++缺失 | C++ 仅 CLI 一次性最终导出；中间解可视化归宿主监听（NestLab/Demo，S24） | svg_exporter.rs:39-65 vs Main.cpp:196-229 |
| 5 | `write_svg` 自动建父目录 | C++缺失 | C++ CLI 假定输出目录存在 | io.rs:108-110 vs Main.cpp:217-228 |
| 6 | `tracker_matches_layout` 交叉断言（7 处 debug_assert） | C++缺失 | C++ 未复刻该开发期哨兵；Optimize 末尾的 `SPProblem::Restore`/`Layout::Restore` 只验证快照结构可恢复，不比较 Tracker 与 CDE，也不检查碰撞可行性，不能作为替代，影响低 | assertions.rs:12-132 vs SparrowOptimizer.cpp:188-210 |

## 结论
util/io 与 svg 导出的**序列化语义**（JSON pretty 字段、SVG 内容）已在 JaguarCore 侧的记录 33/36 核对；SparrowCore 侧接线（ImportSPInstanceJson→Optimize→SerializeExternalSPSolutionJson→ExportLayoutSvg）与 Rust 主干流程可对应，判定基本等价。实质缺口有三：warm-start 的文件级闭环（双格式输入回退 + 实例回写均缺失；库级 `config.warmStart` API 可接收解对象，输入契约与 Restore 边界见 S06）、监听器形态的 live/intermediate SVG 导出（归宿主 UI，S24）、以及 debug 断言集 tracker_matches_layout（C++ 当前未复刻，终态结构恢复不构成等价替代，影响低）。

### 补充审计（2026-09-29）
1. **【核对确认：io.rs 149 行覆盖完整性】** MainCli 7 字段+apply_config_overrides（归 S23）、ExtSPOutput、init_logger、write_svg、write_json、read_spp_input——主文均有条目；`init_logger` 的 fern 格式串（`[LEVEL][HH:MM:SS]<thread>` + 25 列左对齐前缀）与双通道（stdout+文件）细节、`log!("[EPOCH]: jiff 时间戳")` 首行均为应用层设施，C++ 缺失判定维持。
2. **【遗漏补记：svg_exporter.rs 的三个 if let 链与计数器语义】** 主文记了三通道但未记细节：live 路径**对每个 report 都写**（含 ExplImproving，覆盖同名文件）、intermediate 路径 `&& report_type != ExplImproving` 排除改进解且 `svg_counter += 1` 仅在此分支递增（**live 写入不计数**）、final 路径 `&& report_type == Final` 用文件名 stem 而非计数器（svg_exporter.rs:45-64）。C++ 无对应物判定不变；此细节防止后续把"live 计数"误解进中间文件命名序。
3. **【浅覆盖补全：assertions.rs 132 行的结构】** 主文已概括 tracker_matches_layout 全函数；补记内部结构以便定位：a) 前置两断言（pk_idx_map 覆盖 + layout_qt_matches_fresh_qt，:14-15）；b) 双层循环逐 pair 的三分支（有碰撞→loss 近似双序校验+反查 opp_collector（:25-73，含 10% 容差的双向 approx_eq 与"非对称碰撞"宽限）、无碰撞但 stored≠0→反查告警（:104-117））；c) container 逐项（ Exterior 命中→assert_approx_eq ulps=5，否则 assert==0，:118-131）。C++ 判定为缺失；Optimize 末尾只做 snapshot restore/结构检查，不复刻该交叉断言。
4. **【核对确认：write_svg 的 canonicalize 日志】** Rust `fs::canonicalize(path).expect(...)` 在路径异常时 panic（io.rs:114-118）——属观测层日志的既定 panic 差异，主文未单独列出，影响可忽略（写盘成功后才会走到）。
