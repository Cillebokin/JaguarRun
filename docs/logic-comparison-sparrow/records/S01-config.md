# S01. config.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/config.rs`（98 行）
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h` + `SparrowCore/Source/Optimizer/SparrowConfig.cpp`（当前实现；P0 更新于 2026-10-09）
**判定**: 基本等价（字段名/组织重组为 C++ 习惯形态；默认值多数一致，含 5 处形态/语义差异；Rust 无 JSON 反序列化——config.rs 是纯结构体+常量，"serde 语义"由 CLI/bench 侧承担，见 S23）

## 逐条比对（结构体/枚举/常量）

### `SparrowConfig` 结构体（Rust 7-23）
- C++ 对应：`SparrowConfig`（SparrowConfig.h:66-76）。
- 判定：差异（字段集重组）。
- 证据与说明：
  - `rng_seed: Option<usize>` → `std::optional<std::uint64_t> seed = std::nullopt`。Rust `None` 与 `Some(0)` 保持可区分；SparrowCore `Optimize` 仅在 seed 缺省时生成随机根 seed，并把有效 seed 写入最终 metrics，供重放使用。SparrowCli 缺省 `--seed` 时保留空值并报告实际 seed；显式 `--seed 0` 仍是固定零 seed。熵源分别由 Rust `rand::random()` 和 C++ `std::random_device` 提供，二者只要求同为非确定性 seed 语义，不要求独立运行生成相同 seed。阶段派生顺序仍见 S08。
  - `expl_cfg`/`cmpr_cfg` → `exploration`/`compression`（h:71-72），字段等价。
  - `cde_config: CDEConfig`（含 quadtree_depth/cd_threshold/SPSurrogateConfig）→ **C++ 侧无字段**：CDEConfig 属 JaguarCore 实例构建参数（`JaguarCore/Problems/Spp.h` 的 `SPInstance::Create(..., CDEConfig{}, ...)`，见 BenchMain.cpp:123），SparrowCore 的 SparrowConfig 不再携带。判定：C++ 缺失（归属 JaguarCore 层）。
  - `poly_simpl_tolerance: Option<f32>`、`narrow_concavity_cutoff_ratio: Option<(f32,f32)>` → SparrowConfig 本身不持有；它们属于 JaguarCore Importer 形状修改参数（JaguarCore 记录 10/12）。C++ `MakeRustDefaultImporterConfig()` 将 Rust 默认值 `0.001`/`(0.01,0.01)` 映射到 `ImporterConfig`，CLI/WASM/Bench 当前导入入口均使用该 helper；若调用方要覆盖这些参数，仍须在导入前构造自定义 `ImporterConfig`，不能通过 `SparrowConfig` 设置。
  - C++ 额外：`warmStart: std::optional<jaguar::SPSolution>`（h:69，Rust 的初始解走 `optimize()` 的独立参数 `initial_solution`，见 S08）；`constructor: SparrowConstructorConfig`（h:70，Rust 的 LBF 构造参数散在 `LBFBuilder::new(instance, rng, LBF_SAMPLE_CONFIG)`，见 consts.rs LBF_SAMPLE_CONFIG）；`terminator: OptimizationTerminatorConfig`（h:73，Rust 无全局终止器配置，见 S20）。

### `ExplorationConfig`（Rust 25-33）
- C++ 对应：`ExplorationConfig`（SparrowConfig.h:23-40）。
- 判定：基本等价。
- 证据与说明：
  - `shrink_step: f32` → `double shrinkStep = 0.001`（h:30）：一致（f32→double 既定差异）。
  - `time_limit: Duration` → `std::chrono::nanoseconds timeLimit{std::chrono::minutes{9}}`：默认值一致（9min=540s）；配置和 phase deadline 保留纳秒值。Compression TimeBased 比率的 elapsed 与 timeLimit 使用同一纳秒单位。底层 `steady_clock` 的实际分辨率仍取决于平台；本基线验证了 500µs 值能从配置传到 phase deadline 并按 deadline 到期，不据此声称其他时钟平台的分辨率。
  - `max_conseq_failed_attempts: Option<usize>`（默认 `None`）→ `std::optional<std::size_t> maxConsecutiveFailures = std::nullopt`（h:29）：`None`/无值均表示不限，Rust `Some(0)`/C++ 有值 0 均在首次失败入池报告后立即退出；该配置边界已由 S06 的实际收缩失败 Rust/C++ reference 验证。
  - `solution_pool_distribution_stddev: f32` → `double solutionPoolDistributionStddev = 0.25`（h:35）：一致。
  - `separator_config` → `separator`（`SeparatorConfig`，见 S05/S04；Rust 默认 exp 侧 `iter_no_imprv_limit=200/strike=3/n_workers=3/50/25/3`，C++ `SeparatorConfig` 默认值相同：SeparatorWorker.h:20-25 `strikeLimit=3/noImprovementLimit=200/nContainerSamples=50/nFocussedSamples=25/nCoordinateDescents=3/nWorkers=3`）：一致。
  - `large_item_ch_area_cutoff_percentile: f32` → `double largeItemChAreaCutoffPercentile = 0.75`（h:36）：一致。
  - C++ 额外：`maxIterations = 0U`（h:28，注释"Zero means unlimited, matching the Rust phase loop which is bounded by its terminator"）；`infeasiblePoolSize = 0U`（h:32）；`disruptionCount = 1U`/`containedRelocationCount = 1U`（h:33-34）——见 S06 的探索期扰动/搬迁语义。

### `CompressionConfig`（Rust 35-41）
- C++ 对应：`CompressionConfig`（SparrowConfig.h:42-64）。
- 判定：基本等价。
- 证据与说明：
  - `shrink_range: (f32, f32)` → 拆为 `shrinkMin = 0.00001` + `shrinkMax = 0.0005`（h:58-59）：数值一致（Rust `(0.0005, 0.00001)` 为 (max,min) 元组，C++ 拆字段后顺序由名字消除歧义）。
  - `time_limit: Duration`（默认 60s）→ `std::chrono::nanoseconds timeLimit{seconds{60}}`：默认值和配置精度均对应；TimeBased elapsed 比率改为纳秒计算，实测边界见本记录末尾的 P0 闭合记录。
  - `shrink_decay: ShrinkDecayStrategy` → `decayStrategy`（h:54-55）：见下条。
  - `separator_config`：Rust 压缩侧默认 `iter_no_imprv_limit=100/strike=5`，C++ 用 lambda 初始化覆盖 `strikeLimit=5U/noImprovementLimit=100U`，样本/worker 数与默认一致（h:44-53）：一致。
  - C++ 额外：`maxIterations=0U/maxConsecutiveFailures=0U`（h:56-57）；`failureDecay = 0.9`（h:60，把 Rust `FailureBased(f32)` 枚举载荷提为配置字段——默认 0.9 对应 Rust consts.rs:36 `DEFAULT_FAIL_DECAY_RATIO_CMPR = 0.9`，但 Rust 枚举默认值 `TimeBased` 不携带 0.9，C++ 无条件携带）：差异（形态：枚举载荷→平字段；CLI 早停路径 Rust 传 `FailureBased(0.9)`、C++ 传 `SHRINK_DECAY_FAILURE_BASED`+默认 `failureDecay=0.9`，数值等价）。

### `ShrinkDecayStrategy` 枚举（Rust 43-49）
- C++ 对应：`enum class ShrinkDecayStrategy { SHRINK_DECAY_TIME_BASED, SHRINK_DECAY_FAILURE_BASED }`（SparrowConfig.h:17-21）。
- 判定：差异（载荷外提）。
- 证据与说明：Rust `FailureBased(f32)` 携带衰减率；C++ 枚举无载荷，衰减率在 `CompressionConfig::failureDecay`（h:60）。默认策略同为 `TimeBased`（Rust 74 行 vs C++ h:54-55）。

### `DEFAULT_SPARROW_CONFIG` 常量（Rust 51-98）
- C++ 对应：无单一聚合常量；优化器默认值以结构体成员默认初始化值形式分布（SparrowConfig.h:25-78、SeparatorWorker.h:17-28、LbfConfig.h:68-100）。Rust 默认导入侧参数另由 `MakeRustDefaultImporterConfig()` 显式构造（SparrowConfig.h:80-96），不属于 `SparrowConfig` 字段。
- 判定：基本等价（优化器字段的默认值逐项对应；导入默认值通过单独 helper 接线）。
- 证据与说明：Rust 是 `pub const`（`Copy`），可整体覆盖字段；C++ 优化器配置用默认成员初始化器，值对应。Rust `log::Level` 字段（expl Info / cmpr Debug，config.rs:61/78）在 C++ 无对应（日志级别属 CLI/Demo 层，SparrowCore 库不依赖日志框架）。Rust `DEFAULT_SPARROW_CONFIG` 同时携带 CDE 和形状修改导入参数；C++ helper 目前只映射这组默认值，调用方的自定义 Rust 导入参数需要显式配置 JaguarCore Importer。

### JSON 反序列化
- Rust：`SparrowConfig` **不含** `Deserialize` derive（config.rs 全文无 serde）；配置由代码常量+CLI 覆盖构造（见 cli.rs:47-57 `apply_config_overrides`）。JSON 输入只解析 `ExtSPInstance/ExtSPSolution`（io.rs:135-149，见 S22）。
- C++：`SparrowConfig` 同样无 JSON 解析；字段由 `Tools/SparrowCli/Main.cpp:148-164` 逐个赋值。
- 判定：一致（双侧配置均无 JSON 反序列化语义；任务背景中"serde vs JsonCodec"的疑点经核实不适用于 config.rs——JSON 反序列化只存在于实例/解输入，走 JaguarCore `Importer::ImportSPInstanceJson`，见 S22/S23）。

### `Validate()`（C++ 额外）
- C++：`SparrowConfig::Validate` 实现在 `SparrowConfig.cpp:5-23`；`ExplorationConfig::Validate` 在 `Exploration.cpp:868-884`，`CompressionConfig::Validate` 在 `Compression.cpp:157-169`（头文件声明见 SparrowConfig.h:39/63/75）。`SparrowConfig::Validate` 负责按顺序委托到这些子配置校验。
- 判定：C++ 额外。
- 证据与说明：Rust 无对应配置验证链；C++ 在 `Optimize` 入口统一 `config.Validate()`（SparrowOptimizer.cpp:100-105），校验链 constructor→exploration→compression→terminator。不能笼统视作无行为影响的防御：C++ 拒绝 `shrinkStep<=0`/`>=1`、非正 stddev、percentile 越界；Rust `shrinkStep=0/1` 会按宽度边界退出，`Normal(0,stddev=0)` 可构造，越界 percentile 会继续参与候选筛选。压缩侧 C++ 拒绝 `shrinkMin<=0`、`shrinkMax<shrinkMin`/`>=1`、FailureBased ratio 不在 `(0,1)`；Rust enum/tuple 无同类校验，0 衰减率可能令步长立即低于下限，1 衰减率可持续尝试直至其他停止条件。以上是边界配置的代码级语义差异，当前 fixtures 均使用常规合法配置，无专门 reference。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `rng_seed: Option<usize>` vs `optional<uint64_t>` | 已对齐（种子语义） | absent seed 生成并暴露有效根 seed；explicit zero 保持确定值；熵源实现按语言不同，独立 absent-seed 运行不要求相同轨迹 | config.rs:9/cli.rs:72-81 vs SparrowConfig.h/SparrowOptimizer.cpp/Main.cpp；optimizer_config_reference 与 C++ replay/CLI smoke |
| 2 | `cde_config`/`poly_simpl_tolerance`/`narrow_concavity_cutoff_ratio` 不在 SparrowConfig 中 | 已有默认值桥接；自定义值仍由 ImporterConfig 承担 | `MakeRustDefaultImporterConfig()` 映射 Rust 默认 CDE/形状修改参数，CLI/WASM/Bench 已使用；SparrowConfig 本身不能表达调用方的参数覆盖 | config.rs:14/18/22,87-97 vs SparrowConfig.h:80-96; 三个宿主导入调用点 |
| 3 | `ShrinkDecayStrategy::FailureBased(f32)` vs 枚举+`failureDecay` 平字段 | 差异 | 载荷外提；默认值均 0.9，行为等价 | config.rs:44-49 vs SparrowConfig.h:17-21/60 |
| 4 | `log::Level`（expl Info/cmpr Debug）字段 | C++缺失 | 日志分级属应用层；SparrowCore 库无日志依赖，CLI 不分级 | config.rs:61/78 vs SparrowConfig.h:23-64 |
| 5 | C++ 额外 `warmStart`/`constructor`/`terminator` 配置子块 | C++额外 | 把 Rust 散在 optimize() 参数（初始解、LBF 构造参数、无全局终止器）收敛进配置；见 S08/S20 | SparrowConfig.h:69-70/73 vs config.rs:8-23 |
| 6 | `Validate()` 校验链 | C++额外 | 入参前置校验（Result 通道），Rust 无；实现分布在 SparrowConfig/Exploration/Compression | SparrowConfig.cpp:5-23; Exploration.cpp:868-884; Compression.cpp:157-169 vs config.rs（无） |
| 7 | `Option<usize>` 与 `optional<size_t>` 失败上限 | 已对齐 | `None`/无值表示不限，`Some(0)`/有值 0 表示第一次失败后停止；固定收缩失败输入已逐项比较报告顺序 | config.rs:29; explore.rs:239-245 vs SparrowConfig.h:29; Exploration.cpp; S06 reference |
| 8 | Rust `Duration` 与 SparrowCore phase/global duration 的存储精度 | 已对齐（纳秒配置语义） | exploration/compression timeLimit、global maxDuration、phase deadline 和 TimeBased elapsed 比率使用纳秒；平台时钟的硬件分辨率另行限定 | config.rs:5,37-40 vs SparrowConfig.h/OptimizationTerminator.h/.cpp/Compression.cpp；Rust 与 C++ 500µs boundary tests |

## 结论
SparrowConfig 的两阶段字段与默认值（shrink 0.001、9min/60s、0.25、0.75、separator 200/3 与 100/5、50/25/3）与 Rust 逐项对应。2026-10-09 P0 更新已恢复 absent/explicit-zero seed 区分、有效 seed 重放信息和纳秒时限精度。仍存在的形态/边界差异包括：导入参数下沉 JaguarCore 层、枚举载荷外提、C++ 额外配置块及比 Rust 更强的 Validate 校验；这些不是本轮闭合项。失败次数上限已通过 `std::optional` 保留 Rust 的 `None`/`Some(0)` 区分。Rust 的 config.rs 无 serde 反序列化，输入 JSON 的解析语义归 S22/S23 记录。

## P0 闭合记录（2026-10-09）

- `SparrowConfig.seed` 已改为 optional。RustReference 验证默认 `rng_seed=None`，C++ 默认配置验证为空；explicit zero 保留为值 0。顶层无 seed 优化的返回 metrics 提供实际 root seed，用该值固定重跑可逐项复现解；CLI 无 `--seed` 和显式 `--seed 0` 两条路径均以单件 fixture smoke 通过并报告相应 seed。
- phase/global 时限配置改为 `std::chrono::nanoseconds`；`OptimizationTerminator::StartPhase`、`Elapsed` 和 Compression TimeBased 比率保留纳秒单位。Rust 和 C++ reference 均覆盖 500µs；C++ phase deadline 在等待超过限值后结束。
- 空范围 RNG：Rust `random_range(0..0)` panic；C++ `UniformIndex(0)` 抛 `std::invalid_argument`。两边均在 RNG 状态消费前失败。此为错误出口语言机制映射，不要求异常文本相同。调用方有效构造下不会传入空范围；sampler 构造器的错误分类另由 S16 闭合。
- 验证：RustReference Debug/Release 全量测试通过；SparrowCore Debug/Release 全量 runner 通过；SparrowCli Release 重建成功，缺省 seed 和显式零 seed smoke 均 exit 0。该记录只闭合本节列明的 S01/S02 差异，不表示整个 P0 清单完成。

### 补充审计（2026-09-29）
1. **【核对确认：逐行覆盖完整性】** config.rs 98 行全量复核：3 结构体（SparrowConfig 7 字段 / ExplorationConfig 6 字段 / CompressionConfig 4 字段）、1 枚举（ShrinkDecayStrategy 2 变体）、1 常量（DEFAULT_SPARROW_CONFIG 98 行展开）——主文 5 个小节 + JSON 小节全覆盖，无遗漏项；derive（Debug/Clone/Copy）与 doc 注释不构成逻辑项。
2. **【遗漏补记：log_level 字段的四层级联】** 主文差异 #4 记了"expl Info/cmpr Debug 无 C++ 对应"；补记使用链：`separator.rs:91/139/151/183` 的 `log!(self.config.log_level, ...)` 使 separator 日志随相位升降级——即 Rust 侧同一 Separator 在探索期打 Info、压缩期打 Debug。C++ 无日志框架（判定不变），此为该字段唯一消费语义，防后续误查。
3. **【核对确认：DEFAULT 的 cde_config 三参数默认值】** quadtree_depth=4 / cd_threshold=64 / SPSurrogateConfig n_pole_limits=[(64,0.0),(16,0.8),(8,0.9)]、ff_pole_area_ratio=0.5、n_ff_piers=0（config.rs:87-95）——C++ 侧归属 JaguarCore（S01 差异 #2 已记"由调用方传 CDEConfig{}"）；BenchMain.cpp:124 现场确认 `CDEConfig{}` 默认构造（JaguarCore 默认值是否等于 Rust 此组值属 JaguarCore 记录范围，SparrowCore 侧仅登记接线缺失）。判定维持。
