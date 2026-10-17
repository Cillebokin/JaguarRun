# S02. consts.rs + lib.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/consts.rs`（55 行，19 个 `pub const` 声明）+ `CodeResource/sparrow/src/lib.rs`（39 行）
**C++**: 常量分散于 `SparrowCore/Include/SparrowCore/Optimizer/SparrowConfig.h`、`SeparatorWorker.h`、`Quantify/CollisionTracker.h/.cpp`、`Sampling/CoordinateDescent.h`、`Quantify/OverlapProxy.h`、`LbfConfig.h`；随机对应 `Include/SparrowCore/Random/SparrowRng.h`（29 行）+ `Source/Random/SparrowRng.cpp`（116 行）
**判定**: 基本等价（常量逐项数值一致但无集中"consts"文件；SparrowRng 为 xorshift64\* 血统的 xoshiro256++ 重实现，种子派生用 SplitMix64，与 Rust `Xoshiro256PlusPlus::seed_from_u64` 语义对齐；PItemId/LItemId 类公共类型在 sparrow 侧不存在，无需对应）

## 逐条比对（consts.rs 常量）

### `GLS_WEIGHT_MAX_INC_RATIO = 2.0` / `GLS_WEIGHT_MIN_INC_RATIO = 1.2` / `GLS_WEIGHT_DECAY = 0.95`（Rust 4-6）
- C++ 对应：`CollisionTracker::UpdateWeights()` 默认无参重载（CollisionTracker.cpp:698-740）。
- 判定：一致（常量内联；f32 运算表达式已按 Rust 对齐）。
- 证据与说明：CollisionTracker.cpp:723 `1.2F + (2.0F - 1.2F) * (entry.loss / maximumLoss)`；无碰撞项为 `0.95F`。旧 `0.8F` 字面量与 Rust 实际 f32 运算 `2.0F - 1.2F` 相差 1 ULP，已由 S18 的跨语言部分碰撞权重 reference 捕获并修复。带参重载 `UpdateWeights(collisionMultiplier, clearDecay=0.95F)`（CollisionTracker.h:65-66）是 C++ 扩展；公式与边界见 S18。

### `OVERLAP_PROXY_EPSILON_DIAM_RATIO = 0.01`（Rust 7）
- C++ 对应：`OverlapProxy.h/.cpp` 中 epsilon 处理（见 S17）。
- 判定：一致。
- 证据与说明：数值作为直径比例容差在 OverlapProxy 实现（S17 记录覆盖公式级比对）；本记录确认常量数值两侧相同（grep `OVERLAP_PROXY` 在 C++ 无同名常量，值内联于 OverlapProxy.cpp）。

### `CD_STEP_SUCCESS = 1.1` / `CD_STEP_FAIL = 0.5`（Rust 11-14）
- C++ 对应：`CoordinateDescent.h/.cpp`（见 S14）。
- 判定：一致（数值内联）。
- 证据与说明：C++ 无 `kCdStepSuccess` 命名常量，1.1/0.5 内联于 CoordinateDescent.cpp 的步长自适应逻辑（S14 记录已逐行核对公式）。

### `PRE_REFINE_CD_TL_RATIOS (0.25,0.02)` / `PRE_REFINE_CD_R_STEPS (5°,1°)` / `SND_REFINE_CD_TL_RATIOS (0.01,0.001)` / `SND_REFINE_CD_R_STEPS (0.5°,0.05°)`（Rust 17-26）
- C++ 对应：`CoordinateDescent.h` 的精化参数（见 S14）。
- 判定：一致（数值内联/拆入 LbfConfig 相邻字段）。
- 证据与说明：两轮坐标下降精化的平移比例与旋转步长在 C++ CoordinateDescent 实现中等值出现（S14 已核对）；`LbfConfig.h:19-22` 另有 `minTranslationStddevRatio=0.0005/maxTranslationStddevRatio=0.01/minRotationStddevRad=0.5°/maxRotationStddevRad=2°`——这是 LbfSearch（S03）的扰动 stddev 范围，与本组 CD 常量不同源，注意不要混淆。

### `UNIQUE_SAMPLE_THRESHOLD = 0.05`（Rust 29）
- C++ 对应：`BestSamples.h/.cpp`（见 S15）。
- 判定：一致（数值内联）。
- 证据与说明：样本去重阈值（最小维度比例 0.05）在 C++ BestSamples 实现中等值（S15 已核对）。

### `DEFAULT_EXPLORE_TIME_RATIO = 0.8` / `DEFAULT_COMPRESS_TIME_RATIO = 0.2`（Rust 31-32）
- C++ 对应：无常量。
- 判定：C++ 缺失（应用层语义，归属调用方）。
- 证据与说明：Rust 在 cli.rs:40-47/tui.rs:73-84/bench.rs:56-57 把全局时间预算按 0.8/0.2 分摊到两阶段；C++ SparrowCore 要求调用方直接给 `exploration.timeLimit`/`compression.timeLimit`（SparrowConfig.h:37/61），SparrowCli 的 `--time-ms` 则映射到 `terminator.maxDuration`（Main.cpp:163-164）而非按比例分摊——时间预算分摊语义见 S23 差异 3。

### `DEFAULT_MAX_CONSEQ_FAILS_EXPL = 10`（Rust 34）
- C++ 对应：无常量；CLI 侧字面量。
- 判定：一致（数值等价）。
- 证据与说明：SparrowCli/Main.cpp:153 `config.exploration.maxConsecutiveFailures = 10U;`（early-termination 路径），与 Rust cli.rs:54 `Some(DEFAULT_MAX_CONSEQ_FAILS_EXPL)` 数值一致；库侧默认两侧均为"不限"。

### `DEFAULT_FAIL_DECAY_RATIO_CMPR = 0.9`（Rust 36）
- C++ 对应：`CompressionConfig::failureDecay = 0.9`（SparrowConfig.h:60）。
- 判定：一致。
- 证据与说明：见 S01 差异 3（载荷外提形态差异，数值一致）。

### `LOG_LEVEL_FILTER_RELEASE = Info` / `LOG_LEVEL_FILTER_DEBUG = Debug`（Rust 38-40）
- C++ 对应：无。
- 判定：C++ 缺失（应用层，归 CLI/Demo/NestLab）。
- 证据与说明：SparrowCore 库无日志框架依赖（记录 S24 归属判定）；Rust 在 cli.rs:31-34/tui.rs:26-29 按 `cfg!(debug_assertions)` 选择。

### `DRAW_OPTIONS`（SvgDrawOptions，Rust 42-49）
- C++ 对应：`jaguar::SvgExportOptions`（JaguarCore 侧，记录 36）。
- 判定：差异（选项集不同，属 JaguarCore 层）。
- 证据与说明：Rust `{GRAY 主题, quadtree:false, surrogate:false, highlight_collisions:true, draw_cd_shapes:false, highlight_cd_shapes:true}`；C++ SparrowCli 用 `svgOptions.title="SparrowCore Layout"; drawSurrogates=true`（Main.cpp:233-235）——C++ 导出打开 surrogate 绘制而 Rust 关闭，且无 quadtree/cd_shapes 选项（JaguarCore 记录 36 已记录选项差异）。SparrowCore 侧仅此一处接线。

### `LBF_SAMPLE_CONFIG = {1000, 0, 3}`（Rust 51-55）
- C++ 对应：`SparrowConstructorConfig` 默认值（LbfConfig.h:71-73）。
- 判定：一致。
- 证据与说明：`nContainerSamples=1000/nFocussedSamples=0/nCoordinateDescents=3` 逐项相同；Rust 传给 `LBFBuilder::new`（bench.rs:88），C++ 作为构造配置默认值（SparrowOptimizer 路径）或 CLI `--samples` 覆盖（Main.cpp:150）。

## 逐条比对（lib.rs）

### 模块声明 `optimizer/quantify/sample/util/config/eval/consts`（Rust 9-16）
- C++ 对应：目录结构 `Optimizer/Quantify/Sampling/Eval`（Include/SparrowCore/*）。
- 判定：一致（组织映射）。
- 证据与说明：`sample`→`Sampling`、`quantify`→`Quantify`、`eval`→`Eval`；util 层的 terminator/listener→`Optimizer/Optimization{Terminator,Listener}`（S20/S21），io/svg→委托 JaguarCore（S22）。

### `pub static EPOCH: LazyLock<Instant>`（Rust 17）
- C++ 对应：无。
- 判定：C++ 缺失（应用层）。
- 证据与说明：EPOCH 用于日志时间戳（io.rs:76-79、tui/logging.rs:37）与导出元数据 `jagua_rs::probs::spp::io::export(&solution, *EPOCH)`（cli.rs:122）；C++ 日志/导出时间由 CLI/Demo 承担（Main.cpp 无 EPOCH 概念）。

### `static FMT: fn() -> Formatter`（numfmt 3 位有效数字短刻度，Rust 19-23）
- C++ 对应：无。
- 判定：C++ 缺失（TUI 层）。
- 证据与说明：仅用于 dashboard 的吞吐率格式化（dashboard.rs:616-622 复刻了同一 Formatter），归 S24。

### `EXPORT_LIVE_SVG`/`EXPORT_ONLY_FINAL_SVG` 特性开关（Rust 26-39，互斥编译期检查）
- C++ 对应：无。
- 判定：C++ 缺失（应用层开关）。
- 证据与说明：Rust 用 cargo feature 控制 SVG 导出粒度（cli.rs:90-98）；C++ SparrowCore 是库，SVG 导出由 CLI 的 `--svg` 显式开关（Main.cpp:222）承担，无编译期特性。

### 公共类型 PItemId/LItemId
- 判定：不适用（sparrow 侧无此类型）。
- 证据与说明：经全文检索，sparrow 的 lib.rs/consts.rs 不定义 PItemId/LItemId 等公共 ID 类型——ID 类型在 jagua-rs 侧（`jagua_rs::entities`，JaguarCore 记录 23/25 已覆盖）。本组无对应项。

## 逐条比对（SparrowRng vs Rust rand）

### 类（SparrowRng.h:10-28）
- Rust 对应：调用方使用 `rand::rngs::Xoshiro256PlusPlus`（cli.rs:17/65、bench.rs:19/47、tui.rs:34）。
- 判定：C++ 额外（库内自足 RNG；Rust sparrow 直接用 rand crate）。
- 证据与说明：Rust 侧 RNG 是外部依赖 `Xoshiro256PlusPlus::seed_from_u64(seed)`；C++ 把等价算法内置为公共 API `SparrowRng`。

### 构造 `SparrowRng(seed)`（cpp:32-39）
- Rust 对应：`Xoshiro256PlusPlus::seed_from_u64`。
- 判定：一致。
- 证据与说明：C++ 用 SplitMix64 迭代 4 次填 256bit 状态（cpp:15-22,34-38）——与 rand 0.9/0.10 `SeedableRng::seed_from_u64` 对 Xoshiro 家族的标准播撒方式（SplitMix64）一致。

### `NextU64()`（cpp:41-54）
- Rust 对应：`Xoshiro256PlusPlus::next_u64`。
- 判定：一致。
- 证据与说明：xoshiro256++ 标准步进：`rotl(s0+s3,23)+s0` 输出，状态更新 `s2^=s0; s3^=s1; s1^=s2; s0^=s3; s2^=t(=s1<<17); s3=rotl(s3,45)`——与 rand_xoshiro 参考实现逐句一致。

### `NextU32()`（cpp:56-59）
- Rust 对应：`next_u32`。
- 判定：一致。
- 证据与说明：取 `NextU64() >> 32` 高 32 位，rand_xoshiro 的 u32 输出同为 u64 高半字。

### `UniformIndex(upperExclusive)`（h:18，cpp:61-82）
- Rust 对应：`Rng::random_range(0..n)` / rand 0.10 的 u32 区间映射。
- 判定：有效正上界的映射一致；零上界失败类别已通过语言机制映射。
- 证据与说明：对 `upperExclusive > 0`，Lemire 乘法映射 `(u32 * n) >> 32` + 拒绝进位修正与 rand 0.10 的 u32 区间映射对应，并由正 bound fixture 覆盖。`upperExclusive==0` 时 Rust `random_range(0..0)` panic；C++ 现在在调用 RNG 前抛 `std::invalid_argument`，不再返回 0。Rust/C++ 测试均比较失败前后 RNG 状态和后续输出。包裹它的 sampler 构造错误仍由 S16 单独核对。

### `Uniform01F32()`（cpp:83-87）
- Rust 对应：`Rng::random::<f32>()`（Standard）。
- 判定：一致。
- 证据与说明：`u32 >> 9` 取高 23 位 / 2^23（cpp:85-86），即 [0,1) 的标准 f32 采样（rand 对 f32 的 Standard 实现同为 23 位尾数缩放）。`LbfSeed42Reference.tsv` 由 Rust `rng_reference.rs` 直接生成，TestRng.cpp:132-160 对同一 seed 的 4 个 `NextU64` 与 4 个 `Uniform01F32` 原始位模式逐值断言；这验证列明输出，不比较 RNG 内部状态。

### `Normal01F32()`（cpp:89-110）
- Rust 对应：Sparrow Rust 没有此 helper 的对应调用。探索池的 Rust `rand_distr::Normal` 由 C++ `Exploration.cpp` 的 `RustStandardNormalF64` 路径承接（S06）；本 helper 只由额外的 `LocalSearchSampler` 使用。
- 判定：C++ 额外。
- 证据与说明：C++ helper 是三角 Box–Muller：`sqrt(-2 ln u1)`、`2πu2`、cos 返回/sin spare 缓存（cpp:91-109），不是 Marsaglia 极坐标拒绝算法。TestRng 的此项检查 C++ 同种子实例间的确定性，不是 Rust/C++ 差分测试，也不证明与 Rust `StandardNormal` 同序列。

### `State()`（cpp:112-115）
- 判定：C++ 额外（可观察状态，用于测试/断言）。
- 证据与说明：返回 4×u64 状态副本；Rust Xoshiro 无公共状态导出（测试用 `from_seed`）。`TestRng.cpp:258,285,291,318` 用它核对 C++ 同种子实例/状态的一致性，不是 Rust/C++ 状态差分；跨语言 RNG 输出 fixture 在 `TestRng.cpp:132-160`，只验证列明输出，不比较内部状态。

### 种子派生链（SparrowOptimizer.cpp:115-139）
- Rust 对应：optimizer/mod.rs 的子流派生。
- 判定：显式有效 seed 下顺序一致（S08 记录核对）；缺省 seed 入口已恢复非确定性语义。
- 证据与说明：C++ 用 optional seed 或生成的 `effectiveSeed` 构造根 `SparrowRng`，cold-start 依次 `NextU64()` 派生 construction、exploration、compression 子 seed；warm-start 按 Rust 不消耗 construction RNG 的顺序派生 Explore/Compression。无 seed 运行把 `effectiveSeed` 写入 metrics，并通过 CLI stderr 输出；固定该 seed 重跑可复现。随机熵源本身按语言实现，不比较彼此独立的熵输出。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `DEFAULT_EXPLORE_TIME_RATIO/COMPRESS_TIME_RATIO`（0.8/0.2） | C++缺失 | 全局时间→两阶段分摊逻辑归调用方；SparrowCli 不做比例分摊 | consts.rs:31-32 vs SparrowConfig.h:37/61、Main.cpp:163 |
| 2 | `LOG_LEVEL_FILTER_*` | C++缺失 | 日志分级归应用层 | consts.rs:38-40 vs（无） |
| 3 | `DRAW_OPTIONS` vs SvgExportOptions | 差异 | C++ 默认 drawSurrogates=true（Rust false）；选项集不同属 JaguarCore 记录 36 | consts.rs:42-49 vs Main.cpp:233-235 |
| 4 | `EPOCH`/`FMT`/SVG 特性开关 | C++缺失 | 应用层设施（日志时间戳、numfmt、live/final-svg 编译开关） | lib.rs:17-39 vs（无） |
| 5 | `SparrowRng` 内置 RNG 类及额外 `Normal01F32` helper | C++额外 | Xoshiro/SplitMix64/整数区间映射有 Rust reference 覆盖；Box–Muller helper 仅供 C++ LocalSearchSampler，不属于 Rust parity | SparrowRng.h:10-28; SparrowRng.cpp:89-110 vs cli.rs:17 |
| 6 | PItemId/LItemId 等公共类型 | 不适用 | sparrow 侧不定义此类 ID 类型（属 jagua-rs↔JaguarCore） | lib.rs 全文 vs（无） |
| 7 | `UniformIndex(0)` vs Rust `random_range(0..0)` | 已对齐（错误类别/副作用） | Rust panic 对应 C++ `invalid_argument`；两边都在状态消耗前失败。语言出口不同，稳定分类一致 | SparrowRng.cpp:61-67; `rng_reference.rs::empty_uniform_range_panics_without_consuming_rng`; `TestRng.cpp::TestUniformIndexRejectsEmptyRangeWithoutConsumingRng` |

## 结论
consts.rs 的 19 个 `pub const` 中，核心 GLS/CD/LBF 等算法常量在 C++ 对应实现中等值出现；应用层时间比例、日志级别、SVG 开关与 EPOCH 由调用方/宿主承担。SparrowRng 的 Xoshiro256++、SplitMix64 播种、有效 u32 区间映射、零区间失败和 f32 均匀采样按 Rust reference 核对；空区间的 panic/exception 类型不同，但失败前 RNG 状态一致。`Normal01F32` 是 C++ 额外 Box–Muller helper，不能纳入 Rust parity 结论。Rust 探索正态的对应路径单列于 S06。

### P0 闭合记录（2026-10-09）

`UniformIndex(0)` 的 Rust/C++ 对照用例已在 Debug/Release 全量套件通过：Rust `catch_unwind` 捕获空 range panic 后，下一 `next_u64()` 与未触发 panic 的参考 RNG 相同；C++ 捕获 `invalid_argument` 后，状态快照和下一 `NextU64()` 与参考 RNG 相同。固定 seed 的既有 RNG/整数映射 fixture 仍通过。该结果只证明 RNG primitive 的错误时点与副作用，不覆盖 S16 上层构造器如何把无效旋转/空候选转换成错误。

### 补充审计（2026-09-29）
本审计对 consts.rs 的**19 个 `pub const` 声明**与 lib.rs 39 行的**全部声明**做逐项覆盖复核（主文已逐条有记录），现场补验两处：

1. **【浅覆盖补全：`PRE_REFINE_CD_TL_RATIOS` 等 4 组常量的 C++ 落点行号】** 主文只说"数值内联/拆入 LbfConfig 相邻字段"。现场确认实际落点：两轮 CD 精化的 8 个数值（0.25/0.02、5°/1°、0.01/0.001、0.5°/0.05°）内联于 `LbfSearch.cpp:31-63` 与 `SeparatorWorker.cpp:21-53` 的 `MakeRefinementConfig`（两处同构副本，S13 差异已记双副本），不在 LbfConfig.h——LbfConfig.h:19-22 的 stddev 字段属 LbfSearch 扰动范围（主文已警示不要混淆，判定维持）。全部 8 数值与 consts.rs:17-26 逐项一致，无遗漏常量。
2. **【浅覆盖补全：`OVERLAP_PROXY_EPSILON_DIAM_RATIO=0.01` 落点】** 主文说"值内联于 OverlapProxy.cpp"。现场确认：OverlapProxy.cpp:155-157 `epsilonDiameterRatio = 0.01F` 硬编码（无命名常量）；另外 UniformRectSampler 侧的 `RotEntry` 无关。常量值双侧一致。
3. **【浅覆盖补全：lib.rs 的 `pub use` 面（任务指定核对项）】** 现场核验：**lib.rs 不含任何 `pub use` 再导出**（全文仅 `pub mod` ×7 + EPOCH/FMT/特性开关）；sparrow 的公共导出面=各模块自身可见性（`quantify::mod` 的 `pub mod overlap_proxy/tracker/simd` + `mod pair_matrix` 私有 + 3 个 `pub fn`；`sample::mod` 的 oracle-trace 门控 `pub use coord_descent::{CDConfig, refine_coord_desc}`（feature=oracle-trace 时，sample/mod.rs:4）；`eval::mod` 的 `mod collision_loss` 私有 + `pub mod` ×3；`optimizer::mod` 的 `mod worker` 私有）。C++ 头文件导出面对照：PairMatrix 在 C++ 升为公共（S17 差异已记）；CDConfig/RefineCoordinateDescent 在 C++ 公共（CoordinateDescent.h）；`CollisionLossEvaluator` 双侧私有（C++ 摊进 SeparationEvaluator，S12）；`SeparatorWorker` 双侧私有（C++ 为自由函数 FindBestSeparatorMove，S05）——**逐项对齐，无未记录的导出面差异**。
4. **【核对确认：19 个声明清单完整】** GLS×3、EPSILON_DIAM×1、CD_STEP×2、REFINE 组×4、UNIQUE_THRESHOLD×1、TIME_RATIO×2、MAX_CONSEQ_FAILS×1、FAIL_DECAY×1、LOG_FILTER×2、DRAW_OPTIONS×1、LBF_SAMPLE_CONFIG×1——合计 19 个声明，主文 11 个小节全覆盖。
5. **【核对确认：SparrowRng 数学】** 抽验 `UniformIndex`（SparrowRng.cpp:61-81）Lemire+拒绝修正与 `NextU64` rotl(s0+s3,23)+s0 与 rand_xoshiro 参考一致，判定维持"一致"。

### 补充复核（2026-10-17）：常量字段与实际 RNG 调用组合

#### `consts.rs` 的 19 个常量逐字段复核

| Rust 字段 | C++ 落点 / 对应值 | 复核结论 |
|---|---|---|
| `GLS_WEIGHT_MAX_INC_RATIO = 2.0` | `CollisionTracker.cpp:771`，倍率表达式上界 `2.0F` | 一致 |
| `GLS_WEIGHT_MIN_INC_RATIO = 1.2` | `CollisionTracker.cpp:771`，倍率表达式下界 `1.2F` | 一致，表达式运算顺序另由 S18 fixture 锁定 |
| `GLS_WEIGHT_DECAY = 0.95` | `CollisionTracker.cpp:770`，无损失项倍率 `0.95F` | 一致 |
| `OVERLAP_PROXY_EPSILON_DIAM_RATIO = 0.01` | `OverlapProxy.cpp:118-120`，直径比例 `0.01F` | 一致，C++ 内联为局部常量 |
| `CD_STEP_SUCCESS = 1.1` | `CoordinateDescent.cpp:229` | 一致 |
| `CD_STEP_FAIL = 0.5` | `CoordinateDescent.cpp:229` | 一致 |
| `PRE_REFINE_CD_TL_RATIOS = (0.25, 0.02)` | `LbfSearch.cpp:51-52`、`SeparatorWorker.cpp:41-42` | 一致；C++ 有两处同构配置副本 |
| `PRE_REFINE_CD_R_STEPS = (5°, 1°)` | `LbfSearch.cpp:53-54`、`SeparatorWorker.cpp:43-44` | 一致，按 Rust f32 degree-to-radian helper 换算 |
| `SND_REFINE_CD_TL_RATIOS = (0.01, 0.001)` | `LbfSearch.cpp:44-45`、`SeparatorWorker.cpp:34-35` | 一致；C++ 有两处同构配置副本 |
| `SND_REFINE_CD_R_STEPS = (0.5°, 0.05°)` | `LbfSearch.cpp:46-47`、`SeparatorWorker.cpp:36-37` | 一致，按 Rust f32 degree-to-radian helper 换算 |
| `UNIQUE_SAMPLE_THRESHOLD = 0.05` | `LbfSearch.cpp:172`、`SeparatorWorker.cpp:156` | 一致 |
| `DEFAULT_EXPLORE_TIME_RATIO = 0.8` | C++ SparrowCore 无对应比例常量 | C++ 缺失；时间预算分配属于宿主入口，见 S23 |
| `DEFAULT_COMPRESS_TIME_RATIO = 0.2` | C++ SparrowCore 无对应比例常量 | C++ 缺失；时间预算分配属于宿主入口，见 S23 |
| `DEFAULT_MAX_CONSEQ_FAILS_EXPL = 10` | `SparrowCli/Main.cpp:154` 仅在 early-termination 路径设为 `10U` | 数值一致；两侧库默认均不限连续失败 |
| `DEFAULT_FAIL_DECAY_RATIO_CMPR = 0.9` | `SparrowConfig.h:62`，`failureDecay = 0.9` | 一致 |
| `LOG_LEVEL_FILTER_RELEASE = Info` | SparrowCore 无日志过滤器 | C++ 缺失；应用层日志设施，见 S22/S24 |
| `LOG_LEVEL_FILTER_DEBUG = Debug` | SparrowCore 无日志过滤器 | C++ 缺失；应用层日志设施，见 S22/S24 |
| `DRAW_OPTIONS` | SparrowCli `Main.cpp:233-235` 的 `SvgExportOptions` 接线 | 有意记录的选项差异：C++ 打开 surrogate 绘制，选项集由 JaguarCore 承担，见 JaguarCore 记录 36 |
| `LBF_SAMPLE_CONFIG = {1000, 0, 3}` | `LbfConfig.h:67-69` 的 Sparrow constructor 默认值 | 三字段一致 |

复核结果：19 个声明均有映射、宿主层归属或明确差异；没有发现未记录的算法常量数值漂移。`lib.rs` 现仍为 7 个 `pub mod`，无根级 `pub use`；另有公开 `EPOCH`、两组 feature-controlled SVG bool，`FMT` 为私有静态项。模块内部导出差异仍按 S05/S12/S17 等记录处理。

#### rand 0.10.3 的实际调用点组合

| Rust 调用点 | C++ 对应 | 已锁定的组合与边界 |
|---|---|---|
| `worker.rs:43` `SliceRandom::shuffle` | `Separator.cpp:88-145` `Shuffle` | 新增 `SparrowSliceShuffleRustReference.tsv`：seed `0/1/42/u64::MAX` × 长度 `0..=32` 共 132 组；逐项比排列并比对 shuffle 后 4 个 `u64`。包含空/单项零消耗、常规混洗及长度 13 起的 `IncreasingUniform` 随机块补取 |
| `uniform_sampler.rs:81` `SliceRandom::choose` | `UniformRectSampler.cpp` 的旋转项 `UniformIndex` | S16 singleton、窄区间和离散 reference 覆盖既有路径；连续 sampler 的 16 个角 fixture 现逐项比较实际 `sample()` 后 4 个 `u64`，锁定多项 `choose` 与后续坐标采样共同消耗的 RNG 序 |
| `explore.rs:386/397/401` `IteratorRandom::choose` | `Exploration.cpp:236-320, 323-337` `RustCoinFlipper` | 锁定 rand 0.10.3 `Filter` iterator 的 reservoir/CoinFlipper 路径；S06 的 actual Explore trace 覆盖第二选择池 1–10 个候选、空 distinct-pool fallback 与空 large-pool 失败出口，并比较 swap 结果及后续 RNG trace |

新增 Rust 测试 `slice_shuffle_reference.rs` 校验 fixture 由锁定的 Rust RNG 实际生成；C++ `TestRng.cpp::TestSliceShuffleRustReference` 调用生产 shuffle 算法的测试入口，逐组比较排列及后续 RNG 输出。S16 连续 sampler fixture 同时由 Rust/C++ 比较 16 个实际旋转结果及每次采样后的 4 个 `u64`，其中采样器的 `SliceRandom::choose` 处理多项旋转候选。Debug/Release RustReference 全量套件和 SparrowCoreTests 全量程序均通过。

本次收口覆盖的是 Sparrow 当前实际 RNG 调用点和表中固定 seed/长度/候选组合；结论不外推到任意外部迭代器 size hint、`usize` 极大长度或未列出的 Explore 几何轨迹。
