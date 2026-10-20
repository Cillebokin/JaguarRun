# S22. util/io.rs + util/svg_exporter.rs + util/assertions.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/util/io.rs`（149 行）+ `util/svg_exporter.rs`（65 行）+ `util/assertions.rs`（132 行）
**C++**: 无集中对应模块——IO/JSON/SVG 全部委托 JaguarCore 公共 API（`JaguarCore/IO/Importer.h`、`JsonCodec.h`、`SvgExport.h`，JaguarCore 记录 33-36 已覆盖其内部语义），SparrowCore 侧接线分布在 `Tools/SparrowCli/Main.cpp`、`Tools/SparrowWasm/SparrowWasm.cpp`、`bench/BenchMain.cpp`；断言侧委托 `JaguarCore/Assertions.h`（JaguarCore 记录 39）
**判定**: 部分（Rust兼容SPP文件闭环与SVG父目录创建已通过Release CLI运行验收；Rust应用层logger和live/intermediate SVG仍需按宿主范围裁定；SVG绘制器的结构差异见JaguarCore记录36）

## 逐条比对（io.rs）

### `MainCli` 结构 + `apply_config_overrides`（Rust 15-58）
- 归属：CLI 选项语义，见 S23（本记录只登记存在性）。
- 判定：见 S23。

### `ExtSPOutput { #[serde(flatten)] instance, solution }`（Rust 60-65）
- C++ 对应：`jaguar::ExternalSPOutput` + `SerializeExternalSPOutputJson`（JsonCodec.h/.cpp）；CLI将导入实例与优化解合并序列化。WASM `SparrowOptimizeJson` 保留独立的solution-only C ABI格式。
- 判定：CLI输出字段结构及warm-start文件回读已通过Release运行验收（2026-10-19）。
- 证据与说明：Rust输出根对象包含扁平实例字段和嵌套`solution`字段。C++新增同形DTO及Codec parse/serialize API，CLI输出`name/min_item_separation/items/strip_height/solution`，solution内含`strip_width/layout/density/run_time_sec`；运行时间按CLI启动后的整秒写入。以单件SPP fixture执行cold-start导出，再将输出作为warm-start输入；第二次运行成功，实例字段逐值稳定，除运行时间外的solution字段逐值稳定，两次`run_time_sec`均为0。JSON字段语义由JaguarCore记录33的ExternalRepr/JsonCodec映射承接。

### `init_logger(level_filter, log_file_path)`（Rust 67-103：fern，双通道 stdout+文件，[LEVEL][H:M:S]<thread> 前缀）
- C++ 对应：无（SparrowCore 库无日志框架）。
- 判定：C++ 缺失（应用层，归 CLI/Demo/NestLab）。
- 证据与说明：Main.cpp/BenchMain.cpp 均用 `std::cout/std::cerr` 直写，无分级/落盘日志；EPOCH 时间前缀与线程名格式化属 Rust 应用设施（与 S02 差异 4 呼应）。

### `write_svg(document, path, log_lvl)`（Rust 106-120：建父目录→svg::save→日志）
- C++ 对应：CLI `ExportLayoutSvg` + `std::ofstream` 写盘（Main.cpp:222-255）。
- 判定：父目录行为已对齐；SVG绘制内容不逐属性等价。
- 证据与说明：CLI在写`--svg`前用`std::filesystem::create_directories`创建父目录，对应Rust `write_svg`的`create_dir_all`。SVG文档结构、viewBox、坐标翻转和图层差异见JaguarCore记录36；Sparrow入口的有效绘制选项另见本记录的DRAW_OPTIONS条目。

### `write_json(json, path, log_lvl)`（Rust 122-133：pretty JSON 落盘）
- C++ 对应：`SerializeExternalSPOutputJson(..., {true})` + ofstream（Main.cpp）。
- 判定：字段结构已对齐；写JSON函数本身不创建父目录。
- 证据与说明：`{true}`=pretty对应`serde_json::to_writer_pretty`；CLI输出现在同时序列化输入实例和优化解。Rust `write_json`本身也不建父目录；Rust CLI预先创建固定`output/`目录，C++则由调用方传入`--output`路径。字段定义见JaguarCore记录33。

### `read_spp_input(path) -> (ExtSPInstance, Option<ExtSPSolution>)`（Rust 135-149）
- C++ 对应：CLI先尝试`ParseExternalSPOutputJson`，成功后通过`Importer::ImportSPInstance`与`Importer::ImportSPSolution`恢复实例和warm-start；否则回退`ParseExternalSPInstanceJson`。Importer仍使用`MakeRustDefaultImporterConfig()`。
- 判定：完整输出优先解析、warm-start导入与instance-only回退已通过Release CLI运行验收（2026-10-19）。
- 证据与说明：结构对应Rust的“先解析完整输出、失败则仅解析实例”回退。解析成功的解写入`SparrowConfig::warmStart`，随后通过已有`MakeWarmStart`路径进入Optimize。若完整输出格式通过JSON解析但solution无法导入，CLI返回warm-start导入错误；instance-only JSON仍不会尝试解导入。WASM与Bench入口保持instance-only职责。warm-start Restore对剩余需求、解结构及碰撞的既有边界见S06。

### Sparrow入口的 `DRAW_OPTIONS`（Rust `consts.rs:41-49`）
- C++对应：CLI与WASM的`SvgExportOptions`。
- 判定：有效共享选项已对齐；SVG导出器仍缺Rust的quadtree和可配置CD shape开关，完整绘制结果不等价。
- 证据与说明：两入口使用`SvgLayoutTheme::Gray()`（Rust Gray的线宽倍数、容器/物品填充、质量区颜色与透明度、碰撞高亮色），`drawSurrogates=false`、`highlightCollisions=true`、`includeAxes=false`。C++当前固定绘制原始形与CD轮廓，效果对应`draw_cd_shapes=false`、`highlight_cd_shapes=true`；quadtree默认关闭且C++没有对应图层。C++导出整体仍有视口、坐标方向、标签和结构差异，详见JaguarCore记录36。

## 逐条比对（svg_exporter.rs）

### `SvgExporter::new`（Rust 19-37：三路径构造 + 清空中间目录旧 svg）
- C++ 对应：无。
- 判定：C++ 缺失（监听器形态的导出器）。
- 证据与说明：Rust 把 SVG 导出做成 `SolutionListener` 实现，`report()` 按 `ReportType` 分流三通道：live（每报都写 `.live_solution.svg` 覆盖，供 live_viewer.html 轮询）、intermediate（除 ExplImproving 外逐个写 `{counter}_{width:.3}_{suffix}.svg`）、final（仅 Final 报告时写）。C++ 侧解事件走 `OptimizationListener`（S21），但 CLI 只在**结束时**导出一次最终 SVG（Main.cpp:222-255）；中间解/live 导出无实现——库不落盘，宿主（NestLab GUI/Demo）可自行实现 `OnSolution` 监听获取中间解。影响：live/intermediate 可视化在 C++ 生态归 NestLab/Demo（S24），当前 Demo 只展示最终布局。

### `SvgExporter::report` 的文件命名 `{counter}_{width:.3}_{suffix}`（Rust 48）
- 判定：C++ 缺失（随上一条）。
- 证据与说明：C++ CLI 的 SVG 文件名由 `--svg` 参数一次性指定（Main.cpp:27）。

## 逐条比对（assertions.rs）

### `tracker_matches_layout(ct, l) -> bool`（Rust 12-132）
- C++ 对应：Debug-only `CollisionTracker::DebugAssertMatchesLayout`，在 Tracker 重建、移动更新、保留权重恢复以及 worker/separator 的 move 前检查。
- 判定：检查主体已复刻；worker/separator 的 move 前与更新后边界均已接入检查。
- 证据与说明：该函数是 CollisionTracker 与 Layout CDE 的交叉验证（pk_idx_map 覆盖性、quadtree 一致性、pair loss 与 CDE 检测结果双向对照、container loss 逐项、含非对称碰撞的宽限分支 27-73/104-117），Rust 在 tracker/worker/separator 的 7 处 `debug_assert!` 调用（tracker.rs:51/106/120、worker.rs:83/128、separator.rs:267/294）——仅 debug 构建生效。C++ 侧：`CollisionTracker::DebugAssertMatchesLayout` 仅在 Debug 编译，Tracker 重建、移动更新和保留权重恢复后调用；worker round 在恢复快照后及每次 move 前调用，`Separator::MoveItem` 也在 move 前调用。检查覆盖布局重建一致性、handle 覆盖、对角线、pair loss 双向 CDE 与 10% 宽限分支，以及 container loss（5 ULP）。U 形样本已证明单向命中时只告警并保留 loss。Optimize 返回前的 `SPProblem::Restore` 与 `Layout::Restore` 只复核最终快照结构（SparrowOptimizer.cpp:188-210），并非 `ValidateSPSolution` + `Layout::IsFeasible` 终态重验，也不替代 tracker/CDE 双向断言。影响：低——Rust 断言是开发期回归哨兵，Rust release 构建同样不执行；C++ 已实现 Debug-only Tracker/Layout CDE 对照；release 不执行，与 Rust 一致。S18 的近等 bbox U 形 fixture 已在 Rust Debug 实际进入 `inconsistent loss` 非对称告警分支，并由 C++ Debug/Release 核对 CDE 双向结果与 Tracker pair-loss bits；该 fixture 现同时验证 C++ 告警分支、发现方向和 Tracker 状态；它不构成任意几何的一致性证明。S18 中的 Rust/C++ 对照锁定了一个方向性 fixture 的 tracker pair bits，不能作为通用数值一致性证明。

### 其余（无）
- Rust util/assertions.rs 全文即这一个函数（132 行），无其他条目。

### JaguarCore 侧断言集的归属
- 判定：不适用（分层）。
- 证据与说明：Rust 侧另有 jagua-rs 的 `util/assertions.rs`/quadtree/probs 断言，对应 JaguarCore `Assertions.cpp`（JaguarCore 记录 21/28/39 已逐条覆盖）；SparrowCore 只消费 `ValidateSPSolution`（SparrowOptimizer.cpp:57/187）。本记录不重复其内容。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `read_spp_input` 双格式回退（instance+solution → instance） | CLI cold-start/warm-start文件回读已通过Release运行验收 | CLI可将导出的实例+解文件作为warm-start输入；WASM与Bench仍为instance-only入口 | io.rs:135-149 vs SparrowCli/Main.cpp；单件SPP smoke 2026-10-19 |
| 2 | `ExtSPOutput`（实例字段扁平+嵌套solution） | CLI字段与round-trip已通过Release运行验收 | C++ CLI输出Rust同形字段；WASM C ABI保持solution-only扩展格式 | io.rs:60-65 vs JsonCodec.h/.cpp、SparrowCli/Main.cpp；字段稳定性核对 2026-10-19 |
| 3 | `init_logger`（fern 双通道+时间前缀） | C++缺失 | 应用层日志设施归 CLI/Demo/NestLab；C++ 入口仅 cout/cerr | io.rs:67-103 vs Main.cpp（无） |
| 4 | `SvgExporter` 监听器（live/intermediate/final 三通道） | C++缺失 | C++ 仅 CLI 一次性最终导出；中间解可视化归宿主监听（NestLab/Demo，S24） | svg_exporter.rs:39-65 vs Main.cpp |
| 5 | `write_svg` 自动建父目录 | SparrowCli已对齐 | CLI创建`--svg`父目录；WASM返回内存字符串，无磁盘路径 | io.rs:108-110 vs SparrowCli/Main.cpp |
| 6 | `tracker_matches_layout` 交叉断言（7 处 debug_assert） | C++ Debug 已实现 | `DebugAssertMatchesLayout` 在 Tracker 重建、移动更新及保留权重恢复后核对 mapping、pair/container loss 和双向 CDE；U 形单向路径有 Debug 告警测试。worker/separator 的 move 前和 Tracker 更新后均运行该检查；Optimize 末尾 Restore 仍不等同于该检查，影响低 | assertions.rs:12-132 vs CollisionTracker.cpp:933-1087, Separator.cpp:173-262,419-437; TestQuantify.cpp:1869-2064 |

## 结论
已补齐Rust同形的SPP JSON输出DTO与Codec、CLI完整输出回读为warm-start、SVG选项预设和SVG目标父目录创建。2026-10-19以VS2019 MSVC 14.29 + Qt6.5.3 msvc2019_64 qmake Release构建JaguarCore、SparrowCore和SparrowCli成功；单件SPP fixture的cold-start输出、warm-start再读、实例字段及solution稳定性、整秒运行时间、SVG嵌套父目录创建均通过。S22当时的CLI要求显式`--output`，缺失该参数、缺失输入、非法实例和JSON写入失败分别观察到退出码2、3、4、8；S23后`--output`可省略并使用`output/final_<name>.json`默认路径。S22仍标记部分：Rust应用层日志未实现；live/intermediate SVG报告仍由宿主UI工作流承接（S24）；JaguarCore的SVG结果结构和几何展示仍是有意重写，不宣称逐属性相同，详见记录36。`tracker_matches_layout`的Debug检查主体及worker/separator move前、更新后调用边界已接入。

### 补充审计（2026-09-29）
1. **【核对确认：io.rs 149 行覆盖完整性】** MainCli 7 字段+apply_config_overrides（归 S23）、ExtSPOutput、init_logger、write_svg、write_json、read_spp_input——主文均有条目；`init_logger` 的 fern 格式串（`[LEVEL][HH:MM:SS]<thread>` + 25 列左对齐前缀）与双通道（stdout+文件）细节、`log!("[EPOCH]: jiff 时间戳")` 首行均为应用层设施，C++ 缺失判定维持。
2. **【遗漏补记：svg_exporter.rs 的三个 if let 链与计数器语义】** 主文记了三通道但未记细节：live 路径**对每个 report 都写**（含 ExplImproving，覆盖同名文件）、intermediate 路径 `&& report_type != ExplImproving` 排除改进解且 `svg_counter += 1` 仅在此分支递增（**live 写入不计数**）、final 路径 `&& report_type == Final` 用文件名 stem 而非计数器（svg_exporter.rs:45-64）。C++ 无对应物判定不变；此细节防止后续把"live 计数"误解进中间文件命名序。
3. **【浅覆盖补全：assertions.rs 132 行的结构】** 主文已概括 tracker_matches_layout 全函数；补记内部结构以便定位：a) 前置两断言（pk_idx_map 覆盖 + layout_qt_matches_fresh_qt，:14-15）；b) 双层循环逐 pair 的三分支（有碰撞→loss 近似双序校验+反查 opp_collector（:25-73，含 10% 容差的双向 approx_eq 与"非对称碰撞"宽限）、无碰撞但 stored≠0→反查告警（:104-117））；c) container 逐项（ Exterior 命中→assert_approx_eq ulps=5，否则 assert==0，:118-131）。S18 的近等 bbox U 形 fixture 现已实际触发“无碰撞但 stored≠0、反向 CDE 命中”的告警分支；C++ Debug 的 `DebugAssertMatchesLayout` 同样输出该诊断，测试核对正向 0 个 hazard、反向 1 个 hazard 和 pair loss bits `0x409a27bb`，Release 不输出该调试诊断。Optimize 末尾的 snapshot restore/结构检查仍不能替代 Tracker/CDE 对照。
4. **【核对确认：write_svg 的 canonicalize 日志】** Rust `fs::canonicalize(path).expect(...)` 在路径异常时 panic（io.rs:114-118）——属观测层日志的既定 panic 差异，主文未单独列出，影响可忽略（写盘成功后才会走到）。

### 实施记录（2026-10-19）
1. 新增`ExternalSPOutput`及`ParseExternalSPOutputJson`/`SerializeExternalSPOutputJson`，按Rust serde flatten 输出形态读写扁平实例字段和嵌套`solution`。
2. SparrowCli现在先试完整输出、失败回退实例；完整输出的solution经`Importer::ImportSPSolution`装入`SparrowConfig::warmStart`。输出包含原实例、优化解及整秒运行时间。
3. SparrowCli的SVG写入前创建父目录；CLI与WASM的Sparrow SVG入口使用Gray主题、关闭surrogate与额外坐标轴，并保留碰撞高亮。SVG整体几何、路径和图层差异仍按JaguarCore记录36界定。
4. 四个变更源文件（JsonCodec.cpp、SvgExport.cpp、SparrowCli/Main.cpp、SparrowWasm.cpp）通过llvm-mingw g++ C++17 syntax-only；另以VS2019 MSVC 14.29 + Qt6.5.3 msvc2019_64 qmake Release构建JaguarCore、SparrowCore和SparrowCli成功。qmake提示`qt_build_config` feature不可用但继续生成；三个目标编译及链接均成功。g++仅对既有`MSpp.h`/磁盘`Mspp.h`大小写不一致给出可移植性warning，没有改该无关包含路径。
5. CLI运行验收使用`JaguarCore/Tests/Fixtures/RustReference/spp_instance.json`：cold-start输出根级实例字段+嵌套`solution`，SVG写入原先不存在的多层父目录；再把输出作为`--input`完成warm-start。两次输出的实例字段逐值相同，除`run_time_sec`外的solution字段逐值相同，`run_time_sec`均为0。S22当时还检查缺失`--output`、缺失输入、非法实例、JSON输出写入失败的退出码分别为2/3/4/8；S23后省略`--output`会走实例名默认路径。此为单件fixture smoke，不外推为Rust/C++算法parity或全输入错误映射证明。
