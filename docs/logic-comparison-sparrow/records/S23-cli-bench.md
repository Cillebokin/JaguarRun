# S23. main.rs + cli.rs + bench.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/main.rs`（13 行）+ `cli.rs`（127 行）+ `bench.rs`（229 行）
**C++**: `SparrowCore/Tools/SparrowCli/Main.cpp`（235 行）+ `SparrowCore/bench/BenchMain.cpp`（223 行，`SparrowCoreBench.pro`）
**判定**: 基本等价（CLI 主干"读实例→配置覆盖→Optimize→写解/写 SVG"一致；选项集/时间预算语义/输出产物有实质差异；bench 定位不同——Rust 统计基准 vs C++ 回归基准，并行多 run 统计缺失）

## 逐条比对（main.rs + cli.rs vs SparrowCli/Main.cpp）

### 入口选择：`#[cfg(feature = "tui")] mod tui; #[cfg(not)] mod cli;`（main.rs:1-13）
- C++ 对应：无（单一 CLI 入口；TUI 归 S24）。
- 判定：C++ 缺失（TUI 变体，归 S24）。
- 证据与说明：Rust 用 cargo feature 在 CLI/TUI 间切换 `run()`；C++ 入口固定。

### CLI 选项集（Rust MainCli io.rs:15-43 vs C++ Main.cpp:24-30/62-126）
- 判定：差异（子集+改名）。
- 证据与说明：
  | 语义 | Rust | C++ |
  |---|---|---|
  | 输入 | `-i/--input <文件>`（实例或 warm-start 解） | `--input <instance.json>`（仅实例，S22 差异 1） |
  | 全局时限 | `-t/--global-time <秒>`（与 -e/-c 互斥） | `--time-ms <n>`（→`terminator.maxDuration`，非分摊） |
  | 探索/压缩时限 | `-e` 与 `-c`（互相 requires） | 无 |
  | 早停 | `-x/--early-termination` | `--early-termination`（一致：maxConsecutiveFailures=10 + FAILURE_BASED，Main.cpp:151-156 vs cli.rs:53-57） |
  | 种子 | `-s/--rng-seed <u64>` | `--seed <u64>`（一致） |
  | worker 数 | `--workers <NonZeroUsize>`（覆盖两阶段 n_workers，io.rs:52-56） | 无 |
  | 输出路径 | 固定约定 `output/final_{name}.{svg,json}`（cli.rs:88/119） | `--output <solution.json>` + `--svg <layout.svg>`（必填 output） |
  | 采样数 | 无 | `--samples <n>`（默认 1000→constructor.nContainerSamples，Main.cpp:68/150） |
  | 互斥校验 | clap 声明式（conflicts_with/requires） | 手写 if（无 -t/-e/-c 互斥需要） |
  - 语义要点：Rust `-t 600` 按 0.8/0.2 分摊到两阶段 time_limit（cli.rs:40-41），无 -t/-e/-c 时默认 600s 同分摊（cli.rs:45-48）；C++ `--time-ms` 直接作为**全局**终止器 maxDuration（Main.cpp:163-164），两阶段 timeLimit 保持默认 9min/60s 不动——时间预算模型不同（S02 差异 1 的应用层体现）。无 `--time-ms` 时 C++ 走默认（9min+60s 相位超时，与 Rust 无时限默认 600s 分摊=480s/120s 数值不同：Rust 默认总 600s，C++ 默认总 600s 但分摊方式不同——C++ expl 540s vs Rust 480s）。边界也不同：Rust `-t 0` 会给探索/压缩设置零时长 phase deadline；C++ `--time-ms 0` 将全局 maxDuration 设为零，而 `ShouldTerminate` 只在正值时执行全局时长检查，因此零表示关闭该全局限制，默认阶段时限仍生效。计时起点也不同：C++ `Optimize` 在构造前启动全局 timer，Rust 先完成 LBF 构造/恢复 warm-start，再分别启动探索/压缩 phase timer；故构造耗时在 C++ 全局预算内、Rust CLI 阶段预算外。上述零值与构造计时差异有源码证据，尚无专门 Rust/C++ parity fixture。
  - 返回码：Rust anyhow 错误→exit 1；C++ 细分 2/3/4/5/6/7/8/9（Main.cpp:99-253），诊断更强。

### `run()` 流程编排（cli.rs:26-127 vs Main.cpp:62-261）
- 判定：基本等价（主干一致，节点差异如下）。
- 证据与说明：
  1. 读输入：`read_spp_input`（实例+可选解）vs `ImportSPInstanceJson`（仅实例）——S22 差异 1。
  2. Importer 参数：Rust `Importer::new(cde_config, poly_simpl_tolerance, narrow_concavity_cutoff_ratio)`（cli.rs:78，Sparrow 配置三参数生效）；C++ CLI 现在显式用 `jaguar::Importer(sparrow::MakeRustDefaultImporterConfig())`（Main.cpp:139-140；配置 helper 在 SparrowConfig.h:80-96）映射 Rust 默认 CDE 与形状修改参数。旧记录所述“无参构造并走 JaguarCore 默认”已过期。C++ CLI 当前默认值路径已映射；但没有 Rust CLI 对应的任意 importer 参数覆盖选项，因而不能把配置 API 的全域行为称作等价。
  3. RNG：Rust `config.rng_seed.map_or_else(rand::random, seed)`（cli.rs:62-72，未指定→真随机+warn 日志）；C++ `seed=0` 默认（确定性重跑）——S01 差异 1。
  4. 监听器：Rust 传 `SvgExporter`（live/intermediate/final 三通道，cli.rs:87-105+109-117）vs C++ `Optimize(instance, config)` 不传 listener（Main.cpp:176），最终 SVG 由 CLI 事后导出（Main.cpp:222-255）——S22 差异 4。
  5. 终止器：Rust `CtrlCTerminator::new()`（cli.rs:107，Ctrl-C 可中断）；C++ CLI 创建外部 `OptimizationTerminator` 并用 `ScopedCtrlCHandler` 将 SIGINT 映射到 phase stop（Main.cpp；`--time-ms` 仍映射 `maxDuration`）。信号/phase-stop 轨迹见 S20；CLI 的总时限与 Rust 分阶段时限模型仍不同。
  6. 输出：Rust 写 `output/final_{name}.json`（实例+解）+ final SVG（经监听器）；C++ 写 `--output` 解 JSON + 可选 `--svg`——S22 差异 2。
  7. C++ 额外：结尾打印 `SparrowCore/JaguarCore 版本 + width + density` 单行摘要（Main.cpp:256-259）。

### `apply_config_overrides`（io.rs:47-57：seed+workers 覆盖并 warn）
- C++ 对应：Main.cpp:148-156（seed、samples、early-termination 直接赋值；无 workers 覆盖入口）。
- 判定：差异（workers 覆盖缺失，见选项表）。

## 逐条比对（bench.rs vs BenchMain.cpp）

### 入口参数（Rust 32-37：`args[1]`=输入文件、`args[2]`=时限秒、`args[3]`=总 run 数 vs C++ 186-192：仅 `all` 一个字面参数）
- 判定：差异（定位不同）。
- 证据与说明：Rust 是**统计基准**（多 run 分布）；C++ 是**固定回归基准**（两个内置场景、硬编码种子 42、CSV 一行输出），bench 夹具发现逻辑 FindFixture（BenchMain.cpp:32-47）。

### 多 run 并行执行（Rust 59-128：rayon scope 按物理核/worker 数分批、每 run 独立子种子 `rng.random()`、LBFBuilder→exploration_phase→compression_phase 全程）
- C++ 对应：无（单 run×2 场景，串行）。
- 判定：C++ 缺失。
- 证据与说明：Rust `n_runs_per_iter = physical_cores / n_workers`（59 行），批次内 rayon 线程各自 `Xoshiro256PlusPlus::seed_from_u64(rng.next_u64())`（83/87 行）派生独立流；C++ `RunBenchmark` 只调一次 `sparrow::Optimize`（150 行）。影响：性能/收敛分布对比（best/median/stddev）在 C++ 不可得——这是"基准场景覆盖面"的核心差距：C++ 覆盖的是**确定性回归**（checksum 锁定解指纹，BenchMain.cpp:67-82 FNV 哈希 strip.width+每 placement 的 item/transform），Rust 覆盖的是**统计性能**。

### 基准场景覆盖
- 判定：差异。
- 证据与说明：Rust：任意用户实例（argv 传入）、time_limit 可调、n_runs 可调、输出逐 run SVG+best SVG+widths/usages 分布统计（140-169）。C++：两个内置场景 `lbf_spp_small`（夹具 JSON，LbfSppSmall.json）+ `lbf_spp_large`（程序生成 8 类矩形，MakeLargeInstance，84-125 行），固定小预算（maxIterations 256/512、strike 2、noImprovement 6，130-148 行），输出 CSV 指标行（169-181 行：版本/种子/样本数/放置数/剩余需求/宽度/密度/耗时/可行率/surrogate 拒绝数/精确碰撞数/checksum）。C++ 额外字段：surrogateRejectCount/exactCollisionCount/exactCollisionLayout/checksum；Rust 额外产物：分布统计与多 run SVG。

### 统计函数（Rust 176-217：`calculate_percentile`（Excel 公式插值）/`calculate_median`/`calculate_average`/`calculate_stddev`）
- C++ 对应：无。
- 判定：C++ 缺失（随统计基准缺失）。

### `get_git_commit_hash`（Rust 219-229）
- C++ 对应：`sparrow::Version()`/`jaguar::Version()`（BenchMain.cpp:169-170）。
- 判定：差异（等价意图）。
- 证据与说明：两侧都做"输出版本指纹"；Rust 取 git HEAD，C++ 取编译期版本串。

### 结果导出（Rust 115-119/140-146：每 run final_bench_{i}.svg + best SVG vs C++：仅 CSV，无 SVG）
- 判定：C++ 缺失。

### C++ 额外：`SolutionChecksum`（BenchMain.cpp:67-82）
- 判定：C++ 额外。
- 证据与说明：FNV-1a 哈希逐 placement 的 (itemIndex, handle index/generation, rotation, x, y)——为回归比对提供解级指纹，Rust 无此机制（靠 density 数值比对）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | CLI 选项集与时间预算（-t/-e/-c 分摊模型、`--time-ms` 全局时限、--workers、warm-start 输入；零值语义及计时起点） | 差异 | C++ 缺少相位级时限/worker 覆盖；`--time-ms 0` 关闭全局时限而 Rust `-t 0` 设置零时长 phase deadline；C++ 计时包含构造，Rust phase budget 不含构造；默认探索预算为 540s vs Rust 480s | cli.rs:38-55、optimizer/mod.rs:39-57 vs Main.cpp:163-164、SparrowOptimizer.cpp:233-240、OptimizationTerminator.cpp:82-84 |
| 2 | Importer 默认参数传递 | 【默认路径已映射；配置覆盖面有差异】 | C++ CLI 调用 `MakeRustDefaultImporterConfig()`，不再走 JaguarCore 默认值；仍无 CLI 自定义 importer 配置项 | cli.rs:78 vs Main.cpp:139-140、SparrowConfig.h:80-96 |
| 3 | RNG 缺省（真随机 vs seed=0） | 差异 | C++ 默认可复现；Rust 默认随机+日志 | cli.rs:62-72 vs Main.cpp:67-68/149 |
| 4 | bench 定位：多 run 统计基准 vs 固定回归基准 | 差异 | C++ 无多 run/并行/分布统计（percentile/median/stddev 均缺）；有 checksum 指纹与 surrogate/碰撞计数 | bench.rs:59-128/176-217 vs BenchMain.cpp:127-183 |
| 5 | bench 场景覆盖（任意实例×N runs vs 2 内置场景） | 差异 | C++ 不能对用户实例跑基准 | bench.rs:32-37 vs BenchMain.cpp:32-47/84-125 |
| 6 | 输出产物（final_bench_*.svg、分布打印 vs CSV 单行） | 差异 | 形态不同；C++ CSV 利于自动化比对 | bench.rs:140-169 vs BenchMain.cpp:169-181 |
| 7 | 退出码细分 | C++额外 | 9 档退出码 vs anyhow 单档 | Main.cpp:99-253 vs cli.rs（anyhow） |

## 结论
CLI 主干流程形态相似，但当前配置输入仍不能保证同一 Rust 运行预算或结果：显式 seed 选项对应；缺省 seed、worker 覆盖、两阶段时限分配、零时限含义、计时起点及 warm-start 文件输入仍不同。Importer 的当前默认参数已通过 `MakeRustDefaultImporterConfig()` 映射；自定义参数覆盖面仍不同。若 SparrowCli 纳入迁移范围，应先使解析后的有效算法配置与 Rust 一致，再核对最终布局；不能把默认值或时间模型差异归为仅输出层差异。bench 定位另有分叉（Rust 任意实例、多 run、并行分布统计；C++ 固定场景、固定 seed 的确定性回归），要验证算法 parity 应由同一 Rust 输入/seed 驱动并比较结果，性能统计能力另按产品范围补齐。

### 补充审计（2026-09-29，bench.rs 229 行逐场景复核）
**【任务指定核对项】bench.rs 的每个基准场景/函数逐项覆盖核对**：

1. **【遗漏补记：main 的三个参数解析与种子选择】** 主文"入口参数"条目记了 argv 三参但未记种子语义：bench.rs:42-50 `config.rng_seed` 有值（DEFAULT_SPARROW_CONFIG.rng_seed=None，故恒走 None 分支）→ `rand::random()` 真随机 + println（与 CLI 的 warn 日志不同通道）。C++ BenchMain 无对应 RNG 初始化（config.seed=42 硬编码，:131）。判定：【差异】（归入主文差异 #4 的定位分叉）。
2. **【浅覆盖补全：批次划分数学】** bench.rs:59-60 `n_runs_per_iter = (physical_cores / n_workers).min(n_runs_total)`、`n_batches = ceil(n_runs_total / n_runs_per_iter)`——注意 `min(n_runs_total)` 使单批即可完成时不再分批；rayon scope 内每 run 派生 `rng.random()` 子种子（:84）+ 每 run 内部再按构造/探索/压缩三段 `next_u64()` 派生（:91-92，与 mod.rs:37 optimize 的派生序一致——bench 直接调 LBFBuilder/exploration_phase/compression_phase 三件而非 optimize()，**不消耗 optimize 的相位报告/相位超时语义**，terminator 用 BasicTerminator 手动 new_timeout（:96/:103）。C++ 侧 Optimize 单次调用含完整三阶段编排。判定：主文差异 #4（多 run 缺失）维持，此处补记 bench 复用 optimize 内部件的接线形态。
3. **【浅覆盖补全：229 行中的每个函数清单核对】** main（:22-138）、calculate_percentile（:141-168，Excel (n-1) 插值公式 k=pct*(n-1)+1、lower/upper 线性插值）、calculate_median（:171-173 = percentile(0.5)）、calculate_average（:176-178）、calculate_stddev（:181-185，**总体**标准差/无 Bessel 修正）、get_git_commit_hash（:188-198，git rev-parse HEAD）——主文"统计函数"条目已概括，无遗漏函数；输出统计含 widths 与 usages 两组各 7 行（worst/25%/med/75%/best/avg/stddev，:128-137，**usages 的 25%/75% 与 widths 的 pct 参数互补**——widths 用 0.75/0.25 升序、usages 同参数但因 usages 越大越好而"best"取 max）。C++ 无对应（主文判定维持缺失）。
4. **【核对确认：C++ 两个内置场景的现场细节】** BenchMain.cpp:127-152：`lbf_spp_small`（夹具 LbfSppSmall.json）/`lbf_spp_large`（MakeLargeInstance 程序生成 8 类矩形 0.8-1.4×0.9-1.2、demand 2/3、容器宽 8.0，:84-125）；配置 seed=42、constructor 128/32（large 48/12）采样、exploration/compression maxIterations 1-2、maxConsecutiveFailures 3、strike 2、noImprovement 6、timeLimit 0ms（**禁相位超时**）、terminator.maxIterations 256/512、FAILURE_BASED 衰减——全部为 Rust bench 无对应的回归专用配置，主文差异 #4/#5 判定维持。
5. **【核对确认：main.rs 13 行】** 全文为 tui/cli feature 二选一 + `fn main() { run() }`，无遗漏逻辑，主文判定维持。
6. **【核对确认：cli.rs 127 行】** 逐段（时间预算三分支 :40-55、早停 :53-57、种子 :62-72、Importer 三参 :78、SvgExporter 三路径 :87-105、CtrlCTerminator :107、optimize 调用 :109-117、ExtSPOutput 写盘 :119-125）主文与 S22 均有条目，无遗漏。
