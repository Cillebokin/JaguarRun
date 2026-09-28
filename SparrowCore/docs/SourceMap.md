# SparrowCore LBF 源码映射与许可边界

## 1. 文档目的

本文是 SparrowCore LBF 纯 C++ 移植的来源登记表。它记录参考实现、目标文件、保留的行为、不保证逐点相同的部分，以及许可证边界。本文不授权将 Rust 源码逐行复制到 C++；实现必须重新编写，并通过行为/不变量/指标差分验证。

参考目录：

```text
D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\lbf
D:\MyFiles\Gits_Xj\NestLab\NestLab\sparrow
D:\MyFiles\Gits_Xj\NestLab\NestLab\JaguarCore
```

## 2. 许可证和来源

| 来源 | 许可证/用途 | SparrowCore 处理 |
|---|---|---|
| `jagua-rs` / `jagua-rs/lbf` | MPL-2.0；几何、CDE、SPP/BPP API 与 LBF reference behavior | 只参考公开 API、算法行为、字段和测试不变量；不复制实现文本；如后续复制受许可保护的代码片段，必须保留 MPL-2.0 notice 并做发布审查 |
| `sparrow` | MIT；Sparrow LBFBuilder、采样、评估、坐标下降和优化阶段参考 | 只参考算法论文、公开行为和模块职责；不复制 Rust 实现文本；如后续复制受许可保护的代码片段，必须保留 MIT notice |
| `JaguarCore` | 本地项目模块；纯 C++ 几何、CDE、Layout、问题模型和 I/O | 只通过 `JaguarCore/Include` public headers 和既有链接边界使用；不包含 JaguarCore private headers |

SparrowCore 核心仍以 static library 作为 C++ 集成产物；CLI、WASM C ABI、Demo 和 Bench 作为外围发布边界分别构建。DLL/CRT/ABI 的最终政策仍由 JaguarRun 的发布工程决定。

## 3. jagua-rs/lbf 映射

| 参考文件 | 目标文件 | 保留语义 |
|---|---|---|
| `src/config.rs` | `Include/SparrowCore/LbfConfig.h` | CDE 配置、简化配置、seed、sample budget、local-search fraction |
| `src/samplers/rotation_distr.rs` | `Source/Sampling/RotationDistribution.cpp` | None/Discrete/Continuous 旋转策略 |
| `src/samplers/uniform_rect_sampler.rs` | `Source/Sampling/UniformRectSampler.cpp` | bbox 内 x/y/rotation 均匀采样 |
| `src/samplers/ls_sampler.rs` | `Source/Sampling/LocalSearchSampler.cpp` | 平移/旋转正态采样、均值移动、指数衰减 |
| `src/opt/loss.rs` | `Source/Optimizer/LbfLoss.cpp` | `10*x_max+y_max`，采样 bbox 的 x_max tightening |
| `src/opt/search.rs` | `Source/Optimizer/LbfSearch.cpp` | Uniform → Surrogate → exact polygon → Local Search |
| `src/opt/lbf_spp.rs` | `Source/Optimizer/SppLbfOptimizer.cpp` | item order、demand、无位置时扩宽、fitStrip |
| `src/opt/lbf_bpp.rs` | `Source/Optimizer/BppLbfOptimizer.cpp` | open layout 优先、stock bin 次之、库存/成本 |
| `src/util/assertions.rs` | `Source/Support/Version.cpp` 与 JaguarCore 校验边界 | strip width sanity check |
| `src/io/output.rs` | JaguarCore `IO/JsonCodec.h` + SparrowCore Tools | external solution/config/metrics 输出边界 |

### jagua-rs/lbf 的已知差异

- Rust 当前优化入口使用 `Xoshiro256PlusPlus`；SparrowCore 对应使用独立、文档化的 `SparrowRng`，并比较 raw primitive/metrics，而不是把随机数内部序列当成几何等价的充分证明。
- Rust reference 的 `f32` 几何进入 JaguarCore 时转为 `double`；比较时记录转换点并使用明确容差。
- Rust `SPProblem`/`BPProblem` 的内部 private 状态不进入 SparrowCore；使用 JaguarCore public value types 重建结果。
- Rust BPP 空 Layout 由问题模型关闭；MSPP 空 Layout 是合法 solution state，须保留其 JSON round-trip 语义。

## 4. Sparrow 映射

| 参考文件 | 目标文件 | 第一版范围 |
|---|---|---|
| `src/Optimizer/lbf.rs` | `Source/Optimizer/SparrowLbfBuilder.cpp` | initial construction、按 convex-hull-area×diameter 排序、width ceiling |
| `src/sample/search.rs` | `Source/Sampling/BestSamples.cpp` / `Source/Optimizer/LbfSearch.cpp` | container/focussed samples、BestSamples、两阶段 refinement |
| `src/sample/coord_descent.rs` | `Source/Sampling/CoordinateDescent.cpp` | horizontal/vertical/diagonal/wiggle candidate 与 step update |
| `src/sample/uniform_sampler.rs` | `Source/Sampling/UniformRectSampler.cpp` | item bbox 合法化采样 |
| `src/Eval/sample_eval.rs` | `Source/Optimizer/SampleEval.cpp` | Invalid/Collision/Clear 三态排序 |
| `src/Eval/lbf_evaluator.rs` | `Source/Eval/SparrowLbfEvaluator.cpp` | `10*(POI.x+bbox.x)+POI.y+bbox.y` |
| `src/consts.rs` | `Include/SparrowCore/LbfConfig.h` | sample/step constants |
| `src/quantify/overlap_proxy.rs` | `Source/Quantify/OverlapProxy.cpp` | pole-circle overlap proxy and container penalty |
| `src/quantify/pair_matrix.rs` | `Source/Quantify/PairMatrix.cpp` | symmetric triangular pair storage |
| `src/quantify/tracker.rs` | `Source/Quantify/CollisionTracker.cpp` | pair/container loss, weights and snapshots |
| `src/eval/sep_evaluator.rs` | `Source/Eval/SeparationEvaluator.cpp` | surrogate/exact separation candidate evaluation |
| `src/Optimizer/separator.rs` + `worker.rs` | `Source/Optimizer/Separator.cpp` + `SeparatorWorker.cpp` | move, rollback, strike and no-improvement limits |
| `src/Optimizer/explore.rs` | `Source/Optimizer/Exploration.cpp` | repeated strip shrink, bounded infeasible pool, disruption and contained relocation |
| `src/Optimizer/compress.rs` | `Source/Optimizer/Compression.cpp` | time/failure based shrink decay |

## 5. 当前边界

Task 2～7 已补齐 SparrowCore 本体的构造、量化、分离、探索、压缩、顶层 Optimize、CLI、WASM C ABI、Demo 和 Bench；顶层 API 还支持外部 `OptimizationTerminator` 和完整可行解 warm start。当前仍明确不包含：

- NestLab 适配、生产排料替换和上层业务协议；
- 当前机器未安装 Emscripten，因此 Emscripten 生成的最终 glue、宿主内存管理和 CDN 发布仍需在 WASM 工具链环境执行；
- Rust/C++ 全规模逐样本 bit-exact differential oracle；当前保留固定 seed fixture、行为不变量和 JaguarCore 最终验证。

## 6. Fixture 资产

Task1 固定资产：

- `Tests/Fixtures/LbfSppSmall.json`：2 个 2×2 矩形裁片，固定条带高度 4，demand=2；
- `Tests/Fixtures/LbfBppSmall.json`：1 个 2×2 矩形裁片，6×6 Bin，stock=1，cost=10；
- `Tests/Fixtures/LbfConfigSeed42.json`：四叉树深度 4、阈值 32、64 samples、`ls_frac=0.25`、seed=42 的参考配置；
- `Tests/Fixtures/LbfSeed42Reference.tsv`：SparrowRng raw/float primitive 的字段定义和后续 seed=42 oracle 入口；
- `Tests/Fixtures/LbfMetricsReference.tsv`：LBF reference 的 sample count、placed、remaining、width、density、cost 字段定义；SPP 的 cost=0，BPP 的 width=0 表示该指标不适用。

两个 JSON fixture 使用 JaguarCore 已验证的 snake_case external representation：每个 Item 都有 `orientation.rotation`，不含 holes、legacy orientation 字段、NaN 或 Infinity。

## 7. 验证策略

当前验证链路为：

```text
JSON syntax/type/finite-number check
  ↓
JaguarCore parser boundary check
  ↓
SparrowCore LBF/Separator/Optimize
  ↓
JaguarCore solution validation
  ↓
CLI/Bench/WASM boundary smoke test
```
