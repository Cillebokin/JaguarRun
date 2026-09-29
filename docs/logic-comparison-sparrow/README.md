# sparrow ↔ SparrowCore 逐文件逻辑比对记录

比对基线：

- Rust：`CodeResource/sparrow/src`（sparrow，DOI 10.48550/arXiv.2509.13329 版本，含 2026-09-28 前的本仓改动）
- C++：`SparrowCore/Include` + `SparrowCore/Source` + `SparrowCore/Tools`
- 前置依赖：JaguarCore 已于 2026-09-29 封存（基线 `291f057`），SparrowCore 通过其公共 API 调用；jagua-rs↔JaguarCore 的等价性见 [docs/logic-comparison/README.md](../logic-comparison/README.md)（38 份记录）。
- 代码状态：含 `2026-09-23-sparrowcore-rust-equivalence.md` 计划（31/31 完成）的实现。

方法：与 JaguarCore 侧相同的逐文件方法——按 Rust 函数为纲逐条比对，每条差异给【一致】/【差异】/【C++缺失】/【C++额外】四档判定 + 双侧 file:line 证据。记录落在 `records/` 下。

范围排除（与 sparrow 侧性质对应）：

- `tui/`（dashboard、runtime、logging）：Rust 的终端 UI 层；C++ 侧的对应物是 `SparrowCore/Demo`（浏览器）与未来的 NestLab GUI，不属于算法核心，仅记录归属判定。
- `main.rs`/`cli.rs` 的参数解析细节：C++ 对应 `Tools/SparrowCli`，只比对选项语义。

## 文件映射清单（比对顺序）

| # | Rust | C++ | 进度 |
|---|---|---|---|
| S01 | `config.rs` | `optimizer/SparrowConfig.h/.cpp` | [x] |
| S02 | `consts.rs` + `lib.rs`（公共类型） | `optimizer/SparrowConfig.h`、`random/SparrowRng.h` 分散对应 | [x] |
| S03 | `optimizer/lbf.rs` | `optimizer/LbfLoss.h/.cpp` + `LbfSearch.h/.cpp` + `LbfEvaluator`（eval/lbf_evaluator 见 S09） | [x] |
| S04 | `optimizer/separator.rs` | `optimizer/Separator.h/.cpp` | [x] |
| S05 | `optimizer/worker.rs` | `optimizer/SeparatorWorker.h/.cpp` | [x] |
| S06 | `optimizer/explore.rs` | `optimizer/Exploration.h/.cpp` | [x] |
| S07 | `optimizer/compress.rs` | `optimizer/Compression.h/.cpp` | [x] |
| S08 | `optimizer/mod.rs`（SparrowOptimizer 编排） | `optimizer/SparrowOptimizer.h/.cpp` + `SppLbfOptimizer`/`BppLbfOptimizer`/`SparrowLbfBuilder` | [x] |
| S09 | `eval/lbf_evaluator.rs` + `eval/mod.rs` | `eval/LbfEvaluator.h/.cpp` + `eval/SparrowLbfEvaluator.h/.cpp` | [x] |
| S10 | `eval/sample_eval.rs` | `optimizer/SampleEval.h/.cpp` | [x] |
| S11 | `eval/sep_evaluator.rs` | `eval/SeparationEvaluator.h/.cpp` | [x] |
| S12 | `eval/collision_loss.rs` | `optimizer/LbfLoss.h` 的碰撞损失部分 + `Quantify` 权重 | [x] |
| S13 | `sample/search.rs` | `sampling/LocalSearchSampler.h/.cpp` | [x] |
| S14 | `sample/coord_descent.rs` | `sampling/CoordinateDescent.h/.cpp` | [x] |
| S15 | `sample/best_samples.rs` | `sampling/BestSamples.h/.cpp` | [x] |
| S16 | `sample/uniform_sampler.rs` + `sample/mod.rs` | `sampling/UniformRectSampler.h/.cpp` + `RotationDistribution.h/.cpp` | [x] |
| S17 | `quantify/overlap_proxy.rs` + `pair_matrix.rs` | `Quantify/OverlapProxy.h/.cpp` + `PairMatrix.h/.cpp` | [x] |
| S18 | `quantify/tracker.rs` | `Quantify/CollisionTracker.h/.cpp` | [x] |
| S19 | `quantify/simd/`（circles_soa、overlap_proxy_simd） | C++ SoA/AVX2 通道（若有） | [x] |
| S20 | `util/terminator.rs` + `ctrlc_terminator.rs` | `optimizer/OptimizationTerminator.h/.cpp` | [x] |
| S21 | `util/listener.rs` | `optimizer/OptimizationListener.h/.cpp` | [x] |
| S22 | `util/io.rs` + `svg_exporter.rs` + `assertions.rs` | Importer/JsonCodec 委托 + SVG（JaguarCore 侧记录 36） | [x] |
| S23 | `main.rs` + `cli.rs` + `bench.rs` | `Tools/SparrowCli/Main.cpp` + `Bench/` | [x] |
| S24 | `tui/`（dashboard、runtime、logging） | `Demo/`（浏览器）+ 归属判定 | [x] |

## 记录索引

| # | 记录 | 判定 | 差异条数 |
|---|---|---|---|
| S01 | [records/S01-config.md](records/S01-config.md) | 基本等价（种子 Option→零值弱化；三参数下沉 JaguarCore 层） | 6 |
| S02 | [records/S02-consts-lib.md](records/S02-consts-lib.md) | 基本等价（SparrowRng=Xoshiro256++/SplitMix64/Lemure/Box-Muller 逐项对齐） | 6 |
| S03 | [records/S03-lbf.md](records/S03-lbf.md) | 基本等价（LbfConfig.samplesPerItem 默认 5000≠Rust 1000，仅 SolveSppLbf/BppLbf 入口可见） | 10 |
| S04 | [records/S04-separator.md](records/S04-separator.md) | 基本等价（非常驻 worker：每轮全量重建、RNG 按轮回写） | 11 |
| S05 | [records/S05-worker.md](records/S05-worker.md) | 基本等价（best_sample 不变量降格为可空+跳过） | 9 |
| S06 | [records/S06-explore.md](records/S06-explore.md) | 基本等价（2026-09-29 扰动链已改 FollowedByRustF32 位级路径；ZIGNOR 正态位级复刻） | 12 |
| S07 | [records/S07-compress.md](records/S07-compress.md) | 基本等价（split_pos 随机流错位：Rust 双跳多耗主 rng + 采样式不同，分布语义不变） | 10 |
| S08 | [records/S08-optimizer-mod.md](records/S08-optimizer-mod.md) | 基本等价（SppLbfOptimizer 为独立入口：增长比 1.1≠Rust 1.2、无 2×Σ 上限；BppLbfOptimizer 为 C++ 额外） | 10 |
| S09 | [records/S09-lbf-evaluator.md](records/S09-lbf-evaluator.md) | 基本等价 | 6 |
| S10 | [records/S10-sample-eval.md](records/S10-sample-eval.md) | 行为等价（NaN panic→定义序为唯一形态差异） | 6 |
| S11 | [records/S11-sep-evaluator.md](records/S11-sep-evaluator.md) | 基本等价（流式短路缺失：全量收集后一次计算再比较上界） | 8 |
| S12 | [records/S12-collision-loss.md](records/S12-collision-loss.md) | 基本等价（公式在 CollisionTracker/OverlapProxy，非 LbfLoss；额外 max(loss,1e-6) 钳制） | 7 |
| S13 | [records/S13-search.md](records/S13-search.md) | 基本等价 | 7 |
| S14 | [records/S14-coord-descent.md](records/S14-coord-descent.md) | 行为等价（完全并列时 Rust 取后/C++ 取前，边界场景） | 4 |
| S15 | [records/S15-best-samples.md](records/S15-best-samples.md) | 行为等价（FPA 接受/淘汰逐位复刻） | 6 |
| S16 | [records/S16-uniform-sampler.md](records/S16-uniform-sampler.md) | 行为等价（随机消耗序一致；Continuous 旋转 `i*(2π/16)` vs `(2π·i)/16` 乘除序为微风险，fixture 未覆盖） | 7 |
| S17 | [records/S17-overlap-pair.md](records/S17-overlap-pair.md) | 基本等价（PairMatrix 物理布局不同但封闭自洽，仅 ulp 级累加顺序差） | 9 |
| S18 | [records/S18-tracker.md](records/S18-tracker.md) | 基本等价（register_item_move：O(1) 增量 vs O(N²) 全量重建，结果等价） | 11 |
| S19 | [records/S19-simd.md](records/S19-simd.md) | 结果等价/实现不同（Rust simd 为 opt-in 默认关闭；C++ 量化恒标量=与 Rust 默认一致；两侧 SoA 体系不相交） | 7 |
| S20 | [records/S20-terminator.md](records/S20-terminator.md) | 基本等价（ctrlc 信号终止为 C++ 缺失，RequestStop 为 C++ 额外且不可被相位切换重置） | 5 |
| S21 | [records/S21-listener.md](records/S21-listener.md) | 基本等价（事件字段面覆盖 TUI 所需全部数据） | 4 |
| S22 | [records/S22-util-io.md](records/S22-util-io.md) | 基本等价（委托 JaguarCore；warm-start 文件级闭环缺失） | 6 |
| S23 | [records/S23-cli-bench.md](records/S23-cli-bench.md) | 基本等价（时间预算模型与 bench 定位分叉） | 7 |
| S24 | [records/S24-tui.md](records/S24-tui.md) | 排除（归属判定：浏览器 Demo/NestLab GUI；Demo 仅覆盖最终布局可视化） | 5 |

## 汇总结论

逐文件比对完成（24 份记录，约 1870 行，覆盖 sparrow 侧全部 33 个源文件 + tui 归属判定）。判定分布：行为等价 ×4（S10/S14/S15/S16）、基本等价 ×18、结果等价/实现不同 ×1（S19）、排除 ×1（S24）。总体与 2026-09-23 等价性计划（31/31）的结论一致，但每条差异现在有了文件级出处。

1. **算法语义等价且多为位级锁定**：LBF 排料（排序键/1.2 增长/2×Σ 上限/损失公式）、Separator 多 worker（Algorithm 9/10：strike 计数/0.98 双阈值/GLS 权重/keep-weights 回滚）、Exploration 主循环（收缩清池/正态池抽选/回滚扰动）、Compression 三重停机、量化公式链（scale-dependent epsilon/shape penalty/container 公式/GLS 权重）逐项复现；随机数（Xoshiro256++/SplitMix64/Lemure/Box-Muller/ZIGNOR 正态）为位级复刻。`LbfSearchRustTrace`/`SeparatorSearchRustTrace`（loss_f32_bits 位模式）/`PhaseReference`/`SparrowUniformIndexRustReference` 等 18 个 fixture 锁定。
2. **2026-09-29 修复轮**（对照档案发现的差异已处理）：
   - **S06 扰动长变换链**：已改 `InverseRustF32().FollowedByRustF32()` 位级路径——原"唯一潜在数值偏差点"消除。
   - **连带修复**：CollisionTracker/SeparationEvaluator 私有引擎实体句柄缺失（2974bb6 重复实体检查误拒同物品多放置）；两个 C++ 自引用 fixture 旧基线重生成；TestBppJsonRoundTrip 桩化同步；TestSampling 空离散旋转改为 Importer 层验证（对齐 Rust 层级）。Debug/Release 全量测试 + 六 oracle 复验通过。
   - 仍开放（低优先）：
   - **S07 split_pos 随机流错位**：Rust "先变宽再回滚"双跳多消耗主 rng + C++ 用 `Uniform01F32()*w` 采样——分布语义不变、数值序列不同。如需位级对齐需复刻 rand 的 f32 random_range 与 rng 消耗序。
   - **S03/S08 入口参数分叉**：`LbfConfig.samplesPerItem` 默认 5000（Rust 1000，仅 SolveSppLbf/BppLbf 入口可见）；SppLbfOptimizer 增长比 1.1（Rust 1.2）、无 2×Σ 宽度上限。调用方需自知或统一默认值。
   - **S14 并列取值方向**：坐标下降完全并列时 Rust 取后/C++ 取前（边界场景）。
   - **S16 Continuous 旋转微风险**：`i*(2π/16)` vs `(2π·i)/16` 乘除序（fixture 只覆盖 None/Discrete）。
3. **性能形态差异（结果等价）**：S11/S12 分离评估失去流式短路（全量收集后一次计算）；S18 `register_item_move` O(1) 增量 vs O(N²) 全量重建；S04 每轮全量重建 worker；S17 PairMatrix 布局不同（ulp 级累加顺序）。均为大规模场景的优化候选，不影响正确性。
4. **C++ 缺失（低影响）**：ctrlc 信号终止与"跳相位"入口（S20）；warm-start 的文件级双格式回退（S22，库级路径健全）；TUI 的日志双通道/live SVG（S24，归 NestLab/Demo）；bench 的任意实例×分布统计场景（S23，定位互补）。
5. **C++ 额外**：BppLbfOptimizer（BPP 变体，Rust sparrow 仅 SPP）；LocalSearchSampler（高斯采样器，不参与参考搜索编排）；WASM 边界与浏览器 Demo；防御层（1e6 权重钳制、快照对账、maxIterations/infeasiblePoolSize 限流，默认关闭）。
6. **与 JaguarCore 档案的衔接**：S06 的 FollowedBy 精度路径（JaguarCore 记录 06 在 SparrowCore 侧的实际触发点）已于 2026-09-29 修复为位级路径——"长变换链位级对齐"不再是已知分叉；剩余理论风险仅在未覆盖的病态输入与真实 CAD 回归。
