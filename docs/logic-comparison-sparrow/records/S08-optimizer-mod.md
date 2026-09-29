# S08. mod.rs 逐文件逻辑比对
**Rust**: `CodeResource/sparrow/src/optimizer/mod.rs`（122 行）
**C++**: `Include/SparrowCore/Optimizer/SparrowOptimizer.h`（30 行）+ `Source/Optimizer/SparrowOptimizer.cpp`（250 行）+ `SppLbfOptimizer.h/.cpp`（22+174 行）+ `BppLbfOptimizer.h/.cpp`（22+192 行）+ `SparrowLbfBuilder.h/.cpp`（12+230 行）—— 一组内分节
**判定**: 基本等价（optimize 的三阶段编排【构造→探索→压缩】、子 rng 派生序、终止器相位超时、minimum_strip_width 公式逐项复现并被 PhaseReference.tsv 锁定；SppLbfOptimizer/SparrowLbfBuilder 与 lbf.rs 构造流程等价；BppLbfOptimizer 为 C++ 额外的 BPP 变体；差异：Rust 返回可行解链而 C++ 返回最终解+指标、每阶段 separator 复用方式、以及 BPP 的 try-open-then-new-bin 顺序语义）

## 第一节. `optimize`（mod.rs:28-81）vs `SparrowOptimizer.cpp Optimize/OptimizeWithTerminator`（94-249）

### 子 rng 派生（mod.rs:37）
- C++ 对应：`SparrowRng phaseRng(config.seed)` + 三次 `NextU64()`（SparrowOptimizer.cpp:110,114,135-136）。
- 判定：一致（相位序已对齐）。
- 证据与说明：Rust `next_rng = || Xoshiro256PlusPlus::seed_from_u64(rng.next_u64())`，消耗序 = **构造**（:42）→ **探索**（:57）→ **压缩**（:66）。C++ `phaseRng` 从根 seed 派生，消耗序同：`constructorConfig.seed = phaseRng.NextU64()`（:114，仅无 warmStart 时）、`explorationSeed = phaseRng.NextU64()`（:135）、`compressionSeed = phaseRng.NextU64()`（:136），且代码注释（:107-109）明确"derive independent child streams from one root stream in the order construction, exploration, compression"。PhaseReference.tsv 头部注释"after root-RNG child-stream derivation"表明该对齐已被回归锁定。细节差异：Rust 的根 rng 本身是调用方传入的已初始化 Xoshiro（种子已消耗一次 seed_from_u64 的流推进）；C++ 根 `phaseRng(config.seed)` 从原始 seed 起步——若调用方种子相同，两者的"根状态"不同（Rust 根状态=调用方 rng 的当前流位置，C++=seed 直初始化），但派生序一致且 fixture 以 C++ 形态锁定。

### 初始解分支（mod.rs:40-52）
- C++ 对应：`MakeWarmStart`/`ConstructSparrowInitialSolution`（SparrowOptimizer.cpp:116-123,28-88）。
- 判定：基本等价（warm start 的校验深度不同）。
- 证据与说明：
  - None 分支：Rust `LBFBuilder::new(instance, next_rng(), LBF_SAMPLE_CONFIG).construct()?`（:42）→ C++ `ConstructSparrowInitialSolution(instance, constructorConfig)`（:118，构造流程=LbfBuilder，见第三节）。
  - Some 分支：Rust `SPProblem::new(instance).expect(...); prob.restore(init_sol)`（:47-49）——**无校验**。C++ `MakeWarmStart`：Restore + remainingDemand==0 检查（:51-56）+ `ValidateSPSolution`（:57-62）+ layout 可行性（:63-75），任一失败报 Infeasible。判定：差异（C++ 额外校验，属防御；Rust 假定调用方给合法解）。
  - C++ 额外：构造成功的 `OnProgress/OnImprovement`（CONSTRUCTION 相位，:124-133）。

### 探索阶段（mod.rs:55-59）
- C++ 对应：SparrowOptimizer.cpp:138-156。
- 判定：基本等价（相位报告与超时一致；separator 复用差异如下）。
- 证据与说明：Rust `report_phase(Exploration)`（:55，listener.rs:7 默认空实现）→ C++ `OnPhase(OPTIMIZATION_PHASE_EXPLORATION)`（:138-140）；`terminator.new_timeout(expl_config.time_limit)`（:56）→ `terminator.StartPhase(config.exploration.timeLimit)`（:141）；`Separator::new(start_prob, next_rng(), expl_config.separator_config)`（:57）→ C++ **不建 separator**——`Explore(instance, constructed.solution, config.exploration, explorationSeed, ...)` 内部自建（Exploration.cpp:656-673，S06）。判定：差异（形态：Rust 的探索 separator 由 optimize 持有并在压缩时**移交 prob**（`expl_separator.prob` move 给压缩 separator，:64-65）；C++ Explore/Compress 各自从 initialSolution 重建 problem+separator。行为等价——Rust 的移交=prob 状态传递，C++ 用序列化解传递；但注意 Rust 传给压缩阶段的是 `expl_separator.prob`【分离器末态】而 `final_explore_sol` 是解集合的 last（:59）——两者一致因 explore 尾部 sep 停在最优快照【S04 差异 #3 的讨论：Rust separate 返回后 sep 内态可能停在轮末态，但 explore 循环每次成功/失败分支都 rollback/change_strip_width 重置，最后一轮成功后 sep 停在收缩后的新宽度+最优解——即 final_explore_sol 与 expl_separator.prob 同步】。C++ 传 `explored.Value().solution`（=bestSolution，:161-165）：一致）。`solutions.into_iter().last().unwrap()`（:59）→ `explored.Value().solution`（S06 差异 #1）。
  - C++ 额外：探索 history 拼接（:154-156）。

### 压缩阶段（mod.rs:61-75）
- C++ 对应：SparrowOptimizer.cpp:157-175。
- 判定：一致（相位/超时/种子序同上）。
- 证据与说明：`report_phase(Compression)` → `OnPhase(…COMPRESSION)`（:157-159）；`new_timeout(cmpr_config.time_limit)` → `StartPhase(...)`（:160）；`compression_phase(&mut cmpr_separator, &final_explore_sol, ...)` → `Compress(instance, explored.Value().solution, ...)`（:161-167）；history 拼接（:173-175，C++ 额外）。

### 最终报告与返回（mod.rs:77-80）
- C++ 对应：SparrowOptimizer.cpp:177-228。
- 判定：基本等价（C++ 额外最终校验）。
- 证据与说明：Rust `report(ReportType::Final, &cmpr_sol)`（:77）→ C++ `PublishSolutionReport(OPTIMIZATION_REPORT_FINAL, COMPLETE 相位)`（:217-226）+ `OnProgress(COMPLETE)`（:209-216，C++ 额外）。C++ 额外：**最终解合法性双重校验**（Restore + ValidateSPSolution + layout.IsFeasible，:177-204，失败报 Infeasible "produced an invalid final solution"）——Rust 无最终校验（信任构造不变式）。返回：Rust `Ok(cmpr_sol)`（SPSolution）→ C++ `SparrowOptimizationResult{solution, metrics, history}`（:227-228，metrics.sampleCount 取构造阶段计数 :208，reject/exact 取两阶段聚合，S06/S07）。
- C++ 额外：无终止器重载入口 `Optimize(instance, config, listener)`（:232-240，自建 terminator+Start）——Rust 侧对应 bench/cli 的用法；既定差异：Rust `Terminator` 是 trait（util/terminator.rs:5-14），C++ 是具体类（OptimizationTerminator.h:22-40，S20）。

### `minimum_strip_width`（mod.rs:85-122）
- C++ 对应：`Exploration.cpp:57-131` 与 `Compression.cpp:56-127` 各持一份相同副本。
- 判定：一致（公式逐项复现）。
- 证据与说明：
  1. 面积下界：Rust `area=Σ(shape.area * qty)`（f64 累加，:89-94）、`min_width = (area / container.height()) as f32 * (1.0 - REL_TOL)`（:95，REL_TOL=1e-6）→ C++ `totalArea`（f64）、`minimumWidth = totalArea / containerBounds.Height() * (1.0 - 1e-6)`（Exploration.cpp:71-79）：一致（Rust 中间转 f32 后乘容差、C++ 全 f64——舍入级差，属既定差异方向【C++ 更宽域】；`.Height()` 分母同）。
  2. 离散旋转宽度下界：Rust 遍历 `None→[0.0]`、`Discrete(rotations)`、`Continuous→continue`（:97-103）；`tolerance = diameter * REL_TOL`（:104）；逐角 transform 取 bbox，`bbox.height() <= container.height() + tolerance` 时折叠 `min((bbox.width() - tolerance).max(0.0))`（:106-118）→ C++ 逐项对应（Exploration.cpp:81-123）：rotations 构造（None→{0}、Discrete→radians、Continuous continue，:83-98）、tolerance、`TransformedRustF32(FromRotationTranslation(angle,{0,0}))`（:108-109，对应 Rust 的 `Transformation::from_rotation` 矩阵变换）、`bounds.Height() <= containerHeight + tolerance` → `min(max(0, Width()-tolerance))`（:112-121）：一致（含 f32 旋转的 `transform_from` → `TransformedRustF32` 位级对应——这正是任务背景提示的"长变换链 FollowedByRustF32 调用路径"之一：**此处 C++ 确实用 TransformedRustF32（f32 位级）**，与 Rust 的 SPolygon::transform_from 精度对齐）。
  3. inset：Rust `min_width + width_inset`（strip_width - container.width()，:88,121）→ C++ `minimumWidth + (StripWidth() - containerBounds.Width())`（:128-129）：一致（C++ 加 `isfinite&&>0` 否则返 0 的防御）。
  4. Rust 注释 "ponytail: continuous rotations use only the area bound"（:101）→ C++ 同跳过 Continuous：一致。
  5. 差异（实现形态）：Rust 复用 `shape_buffer` 单缓冲反复 transform_from（:105-113）；C++ 每角新建变换结果：性能形态，无行为差。**双侧副本**：C++ 该函数在 Exploration.cpp 与 Compression.cpp 各一份（重复代码，维护成本项，非行为差异）。

## 第二节. `SppLbfOptimizer`（SppLbfOptimizer.cpp:34-173）vs lbf.rs 构造流程
- 判定：基本等价（与 SparrowLbfBuilder 同构的构造循环，差异：排序键可选、无宽度上限判定、sampleCount 口径含 localSearchFraction）。
- 证据与说明：
  1. **排序键**：`config.useSparrowConstructorOrder`（默认 true）→ `ConvexHullArea()*Diameter()`；false → 仅 `Diameter()`（SppLbfOptimizer.cpp:58-60）。判定：差异（C++ 额外的可选排序模式；Rust lbf.rs:58-63 只有 ch_area*diameter 一种。`useSparrowConstructorOrder=false` 路径为 jagua-rs 老版 lbf 的直径排序兼容模式）。
  2. **放置循环**：外层 order、内层 `while (RemainingDemand > 0)`（:87-89）= Rust 展开序（S03 条目 2）。每轮 Save+Restore layout 视图（:91-97）、质量 filter（:99-107）、`SearchLbf`（:109-114）、nullopt→宽度增长 `StripWidth() * widthGrowthRatio`（默认 1.1！LbfConfig.h:24）+ 尝试上限 32（:125-136）、成功→PlaceItem+sampleCount（:139-145）。
  3. **与 SparrowLbfBuilder 的差别**：a) **无 `2×Σ直径` 宽度上限**（只查 32 次尝试上限）——Rust lbf.rs:104-111 有该上限；b) **宽度增长比默认 1.1 vs SparrowLbfBuilder 的 1.2**（LbfConfig.h:24 vs SparrowConstructorConfig widthGrowthRatio=1.2，LbfConfig.h:75）——两个 C++ 入口的默认不同，Rust 只有 1.2；c) `localSearchFraction` 参与 sampleCount 统计（:121-124，失败一轮计 `samplesPerItem - floor(samplesPerItem*localSearchFraction)`）——Rust 无此统计口径（n_evals 直接累计）。判定：差异（统计口径；`localSearchFraction` 默认 0.2 时失败轮计 0.8×5000=4000，而实际评估数未必——观测数据偏差，非行为差）。
  4. **收尾**：`PlacedItemCount() != TotalDemand → Infeasible`（:149-152，C++ 额外防御）、`FitStrip`（:153-157）、metrics+Validate（:159-172）。fixture：`SppLbfCppReference.tsv`（samples=738, width=4.0028257）与 `LbfMetricsReference.tsv` 的 rust 行（samples=128, width=4.276253——jagua-rs 老 lbf 参考，C++ 用自身参考）锁定该入口。
  5. **归属判定**：SppLbfOptimizer 是**独立的公共求解器入口**（`SolveSppLbf`，仅 SPP 构造阶段、无探索/压缩）；Rust sparrow 的对应物是 lbf.rs 的 LBFBuilder（被 optimize 内嵌使用）+ jagua-rs 的独立 lbf crate（CodeReference 意义上的祖先）。SparrowLbfBuilder 才是 optimize 路径的严格对应。

## 第三节. `SparrowLbfBuilder`（SparrowLbfBuilder.cpp:93-229）vs lbf.rs
- 判定：一致（S03 已逐条比对：排序/展开/1.2 增长/2×Σ 上限/尝试上限【C++ 额外】/FitStrip/metrics）。
- 证据与说明：构造键恒为 `ConvexHullArea()*Diameter()`（:119）、宽度上限 `Σ(diameter*demand) × widthCeilingFactor(2.0)`（:125,135）、增长比 1.2（SparrowConstructorConfig 默认）、`MakeSearchConfig` 把 `samplesPerItem` 覆写为 `nContainerSamples=1000`（:37）——与 Rust `LBF_SAMPLE_CONFIG` 一致。fixture `SparrowLbfRustReference.tsv`（`# generator=Rust LBFBuilder`，width=2.001098871 位级）锁定。C++ 额外：feasible 双重校验（ValidateSPSolution+layout.IsFeasible，:215-227）。

## 第四节. `BppLbfOptimizer`（BppLbfOptimizer.cpp:27-191）—— C++ 额外（Rust sparrow 无 BPP 变体）
- 判定：C++ 额外。
- 证据与说明：Rust sparrow 全 crate 只有 SPP（`probs::spp`，mod.rs/lib.rs 无 bpp 引用）；BPP 支撑来自 jagua-rs 的 bpp crate（JaguarCore 侧 `Problems/Bpp.h`，比对记录 29）。C++ `SolveBppLbf`：同 LbfConfig、同排序（useSparrowConstructorOrder 二态，:52-54）、**try-open-layouts-first**：先遍历已打开 layouts（`current.layouts`，Open 目标，:86-122），全失败再遍历有剩余库存的 bin（Closed 目标，:124-162），每目标跑 `SearchLbf`（沿用同一 `rng`【顺序消耗】），仍失败则 `break`（跳出该 item 的 while——**静默放弃该项**，:164-167，后续 metrics.remainingDemand 反映）。无宽度增长（BPP 无条带）、无 Infeasible 返回（只要 metrics 可行性标志）。sampleCount 口径：命中计 `samples`、未命中计 `samplesPerItem`（:109-111,148-150）。fixture `BppLbfCppReference.tsv`（`# generated from TestBppLbf.cpp seed=42`，C++ 自参考）与 `LbfMetricsReference.tsv` 的 bpp 行（rust 参考 cost/width 口径）。判定：整体为 C++ 额外的 BPP 构造求解器，行为模板【排序+逐需求+搜索+首适配式布局选择】与 SPP 侧同源，但其"静默放弃"语义（break 后继续下一 item）在 SPP 侧对应物中不存在（SPP 失败必须报 Infeasible）。

## 差异汇总表
| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 三阶段编排与子 rng 派生序（构造→探索→压缩） | 一致 | PhaseReference.tsv（含 optimize 行）锁定 | mod.rs:37-75 vs SparrowOptimizer.cpp:110-167 |
| 2 | warm start 无校验 vs Restore+demand+validate+feasible 四重校验 | 差异（防御） | 合法输入一致 | mod.rs:45-51 vs SparrowOptimizer.cpp:28-88 |
| 3 | 阶段间 separator：Rust 持有并 move prob vs Explore/Compress 各自重建 | 差异（形态） | 行为等价（解内容传递）；C++ 多两次 problem 重建 | mod.rs:57,64-67 vs SparrowOptimizer.cpp:142-143,161, Exploration.cpp:656-673 |
| 4 | 最终解双重校验（ValidateSPSolution+IsFeasible） | C++额外 | 失败报 Infeasible；Rust 无最终校验 | mod.rs:77-80 vs SparrowOptimizer.cpp:177-204 |
| 5 | minimum_strip_width 逐项公式（面积/离散旋转/容差/inset） | 一致 | 含 TransformedRustF32 的 f32 位级对应；C++ 在两文件持重复副本 | mod.rs:85-122 vs Exploration.cpp:57-131 = Compression.cpp:56-127 |
| 6 | SppLbfOptimizer：排序键二态 + 无 2×Σ 上限 + 增长比默认 1.1 | 差异 | 独立入口，非 optimize 路径；1.1≠Rust 1.2，且无 Rust 的宽度上限 | lbf.rs:58-111 vs SppLbfOptimizer.cpp:52-136 + LbfConfig.h:24 |
| 7 | sampleCount 统计口径（localSearchFraction 折算） | 差异（观测） | 失败轮计 0.8×samplesPerItem，与 Rust 的 n_evals 累计口径不同 | mod.rs（无）vs SppLbfOptimizer.cpp:121-124 |
| 8 | BppLbfOptimizer（BPP 构造求解器，open→new-bin 顺序） | C++额外 | Rust sparrow 只有 SPP；BPP 沿 jagua-rs bpp crate 血统，C++ 自参考 fixture | mod.rs/lib.rs（无 bpp）vs BppLbfOptimizer.cpp:27-191 |
| 9 | 返回：SPSolution vs solution+metrics+history（含构造 sampleCount 与两阶段 reject/exact 聚合） | 差异（形态） | 调用方拿到更完整的观测数据 | mod.rs:80 vs SparrowOptimizer.cpp:227-228 |
| 10 | 无终止器入口重载 Optimize(...)（自建 terminator） | C++额外 | API 便利层；Terminator trait→具体类（S20） | mod.rs:31-32 vs SparrowOptimizer.cpp:232-249 |

## 结论
optimize 的三阶段编排（含"构造→探索→压缩"的子 rng 派生序、相位报告、相位超时）与 `minimum_strip_width` 公式在 C++ `OptimizeWithTerminator` 中逐项复现——特别是 min-width 的离散旋转下界走 `TransformedRustF32` 保持了与 Rust f32 矩阵变换的位级对齐——并被 PhaseReference.tsv 的 exploration/compression/optimize 三行锁定，判定基本等价。组内四个 C++ 件的定位清楚：SparrowLbfBuilder 是 lbf.rs 构造流程的严格对应（fixture 位级锁定）；SppLbfOptimizer 是独立 SPP 构造入口（排序二态、增长比 1.1、无宽度上限——与 Rust 有实差）；BppLbfOptimizer 是 Rust sparrow 不存在的 BPP 额外变体（open-layout 优先、失败静默跳项）。最值得关注的行为差是 SppLbf 入口与 Rust 的 1.1/1.2 增长比与上限判定差，以及 optimize 返回形态从"单一解"扩展为"解+指标+历史"。
