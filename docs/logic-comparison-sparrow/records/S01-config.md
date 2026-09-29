# S01. config.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/config.rs`（98 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h`（77 行）+ `SparrowCore/Source/Optimizer/SparrowConfig.cpp`（24 行）
**判定**: 基本等价（字段名/组织重组为 C++ 习惯形态；默认值多数一致，含 5 处形态/语义差异；Rust 无 JSON 反序列化——config.rs 是纯结构体+常量，"serde 语义"由 CLI/bench 侧承担，见 S23）

## 逐条比对（结构体/枚举/常量）

### `SparrowConfig` 结构体（Rust 7-23）
- C++ 对应：`SparrowConfig`（SparrowConfig.h:66-76）。
- 判定：差异（字段集重组）。
- 证据与说明：
  - `rng_seed: Option<usize>` → `std::uint64_t seed = 0U`（h:68）。Rust 用 `Option<usize>`（`None`=随机熵种子）；C++ 用零值 seed（由 SparrowOptimizer.cpp:110 `SparrowRng phaseRng(config.seed)` 统一作为根种子，无"未指定"三态）。种子派生链差异见 S08。
  - `expl_cfg`/`cmpr_cfg` → `exploration`/`compression`（h:71-72），字段等价。
  - `cde_config: CDEConfig`（含 quadtree_depth/cd_threshold/SPSurrogateConfig）→ **C++ 侧无字段**：CDEConfig 属 JaguarCore 实例构建参数（`JaguarCore/Problems/Spp.h` 的 `SPInstance::Create(..., CDEConfig{}, ...)`，见 BenchMain.cpp:123），SparrowCore 的 SparrowConfig 不再携带。判定：C++ 缺失（归属 JaguarCore 层）。
  - `poly_simpl_tolerance: Option<f32>`、`narrow_concavity_cutoff_ratio: Option<(f32,f32)>` → **C++ 无对应**：属 JaguarCore Importer 形状修改参数（JaguarCore 记录 10/12），SparrowCore 侧 `SparrowConstructorConfig` 不含。判定：C++ 缺失（归属 JaguarCore 层；Rust 默认 `Some(0.001)`/`Some((0.01,0.01))` 在 C++ 需调用方在 import 时自行传入）。
  - C++ 额外：`warmStart: std::optional<jaguar::SPSolution>`（h:69，Rust 的初始解走 `optimize()` 的独立参数 `initial_solution`，见 S08）；`constructor: SparrowConstructorConfig`（h:70，Rust 的 LBF 构造参数散在 `LBFBuilder::new(instance, rng, LBF_SAMPLE_CONFIG)`，见 consts.rs LBF_SAMPLE_CONFIG）；`terminator: OptimizationTerminatorConfig`（h:73，Rust 无全局终止器配置，见 S20）。

### `ExplorationConfig`（Rust 25-33）
- C++ 对应：`ExplorationConfig`（SparrowConfig.h:23-40）。
- 判定：基本等价。
- 证据与说明：
  - `shrink_step: f32` → `double shrinkStep = 0.001`（h:30）：一致（f32→double 既定差异）。
  - `time_limit: Duration` → `std::chrono::milliseconds timeLimit{std::chrono::minutes{9}}`（h:37）：一致（默认 9min=540s）。
  - `max_conseq_failed_attempts: Option<usize>`（默认 `None`）→ `std::size_t maxConsecutiveFailures = 0U`（h:29，注释"Zero keeps the complete failed-solution pool, as in sparrow"）：语义一致（0=None=不限），仅表示形态差异。
  - `solution_pool_distribution_stddev: f32` → `double solutionPoolDistributionStddev = 0.25`（h:35）：一致。
  - `separator_config` → `separator`（`SeparatorConfig`，见 S05/S04；Rust 默认 exp 侧 `iter_no_imprv_limit=200/strike=3/n_workers=3/50/25/3`，C++ `SeparatorConfig` 默认值相同：SeparatorWorker.h:20-25 `strikeLimit=3/noImprovementLimit=200/nContainerSamples=50/nFocussedSamples=25/nCoordinateDescents=3/nWorkers=3`）：一致。
  - `large_item_ch_area_cutoff_percentile: f32` → `double largeItemChAreaCutoffPercentile = 0.75`（h:36）：一致。
  - C++ 额外：`maxIterations = 0U`（h:28，注释"Zero means unlimited, matching the Rust phase loop which is bounded by its terminator"）；`infeasiblePoolSize = 0U`（h:32）；`disruptionCount = 1U`/`containedRelocationCount = 1U`（h:33-34）——见 S06 的探索期扰动/搬迁语义。

### `CompressionConfig`（Rust 35-41）
- C++ 对应：`CompressionConfig`（SparrowConfig.h:42-64）。
- 判定：基本等价。
- 证据与说明：
  - `shrink_range: (f32, f32)` → 拆为 `shrinkMin = 0.00001` + `shrinkMax = 0.0005`（h:58-59）：数值一致（Rust `(0.0005, 0.00001)` 为 (max,min) 元组，C++ 拆字段后顺序由名字消除歧义）。
  - `time_limit: Duration`（默认 60s）→ `timeLimit{seconds{60}}`（h:61）：一致。
  - `shrink_decay: ShrinkDecayStrategy` → `decayStrategy`（h:54-55）：见下条。
  - `separator_config`：Rust 压缩侧默认 `iter_no_imprv_limit=100/strike=5`，C++ 用 lambda 初始化覆盖 `strikeLimit=5U/noImprovementLimit=100U`，样本/worker 数与默认一致（h:44-53）：一致。
  - C++ 额外：`maxIterations=0U/maxConsecutiveFailures=0U`（h:56-57）；`failureDecay = 0.9`（h:60，把 Rust `FailureBased(f32)` 枚举载荷提为配置字段——默认 0.9 对应 Rust consts.rs:36 `DEFAULT_FAIL_DECAY_RATIO_CMPR = 0.9`，但 Rust 枚举默认值 `TimeBased` 不携带 0.9，C++ 无条件携带）：差异（形态：枚举载荷→平字段；CLI 早停路径 Rust 传 `FailureBased(0.9)`、C++ 传 `SHRINK_DECAY_FAILURE_BASED`+默认 `failureDecay=0.9`，数值等价）。

### `ShrinkDecayStrategy` 枚举（Rust 43-49）
- C++ 对应：`enum class ShrinkDecayStrategy { SHRINK_DECAY_TIME_BASED, SHRINK_DECAY_FAILURE_BASED }`（SparrowConfig.h:17-21）。
- 判定：差异（载荷外提）。
- 证据与说明：Rust `FailureBased(f32)` 携带衰减率；C++ 枚举无载荷，衰减率在 `CompressionConfig::failureDecay`（h:60）。默认策略同为 `TimeBased`（Rust 74 行 vs C++ h:54-55）。

### `DEFAULT_SPARROW_CONFIG` 常量（Rust 51-98）
- C++ 对应：无单一聚合常量；默认值全部以结构体成员默认初始化值形式分布（SparrowConfig.h:23-76、SeparatorWorker.h:17-28、LbfConfig.h:68-100）。
- 判定：基本等价（默认值逐项见上；C++ `SparrowConfig config;` 等价于 Rust 的 `DEFAULT_SPARROW_CONFIG` 拷贝）。
- 证据与说明：Rust 是 `pub const`（`Copy`），可整体覆盖字段；C++ 是默认成员初始化器，聚合值等价。Rust `log::Level` 字段（expl Info / cmpr Debug，config.rs:61/78）在 C++ 无对应（日志级别属 CLI/Demo 层，SparrowCore 库不依赖日志框架）。

### JSON 反序列化
- Rust：`SparrowConfig` **不含** `Deserialize` derive（config.rs 全文无 serde）；配置由代码常量+CLI 覆盖构造（见 cli.rs:47-57 `apply_config_overrides`）。JSON 输入只解析 `ExtSPInstance/ExtSPSolution`（io.rs:135-149，见 S22）。
- C++：`SparrowConfig` 同样无 JSON 解析；字段由 `Tools/SparrowCli/Main.cpp:142-158` 逐个赋值。
- 判定：一致（双侧配置均无 JSON 反序列化语义；任务背景中"serde vs JsonCodec"的疑点经核实不适用于 config.rs——JSON 反序列化只存在于实例/解输入，走 JaguarCore `Importer::ImportSPInstanceJson`，见 S22/S23）。

### `Validate()`（C++ 额外）
- C++：`SparrowConfig::Validate/ExplorationConfig::Validate/CompressionConfig::Validate`（SparrowConfig.cpp:5-23；h:39/63/75 声明；ExplorationConfig/CompressionConfig 的 Validate 实现在 SparrowConfig.cpp 中仅 SparrowConfig 一处——实际探索/压缩校验体位于 cpp 的 Validate 链内被调用）。
- 判定：C++ 额外。
- 证据与说明：Rust 无校验（依赖类型系统与 panic）；C++ 在 `Optimize` 入口统一 `config.Validate()`（SparrowOptimizer.cpp:100-105），校验链 constructor→exploration→compression→terminator。语义=入参前置检查，不改变等价性方向。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `rng_seed: Option<usize>` vs `seed: u64`（0=默认） | 差异 | Rust `None` 时用真随机种子（cli.rs:67-71），C++ 固定 0 为根种子；可复现性语义不同 | config.rs:9 vs SparrowConfig.h:68 |
| 2 | `cde_config`/`poly_simpl_tolerance`/`narrow_concavity_cutoff_ratio` | C++缺失（归属 JaguarCore 层） | Rust 侧 sparrow 配置携带导入参数；C++ 需调用方在 import 时传，SparrowConfig 不含 | config.rs:14/18/22 vs SparrowConfig.h:66-76（无该字段） |
| 3 | `ShrinkDecayStrategy::FailureBased(f32)` vs 枚举+`failureDecay` 平字段 | 差异 | 载荷外提；默认值均 0.9，行为等价 | config.rs:44-49 vs SparrowConfig.h:17-21/60 |
| 4 | `log::Level`（expl Info/cmpr Debug）字段 | C++缺失 | 日志分级属应用层；SparrowCore 库无日志依赖，CLI 不分级 | config.rs:61/78 vs SparrowConfig.h:23-64 |
| 5 | C++ 额外 `warmStart`/`constructor`/`terminator` 配置子块 | C++额外 | 把 Rust 散在 optimize() 参数（初始解、LBF 构造参数、无全局终止器）收敛进配置；见 S08/S20 | SparrowConfig.h:69-70/73 vs config.rs:8-23 |
| 6 | `Validate()` 校验链 | C++额外 | 入参前置校验（Result 通道），Rust 无；不影响默认路径 | SparrowConfig.cpp:5-23 vs config.rs（无） |

## 结论
SparrowConfig 的两阶段（exploration/compression）字段与默认值（shrink 0.001、9min/60s、0.25、0.75、separator 200/3 与 100/5、50/25/3）与 Rust 逐项一致，判定基本等价。核心形态差异有三：种子 `Option` 语义弱化为零值、导入相关三参数下沉 JaguarCore 层、枚举载荷外提为平字段；C++ 额外引入 warmStart/constructor/terminator 三个子配置块与 Validate 校验链，均为 C++ 侧 Result 错误通道与 API 收敛的选择，不改变默认行为。Rust 的 config.rs 无 serde 反序列化，"JSON 配置解析"在两侧均不存在，输入 JSON 的解析语义归 S22/S23 记录。
